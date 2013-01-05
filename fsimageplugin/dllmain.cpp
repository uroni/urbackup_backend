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

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif

#include <iostream>
#include <memory.h>

#define DEF_SERVER
#include "../Interface/Server.h"
#include "../Interface/Action.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "../urbackupcommon/sha2/sha2.h"

#include <stdlib.h>

#include "vhdfile.h"
#include "fs/ntfs.h"

#include "pluginmgr.h"

IServer *Server;


CImagePluginMgr *imagepluginmgr;

void PrintInfo(IFilesystem *fs);

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	std::string devinfo=Server->getServerParameter("devinfo");

	if(!devinfo.empty())
	{
		FSNTFS ntfs(L"\\\\.\\"+widen(devinfo)+L":");
	
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
		FSNTFS fs(Server->ConvertToUnicode(device_verify));
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
		char *buf=new char[ntfs_blocksize];
		int diff=0;
		int last_pc=0;

		for(;currpos<size;currpos+=ntfs_blocksize)
		{
			bool has_block=fs.readBlock(currpos/ntfs_blocksize, NULL);

			if(has_block)
			{
				bool read_block=fs.readBlock(currpos/ntfs_blocksize, buf);

				if(!read_block)
				{
					Server->Log("Could not read block "+nconvert(currpos/ntfs_blocksize), LL_ERROR);
				}

				sha256_update(&ctx, (unsigned char*)buf, ntfs_blocksize);
			}
			else
			{
				memset(buf, 0, ntfs_blocksize);
				sha256_update(&ctx, (unsigned char*)buf, ntfs_blocksize);
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
			Server->Log("Could not open vhd file", LL_ERROR);
			exit(7);
		}

		if(Server->getServerParameter("fix")!="true")
		{
			FSNTFS fs(&vhd, true);
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
