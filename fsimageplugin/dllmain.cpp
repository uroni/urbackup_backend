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

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif

#include <iostream>
#include <memory.h>
#include <stdio.h>
#include <string>

#ifndef STATIC_PLUGIN
#define DEF_SERVER
#endif

#include "../Interface/Server.h"

#ifndef STATIC_PLUGIN
IServer *Server;
#else
#include "../StaticPluginRegistration.h"

extern IServer* Server;

#define LoadActions LoadActions_fsimageplugin
#define UnloadActions UnloadActions_fsimageplugin
#endif

#include "../Interface/Action.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../urbackupcommon/mbrdata.h"

#include <stdlib.h>

#include "vhdfile.h"
#ifndef _WIN32
#include "cowfile.h"
#endif
#include "fs/ntfs.h"

#include "pluginmgr.h"

#include "CompressedFile.h"

#ifdef _WIN32
#include "win_dialog.h"
#endif
#include "FileWrapper.h"

#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <errno.h>
#endif

#include "ClientBitmap.h"


CImagePluginMgr *imagepluginmgr;

void PrintInfo(IFilesystem *fs);

namespace
{
	bool decompress_vhd(const std::string& fn, const std::string& output)
	{
		std::string tmp_output=output;

		if(fn==output)
		{
			tmp_output+=".tmp";
		}

		IFile* out = Server->openFile(tmp_output, MODE_WRITE);
		ObjectScope out_file(out);

		if(out==NULL)
		{
			Server->Log("Error opening output file \""+output+"\"", LL_ERROR);
			return false;
		}

		if(findextension(fn)=="vhdz")
		{
			std::auto_ptr<VHDFile> vhdfile(new VHDFile(fn, true, 0));

			if(vhdfile->isOpen() && vhdfile->getParent()!=NULL)
			{
				if(vhdfile->isCompressed())
				{
					std::string parent_fn = vhdfile->getParent()->getFilename();
					vhdfile.reset();
					Server->Log("Decompressing parent VHD \""+parent_fn+"\"...", LL_INFO);
					bool b = decompress_vhd(parent_fn, parent_fn);

					if(!b)
					{
						Server->Log("Error decompressing parent VHD", LL_ERROR);
						return false;
					}
				}				
			}
		}

		{
			CompressedFile compFile(fn, MODE_READ);

			if(compFile.hasError())
			{
				if(compFile.hasNoMagic())
				{
					Server->Log("File is not compressed. No need to decompress.", LL_WARNING);
					return true;
				}
				else
				{
					Server->Log("Error while reading compressed file header", LL_ERROR);
					return false;
				}
			}

			int pcdone = -1;
			__int64 decompressed = 0;
			char buffer[32768];
			_u32 read;
			do 
			{
				read = compFile.Read(buffer, 32768);
				if(read>0)
				{
					if(out->Write(buffer, read)!=read)
					{
						Server->Log("Error writing to output file", LL_ERROR);
						return false;
					}
				}
				decompressed+=read;
				int currentpc = static_cast<int>(static_cast<float>(decompressed)/compFile.Size()*100.f+0.5f);
				if(currentpc!=pcdone)
				{
					pcdone=currentpc;
					Server->Log("Decompressing \""+fn+"\"... "+convert(pcdone)+"%", LL_INFO);
				}
			} while (read>0);

			compFile.finish();
		}		

		if(fn==output)
		{
			out_file.clear();
#ifdef _WIN32
			return MoveFileExW(Server->ConvertToWchar(tmp_output).c_str(), Server->ConvertToWchar(fn).c_str(), MOVEFILE_REPLACE_EXISTING)==TRUE;
#else
			return rename(tmp_output.c_str(), fn.c_str())==0;
#endif
		}
		else
		{
			return true;
		}
	}


	IVHDFile* open_device_file(std::string device_verify)
	{
		std::string ext = strlower(findextension(device_verify));
		if(ext=="vhd" || ext=="vhdz")
		{
			return new VHDFile(device_verify, true,0);
		}
#if !defined(_WIN32) && !defined(__APPLE__)
		else if(ext=="raw")
		{
			return new CowFile(device_verify, true,0);
		}
#endif
		else
		{
			Server->Log("Unknown image file extension \""+ext+"\"", LL_ERROR);
			return NULL;
		}
	}
	
	struct partition
	{
		unsigned char boot_flag;       
		unsigned char chs_begin[3];
		unsigned char sys_type;
		unsigned char chs_end[3];
		unsigned int start_sector;
		unsigned int nr_sector;
	};

	void show_progress(int64 pos, int64 max)
	{
		static int count=0;
		++count;
		if(count%1000==0)
		{
			static int lastpc=-1;

			int currentpc = static_cast<int>(static_cast<float>(pos)/max*100.f+0.5f);
			if(currentpc!=lastpc)
			{
				lastpc=currentpc;
				Server->Log("Assembling... "+convert(currentpc)+"%", LL_INFO);
			}
		}
	}

	bool assemble_vhd(const std::vector<std::string>& fn, const std::string& output)
	{
		//This function leaks currently. Do exit program after function!

		if(fn.empty())
		{
			Server->Log("No input VHD", LL_ERROR);
			return false;
		}

		std::vector<SMBRData> mbrdatas;

		for(size_t i=0;i<fn.size();++i)
		{
			std::auto_ptr<IFile> f(Server->openFile(fn[i]+".mbr", MODE_READ));
			if(f.get()==NULL)
			{
				Server->Log("Could not open MBR file "+fn[i]+".mbr", LL_ERROR);
				exit(1);
			}
			size_t fsize=(size_t)f->Size();
			std::vector<char> buf;
			buf.resize(fsize);
			f->Read(buf.data(), (_u32)fsize);

			CRData mbr(buf.data(), fsize);
			SMBRData mbrdata(mbr);
			if(mbrdata.hasError())
			{
				Server->Log("Error while parsing MBR data", LL_ERROR);
				return false;
			}

			for(size_t j=0;j<mbrdatas.size();++j)
			{
				if(mbrdatas[j].partition_number==mbrdata.partition_number)
				{
					Server->Log("Volume "+mbrdatas[j].volume_name+" ("+
						fn[j]+") has the same partition number as the volume "+mbrdata.volume_name+
						" ("+fn[i]+"). Please make sure you only select volumes from one device.", LL_ERROR);
					return false;
				}
			}

			mbrdatas.push_back(mbrdata);
		}

		std::string mbr = mbrdatas[0].mbr_data;

		partition* partitions = reinterpret_cast<partition*>(&mbr[446]);

		std::string skip_s=Server->getServerParameter("skip");
		int skip=1024*512;
		if(!skip_s.empty())
		{
			skip=atoi(skip_s.c_str());
		}

		
		std::vector<IFile*> input_files;
		std::vector<IFilesystem*> input_fs;

		input_files.resize(fn.size());
		input_fs.resize(fn.size());

		int64 total_copy_bytes = 0;
		const int64 c_sector_size=512;
		int64 total_size = 0;
		int64 total_written = 0;

		for(size_t i=0;i<fn.size();++i)
		{
			VHDFile* in(new VHDFile(fn[i], true, 0));
			if(!in->isOpen())
			{
				Server->Log("Error opening VHD-File \""+fn[i]+"\"", LL_ERROR);
				return false;
			}

			input_files[i] = new FileWrapper(in, skip);

			FSNTFS *ntfs = new FSNTFS(input_files[i], false, false);

			if(!ntfs->hasError())
			{
				input_fs[i]=ntfs;

				total_copy_bytes+=ntfs->calculateUsedSpace();
			}
			else
			{
				Server->Log("Volume "+mbrdatas[i].volume_name+" not an NTFS volume. Copying all blocks...", LL_WARNING);
				total_copy_bytes+=input_files[i]->Size();
			}

			partition* cpart = partitions + (mbrdatas[i].partition_number-1);

			/*unsigned char chs_expected[3] = { 0xfe, 0xff, 0xff };
			if(memcmp(cpart->chs_begin, chs_expected, 3)!=0
				|| memcmp(cpart->chs_end, chs_expected,3)!=0)
			{
				Server->Log(L"MBR partition of Volume "+mbrdatas[i].volume_name+L" does not use LBA addressing scheme", LL_ERROR);
				return false;
			}*/

			cpart->start_sector=little_endian(cpart->start_sector);
			cpart->nr_sector=little_endian(cpart->nr_sector);

			total_size = (std::max)(total_size, cpart->start_sector*c_sector_size + cpart->nr_sector*c_sector_size);
		}

		VHDFile vhdout(output, false, total_size, 2*1024*1024, true, false);
		if(!vhdout.isOpen())
		{
			Server->Log("Error opening output VHD-File \""+output+"\"", LL_ERROR);
			return false;
		}

		int64 curr_pos=0;

		vhdout.Seek(0);

		if(vhdout.Write(mbr.data(), static_cast<_u32>(mbr.size()))!=static_cast<_u32>(mbr.size()))
		{
			Server->Log("Error writing MBR", LL_ERROR);
			return false;
		}

		for(size_t i=0;i<fn.size();++i)
		{
			Server->Log("Writing "+fn[i]+" into output VHD...");

			partition* cpart = partitions + (mbrdatas[i].partition_number-1);
			int64 out_pos = cpart->start_sector*c_sector_size;
			int64 max_pos = out_pos + cpart->nr_sector*c_sector_size;

			if(input_fs[i]!=NULL)
			{
				Server->Log("Optimized by only writing used NTFS sectors...");

				int64 blocksize = input_fs[i]->getBlocksize();
				int64 numblocks = input_fs[i]->getSize()/blocksize;

				for(int64 block=0;block<numblocks;++block)
				{
					char* buf = input_fs[i]->readBlock(block);
					if(buf!=NULL)
					{
						fs_buffer fsb(input_fs[i], buf);

						vhdout.Seek(out_pos);
						if(vhdout.Write(buf, static_cast<_u32>(blocksize))!=static_cast<_u32>(blocksize))
						{
							Server->Log("Error writing to VHD output file", LL_ERROR);
							return false;
						}
					}					
					
					out_pos+=blocksize;

					if(out_pos>max_pos)
					{
						Server->Log("Trying to write beyon partition", LL_ERROR);
						return false;
					}


					total_written+=blocksize;
					show_progress(total_written, total_size);
				}
			}
			else
			{
				std::vector<char> buf;
				buf.resize(32768);

				vhdout.Seek(out_pos);

				if(out_pos+input_files[i]->Size()>max_pos)
				{
					Server->Log("Trying to write beyond partition (2)", LL_ERROR);
					return false;
				}

				input_files[i]->Seek(0);

				for(int64 pos=0, size=input_files[i]->Size();pos<size;)
				{
					int64 toread = (std::min)(static_cast<int64>(buf.size()), size-pos);

					_u32 read = input_files[i]->Read(buf.data(), static_cast<_u32>(toread));

					if(read!=toread)
					{
						Server->Log("Error reading from input VHD file", LL_ERROR);
						return false;
					}

					if(vhdout.Write(buf.data(), read)!=read)
					{
						Server->Log("Error writing to VHD output file (2)", LL_ERROR);
						return false;
					}

					total_written+=read;
					pos+=read;
					show_progress(total_written, total_size);
				}
			}
		}

		return true;
	}

#ifdef __linux__
	void fibmap_test(std::string fn, std::string bitmap_source)
	{
#ifdef FIBMAP
		//leaks

		std::auto_ptr<IFile> f(Server->openFile(fn, MODE_READ));

		int fd = open64(fn.c_str(), O_RDONLY| O_LARGEFILE);

		if (fd == -1 || f.get()==NULL)
		{
			Server->Log("Error opening " + fn, LL_ERROR);
			return;
		}

		IReadOnlyBitmap* bitmap = NULL;
		CowFile* cowfile = NULL;
		if (next(bitmap_source, 0, "raw:"))
		{
			cowfile = new CowFile(bitmap_source.substr(4), true, 0);
			if (!cowfile->isOpen())
			{
				Server->Log("Error opening cowfile " + bitmap_source, LL_ERROR);
				return;
			}

			FileWrapper* file_wrapper = new FileWrapper(cowfile, 512 * 1024);

			FSNTFS* ntfs = new FSNTFS(file_wrapper, false, false);

			if (ntfs->hasError())
			{
				Server->Log("Error opening ntfs on cowfile " + bitmap_source, LL_ERROR);
				return;
			}

			bitmap = ntfs;
		}
		else if (next(bitmap_source, 0, "cbitmap:"))
		{
			ClientBitmap* cbitmap = new ClientBitmap(bitmap_source.substr(8));
			if (cbitmap->hasError())
			{
				Server->Log("Error opening client bitmap " + bitmap_source, LL_ERROR);
				return;
			}

			bitmap = cbitmap;
		}
		else
		{
			Server->Log("Unknown bitmap source: " + bitmap_source, LL_ERROR);
			return;
		}
		
		int block_size;
		int rc = ioctl(fd, FIGETBSZ, &block_size);
		if(rc!=0)
		{
			Server->Log("FIGETBSZ ioctl failed with errno "+convert(errno), LL_ERROR);
			return;
		}
		
		Server->Log("Block size: "+convert(block_size)+" File size: "+convert(f->Size()), LL_INFO);

		_u32 exp_block = 0;
		for (int64 i = 0; i < f->Size(); i += block_size)
		{
			_u32 input_blocknum = i / block_size;
			int rc = ioctl(fd, FIBMAP, &input_blocknum);
			int64 blocknum = input_blocknum;

			if (rc != 0)
			{
				Server->Log("FIBMAP ioctl failed with errno "+convert(errno), LL_ERROR);
				return;
			}

			if (cowfile != NULL)
			{
				cowfile->Seek(512*1024 + blocknum * block_size);
				if (!cowfile->has_sector())
				{
					Server->Log("Cowfile does not have block at " + convert(blocknum*block_size), LL_ERROR);
				}
			}

			if (bitmap != NULL)
			{
				if (!bitmap->hasBlock((blocknum*block_size)/bitmap->getBlocksize()))
				{
					Server->Log("Bitmap bit for block at " + convert(blocknum*block_size)+" not set", LL_ERROR);
				}
			}

			if (blocknum!=0 && blocknum != exp_block)
			{
				Server->Log("Block " + convert(i / block_size) + " is at " + convert(blocknum*block_size));
			}

			exp_block = blocknum + 1;
		}
#endif //FIBMAP
	}
#endif //__linux__
}

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	std::string compress_file = Server->getServerParameter("compress");
	if(!compress_file.empty())
	{
		IFile* in = Server->openFile(compress_file, MODE_READ_SEQUENTIAL);
		if(in==NULL)
		{
			Server->Log("Cannot open file \""+compress_file+"\" to compress", LL_ERROR);
			exit(1);
		}

		{
			Server->deleteFile(compress_file+".urz");
			CompressedFile compFile(compress_file+".urz", MODE_RW_CREATE);

			if(compFile.hasError())
			{
				Server->Log("Error opening compressed file", LL_ERROR);
				exit(3);
			}

			char buffer[32768];
			_u32 read;
			do 
			{
				read = in->Read(buffer, 32768);
				if(read>0)
				{
					if(compFile.Write(buffer, read)!=read)
					{
						Server->Log("Error writing to compressed file", LL_ERROR);
						exit(2);
					}
				}			
			} while (read>0);

			compFile.finish();
			delete in;
		}	

		exit(0);
	}

	std::string decompress = Server->getServerParameter("decompress");
	if(!decompress.empty())
	{
		bool selected_via_gui=false;
#ifdef _WIN32
		if(decompress=="SelectViaGUI")
		{
			std::string filter;
			filter += "Compressed image files (*.vhdz)";
			filter += '\0';
			filter += "*.vhdz";
			filter += '\0';
			filter += '\0';
			std::vector<std::string> res = file_via_dialog("Please select compressed image file to decompress",
				filter, false, true, "");
			if(!res.empty())
			{
				decompress = res[0];
			}
			else
			{
				decompress.clear();
			}
			

			if(decompress.empty())
			{
				exit(1);
			}
			else
			{
				selected_via_gui=true;
			}
		}
#endif

		std::string targetName = decompress;
		if(findextension(decompress)!="vhdz" && findextension(decompress)!="urz")
		{
			Server->Log("Unknown file extension: "+findextension(decompress), LL_ERROR);
			exit(1);
		}

		if(!Server->getServerParameter("output_fn").empty() && !selected_via_gui)
		{
			targetName = Server->getServerParameter("output_fn");
		}

		bool b = decompress_vhd(decompress, targetName);		

		exit(b?0:3);
	}

	std::string assemble = Server->getServerParameter("assemble");
	if(!assemble.empty())
	{
		bool selected_via_gui=false;
		std::vector<std::string> input_files;
		std::string output_file;
#ifdef _WIN32
		if(assemble=="SelectViaGUI")
		{
			std::string filter;
			filter += "Image files (*.vhdz;*.vhd)";
			filter += '\0';
			filter += "*.vhdz;*.vhd";
			filter += '\0';
			/*filter += L"Image files (*.vhd)";
			filter += '\0';
			filter += L"*.vhd";
			filter += '\0';*/
			filter += '\0';
			input_files = file_via_dialog("Please select all the images to assemble into one image",
				filter, true, true, "");

			filter.clear();
			filter += "Image file (*.vhd)";
			filter += '\0';
			filter += "*.vhd";
			filter += '\0';
			filter += '\0';

			std::vector<std::string> output_files = file_via_dialog("Please select where to save the output image",
				filter, false, false, "vhd");

			if(!output_files.empty())
			{
				output_file = output_files[0];
			}			

			selected_via_gui=true;
		}
#endif

		if(!selected_via_gui)
		{
			TokenizeMail(assemble, input_files, ";");
			output_file = Server->getServerParameter("output_file");
		}

		if(input_files.empty())
		{
			Server->Log("No input files selected", LL_ERROR);
			exit(1);
		}

		if(output_file.empty())
		{
			Server->Log("No output file selected", LL_ERROR);
			exit(1);
		}

		bool b = assemble_vhd(input_files, output_file);		

		exit(b?0:3);
	}


	std::string devinfo=Server->getServerParameter("devinfo");

	if(!devinfo.empty())
	{
		FSNTFS ntfs("\\\\.\\"+devinfo+":", false, true);
	
		if(!ntfs.hasError())
		{
			Server->Log("Used Space: "+convert(ntfs.calculateUsedSpace())+" of "+convert(ntfs.getSize()));
			Server->Log(convert(((float)ntfs.calculateUsedSpace()/(float)ntfs.getSize())*100.0f)+" %");
		}
	}

	std::string vhdcopy_in=Server->getServerParameter("vhdcopy_in");
	if(!vhdcopy_in.empty())
	{
		Server->Log("VHDCopy.");
		VHDFile in(vhdcopy_in, true,0);
		if(in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+vhdcopy_in+"\"", LL_ERROR);
			exit(4);
		}

		uint64 vhdsize=in.getSize();
		float vhdsize_gb=(vhdsize/1024)/1024.f/1024.f;
		uint64 vhdsize_mb=vhdsize/1024/1024;
		Server->Log("VHD Info: Size: "+convert(vhdsize_gb)+" GB "+convert(vhdsize_mb)+" MB",LL_INFO);
		unsigned int vhd_blocksize=in.getBlocksize();
		
		std::string vhdcopy_out=Server->getServerParameter("vhdcopy_out");
		if(vhdcopy_out.empty())
		{
			Server->Log("'vhdcopy_out' not specified. Not copying.", LL_ERROR);
			exit(5);
		}
		else
		{
			IFile *out=Server->openFile(vhdcopy_out, MODE_RW);
			if(out==NULL)
			{
				Server->Log("Couldn't open output file", LL_ERROR);
				exit(6);
			}
			else
			{
				std::string skip_s=Server->getServerParameter("skip");
				int skip=1024*512;
				if(!skip_s.empty())
				{
					skip=atoi(skip_s.c_str());
				}

				Server->Log("Skipping "+convert(skip)+" bytes...", LL_INFO);
				in.Seek(skip);
				char buffer[4096];
				size_t read;
				int last_pc=0;
				int p_skip=0;
				uint64 currpos=skip;
				bool is_ok=true;

				out->Seek(0);
				while(currpos%vhd_blocksize!=0)
				{
					is_ok=in.Read(buffer, 512, read);
					if(read>0)
					{
						_u32 rc=out->Write(buffer, (_u32)read);
						if(rc!=read)
						{
							Server->Log("Writing to output file failed", LL_ERROR);
							exit(7);
						}
					}
					currpos+=read;
				}

				if(currpos!=skip)
				{
					Server->Log("First VHD sector at "+convert(currpos), LL_INFO);
				}

				do
				{
					if(in.has_sector())
					{
						is_ok=in.Read(buffer, 4096, read);
						if(read>0)
						{
							_u32 rc=out->Write(buffer, (_u32)read);
							if(rc!=read)
							{
								Server->Log("Writing to output file failed", LL_ERROR);
								exit(7);
							}
						}
						currpos+=read;
					}
					else
					{
						read=4096;
						currpos+=read;
						in.Seek(currpos);
						out->Seek(currpos-skip);
					}
					
					++p_skip;
					if(p_skip>100)
					{
						p_skip=0;
						int pc=(int)(((float)currpos/(float)vhdsize)*100.f+0.5f);
						if(pc!=last_pc)
						{
							Server->Log(convert(pc)+"%", LL_INFO);
							last_pc=pc;
						}
					}
				}
				while( read==4096 && is_ok );

				Server->destroy(out);
				Server->Log("Copy process finished sucessfully.", LL_INFO);
				exit(0);
			}
		}
	}
	
	std::string hashfilecomp_1=Server->getServerParameter("hashfilecomp_1");
	if(!hashfilecomp_1.empty())
	{
		IFile *hf1=Server->openFile(hashfilecomp_1, MODE_READ);
		IFile *hf2=Server->openFile(Server->getServerParameter("hashfilecomp_2"), MODE_READ);
		
		if(hf1==NULL || hf2==NULL )
		{
			Server->Log("Error opening hashfile", LL_ERROR);
		}
		else
		{
			size_t h_equal=0;
			size_t h_diff=0;
			_i64 fsize=hf1->Size();
			for(_i64 p=0;p<fsize;p+=32)
			{
				char buf1[32];
				hf1->Read(buf1, 32);
				char buf2[32];
				hf2->Read(buf2, 32);
				if( memcmp(buf1, buf2, 32)==0)
				{
					++h_equal;
				}
				else
				{
					++h_diff;
				}
			}
			
			std::cout << "Hashfile analysis: " << h_equal << " equal hashes; " << h_diff << " differences " << std::endl;
		}
		
		exit(5);
	}
	
	std::string vhdinfo=Server->getServerParameter("vhdinfo");
	if(!vhdinfo.empty())
	{
		std::cout << "--VHDINFO--" << std::endl;
		VHDFile in(vhdinfo, true,0);
		if(in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+vhdinfo+"\"", LL_ERROR);
			exit(4);
		}

		uint64 vhdsize=in.getSize();
		float vhdsize_gb=(vhdsize/1024)/1024.f/1024.f;
		uint64 vhdsize_mb=vhdsize/1024/1024;
		std::cout << ("VHD Info: Size: "+convert(vhdsize_gb)+" GB "+convert(vhdsize_mb)+" MB") << std::endl;
		std::cout << "Blocksize: " << in.getBlocksize() << " Bytes" << std::endl;
		
		uint64 new_blocks=0;
		uint64 total_blocks=0;
		unsigned int bs=in.getBlocksize();
		for(uint64 pos=0;pos<vhdsize;pos+=bs)
		{
			in.Seek(pos);
			if(in.this_has_sector())
			{
				++new_blocks;
			}
			++total_blocks;
		}
		
		std::cout << "Blocks: " << new_blocks << "/" << total_blocks << std::endl;
		exit(3);
	}

	std::string image_verify=Server->getServerParameter("image_verify");
	if(!image_verify.empty())
	{
		std::auto_ptr<IVHDFile> in(open_device_file(image_verify));

		if(in.get()==NULL || in->isOpen()==false)
		{
			Server->Log("Error opening Image-File \""+image_verify+"\"", LL_ERROR);
			exit(4);
		}

		std::string s_hashfile=Server->getServerParameter("hashfile");
		bool has_hashfile=true;
		if(s_hashfile.empty())
		{
			has_hashfile=false;
			s_hashfile=image_verify+".hash";
		}

		IFile *hashfile=Server->openFile(s_hashfile, MODE_READ);
		if(hashfile==NULL)
		{
			Server->Log("Error opening hashfile");
			exit(5);
		}

		bool verify_all = Server->getServerParameter("verify_all")=="true";
		
		if(verify_all)
		{
			Server->Log("Verifying empty blocks");
		}
		
		const int64 vhd_blocksize=(1024*1024)/2;
		int skip=1024*512;
		in->Seek(skip);
		uint64 currpos=skip;
		uint64 size=in->getSize();
		sha256_ctx ctx;
		sha256_init(&ctx);
		char buf[512];
		int diff=0;
		int diff_w=0;
		size_t ok_blocks=0;
		int last_pc=0;
		unsigned char dig_z[32];
		bool has_dig_z=false;
		for(;currpos<size;currpos+=vhd_blocksize)
		{
			in->Seek(currpos);
			bool has_sector=verify_all || in->this_has_sector(vhd_blocksize);

			if(!has_sector && !has_dig_z)
			{
				for(unsigned int i=0;i<vhd_blocksize;i+=512)
				{
					size_t read;
					in->Read(buf, 512, read);
					sha256_update(&ctx, (unsigned char*)buf, 512);
				}
				sha256_final(&ctx, dig_z);
				sha256_init(&ctx);
				has_dig_z=true;
			}
			unsigned char dig_r[32];
			unsigned char dig_f[32];

			if(has_sector)
			{
				for(unsigned int i=0;i<vhd_blocksize && currpos+i<size;i+=512)
				{
					size_t read;
					in->Read(buf, 512, read);
					sha256_update(&ctx, (unsigned char*)buf, 512);
				}
				
				_u32 dr=hashfile->Read((char*)dig_f, 32);
				if( dr!=32 )
				{
					Server->Log("Could not read hash from file", LL_ERROR);
				}
				sha256_final(&ctx, dig_r);
				sha256_init(&ctx);
			}
			else
			{
				hashfile->Read((char*)dig_f, 32);
				memcpy(dig_r, dig_z, 32);
			}

			if(memcmp(dig_r, dig_f, 32)!=0)
			{
				++diff;
				Server->Log("Different blocks: "+convert(diff)+" at pos "+convert(currpos)+" has_sector="+convert(has_sector));
			}
			else if(has_sector && has_hashfile)
			{
				++diff_w;
				Server->Log("Wrong difference: "+convert(diff_w)+" at pos "+convert(currpos));
			}
			else
			{
				++ok_blocks;
			}
		
			int pc=(int)((float)((float)currpos/(float)size)*100.f+0.5f);
			if(pc!=last_pc)
			{
				last_pc=pc;
				Server->Log("Checking hashfile: "+convert(pc)+"%");
			}
			
		}
		if(diff==0)
		{
			Server->Log("Hashfile does match");
		}
		Server->Log("Blocks with correct hash: "+convert(ok_blocks));
		Server->Log("Different blocks: "+convert(diff));
		Server->Log("Wrong differences: "+convert(diff_w));
		exit(diff==0?0:7);
	}

	std::string device_verify=Server->getServerParameter("device_verify");
	if(!device_verify.empty())
	{
		std::auto_ptr<IVHDFile> in(open_device_file(device_verify));

		if(in.get()==NULL || in->isOpen()==false)
		{
			Server->Log("Error opening Image-File \""+device_verify+"\"", LL_ERROR);
			exit(4);
		}

		int skip=1024*512;
		FileWrapper wrapper(in.get(), skip);
		FSNTFS fs(&wrapper, false, false);
		if(fs.hasError())
		{
			Server->Log("Error opening device file", LL_ERROR);
			exit(3);
		}

		PrintInfo(&fs);

		std::string s_hashfile=Server->getServerParameter("hash_file");
		if(s_hashfile.empty())
		{
			s_hashfile = device_verify+".hash";
		}

		IFile *hashfile=Server->openFile(s_hashfile, MODE_READ);
		if(hashfile==NULL)
		{
			Server->Log("Error opening hashfile "+s_hashfile);
			exit(7);
		}

		unsigned int ntfs_blocksize=(unsigned int)fs.getBlocksize();
		unsigned int vhd_sectorsize=(1024*1024)/2;

		uint64 currpos=0;
		uint64 size=fs.getSize();
		sha256_ctx ctx;
		sha256_init(&ctx);
		int diff=0;
		int last_pc=0;
		int mixed=0;
		std::vector<char> zerobuf;
		zerobuf.resize(ntfs_blocksize);
		memset(&zerobuf[0], 0, ntfs_blocksize);

		for(;currpos<size;currpos+=ntfs_blocksize)
		{
			bool has_block=fs.hasBlock(currpos/ntfs_blocksize);

			if(has_block)
			{
				fs_buffer buf(&fs, fs.readBlock(currpos/ntfs_blocksize));

				if(buf.get()==NULL)
				{
					Server->Log("Could not read block "+convert(currpos/ntfs_blocksize), LL_ERROR);
				}
				else
				{
					sha256_update(&ctx, (unsigned char*)buf.get(), ntfs_blocksize);
				}
				mixed = mixed | 1;
			}
			else
			{
				sha256_update(&ctx, (unsigned char*)&zerobuf[0], ntfs_blocksize);

				mixed = mixed | 2;
			}

			if( (currpos+ntfs_blocksize)%vhd_sectorsize==0 )
			{
				unsigned char dig_r[32];
				unsigned char dig_f[32];

				_u32 dr=hashfile->Read((char*)dig_f, 32);
				if( dr!=32 )
				{
					Server->Log("Could not read hash from file", LL_ERROR);
				}

				sha256_final(&ctx, dig_r);
				sha256_init(&ctx);

				if(memcmp(dig_r, dig_f, 32)!=0 && mixed!=2)
				{
					++diff;
					Server->Log("Different blocks: "+convert(diff)+" at pos "+convert(currpos)+" mixed = "+
						(mixed==3? "true":"false")+" ("+convert(mixed)+")" );
				}

				mixed=0;
			}		
		
			int pc=(int)((float)((float)currpos/(float)size)*100.f+0.5f);
			if(pc!=last_pc)
			{
				last_pc=pc;
				Server->Log("Checking device hashsums: "+convert(pc)+"%");
			}
			
		}
		if(diff>0)
		{
			Server->Log("Device does not match hash file");
		}
		Server->Log("Different blocks: "+convert(diff));
		exit(7);
	}

	std::string vhd_cmp=Server->getServerParameter("vhd_cmp");
	if(!vhd_cmp.empty())
	{
		VHDFile in(vhd_cmp, true,0);
		if(in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+vhd_cmp+"\"", LL_ERROR);
			exit(4);
		}

		std::string other=Server->getServerParameter("other");
		VHDFile other_in(other, true,0);
		if(other_in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+other+"\"", LL_ERROR);
			exit(4);
		}

		unsigned int blocksize=in.getBlocksize();
		int skip=1024*512;
		in.Seek(skip);
		other_in.Seek(skip);
		uint64 currpos=skip;
		uint64 size=(std::min)(in.getSize(), other_in.getSize());

		char buf1[512];
		char buf2[512];
		int diff=0;
		int last_pc=0;

		for(;currpos<size;currpos+=blocksize)
		{
			in.Seek(currpos);
			other_in.Seek(currpos);
			bool has_sector=in.this_has_sector() || other_in.this_has_sector();

			if(in.this_has_sector() && !other_in.this_has_sector())
			{
				Server->Log("Sector only in file 1 at pos "+convert(currpos));
			}
			if(!in.this_has_sector() && other_in.this_has_sector())
			{
				Server->Log("Sector only in file 2 at pos "+convert(currpos));
			}

			if(has_sector)
			{
				bool hdiff=false;
				for(unsigned int i=0;i<blocksize;i+=512)
				{
					size_t read;
					in.Read(buf1, 512, read);
					other_in.Read(buf2, 512, read);
					int mr=memcmp(buf1, buf2, 512);
					if(mr!=0)
					{
						int n=0;
						for(size_t i=0;i<512;++i)
						{
							if(buf1[i]!=buf2[i])
							{
								++n;
							}
						}
						if(n==2)
						{
							NTFSFileRecord *fr=(NTFSFileRecord*)buf1;
							if(fr->magic[0]=='F' && fr->magic[1]=='I' && fr->magic[2]=='L' && fr->magic[3]=='E' )
							{
								MFTAttribute attr;
								attr.length=fr->attribute_offset;
								int pos=0;
								do
								{
									pos+=attr.length;
									memcpy((char*)&attr, buf1+pos, sizeof(MFTAttribute) );
									if(attr.type==0x30 && attr.nonresident==0) //FILENAME
									{
										MFTAttributeFilename fn;
										memcpy((char*)&fn, buf1+pos+attr.attribute_offset, sizeof(MFTAttributeFilename) );
										std::string fn_uc;
										fn_uc.resize(fn.filename_length*2);
										memcpy(&fn_uc[0], buf1+pos+attr.attribute_offset+sizeof(MFTAttributeFilename), fn.filename_length*2);
										Server->Log("Filename="+Server->ConvertFromUTF16(fn_uc) , LL_DEBUG);
									}
									Server->Log("Attribute Type: "+convert(attr.type)+" nonresident="+convert(attr.nonresident)+" length="+convert(attr.length), LL_DEBUG);
								}while( attr.type!=0xFFFFFFFF && attr.type!=0x80);
							}

							for(size_t i=0;i<512;++i)
							{
								if(buf1[i]!=buf2[i])
								{
									Server->Log("Position "+convert(i)+": "+convert((int)buf1[i])+"<->"+convert((int)buf2[i]));
								}
							}
						}
						hdiff=true;
						break;
					}
				}
				
				if(hdiff)
				{
					++diff;
					Server->Log("Different blocks: "+convert(diff)+" at pos "+convert(currpos));
				}
			}
		
			int pc=(int)((float)((float)currpos/(float)size)*100.f+0.5f);
			if(pc!=last_pc)
			{
				last_pc=pc;
				Server->Log("Checking hashfile: "+convert(pc)+"%");
			}
			
		}
		Server->Log("Different blocks: "+convert(diff));
		exit(7);
	}

	std::string vhd_fixmftmirr=Server->getServerParameter("vhd_checkmftmirr");
	if(!vhd_fixmftmirr.empty())
	{
		VHDFile vhd(vhd_fixmftmirr, false, 0);
		vhd.addVolumeOffset(1024*512);

		if(vhd.isOpen()==false)
		{
			Server->Log("Could not open vhd file", LL_ERROR);
			exit(7);
		}

		if(Server->getServerParameter("fix")!="true")
		{
			FSNTFS fs(&vhd, true, false);
			if(fs.hasError())
			{
				Server->Log("NTFS filesystem has errors", LL_ERROR);
			}
			exit(7);
		}
		else
		{
			FSNTFS fs(&vhd, true, true);
			if(fs.hasError())
			{
				Server->Log("NTFS filesystem has errors", LL_ERROR);
			}
			exit(7);
		}
		exit(0);
	}

	std::string fibmap_test_fn = Server->getServerParameter("fibmap_test");
	if (!fibmap_test_fn.empty())
	{
#ifdef __linux__
		fibmap_test(fibmap_test_fn, Server->getServerParameter("bitmap_source"));
#endif
		exit(0);
	}

	imagepluginmgr=new CImagePluginMgr;

	Server->RegisterPluginThreadsafeModel( imagepluginmgr, "fsimageplugin");

#ifndef STATIC_PLUGIN
	Server->Log("Loaded -fsimageplugin- plugin", LL_INFO);
#endif
}

DLLEXPORT void UnloadActions(void)
{
	
}

#ifdef STATIC_PLUGIN
namespace
{
	static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 0);
}
#endif
