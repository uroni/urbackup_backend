/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "vhdfile.h"
#include "../Interface/Server.h"
#include "../Interface/Types.h"
#include "../stringtools.h"
#include "CompressedFile.h"
#include <memory.h>
#include <stdlib.h>
#include "FileWrapper.h"
#include "ClientBitmap.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#endif
#include "fs/ntfs.h"

const uint64 def_header_offset=0;
const uint64 def_dynamic_header_offset=512;
const uint64 def_bat_offset=512+1024;
const uint64 def_bat_offset_parent=2*1024;
const int64 unixtime_offset=946684800;

const unsigned int sector_size=512;

VHDFile::VHDFile(const std::string &fn, bool pRead_only, uint64 pDstsize, unsigned int pBlocksize, bool fast_mode, bool compress)
	: dstsize(pDstsize), blocksize(pBlocksize), fast_mode(fast_mode), bitmap_offset(0), bitmap_dirty(false), volume_offset(0), finished(false),
	file(NULL)
{
	compressed_file=NULL;
	parent=NULL;
	read_only=pRead_only;
	is_open=false;
	curr_offset=0;
	currblock=0xFFFFFFFF;

	backing_file = Server->openFile(fn, (read_only ? MODE_READ : MODE_RW));

	bool openedExisting = true;

	if(!backing_file)
	{
		if(read_only==false)
		{
			backing_file = Server->openFile(fn, MODE_RW_CREATE);
			openedExisting=false;
		}
		if(backing_file==NULL)
		{
			Server->Log("Error opening VHD file", LL_ERROR);
			return;
		}
	}

	if(check_if_compressed() || compress)
	{
		compressed_file = new CompressedFile(backing_file, openedExisting, read_only);
		file = compressed_file;

		if(compressed_file->hasError())
		{
			return;
		}
	}
	else
	{
		file = backing_file;
	}

	if(file->Size()==0 && !read_only) // created file
	{
		header_offset=def_header_offset;
		dynamic_header_offset=def_dynamic_header_offset;
		bat_offset=def_bat_offset;

		batsize=(unsigned int)(dstsize/blocksize);
		if(dstsize%blocksize!=0)
			++batsize;

		bat=new unsigned int[batsize];
		for(size_t i=0;i<batsize;++i)
		{
			bat[i]=0xFFFFFFFF;
		}

		write_header(false);
		write_dynamicheader(NULL, 0, "");
		write_bat();

		nextblock_offset=bat_offset+batsize*sizeof(unsigned int);
		nextblock_offset=nextblock_offset+(sector_size-nextblock_offset%sector_size);

		write_footer();

		is_open=true;
	}
	else
	{
		if(read_footer() )
		{
			if(process_footer() )
			{
				if(read_dynamicheader() )
				{
					if(read_bat() )
					{
						nextblock_offset=file->Size()-sector_size;
						if(nextblock_offset%sector_size!=0)
							nextblock_offset+=sector_size-nextblock_offset%sector_size;


						is_open=true;
					}
				}
			}
		}

	}
}

VHDFile::VHDFile(const std::string &fn, const std::string &parent_fn, bool pRead_only, bool fast_mode, bool compress)
	: fast_mode(fast_mode), bitmap_offset(0), bitmap_dirty(false), volume_offset(0), finished(false), file(NULL)
{
	compressed_file=NULL;
	curr_offset=0;
	is_open=false;
	read_only=pRead_only;
	parent=NULL;
	curr_offset=0;
	currblock=0xFFFFFFFF;

	backing_file=Server->openFile(fn, (read_only?MODE_READ:MODE_RW) );
	bool openedExisting = true;
	if(!backing_file)
	{
		if(read_only==false)
		{
			backing_file=Server->openFile(fn, MODE_RW_CREATE);
			openedExisting=false;
		}
		if(backing_file==NULL)
		{
			Server->Log("Error opening VHD file", LL_ERROR);
			return;
		}
	}

	if(check_if_compressed() || compress)
	{
		file = new CompressedFile(backing_file, openedExisting, read_only);
	}
	else
	{
		file = backing_file;
	}

	parent=new VHDFile(parent_fn, true, 0);

	if(parent->isOpen()==false)
	{
		Server->Log("Error opening parent VHD", LL_ERROR);
		return;
	}

	dstsize=parent->getRealSize();
	blocksize=parent->getBlocksize();

	if(file->Size()==0 && !read_only) // created file
	{
		header_offset=def_header_offset;
		dynamic_header_offset=def_dynamic_header_offset;
		bat_offset=def_bat_offset_parent;

		batsize=(unsigned int)(dstsize/blocksize);
		if(dstsize%blocksize!=0)
			++batsize;

		bat=new unsigned int[batsize];
		for(size_t i=0;i<batsize;++i)
		{
			bat[i]=0xFFFFFFFF;
		}

		nextblock_offset=bat_offset+batsize*sizeof(unsigned int);
		nextblock_offset=nextblock_offset+(sector_size-nextblock_offset%sector_size);

		write_header(true);
		write_dynamicheader(parent->getUID(), parent->getTimestamp(), parent_fn );
		write_bat();

		write_footer();

		is_open=true;
	}
	else
	{
		if(read_footer() )
		{
			if(process_footer() )
			{
				if(read_dynamicheader() )
				{
					if(read_bat() )
					{
						nextblock_offset=file->Size()-sector_size;
						if(nextblock_offset%sector_size!=0)
							nextblock_offset+=sector_size-nextblock_offset%sector_size;


						is_open=true;
					}
				}
			}
		}

	}
}

VHDFile::~VHDFile()
{
	if(!finished && file!=NULL)
	{
		finish();
	}
	delete file;
	delete parent;
}

bool VHDFile::write_header(bool diff)
{
	footer.cookie[0]='c';
	footer.cookie[1]='o';
	footer.cookie[2]='n';
	footer.cookie[3]='e';
	footer.cookie[4]='c';
	footer.cookie[5]='t';
	footer.cookie[6]='i';
	footer.cookie[7]='x';
	footer.features=big_endian((unsigned int)0x00000002);
	footer.format_version=big_endian((unsigned int)0x00010000);
	footer.data_offset=big_endian(dynamic_header_offset);
	footer.timestamp=big_endian((unsigned int)(Server->getTimeSeconds()-unixtime_offset));
	footer.creator_application[0]='v';
	footer.creator_application[1]='p';
	footer.creator_application[2]='c';
	footer.creator_application[3]=' ';
	footer.creator_version=big_endian((unsigned int)0x00050003);
	footer.creator_os=big_endian((unsigned int)0x5769326B);
	footer.original_size=big_endian(dstsize);
	footer.current_size=big_endian(dstsize);
	footer.disk_geometry=calculate_chs();
	if(diff)
		footer.disk_type=big_endian((unsigned int)4);
	else
		footer.disk_type=big_endian((unsigned int)3);
	footer.checksum=0;
	Server->randomFill(footer.uid, 16);
	footer.saved_state=0;
	memset(footer.reserved, 0, 427);


	footer.checksum=calculate_checksum((unsigned char*)&footer, sizeof(VHDFooter) );


	if(!file->Seek(header_offset))
		return false;

	_u32 rc=file->Write((char*)&footer, sizeof(VHDFooter));
	if(sizeof(VHDFooter)!=rc)
		return false;
	else
		return true;
}

unsigned int VHDFile::calculate_chs(void)
{
	int64 totalSectors=dstsize/sector_size;
	unsigned int cylinderTimesHeads;
	unsigned char heads;
	unsigned char sectorsPerTrack;

	if (totalSectors > 65535 * 16 * 255)
	{
	   totalSectors = 65535 * 16 * 255;
	}

	if (totalSectors >= 65535 * 16 * 63)
	{
	   sectorsPerTrack = 255;
	   heads = 16;
	   cylinderTimesHeads = (unsigned int)(totalSectors / sectorsPerTrack);
	}
	else
	{
	   sectorsPerTrack = 17; 
	   cylinderTimesHeads = (unsigned int)(totalSectors / sectorsPerTrack);

	   heads = (cylinderTimesHeads + 1023) / 1024;
	      
	   if (heads < 4)
	   {
		  heads = 4;
	   }
	   if (cylinderTimesHeads >= ((unsigned int)heads * 1024) || heads > 16)
	   {
		  sectorsPerTrack = 31;
		  heads = 16;
		  cylinderTimesHeads = (unsigned int)(totalSectors / sectorsPerTrack);	
	   }
	   if (cylinderTimesHeads >= ((unsigned int)heads * 1024))
	   {
		  sectorsPerTrack = 63;
		  heads = 16;
		  cylinderTimesHeads = (unsigned int)(totalSectors / sectorsPerTrack);
	   }
	}
	unsigned short cylinders = cylinderTimesHeads / heads;

	unsigned int chs=(cylinders<<16)+(heads<<8)+sectorsPerTrack;
	return big_endian(chs);
}

unsigned int VHDFile::calculate_checksum(const unsigned char * data, size_t dsize)
{
	unsigned int checksum=0;
	for(size_t i=0;i<dsize;++i)
	{
		checksum+=data[i];
	}
	return ~big_endian(checksum);
}

bool VHDFile::write_dynamicheader(char *parent_uid, unsigned int parent_timestamp, std::string parentfn)
{
	memset(&dynamicheader, 0, sizeof(VHDDynamicHeader) );

	dynamicheader.cookie[0]='c';
	dynamicheader.cookie[1]='x';
	dynamicheader.cookie[2]='s';
	dynamicheader.cookie[3]='p';
	dynamicheader.cookie[4]='a';
	dynamicheader.cookie[5]='r';
	dynamicheader.cookie[6]='s';
	dynamicheader.cookie[7]='e';
	dynamicheader.dataoffset=0xFFFFFFFFFFFFFFFFLLU;
	dynamicheader.tableoffset=big_endian(bat_offset);
	dynamicheader.header_version=big_endian((unsigned int)0x00010000);
	dynamicheader.table_entries=big_endian(batsize);
	dynamicheader.blocksize=big_endian(blocksize);
	dynamicheader.checksum=0;
	if(parent_uid!=NULL)
	{
		//Differencing file
		memcpy(dynamicheader.parent_uid, parent_uid, 16);
		dynamicheader.parent_timestamp=big_endian(parent_timestamp);

		std::string unicodename_source;
		std::string dirname = ExtractFileName(ExtractFilePath(parentfn));
		if (dirname.find("Image") != std::string::npos)
		{
			unicodename_source = "../" + dirname + "/" + ExtractFileName(parentfn);
		}
		else
		{
			unicodename_source = ExtractFileName(parentfn);
		}

		std::string unicodename=big_endian_utf16(Server->ConvertToUTF16(unicodename_source));

		std::string rel_unicodename;

		if (dirname.find("Image") != std::string::npos)
		{
			rel_unicodename = Server->ConvertToUTF16("..\\" +dirname+"\\"+ ExtractFileName(parentfn));
		}
		else
		{
			rel_unicodename = Server->ConvertToUTF16(".\\" + ExtractFileName(parentfn));
		}

		std::string abs_unicodename=Server->ConvertToUTF16(parentfn);
		unicodename.resize(unicodename.size()+2);
		unicodename[unicodename.size()-2]=0;
		unicodename[unicodename.size()-1]=0;
		memcpy(dynamicheader.parent_unicodename, &unicodename[0], unicodename.size());
		unsigned int locator_blocks_abs=static_cast<unsigned int>((abs_unicodename.size())/sector_size)+(((abs_unicodename.size())%sector_size!=0)?1:0);
		dynamicheader.parentlocator[0].platform_code=big_endian((unsigned int)0x57326B75);
		dynamicheader.parentlocator[0].platform_space=big_endian(locator_blocks_abs*sector_size);
		dynamicheader.parentlocator[0].platform_length=big_endian((unsigned int)abs_unicodename.size());
		uint64 abs_locator_offset=def_bat_offset_parent-512;
		if(locator_blocks_abs>1)
		{
			abs_locator_offset=nextblock_offset;
			nextblock_offset+=abs_locator_offset*sector_size;
		}
		dynamicheader.parentlocator[0].platform_offset=big_endian(abs_locator_offset);

		if(!file->Seek(abs_locator_offset))
			return false;
		_u32 rc=file->Write(abs_unicodename.c_str(), (_u32)abs_unicodename.size());
		if(rc!=abs_unicodename.size())
			return false;

		dynamicheader.parentlocator[1].platform_code=big_endian((unsigned int)0x57327275);
		unsigned int locator_blocks=(rel_unicodename.size())/sector_size+((rel_unicodename.size())%sector_size!=0)?1:0;
		if(locator_blocks<128) locator_blocks=128;
		dynamicheader.parentlocator[1].platform_space=big_endian(locator_blocks*sector_size);
		dynamicheader.parentlocator[1].platform_length=big_endian((unsigned int)rel_unicodename.size());
		dynamicheader.parentlocator[1].platform_offset=big_endian(nextblock_offset);
		if(!file->Seek(nextblock_offset))
			return false;

		rc=file->Write(rel_unicodename.c_str(), (_u32)rel_unicodename.size());
		if(rc!=rel_unicodename.size())
			return false;

		nextblock_offset+=locator_blocks*sector_size;
	}

	init_bitmap();

	dynamicheader.checksum=calculate_checksum((unsigned char*)&dynamicheader, sizeof(VHDDynamicHeader) );

	if(!file->Seek(dynamic_header_offset))
		return false;

	_u32 rc=file->Write((char*)&dynamicheader, sizeof(VHDDynamicHeader) );
	if(rc!=sizeof(VHDDynamicHeader))
		return false;
	else
		return true;
}

bool VHDFile::write_bat(void)
{
	if(!file->Seek(bat_offset)) return false;
	_u32 rc=file->Write((char*)bat, batsize*sizeof(unsigned int) );
	if(rc!=batsize*sizeof(unsigned int))
		return false;
	else
		return true;
}

bool VHDFile::write_footer(void)
{
	if(!file->Seek(nextblock_offset))return false;
	_u32 rc=file->Write((char*)&footer, sizeof(VHDFooter));
	if(rc!=sizeof(VHDFooter))
		return false;
	else
		return true;
}

bool VHDFile::read_footer(void)
{
	size_t fsize=sizeof(VHDFooter);
	bool b=file->Seek(file->Size()-sizeof(VHDFooter));
	if(!b)
	{
		Server->Log("Error seeking -2");
		return false;
	}
	if(file->Read((char*)&footer, sizeof(VHDFooter))!=sizeof(VHDFooter))
	{
		Server->Log("Cannot read footer", LL_ERROR);
		return false;
	}

	unsigned int checksum=footer.checksum;
	footer.checksum=0;
	unsigned int cchecksum=calculate_checksum((unsigned char*)&footer, sizeof(VHDFooter) );
	if(checksum!=cchecksum)
	{
		Server->Log("Footer checksum wrong. Switching to header", LL_ERROR);
		file->Seek(0);
		if(file->Read((char*)&footer, sizeof(VHDFooter))!=sizeof(VHDFooter) )
		{
			Server->Log("Cannot read footer", LL_ERROR);
			return false;
		}
		else
		{
			checksum=footer.checksum;
			footer.checksum=0;
			unsigned int cchecksum=calculate_checksum((unsigned char*)&footer, sizeof(VHDFooter) );
			if(checksum!=cchecksum)
			{
				Server->Log("Header and footer checksum wrong", LL_ERROR);
				return false;
			}
			else
			{
				footer.checksum=checksum;
				return true;
			}
		}
	}
	else
	{
		footer.checksum=checksum;
		return true;
	}
}

bool VHDFile::process_footer(void)
{
	if(big_endian(footer.format_version)!=0x00010000)
	{
		Server->Log("Unrecognized VHD format version", LL_ERROR);
		return false;
	}

	if(big_endian(footer.disk_type)!=3 && big_endian(footer.disk_type)!=4 )
	{
		Server->Log("Unsupported disk type", LL_ERROR);
		return false;
	}

	dstsize=big_endian(footer.current_size);
	header_offset=0;
	dynamic_header_offset=big_endian(footer.data_offset);
	return true;
}

bool VHDFile::read_dynamicheader(void)
{
	bool b=file->Seek(dynamic_header_offset);
	if(!b)
	{
		Server->Log("Error seeking -2");
		return false;
	}
	_u32 rc=file->Read((char*)&dynamicheader, sizeof(VHDDynamicHeader) );
	if(rc!=sizeof(VHDDynamicHeader))
	{
		Server->Log("Error reading dynamic header", LL_ERROR);
		return false;
	}

	unsigned int checksum=dynamicheader.checksum;
	dynamicheader.checksum=0;
	unsigned int cchecksum=calculate_checksum((unsigned char*)&dynamicheader, sizeof(VHDDynamicHeader) );
	if(checksum!=cchecksum)
	{
		Server->Log("Dynamicheader checksum wrong", LL_ERROR);
		return false;
	}
	dynamicheader.checksum=checksum;

	bat_offset=big_endian(dynamicheader.tableoffset);
	batsize=big_endian(dynamicheader.table_entries);
	blocksize=big_endian(dynamicheader.blocksize);

	if(big_endian(footer.disk_type)==4)
	{
		//differencing hd
		std::string parent_unicodename;
		parent_unicodename.resize(512);
		memcpy(&parent_unicodename[0], dynamicheader.parent_unicodename, 512);
		parent_unicodename=big_endian_utf16(parent_unicodename);
		std::wstring parent_fn=Server->ConvertToWchar(Server->ConvertFromUTF16(parent_unicodename));
		parent_fn.resize(wcslen(parent_fn.c_str()));

		std::string curr_dir = ExtractFilePath(file->getFilename());

		bool is_in_other_folder = false;
		while(parent_fn.size()>3
			&& (parent_fn.find(L"../")==0
				|| parent_fn.find(L"..\\")==0 ) )
		{
			curr_dir = ExtractFilePath(curr_dir);
			parent_fn = parent_fn.substr(3);
			is_in_other_folder = true;
		}

		parent_fn=Server->ConvertToWchar(curr_dir)+L"/"+parent_fn;
		std::string utf8_parent_fn = Server->ConvertFromWchar(parent_fn);
		Server->Log("VHD-Parent: \""+utf8_parent_fn+"\"", LL_INFO);

		if (!FileExists(utf8_parent_fn))
		{
			if (is_in_other_folder)
			{
				parent_fn = Server->ConvertToWchar(ExtractFilePath(file->getFilename())) + L"/" + Server->ConvertToWchar(ExtractFileName(Server->ConvertFromWchar(parent_fn)));
				utf8_parent_fn = Server->ConvertFromWchar(parent_fn);
				Server->Log("Corrected VHD-Parent to: \"" + utf8_parent_fn + "\"", LL_INFO);
			}
			else
			{
				parent_fn = Server->ConvertToWchar(ExtractFilePath(ExtractFilePath(file->getFilename()))) + L"/" + Server->ConvertToWchar(ExtractFileName(Server->ConvertFromWchar(parent_fn)));
				utf8_parent_fn = Server->ConvertFromWchar(parent_fn);
				Server->Log("Corrected VHD-Parent to: \"" + utf8_parent_fn + "\"", LL_INFO);
			}
		}


		parent=new VHDFile(utf8_parent_fn, true, 0);

		if(parent->isOpen()==false)
		{
			Server->Log("Error opening Parentvhdfile \""+utf8_parent_fn+"\"", LL_ERROR);
			return false;
		}

		if(memcmp(parent->getUID(), dynamicheader.parent_uid, 16)!=0)
		{
			Server->Log("Parent uid wrong", LL_ERROR);
			return false;
		}

		if(parent->getTimestamp()!=big_endian(dynamicheader.parent_timestamp) )
		{
			Server->Log("Parent timestamp wrong. Parent was modified? Continuing anyways. But this is dangerous!", LL_ERROR);
		}
	}

	init_bitmap();

	return true;
}

void VHDFile::init_bitmap(void)
{
	bitmap_size=blocksize/sector_size/8;
	if(blocksize%sector_size!=0 || (blocksize/sector_size)%8!=0)
		++bitmap_size;

	if(bitmap_size%sector_size!=0)
		bitmap_size+=sector_size-bitmap_size%sector_size;

	bitmap.resize(bitmap_size);
}

bool VHDFile::read_bat(void)
{
	bool b=file->Seek(bat_offset);
	if(!b)
	{
		Server->Log("Error seeking -3");
		return false;
	}
	bat=new unsigned int[batsize];
	_u32 rc=file->Read((char*)bat, batsize*sizeof(unsigned int));
	if(rc!=batsize*sizeof(unsigned int))
	{
		Server->Log("Error reading BAT", LL_ERROR);
		return false;
	}
	return true;
}

bool VHDFile::Seek(_i64 offset)
{
	curr_offset=(uint64)offset+volume_offset;
	return true;
}

inline bool VHDFile::isBitmapSet(unsigned int offset)
{
	size_t sector=offset/sector_size;
	size_t bitmap_byte=(size_t)(sector/8);
	size_t bitmap_bit=sector%8;

	unsigned char b=bitmap[bitmap_byte];

	bool has_bit=((b & (1<<(7-bitmap_bit)))>0);

	return has_bit;
}

inline bool VHDFile::setBitmapBit(unsigned int offset, bool v)
{
	size_t sector=offset/sector_size;
	size_t bitmap_byte=(size_t)(sector/8);
	size_t bitmap_bit=sector%8;

	unsigned char b=bitmap[bitmap_byte];

	bool has_bit = ((b & (1 << (7 - bitmap_bit)))>0);

	if(v==true)
		b=b|(1<<(7-bitmap_bit));
	else
		b=b&(~(1<<(7-bitmap_bit)));

	bitmap[bitmap_byte]=b;
	if (has_bit != v)
	{
		bitmap_dirty = true;
		return true;
	}
	else
	{
		return false;
	}
}

bool VHDFile::Read(char* buffer, size_t bsize, size_t &read)
{
	unsigned int block=(unsigned int)(curr_offset/blocksize);
	size_t blockoffset=curr_offset%blocksize;
	size_t remaining=blocksize-blockoffset;
	size_t toread=bsize;
	bool firstr=true;
	read=0;

	if(curr_offset>=dstsize)
	{
		return false;
	}

	while(true)
	{
		unsigned int bat_off=big_endian(bat[block]);
		if(bat_off==0xFFFFFFFF)
		{
			unsigned int wantread=(unsigned int)(std::min)(remaining, toread);

			if(parent==NULL)
				memset(&buffer[read], 0, wantread );
			else
			{
				parent->Seek(curr_offset);
				size_t p_read;
				bool b=parent->Read(&buffer[read], wantread, p_read);
				if(!b)
				{
					Server->Log("Reading from parent failed -1", LL_ERROR);
				}
			}

			read+=wantread;
			curr_offset+=wantread;
			blockoffset+=wantread;
			remaining-=wantread;
			toread-=wantread;
			if(toread==0)
				break;
			else
			{
				++block;
				blockoffset=0;
				remaining=blocksize;
				continue;
			}
		}
		uint64 dataoffset=(uint64)bat_off*(uint64)sector_size;
		if(block!=currblock)
		{
			switchBitmap(dataoffset);

			file->Seek(dataoffset);

			if(dataoffset+bitmap_size+blockoffset+bsize>(uint64)file->Size() )
			{
				Server->Log("Wrong dataoffset: "+convert(dataoffset), LL_ERROR);
				return false;
			}

			if(file->Read(reinterpret_cast<char*>(bitmap.data()), bitmap_size)!=bitmap_size)
			{
				Server->Log("Error reading bitmap", LL_ERROR);
				return false;
			}
			currblock=block;
		}

		bool b=file->Seek(dataoffset+bitmap_size+blockoffset);
		if(!b)
			Server->Log("Seeking failed!- 1", LL_ERROR);

		while( blockoffset<blocksize )
		{
			size_t wantread=(std::min)((size_t)sector_size, toread);
			if(remaining<wantread)
				wantread=remaining;
			if(firstr && blockoffset%sector_size!=0 && wantread>sector_size-blockoffset%sector_size )
			{
				wantread=sector_size-blockoffset%sector_size;
				firstr=false;
			}
			else
			{
				firstr=false;
			}

			if( curr_offset+wantread>dstsize )
			{
				return true;
			}

			if( isBitmapSet((unsigned int)blockoffset) )
			{
				wantread=(size_t)file->Read(&buffer[read], (_u32)wantread );
			}
			else
			{
				if(parent!=NULL)
				{
					parent->Seek(curr_offset);
					bool b=parent->Read(&buffer[read], wantread, wantread);
					if(!b)
					{
						Server->Log("Reading from parent failed -2", LL_ERROR);
					}
				}
				else
				{
					memset(&buffer[read], 0, wantread );
				}
				file->Seek(dataoffset+bitmap_size+blockoffset+wantread);
			}
			read+=wantread;
			curr_offset+=wantread;
			blockoffset+=wantread;
			remaining-=wantread;
			toread-=wantread;
			if(toread==0)
			{
				break;
			}
			if(remaining==0)
			{
				break;
			}
		}

		if(toread==0)
			break;

		++block;
		blockoffset=0;
		remaining=blocksize;

		if(curr_offset>=dstsize)
		{
			return true;
		}
	}

	return true;
}

_u32 VHDFile::Write(const char *buffer, _u32 bsize, bool *has_error)
{
	if(read_only)
	{
		Server->Log("VHD file is read only", LL_ERROR);
		if(has_error) *has_error=true;
		return 0;
	}
	if(bsize+curr_offset>dstsize)
	{
		Server->Log("VHD file is not large enough. Want to write till "+convert(bsize+curr_offset)+" but size is "+convert(dstsize), LL_ERROR);
		if(has_error) *has_error=true;
		return 0;
	}

	bool dwrite_footer=false;

	uint64 block=curr_offset/((uint64)blocksize);
	size_t blockoffset=curr_offset%blocksize;
	size_t remaining=blocksize-blockoffset;
	size_t towrite=bsize;
	size_t bufferoffset=0;
	bool firstw=true;

	while(true)
	{
		uint64 dataoffset;
		unsigned int bat_ref=big_endian(bat[block]);
		bool new_block=false;
		if(bat_ref==0xFFFFFFFF)
		{
			dataoffset=nextblock_offset;
			nextblock_offset+=blocksize+bitmap_size;
			nextblock_offset=nextblock_offset+(sector_size-nextblock_offset%sector_size);
			dwrite_footer=true;
			new_block=true;
			bat[block]=big_endian((unsigned int)(dataoffset/(uint64)(sector_size)));
		}
		else
		{
			dataoffset=(uint64)bat_ref*(uint64)sector_size;
		}
		if(currblock!=block)
		{
			switchBitmap(dataoffset);

			bool b=file->Seek(dataoffset);
			if(!b)
				Server->Log("Seeking failed", LL_ERROR);

			if(!new_block)
				file->Read(reinterpret_cast<char*>(bitmap.data()), bitmap_size);
			else
			{
				memset(bitmap.data(), 0, bitmap_size );
				_u32 rc=file->Write(reinterpret_cast<char*>(bitmap.data()), bitmap_size);
				if(rc!=bitmap_size)
				{
					Server->Log("Writing bitmap failed", LL_ERROR);
					print_last_error();
					if(has_error) *has_error=true;
					return 0;
				}
			}
			currblock=block;
		}

		bool b=file->Seek(dataoffset+bitmap_size+blockoffset);
		if(!b)
		{
			Server->Log("Seeking in file failed", LL_ERROR);
			if(has_error) *has_error=true;
			return 0;
		}

		while( blockoffset<blocksize )
		{
			size_t wantwrite=(std::min)((size_t)sector_size, towrite);
			if(remaining<wantwrite)
				wantwrite=remaining;
			if(firstw && blockoffset%sector_size!=0 && wantwrite>sector_size-blockoffset%sector_size )
			{
				wantwrite=sector_size-blockoffset%sector_size;
				firstw=false;
			}
			else
			{
				firstw=false;
			}

			setBitmapBit((unsigned int)blockoffset, true);

			_u32 rc=file->Write(&buffer[bufferoffset], (_u32)wantwrite);
			if(rc!=wantwrite)
			{
				Server->Log("Writing to file failed", LL_ERROR);
				if(has_error) *has_error=true;
				print_last_error();
				return 0;
			}

			bufferoffset+=wantwrite;
			blockoffset+=wantwrite;
			remaining-=wantwrite;
			towrite-=wantwrite;
			if(towrite==0)
			{
				break;
			}
			if(remaining==0)
			{
				break;
			}
		}

		if(!fast_mode)
		{
			file->Seek(dataoffset);
			_u32 rc=file->Write(reinterpret_cast<char*>(bitmap.data()), bitmap_size);
			if(rc!=bitmap_size)
			{
				Server->Log("Writing bitmap failed", LL_ERROR);
				print_last_error();
				if(has_error) *has_error=true;
				return 0;
			}
		}

		if(towrite==0)
			break;

		++block;
		blockoffset=0;
		remaining=blocksize;
	}

	if(dwrite_footer && !fast_mode)
	{
		if(!write_footer())
		{
			Server->Log("Error writing footer", LL_ERROR);
			if(has_error) *has_error=true;
			return 0;
		}
		if(!write_bat())
		{
			Server->Log("Error writing BAT", LL_ERROR);
			if(has_error) *has_error=true;
			return 0;
		}
	}

	curr_offset+=bsize;

	return bsize;
}

_u32 VHDFile::Write(int64 spos, const char *buffer, _u32 bsize, bool* has_error)
{
	if(!Seek(spos))
	{
		if (has_error) *has_error = true;
		return 0;
	}

	return Write(buffer, bsize, has_error);
}

bool VHDFile::has_block(bool use_parent)
{
	unsigned int block=(unsigned int)(curr_offset/blocksize);
	size_t blockoffset=curr_offset%blocksize;

	if(curr_offset>=dstsize)
	{
		return false;
	}

	unsigned int bat_off=big_endian(bat[block]);
	if(bat_off==0xFFFFFFFF)
	{
		if(parent==NULL || !use_parent)
		{
			return false;
		}
		else
		{
			parent->Seek(curr_offset);
			return parent->has_block();
		}
	}

	uint64 dataoffset=(uint64)bat_off*(uint64)sector_size;
	if(block!=currblock)
	{
		switchBitmap(dataoffset);

		file->Seek(dataoffset);

		if(dataoffset+bitmap_size+blockoffset>(uint64)file->Size() )
		{
			Server->Log("Wrong dataoffset: "+convert(dataoffset), LL_ERROR);
			return false;
		}

		if(file->Read(reinterpret_cast<char*>(bitmap.data()), bitmap_size)!=bitmap_size)
		{
			Server->Log("Error reading bitmap", LL_ERROR);
			return false;
		}
		currblock=block;
	}

	if( isBitmapSet((unsigned int)blockoffset) )
	{
		return true;
	}
	else
	{
		if(parent!=NULL && use_parent)
		{
			parent->Seek(curr_offset);
			return parent->has_block();
		}
		else
		{
			return false;
		}
	}
}

void VHDFile::switchBitmap(uint64 new_offset)
{
	if(fast_mode && !read_only && bitmap_dirty && bitmap_offset!=0)
	{
		file->Seek(bitmap_offset);
		_u32 rc=file->Write(reinterpret_cast<char*>(bitmap.data()), bitmap_size);
		if(rc!=bitmap_size)
		{
			Server->Log("Writing bitmap failed", LL_ERROR);
			print_last_error();
		}
		bitmap_dirty=false;
	}

	bitmap_offset=new_offset;
	bitmap_dirty=false;
}

uint64 VHDFile::getSize(void)
{
	return dstsize-volume_offset;
}

uint64 VHDFile::getRealSize(void)
{
	return dstsize;
}

uint64 VHDFile::usedSize(void)
{
	uint64 offset_backup=curr_offset;

	uint64 used_size=0;

	for(uint64 i=0;i<dstsize;i+=blocksize)
	{
		if(has_sector())
		{
			used_size+=blocksize;
		}
	}

	curr_offset=offset_backup;

	return used_size;
}

bool VHDFile::has_sector(_i64 sector_size)
{
	unsigned int block=(unsigned int)(curr_offset/blocksize);
	unsigned int bat_ref=big_endian(bat[block]);
	if(bat_ref==0xFFFFFFFF)
	{
		if(parent!=NULL)
		{
			parent->Seek(curr_offset);
			return parent->has_sector();
		}
		else
			return false;
	}
	else
	{
		return true;
	}
}

bool VHDFile::this_has_sector(_i64 sector_size)
{
	unsigned int block=(unsigned int)(curr_offset/blocksize);
	unsigned int bat_ref=big_endian(bat[block]);
	if(bat_ref==0xFFFFFFFF)
	{
		return false;
	}
	else
	{
		return true;
	}
}

char *VHDFile::getUID(void)
{
	return footer.uid;
}

unsigned int VHDFile::getTimestamp(void)
{
	return big_endian(footer.timestamp);
}

unsigned int VHDFile::getBlocksize()
{
	return blocksize;
}

bool VHDFile::isOpen(void)
{
	return is_open;
}

std::string VHDFile::getFilename(void)
{
	return file->getFilename();
}

std::string VHDFile::Read(_u32 tr, bool *has_error)
{
	std::string ret;
	ret.resize(4096);
	size_t read;
	bool b=Read((char*)ret.c_str(), 4096, read);
	if(!b)
	{
		if(has_error) *has_error=true;
		ret.clear();
		return ret;
	}

	ret.resize(read);
	return ret;
}

std::string VHDFile::Read(int64 spos, _u32 tr, bool* has_error)
{
	if(!Seek(spos))
	{
		if (has_error) *has_error = true;
		return std::string();
	}

	return Read(tr);
}

_u32 VHDFile::Read(char* buffer, _u32 bsize, bool *has_error)
{
	size_t read;
	bool b=Read(buffer, bsize, read);
	if(!b)
	{
		if(has_error) *has_error=true;
		return 0;
	}
	else
	{
		return (_u32)read;
	}
}

_u32 VHDFile::Read(int64 spos, char* buffer, _u32 bsize, bool* has_error)
{
	if(!Seek(spos))
	{
		if (has_error) *has_error = true;
		return 0;
	}

	return Read(buffer, bsize);
}

_u32 VHDFile::Write(const std::string &tw, bool *has_error)
{
	return Write(tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 VHDFile::Write(int64 spos, const std::string &tw, bool *has_error)
{
	return Write(spos, tw.c_str(), (_u32)tw.size(), has_error);
}

_i64 VHDFile::Size(void)
{
	return (_i64)getSize();
}

_i64 VHDFile::RealSize(void)
{
	return (_i64)usedSize();
}

void VHDFile::addVolumeOffset(_i64 offset)
{
	volume_offset=offset;
}

void VHDFile::print_last_error()
{
#ifdef _WIN32
	Server->Log("Last error: "+convert((int)GetLastError()), LL_ERROR);
#else
	Server->Log("Last error: "+convert(errno), LL_ERROR);
#endif
}

bool VHDFile::check_if_compressed()
{
	const char header_magic[] = "URBACKUP COMPRESSED FILE";
	std::string magic = backing_file->Read(sizeof(header_magic)-1);

	return magic == std::string(header_magic);
}

bool VHDFile::finish()
{
	if (finished)
	{
		return true;
	}

	if (!is_open)
	{
		return false;
	}

	switchBitmap(0);
	if(fast_mode && !read_only)
	{
		if(!write_footer())
		{
			Server->Log("Error writing footer", LL_ERROR);
			return false;
		}
		if(!write_bat())
		{
			Server->Log("Error writing BAT", LL_ERROR);
			return false;
		}
	}

	if(parent!=NULL)
	{
		if(!parent->finish())
		{
			return false;
		}
	}

	CompressedFile* compfile = dynamic_cast<CompressedFile*>(file);
	if(compfile!=NULL)
	{
		if (compfile->finish())
		{
			finished = true;
			return true;
		}
	}
	else
	{
		if (read_only)
		{
			finished = true;
			return true;
		}
		if (file->Sync())
		{
			finished = true;
			return true;
		}
	}

	return false;
}

VHDFile* VHDFile::getParent()
{
	return parent;
}

bool VHDFile::isCompressed()
{
	return compressed_file!=NULL;
}

bool VHDFile::makeFull( _i64 fs_offset, IVHDWriteCallback* write_callback)
{
	FileWrapper devfile(this, fs_offset);
	std::auto_ptr<IReadOnlyBitmap> bitmap_source;

	bitmap_source.reset(new ClientBitmap(backing_file->getFilename() + ".cbitmap"));

	if (bitmap_source->hasError())
	{
		Server->Log("Error reading client bitmap. Falling back to reading bitmap from NTFS", LL_WARNING);

		bitmap_source.reset(new FSNTFS(&devfile, false, false));
	}

	if(bitmap_source->hasError())
	{
		Server->Log("Error opening NTFS bitmap. Cannot convert incremental to full image.", LL_WARNING);
		return false;
	}

	unsigned int bitmap_blocksize = static_cast<unsigned int>(bitmap_source->getBlocksize());

	std::vector<char> buffer;
	buffer.resize(sector_size);

	int64 ntfs_blocks_per_vhd_sector = blocksize / bitmap_blocksize;

	for(int64 ntfs_block=0, n_ntfs_blocks = devfile.Size()/ bitmap_blocksize;
		ntfs_block<n_ntfs_blocks; ntfs_block+= ntfs_blocks_per_vhd_sector)
	{
		bool has_vhd_sector = false;
		for (int64 i = ntfs_block;
			i < ntfs_block + ntfs_blocks_per_vhd_sector
			&& i<n_ntfs_blocks; ++i)
		{
			if ( bitmap_source->hasBlock(i) )
			{
				has_vhd_sector = true;
				break;
			}
		}

		if(has_vhd_sector)
		{
			int64 block_pos = fs_offset + ntfs_block*bitmap_blocksize;
			int64 max_block_pos = (std::min)(fs_offset + ntfs_block*bitmap_blocksize + blocksize,
				fs_offset + n_ntfs_blocks*bitmap_blocksize);
			for(int64 i = block_pos;i<max_block_pos;i+=sector_size)
			{
				Seek(i);

				if( !has_block(false)
					&& has_block(true) )
				{
					bool has_error = false;
					if(Read(buffer.data(), sector_size)!=sector_size)
					{
						Server->Log("Error converting incremental to full image. Cannot read from parent VHD file at position "+convert(i), LL_WARNING);
						return false;
					}
					
					if(!write_callback->writeVHD(i, buffer.data(), sector_size))
					{
						Server->Log("Error converting incremental to full image. Cannot write to VHD file at position "+convert(i), LL_WARNING);
						return false;
					}
				}
			}
		}
		else
		{
			int64 block_pos = ntfs_block*bitmap_blocksize;
			int64 max_block_pos = (std::min)(ntfs_block*bitmap_blocksize + blocksize,
				n_ntfs_blocks*bitmap_blocksize);

			write_callback->emptyVHDBlock(block_pos, max_block_pos);
		}
	}

	delete parent;
	parent = NULL;

	Server->Log("Writing new headers...", LL_INFO);

	/**
	* For some reason Windows won't open the VHD if
	* the BAT is not shifted to follow the dynamic header
	*/
	bat_offset = def_bat_offset;

	if(!write_header(false) ||
		!write_dynamicheader(NULL, 0, "") ||
		!write_footer() ||
		!write_bat() )
	{
		Server->Log("Error writing new headers", LL_WARNING);
		return false;
	}

	return true;
}

bool VHDFile::setUnused(_i64 unused_start, _i64 unused_end)
{
	if (!Seek(unused_start))
	{
		Server->Log("Error while sseking to "+convert(unused_end)+" in VHD file. Size is "+convert(dstsize)+" -2", LL_ERROR);
		return false;
	}

	if (read_only)
	{
		Server->Log("VHD file is read only -2", LL_ERROR);
		return false;
	}

	if (static_cast<uint64>(unused_end)>dstsize)
	{
		Server->Log("VHD file is not large enough. Want to trim till " + convert(unused_end) + " but size is " + convert(dstsize), LL_ERROR);
		return false;
	}

	bool dwrite_footer = false;

	uint64 block = curr_offset / ((uint64)blocksize);
	size_t blockoffset = curr_offset%blocksize;
	size_t remaining = blocksize - blockoffset;
	size_t towrite = unused_end- unused_start;
	size_t bufferoffset = 0;
	bool firstw = true;
	std::vector<char> zero_buf;

	while (true)
	{
		uint64 dataoffset;
		unsigned int bat_ref = big_endian(bat[block]);
		bool new_block = false;
		if (bat_ref == 0xFFFFFFFF)
		{
			dataoffset = nextblock_offset;
			nextblock_offset += blocksize + bitmap_size;
			nextblock_offset = nextblock_offset + (sector_size - nextblock_offset%sector_size);
			dwrite_footer = true;
			new_block = true;
			bat[block] = big_endian((unsigned int)(dataoffset / (uint64)(sector_size)));
		}
		else
		{
			dataoffset = (uint64)bat_ref*(uint64)sector_size;
		}
		if (currblock != block)
		{
			switchBitmap(dataoffset);

			bool b = file->Seek(dataoffset);
			if (!b)
				Server->Log("Seeking failed", LL_ERROR);

			if (!new_block)
				file->Read(reinterpret_cast<char*>(bitmap.data()), bitmap_size);
			else
			{
				memset(bitmap.data(), 0, bitmap_size);
				_u32 rc = file->Write(reinterpret_cast<char*>(bitmap.data()), bitmap_size);
				if (rc != bitmap_size)
				{
					Server->Log("Writing bitmap failed", LL_ERROR);
					print_last_error();
					return false;
				}
			}
			currblock = block;
		}

		while (blockoffset < blocksize)
		{
			size_t wantwrite = (std::min)((size_t)sector_size, towrite);
			if (remaining < wantwrite)
				wantwrite = remaining;
			if (firstw && blockoffset%sector_size != 0 && wantwrite > sector_size - blockoffset%sector_size)
			{
				wantwrite = sector_size - blockoffset%sector_size;
				firstw = false;
			}
			else
			{
				firstw = false;
			}

			/**
			* This is counter-intuitive. We want it to return zeroes on reads but if we
			* set the bit to false it will read from the parent. So set it to true
			* to read the zeroes from the current VHD file.
			* This only works if we have not written to the same location previously, so
			* write zeroes in this case.
			*/
			if (!setBitmapBit((unsigned int)blockoffset, true))
			{
				if (zero_buf.size() != wantwrite)
				{
					zero_buf.resize(wantwrite);
				}
				_u32 rc = file->Write(dataoffset + bitmap_size + blockoffset, zero_buf.data(), (_u32)wantwrite);
				if (rc != wantwrite)
				{
					Server->Log("Writing to file failed (2)", LL_ERROR);
					print_last_error();
					return false;
				}
			}

			bufferoffset += wantwrite;
			blockoffset += wantwrite;
			remaining -= wantwrite;
			towrite -= wantwrite;
			if (towrite == 0)
			{
				break;
			}
			if (remaining == 0)
			{
				break;
			}
		}

		if (!fast_mode)
		{
			file->Seek(dataoffset);
			_u32 rc = file->Write(reinterpret_cast<char*>(bitmap.data()), bitmap_size);
			if (rc != bitmap_size)
			{
				Server->Log("Writing bitmap failed", LL_ERROR);
				print_last_error();
				return false;
			}
		}

		if (towrite == 0)
			break;

		++block;
		blockoffset = 0;
		remaining = blocksize;
	}

	if (dwrite_footer && !fast_mode)
	{
		if (!write_footer())
		{
			Server->Log("Error writing footer (2)", LL_ERROR);
			return false;
		}
		if (!write_bat())
		{
			Server->Log("Error writing BAT (2)", LL_ERROR);
			return false;
		}
	}

	return true;
}

bool VHDFile::PunchHole( _i64 spos, _i64 size )
{
	return false;
}

bool VHDFile::Sync()
{
	return finish();
}
