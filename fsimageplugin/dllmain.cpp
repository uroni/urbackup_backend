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

#include <stdlib.h>

#include "vhdfile.h"
#include "fs/ntfs.h"
#include "../urbackup/sha2/sha2.h"

#include "pluginmgr.h"

IServer *Server;

#ifndef _WIN32
#define exit _exit
#endif

CImagePluginMgr *imagepluginmgr;

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	/*int c=0;

	FSNTFS ntfs(L"\\\\.\\Volume{975a91a4-aa07-11dc-98b4-806e6f6e6963}");
	
	Server->Log("Used Space: "+nconvert(ntfs.calculateUsedSpace())+" of "+nconvert(ntfs.getSize()));
	Server->Log(nconvert(((float)ntfs.calculateUsedSpace()/(float)ntfs.getSize())*100.0f)+" %");

	{
		IFile *file=Server->openFile("G:\\Root\\Setup\\rsrc\\Prototype.exe",MODE_READ);  //"\\\\.\\Volume{975a91a4-aa07-11dc-98b4-806e6f6e6963}", MODE_READ);
		VHDFile vhd(L"G:\\test.vhd", false, (uint64)20*(uint64)1024*(uint64)1024*(uint64)1024);
		vhd.Seek(0);
		if(file!=NULL)
		{
			char buffer[4096];
			_u32 rc;
			while( (rc=file->Read(buffer, 4096))!=0 ) 
			{
				vhd.Write(buffer, rc);
				++c;
				if(c>=5120) break;
			}
		}
	}
	{
		IFile *file=Server->openFile("G:\\Root\\Setup\\rsrc\\Prototype.exe",MODE_READ); //"\\\\.\\Volume{975a91a4-aa07-11dc-98b4-806e6f6e6963}", MODE_READ);
		VHDFile vhd(L"G:\\test.vhd", true, (uint64)20*(uint64)1024*(uint64)1024*(uint64)1024);
		vhd.Seek(0);
		c=0;
		if(file!=NULL)
		{
			char buffer[4096];
			char buffer2[4096];
			_u32 rc;
			while( (rc=file->Read(buffer, 4096))!=0 ) 
			{
				size_t read;
				vhd.Read(buffer2, rc, read);
				if(rc!=read)
				{
					Server->Log("Not enough data");
					break;
				}
				for(size_t i=0;i<rc;++i)
				{
					if(buffer[i]!=buffer2[i])
					{
						Server->Log("Error. Data differs");
						break;
					}
				}
				++c;
				if(c>=5120) break;
			}
		}
	}*/
	/*{
		VHDFile vhd(L"C:\\Program Files\\IE6AppCompatVHD\\IE6 on XP SP3.vhd", true, 0);
		VHDFile vhdout(L"G:\\urbackup\\test2.vhd", false, (uint64)15*(uint64)1024*(uint64)1024*(uint64)1024);

		uint64 dsize=vhd.getSize();
		char b2[4096];
		unsigned int bpos=0;
		uint64 si=0;
		for(uint64 i=0;i<dsize;i+=512)
		{
			vhd.Seek(i);
			char buffer[512];
			if(!vhd.has_sector() )
				continue;

			size_t read;
			vhd.Read(buffer, 512, read);
			if(read!=512)
			{
				Server->Log("Read too few bytes!", LL_ERROR);
			}

			memcpy(&b2[bpos], buffer, 512);
			bpos+=512;
			if(bpos==4096)
			{
				vhdout.Seek(si);
				vhdout.Write(b2, 4096);
				si=i+512;
				bpos=0;
			}
		}
	}*/

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
		
		std::string vhdcopy_out=Server->getServerParameter("vhdcopy_out");
		if(vhdcopy_out.empty())
		{
			Server->Log("'vhdcopy_out' not specified. Not copying.", LL_ERROR);
			exit(5);
		}
		else
		{
			IFile *out=Server->openFile(vhdcopy_out, MODE_RW);
			out->Seek(0);
			if(out==NULL)
			{
				Server->Log("Couldn't open output file", LL_ERROR);
				exit(6);
			}
			else
			{
				std::string skip_s=Server->getServerParameter("skip");
				int skip=512*512;
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
		int skip=512*512;
		in.Seek(skip);
		uint64 currpos=skip;
		uint64 size=in.getSize();
		sha256_ctx ctx;
		sha256_init(&ctx);
		char buf[512];
		int diff=0;
		int diff_w=0;
		int last_pc=0;
		for(;currpos<size;currpos+=blocksize)
		{
			bool has_sector=in.this_has_sector();

			for(unsigned int i=0;i<blocksize;i+=512)
			{
				size_t read;
				in.Read(buf, 512, read);
				sha256_update(&ctx, (unsigned char*)buf, 512);
			}
			unsigned char dig_r[32];
			unsigned char dig_f[32];
			hashfile->Read((char*)dig_f, 32);
			sha256_final(&ctx, dig_r);
			sha256_init(&ctx);

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
	imagepluginmgr=new CImagePluginMgr;

	Server->RegisterPluginThreadsafeModel( imagepluginmgr, "fsimageplugin");

	Server->Log("Loaded -fsimageplugin- plugin", LL_INFO);
}

DLLEXPORT void UnloadActions(void)
{
	
}
