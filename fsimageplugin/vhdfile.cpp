/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "vhdfile.h"
#include "../Interface/Server.h"
#include "../Interface/Types.h"
#include "../stringtools.h"
#include <memory.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

const uint64 def_header_offset=0;
const uint64 def_dynamic_header_offset=512;
const uint64 def_bat_offset=512+1024;
const uint64 def_bat_offset_parent=2*1024;
const int64 unixtime_offset=946684800;

const unsigned int sector_size=512;

inline unsigned int endian_swap(unsigned int x)
{
    return (x>>24) | 
        ((x<<8) & 0x00FF0000) |
        ((x>>8) & 0x0000FF00) |
        (x<<24);
}

inline unsigned short endian_swap(unsigned short x)
{
    return x = (x>>8) | 
        (x<<8);
}

inline std::string endian_swap_utf16(std::string str)
{
	for(size_t i=0;i<str.size();i+=2)
	{
		unsigned short *t=(unsigned short*)&str[i];
		*t=endian_swap(*t);
	}
	return str;
}

inline uint64 endian_swap(uint64 x)
{
#ifdef _WIN32
    return (x>>56) | 
        ((x<<40) & 0x00FF000000000000) |
        ((x<<24) & 0x0000FF0000000000) |
        ((x<<8)  & 0x000000FF00000000) |
        ((x>>8)  & 0x00000000FF000000) |
        ((x>>24) & 0x0000000000FF0000) |
        ((x>>40) & 0x000000000000FF00) |
        (x<<56);
#else
    return (x>>56) | 
        ((x<<40) & 0x00FF000000000000LLU) |
        ((x<<24) & 0x0000FF0000000000LLU) |
        ((x<<8)  & 0x000000FF00000000LLU) |
        ((x>>8)  & 0x00000000FF000000LLU) |
        ((x>>24) & 0x0000000000FF0000LLU) |
        ((x>>40) & 0x000000000000FF00LLU) |
        (x<<56);
#endif
}

VHDFile::VHDFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize, unsigned int pBlocksize, bool fast_mode)
	: dstsize(pDstsize), blocksize(pBlocksize), fast_mode(fast_mode), bitmap_offset(0), bitmap_dirty(false), volume_offset(0)
{
	parent=NULL;
	read_only=pRead_only;
	is_open=false;
	curr_offset=0;
	bitmap=NULL;
	currblock=0xFFFFFFFF;

	file=Server->openFile(fn, (read_only?MODE_READ:MODE_RW) );

	if(!file)
	{
		if(read_only==false)
		{
			file=Server->openFile(fn, MODE_WRITE);
		}
		if(file==NULL)
		{
			Server->Log("Error opening VHD file", LL_ERROR);
			return;
		}
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
		write_dynamicheader(NULL, 0, L"");
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

VHDFile::VHDFile(const std::wstring &fn, const std::wstring &parent_fn, bool pRead_only, bool fast_mode)
	: fast_mode(fast_mode), bitmap_offset(0), bitmap_dirty(false), volume_offset(0)
{
	curr_offset=0;
	is_open=false;
	read_only=pRead_only;
	parent=NULL;
	curr_offset=0;
	bitmap=NULL;
	currblock=0xFFFFFFFF;

	file=Server->openFile(fn, (read_only?MODE_READ:MODE_RW) );
	if(!file)
	{
		if(read_only==false)
		{
			file=Server->openFile(fn, MODE_WRITE);
		}
		if(file==NULL)
		{
			Server->Log("Error opening VHD file", LL_ERROR);
			return;
		}
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
	switchBitmap(0);
	if(fast_mode && !read_only)
	{
		if(!write_footer())
		{
			Server->Log("Error writing footer", LL_ERROR);
		}
		if(!write_bat())
		{
			Server->Log("Error writing BAT", LL_ERROR);
		}
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
	footer.features=endian_swap((unsigned int)0x00000002);
	footer.format_version=endian_swap((unsigned int)0x00010000);
	footer.data_offset=endian_swap(dynamic_header_offset);
	footer.timestamp=endian_swap((unsigned int)(Server->getTimeSeconds()-unixtime_offset));
	footer.creator_application[0]='v';
	footer.creator_application[1]='p';
	footer.creator_application[2]='c';
	footer.creator_application[3]=' ';
	footer.creator_version=endian_swap((unsigned int)0x00050003);
	footer.creator_os=endian_swap((unsigned int)0x5769326B);
	footer.original_size=endian_swap(dstsize);
	footer.current_size=endian_swap(dstsize);
	footer.disk_geometry=calculate_chs();
	if(diff)
		footer.disk_type=endian_swap((unsigned int)4);
	else
		footer.disk_type=endian_swap((unsigned int)3);
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
	return endian_swap(chs);
}

unsigned int VHDFile::calculate_checksum(const unsigned char * data, size_t dsize)
{
	unsigned int checksum=0;
	for(size_t i=0;i<dsize;++i)
	{
		checksum+=data[i];
	}
	return ~endian_swap(checksum);
}

bool VHDFile::write_dynamicheader(char *parent_uid, unsigned int parent_timestamp, std::wstring parentfn)
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
#ifdef _WIN32
	dynamicheader.dataoffset=0xFFFFFFFFFFFFFFFF;
#else
	dynamicheader.dataoffset=0xFFFFFFFFFFFFFFFFLLU;
#endif
	dynamicheader.tableoffset=endian_swap(bat_offset);
	dynamicheader.header_version=endian_swap((unsigned int)0x00010000);
	dynamicheader.table_entries=endian_swap(batsize);
	dynamicheader.blocksize=endian_swap(blocksize);
	dynamicheader.checksum=0;
	if(parent_uid!=NULL)
	{
		//Differencing file
		memcpy(dynamicheader.parent_uid, parent_uid, 16);
		dynamicheader.parent_timestamp=endian_swap(parent_timestamp);
		std::string unicodename=endian_swap_utf16(Server->ConvertToUTF16(ExtractFileName(parentfn)));
		std::string rel_unicodename=Server->ConvertToUTF16(L".\\"+ExtractFileName(parentfn));
		std::string abs_unicodename=Server->ConvertToUTF16(parentfn);
		unicodename.resize(unicodename.size()+2);
		unicodename[unicodename.size()-2]=0;
		unicodename[unicodename.size()-1]=0;
		memcpy(dynamicheader.parent_unicodename, &unicodename[0], unicodename.size());
		unsigned int locator_blocks_abs=(abs_unicodename.size())/sector_size+((abs_unicodename.size())%sector_size!=0)?1:0;
		dynamicheader.parentlocator[0].platform_code=endian_swap((unsigned int)0x57326B75);
		dynamicheader.parentlocator[0].platform_space=endian_swap(locator_blocks_abs*sector_size);
		dynamicheader.parentlocator[0].platform_length=endian_swap((unsigned int)abs_unicodename.size());
		uint64 abs_locator_offset=def_bat_offset_parent-512;
		if(locator_blocks_abs>1)
		{
			abs_locator_offset=nextblock_offset;
			nextblock_offset+=abs_locator_offset*sector_size;
		}
		dynamicheader.parentlocator[0].platform_offset=endian_swap(abs_locator_offset);

		if(!file->Seek(abs_locator_offset))
			return false;
		_u32 rc=file->Write(abs_unicodename.c_str(), (_u32)abs_unicodename.size());
		if(rc!=abs_unicodename.size())
			return false;

		dynamicheader.parentlocator[1].platform_code=endian_swap((unsigned int)0x57327275);
		unsigned int locator_blocks=(rel_unicodename.size())/sector_size+((rel_unicodename.size())%sector_size!=0)?1:0;
		if(locator_blocks<128) locator_blocks=128;
		dynamicheader.parentlocator[1].platform_space=endian_swap(locator_blocks*sector_size);
		dynamicheader.parentlocator[1].platform_length=endian_swap((unsigned int)rel_unicodename.size());
		dynamicheader.parentlocator[1].platform_offset=endian_swap(nextblock_offset);
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
	if(endian_swap(footer.format_version)!=0x00010000)
	{
		Server->Log("Unrecognized vhd format version", LL_ERROR);
		return false;
	}

	if(endian_swap(footer.disk_type)!=3 && endian_swap(footer.disk_type)!=4 )
	{
		Server->Log("Unsupported disk type", LL_ERROR);
		return false;
	}

	dstsize=endian_swap(footer.current_size);
	header_offset=0;
	dynamic_header_offset=endian_swap(footer.data_offset);
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

	bat_offset=endian_swap(dynamicheader.tableoffset);
	batsize=endian_swap(dynamicheader.table_entries);
	blocksize=endian_swap(dynamicheader.blocksize);

	if(endian_swap(footer.disk_type)==4)
	{
		//differencing hd
		std::string parent_unicodename;
		parent_unicodename.resize(512);
		memcpy(&parent_unicodename[0], dynamicheader.parent_unicodename, 512);
		parent_unicodename=endian_swap_utf16(parent_unicodename);
		std::wstring parent_fn=Server->ConvertFromUTF16(parent_unicodename);
		parent_fn.resize(wcslen(parent_fn.c_str()));
		parent_fn=ExtractFilePath(file->getFilenameW())+L"/"+parent_fn;
		Server->Log(L"VHD-Parent: \""+parent_fn+L"\"", LL_INFO);
		parent=new VHDFile(parent_fn, true, 0);

		if(parent->isOpen()==false)
		{
			Server->Log(L"Error opening Parentvhdfile \""+parent_fn+L"\"", LL_ERROR);
			return false;
		}

		if(memcmp(parent->getUID(), dynamicheader.parent_uid, 16)!=0)
		{
			Server->Log("Parent uid wrong", LL_ERROR);
			return false;
		}

		if(parent->getTimestamp()!=endian_swap(dynamicheader.parent_timestamp) )
		{
			Server->Log("Parent timestamp wrong. Parent was modified? Continueing anyways. But this is dangerous!", LL_ERROR);
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

	bitmap=new unsigned char[bitmap_size];
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

inline void VHDFile::setBitmapBit(unsigned int offset, bool v)
{
	size_t sector=offset/sector_size;
	size_t bitmap_byte=(size_t)(sector/8);
	size_t bitmap_bit=sector%8;

	unsigned char b=bitmap[bitmap_byte];

	if(v==true)
		b=b|(1<<(7-bitmap_bit));
	else
		b=b&(~(1<<(7-bitmap_bit)));

	bitmap[bitmap_byte]=b;
	bitmap_dirty=true;
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
		unsigned int bat_off=endian_swap(bat[block]);
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
				Server->Log("Wrong dataoffset: "+nconvert(dataoffset), LL_ERROR);
				return false;
			}

			if(file->Read((char*)bitmap, bitmap_size)!=bitmap_size)
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

_u32 VHDFile::Write(const char *buffer, _u32 bsize)
{
	if(read_only)
	{
		Server->Log("VHD file is read only", LL_ERROR);
		return 0;
	}
	if(bsize+curr_offset>dstsize)
	{
		Server->Log("VHD file is not large enough", LL_ERROR);
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
		unsigned int bat_ref=endian_swap(bat[block]);
		bool new_block=false;
		if(bat_ref==0xFFFFFFFF)
		{
			dataoffset=nextblock_offset;
			nextblock_offset+=blocksize+bitmap_size;
			nextblock_offset=nextblock_offset+(sector_size-nextblock_offset%sector_size);
			dwrite_footer=true;
			new_block=true;
			bat[block]=endian_swap((unsigned int)(dataoffset/(uint64)(sector_size)));
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
				file->Read((char*)bitmap, bitmap_size);
			else
			{
				memset(bitmap, 0, bitmap_size );
				_u32 rc=file->Write((char*)bitmap, bitmap_size);
				if(rc!=bitmap_size)
				{
					Server->Log("Writing bitmap failed", LL_ERROR);
					return 0;
				}
			}
			currblock=block;
		}

		bool b=file->Seek(dataoffset+bitmap_size+blockoffset);
		if(!b)
		{
			Server->Log("Seeking in file failed", LL_ERROR);
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
			_u32 rc=file->Write((char*)bitmap, bitmap_size);
			if(rc!=bitmap_size)
			{
				Server->Log("Writing bitmap failed", LL_ERROR);
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
			return 0;
		}
		if(!write_bat())
		{
			Server->Log("Error writing BAT", LL_ERROR);
			return 0;
		}
	}

	curr_offset+=bsize;

	return bsize;
}

void VHDFile::switchBitmap(uint64 new_offset)
{
	if(fast_mode && !read_only && bitmap_dirty && bitmap_offset!=0)
	{
		file->Seek(bitmap_offset);
		_u32 rc=file->Write((char*)bitmap, bitmap_size);
		if(rc!=bitmap_size)
		{
			Server->Log("Writing bitmap failed", LL_ERROR);
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

bool VHDFile::has_sector(void)
{
	unsigned int block=(unsigned int)(curr_offset/blocksize);
	unsigned int bat_ref=endian_swap(bat[block]);
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

bool VHDFile::this_has_sector(void)
{
	unsigned int block=(unsigned int)(curr_offset/blocksize);
	unsigned int bat_ref=endian_swap(bat[block]);
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
	return endian_swap(footer.timestamp);
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

std::wstring VHDFile::getFilenameW(void)
{
	return file->getFilenameW();
}

std::string VHDFile::Read(_u32 tr)
{
	std::string ret;
	ret.resize(4096);
	size_t read;
	bool b=Read((char*)ret.c_str(), 4096, read);
	if(!b)
	{
		ret.clear();
		return ret;
	}

	ret.resize(read);
	return ret;
}

_u32 VHDFile::Read(char* buffer, _u32 bsize)
{
	size_t read;
	bool b=Read(buffer, bsize, read);
	if(!b)
	{
		return 0;
	}
	else
	{
		return (_u32)read;
	}
}

_u32 VHDFile::Write(const std::string &tw)
{
	return Write(tw.c_str(), (_u32)tw.size());
}

_i64 VHDFile::Size(void)
{
	return (_i64)getSize();
}

void VHDFile::addVolumeOffset(_i64 offset)
{
	volume_offset=offset;
}