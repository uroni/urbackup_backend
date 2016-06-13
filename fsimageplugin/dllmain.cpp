/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#define DEF_SERVER
#include "../Interface/Server.h"
IServer *Server;

#include "../Interface/Action.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../urbackupcommon/mbrdata.h"

#include <stdlib.h>

#include "vhdfile.h"
#include "fs/ntfs.h"

#include "pluginmgr.h"

#include "CompressedFile.h"

#ifdef _WIN32
#include "win_dialog.h"
#endif
#include "FileWrapper.h"


CImagePluginMgr *imagepluginmgr;

void PrintInfo(IFilesystem *fs);

namespace
{
	bool decompress_vhd(const std::wstring& fn, const std::wstring& output)
	{
		std::wstring tmp_output=output;

		if(fn==output)
		{
			tmp_output+=L".tmp";
		}

		IFile* out = Server->openFile(tmp_output, MODE_WRITE);
		ObjectScope out_file(out);

		if(out==NULL)
		{
			Server->Log(L"Error opening output file \""+output+L"\"", LL_ERROR);
			return false;
		}

		if(findextension(fn)==L"vhdz")
		{
			std::auto_ptr<VHDFile> vhdfile(new VHDFile(fn, true, 0));

			if(vhdfile->isOpen() && vhdfile->getParent()!=NULL)
			{
				if(vhdfile->isCompressed())
				{
					std::wstring parent_fn = vhdfile->getParent()->getFilenameW();
					vhdfile.reset();
					Server->Log(L"Decompressing parent VHD \""+parent_fn+L"\"...", LL_INFO);
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
					Server->Log(L"Decompressing \""+fn+L"\"... "+convert(pcdone)+L"%", LL_INFO);
				}
			} while (read>0);

			compFile.finish();
		}		

		if(fn==output)
		{
			out_file.clear();
#ifdef _WIN32
			return MoveFileExW(tmp_output.c_str(), fn.c_str(), MOVEFILE_REPLACE_EXISTING)==TRUE;
#else
			return rename(Server->ConvertToUTF8(tmp_output).c_str(), Server->ConvertToUTF8(fn).c_str())==0;
#endif
		}
		else
		{
			return true;
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
				Server->Log(L"Assembling... "+convert(currentpc)+L"%", LL_INFO);
			}
		}
	}

	bool assemble_vhd(const std::vector<std::wstring>& fn, const std::wstring& output)
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
			std::auto_ptr<IFile> f(Server->openFile(fn[i]+L".mbr", MODE_READ));
			if(f.get()==NULL)
			{
				Server->Log(L"Could not open MBR file "+fn[i]+L".mbr", LL_ERROR);
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
					Server->Log(L"Volume "+mbrdatas[j].volume_name+L" has the same partition number as the volume "+mbrdata.volume_name+L". Please make sure you only select volumes from one device.", LL_ERROR);
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
				Server->Log(L"Error opening VHD-File \""+fn[i]+L"\"", LL_ERROR);
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
				Server->Log(L"Volume "+mbrdatas[i].volume_name+L" not an NTFS volume. Copying all blocks...", LL_WARNING);
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
			Server->Log(L"Error opening output VHD-File \""+output+L"\"", LL_ERROR);
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
			Server->Log(L"Writing "+fn[i]+L" into output VHD...");

			partition* cpart = partitions + (mbrdatas[i].partition_number-1);
			int64 out_pos = cpart->start_sector*c_sector_size;
			int64 max_pos = out_pos + cpart->nr_sector*c_sector_size;

			if(input_fs[i]!=NULL)
			{
				Server->Log(L"Optimized by only writing used NTFS sectors...");

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
						Server->Log("Trying to write beyond partition", LL_ERROR);
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
			CompressedFile compFile(widen(compress_file+".urz"), MODE_RW_CREATE);

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
			std::wstring filter;
			filter += L"Compressed image files (*.vhdz)";
			filter += (wchar_t)'\0';
			filter += L"*.vhdz";
			filter += (wchar_t)'\0';
			filter += (wchar_t)'\0';
			std::vector<std::wstring> res = file_via_dialog(L"Please select compressed image file to decompress",
				filter, false, true, L"");
			if(!res.empty())
			{
				decompress = Server->ConvertToUTF8(res[0]);
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

		bool b = decompress_vhd(Server->ConvertToUnicode(decompress), Server->ConvertToUnicode(targetName));		

		exit(b?0:3);
	}

	std::string assemble = Server->getServerParameter("assemble");
	if(!assemble.empty())
	{
		bool selected_via_gui=false;
		std::vector<std::wstring> input_files;
		std::wstring output_file;
#ifdef _WIN32
		if(assemble=="SelectViaGUI")
		{
			std::wstring filter;
			filter += L"Image files (*.vhdz;*.vhd)";
			filter += (wchar_t)'\0';
			filter += L"*.vhdz;*.vhd";
			filter += (wchar_t)'\0';
			/*filter += L"Image files (*.vhd)";
			filter += (wchar_t)'\0';
			filter += L"*.vhd";
			filter += (wchar_t)'\0';*/
			filter += (wchar_t)'\0';
			input_files = file_via_dialog(L"Please select all the images to assemble into one image",
				filter, true, true, L"");

			filter.clear();
			filter += L"Image file (*.vhd)";
			filter += (wchar_t)'\0';
			filter += L"*.vhd";
			filter += (wchar_t)'\0';
			filter += (wchar_t)'\0';

			std::vector<std::wstring> output_files = file_via_dialog(L"Please select where to save the output image",
				filter, false, false, L"vhd");

			if(!output_files.empty())
			{
				output_file = output_files[0];
			}			

			selected_via_gui=true;
		}
#endif

		if(!selected_via_gui)
		{
			TokenizeMail(Server->ConvertToUnicode(assemble), input_files, L";");
			output_file = Server->ConvertToUnicode(Server->getServerParameter("output_file"));
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
		FSNTFS ntfs(L"\\\\.\\"+widen(devinfo)+L":", false, true);
	
		if(!ntfs.hasError())
		{
			Server->Log("Used Space: "+nconvert(ntfs.calculateUsedSpace())+" of "+nconvert(ntfs.getSize()));
			Server->Log(nconvert(((float)ntfs.calculateUsedSpace()/(float)ntfs.getSize())*100.0f)+" %");
		}
	}

	std::string vhdcopy_in=Server->getServerParameter("vhdcopy_in");
	if(!vhdcopy_in.empty())
	{
		Server->Log("VHDCopy.");
		VHDFile in(Server->ConvertToUnicode(vhdcopy_in), true,0);
		if(in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+vhdcopy_in+"\"", LL_ERROR);
			exit(4);
		}

		uint64 vhdsize=in.getSize();
		float vhdsize_gb=(vhdsize/1024)/1024.f/1024.f;
		uint64 vhdsize_mb=vhdsize/1024/1024;
		Server->Log("VHD Info: Size: "+nconvert(vhdsize_gb)+" GB "+nconvert(vhdsize_mb)+" MB",LL_INFO);
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

				Server->Log("Skipping "+nconvert(skip)+" bytes...", LL_INFO);
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
					Server->Log("First VHD sector at "+nconvert(currpos), LL_INFO);
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
							Server->Log(nconvert(pc)+"%", LL_INFO);
							last_pc=pc;
						}
					}
				}
				while( read==4096 && is_ok );

				Server->destroy(out);
				Server->Log("Copy process finished successfully.", LL_INFO);
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
		VHDFile in(Server->ConvertToUnicode(vhdinfo), true,0);
		if(in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+vhdinfo+"\"", LL_ERROR);
			exit(4);
		}

		uint64 vhdsize=in.getSize();
		float vhdsize_gb=(vhdsize/1024)/1024.f/1024.f;
		uint64 vhdsize_mb=vhdsize/1024/1024;
		std::cout << ("VHD Info: Size: "+nconvert(vhdsize_gb)+" GB "+nconvert(vhdsize_mb)+" MB") << std::endl;
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

	std::string vhd_verify=Server->getServerParameter("vhd_verify");
	if(!vhd_verify.empty())
	{
		VHDFile in(Server->ConvertToUnicode(vhd_verify), true,0);
		if(in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+vhdcopy_in+"\"", LL_ERROR);
			exit(4);
		}

		std::string s_hashfile=Server->getServerParameter("hashfile");
		bool has_hashfile=true;
		if(s_hashfile.empty())
		{
			has_hashfile=false;
			s_hashfile=vhd_verify+".hash";
		}

		std::string s_hashfile2=Server->getServerParameter("hashfile_2");

		IFile *hashfile=Server->openFile(s_hashfile, MODE_READ);
		if(hashfile==NULL)
		{
			Server->Log("Error opening hashfile");
			exit(5);
		}

		IFile *hashfile2=NULL;
		if(!s_hashfile2.empty())
		{
			hashfile2=Server->openFile(s_hashfile, MODE_READ);
			if(hashfile2==NULL)
			{
				Server->Log("Error opening hashfile");
				exit(5);
			}
		}

		unsigned int blocksize=in.getBlocksize();
		int skip=1024*512;
		in.Seek(skip);
		uint64 currpos=skip;
		uint64 size=in.getSize();
		sha256_ctx ctx;
		sha256_init(&ctx);
		char buf[512];
		int diff=0;
		int diff_w=0;
		int last_pc=0;
		unsigned char dig_z[32];
		bool has_dig_z=false;
		for(;currpos<size;currpos+=blocksize)
		{
			in.Seek(currpos);
			bool has_sector=in.this_has_sector();

			if(!has_sector && !has_dig_z)
			{
				for(unsigned int i=0;i<blocksize;i+=512)
				{
					size_t read;
					in.Read(buf, 512, read);
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
				for(unsigned int i=0;i<blocksize;i+=512)
				{
					size_t read;
					in.Read(buf, 512, read);
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
				Server->Log("Different blocks: "+nconvert(diff)+" at pos "+nconvert(currpos));
			}
			else if(has_sector && has_hashfile)
			{
				++diff_w;
				Server->Log("Wrong difference: "+nconvert(diff_w)+" at pos "+nconvert(currpos));
			}
		
			int pc=(int)((float)((float)currpos/(float)size)*100.f+0.5f);
			if(pc!=last_pc)
			{
				last_pc=pc;
				Server->Log("Checking hashfile: "+nconvert(pc)+"%");
			}
			
		}
		if(diff==0)
		{
			Server->Log("Hashfile does match");
		}
		Server->Log("Different blocks: "+nconvert(diff));
		Server->Log("Wrong differences: "+nconvert(diff_w));
		exit(7);
	}

	std::string device_verify=Server->getServerParameter("device_verify");
	if(!device_verify.empty())
	{
		FSNTFS fs(Server->ConvertToUnicode(device_verify), true, false);
		if(fs.hasError())
		{
			Server->Log("Error opening device file", LL_ERROR);
			exit(3);
		}

		PrintInfo(&fs);

		std::string s_hashfile=Server->getServerParameter("hash_file");
		if(s_hashfile.empty())
		{
			Server->Log("Hash file parameter not set (--hash_file)", LL_ERROR);
			exit(4);
		}

		IFile *hashfile=Server->openFile(s_hashfile, MODE_READ);
		if(hashfile==NULL)
		{
			Server->Log("Error opening hashfile");
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
		std::vector<char> zerobuf;
		zerobuf.resize(ntfs_blocksize);
		memset(&zerobuf[0], 0, ntfs_blocksize);

		for(;currpos<size;currpos+=ntfs_blocksize)
		{
			bool has_block=fs.hasBlock(currpos/ntfs_blocksize);

			if(has_block)
			{
				fs_buffer buf(&fs, fs.readBlock(currpos/ntfs_blocksize));

				if(buf.get())
				{
					Server->Log("Could not read block "+nconvert(currpos/ntfs_blocksize), LL_ERROR);
				}
				else
				{
					sha256_update(&ctx, (unsigned char*)buf.get(), ntfs_blocksize);
				}
			}
			else
			{
				sha256_update(&ctx, (unsigned char*)&zerobuf[0], ntfs_blocksize);
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

				if(memcmp(dig_r, dig_f, 32)!=0)
				{
					++diff;
					Server->Log("Different blocks: "+nconvert(diff)+" at pos "+nconvert(currpos));
				}
			}		
		
			int pc=(int)((float)((float)currpos/(float)size)*100.f+0.5f);
			if(pc!=last_pc)
			{
				last_pc=pc;
				Server->Log("Checking device hashsums: "+nconvert(pc)+"%");
			}
			
		}
		if(diff>0)
		{
			Server->Log("Device does not match hash file");
		}
		Server->Log("Different blocks: "+nconvert(diff));
		exit(7);
	}

	std::string vhd_cmp=Server->getServerParameter("vhd_cmp");
	if(!vhd_cmp.empty())
	{
		VHDFile in(Server->ConvertToUnicode(vhd_cmp), true,0);
		if(in.isOpen()==false)
		{
			Server->Log("Error opening VHD-File \""+vhd_cmp+"\"", LL_ERROR);
			exit(4);
		}

		std::string other=Server->getServerParameter("other");
		VHDFile other_in(Server->ConvertToUnicode(other), true,0);
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
				Server->Log("Sector only in file 1 at pos "+nconvert(currpos));
			}
			if(!in.this_has_sector() && other_in.this_has_sector())
			{
				Server->Log("Sector only in file 2 at pos "+nconvert(currpos));
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
										Server->Log(L"Filename="+Server->ConvertFromUTF16(fn_uc) , LL_DEBUG);
									}
									Server->Log("Attribute Type: "+nconvert(attr.type)+" nonresident="+nconvert(attr.nonresident)+" length="+nconvert(attr.length), LL_DEBUG);
								}while( attr.type!=0xFFFFFFFF && attr.type!=0x80);
							}

							for(size_t i=0;i<512;++i)
							{
								if(buf1[i]!=buf2[i])
								{
									Server->Log("Position "+nconvert(i)+": "+nconvert((int)buf1[i])+"<->"+nconvert((int)buf2[i]));
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
					Server->Log("Different blocks: "+nconvert(diff)+" at pos "+nconvert(currpos));
				}
			}
		
			int pc=(int)((float)((float)currpos/(float)size)*100.f+0.5f);
			if(pc!=last_pc)
			{
				last_pc=pc;
				Server->Log("Checking hashfile: "+nconvert(pc)+"%");
			}
			
		}
		Server->Log("Different blocks: "+nconvert(diff));
		exit(7);
	}

	std::string vhd_fixmftmirr=Server->getServerParameter("vhd_checkmftmirr");
	if(!vhd_fixmftmirr.empty())
	{
		VHDFile vhd(Server->ConvertToUnicode(vhd_fixmftmirr), false, 0);
		vhd.addVolumeOffset(1024*512);

		if(vhd.isOpen()==false)
		{
			Server->Log("Could not open VHD file", LL_ERROR);
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

	imagepluginmgr=new CImagePluginMgr;

	Server->RegisterPluginThreadsafeModel( imagepluginmgr, "fsimageplugin");

	Server->Log("Loaded -fsimageplugin- plugin", LL_INFO);
}

DLLEXPORT void UnloadActions(void)
{
	
}
