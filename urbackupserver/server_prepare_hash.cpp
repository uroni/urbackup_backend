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

#ifndef CLIENT_ONLY

#include "server_prepare_hash.h"
#include "server_hash.h"
#include "../common/data.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "server_log.h"
#include "../urbackupcommon/os_functions.h"
#include "../fileservplugin/chunk_settings.h"
#include "../md5.h"
#include <memory.h>

BackupServerPrepareHash::BackupServerPrepareHash(IPipe *pPipe, IPipe *pOutput, int pClientid)
{
	pipe=pPipe;
	output=pOutput;
	clientid=pClientid;
	working=false;
	chunk_patcher.setCallback(this);
	has_error=false;
}

BackupServerPrepareHash::~BackupServerPrepareHash(void)
{
	Server->destroy(pipe);
}

void BackupServerPrepareHash::operator()(void)
{
	while(true)
	{
		working=false;
		std::string data;
		size_t rc=pipe->Read(&data);
		if(data=="exit")
		{
			output->Write("exit");
			Server->Log("server_prepare_hash Thread finished (exit)");
			delete this;
			return;
		}
		else if(data=="flush")
		{
			continue;
		}

		if(rc>0)
		{
			working=true;
			
			CRData rd(&data);

			std::string temp_fn;
			rd.getStr(&temp_fn);

			int backupid;
			rd.getInt(&backupid);

			char incremental;
			rd.getChar(&incremental);

			std::string tfn;
			rd.getStr(&tfn);

			std::string hashpath;
			rd.getStr(&hashpath);

			std::string hashoutput_fn;
			rd.getStr(&hashoutput_fn);

			bool diff_file=!hashoutput_fn.empty();

			std::string old_file_fn;
			rd.getStr(&old_file_fn);

			IFile *tf=Server->openFile(os_file_prefix(Server->ConvertToUnicode(temp_fn)), MODE_READ);
			IFile *old_file=NULL;
			if(diff_file)
			{
				old_file=Server->openFile(os_file_prefix(Server->ConvertToUnicode(old_file_fn)), MODE_READ);
				if(old_file==NULL)
				{
					ServerLogger::Log(clientid, "Error opening file \""+old_file_fn+"\" from pipe for reading. File: old_file", LL_ERROR);
					if(tf!=NULL) Server->destroy(tf);
					continue;
				}
			}

			if(tf==NULL)
			{
				ServerLogger::Log(clientid, "Error opening file \""+temp_fn+"\" from pipe for reading file. File: temp_fn", LL_ERROR);
				has_error=true;
				if(old_file!=NULL)
				{
					Server->destroy(old_file);
				}
			}
			else
			{
				ServerLogger::Log(clientid, "PT: Hashing file \""+ExtractFileName(tfn)+"\"", LL_DEBUG);
				std::string h;
				if(!diff_file)
				{
					h=hash(tf);
				}
				else
				{
					h=hash_with_patch(old_file, tf);
				}

				Server->destroy(tf);
				if(old_file!=NULL)
				{
					Server->destroy(old_file);
				}
				
				CWData data;
				data.addInt(BackupServerHash::EAction_LinkOrCopy);
				data.addString(temp_fn);
				data.addInt(backupid);
				data.addChar(incremental);
				data.addString(tfn);
				data.addString(hashpath);
				data.addString(h);
				data.addString(hashoutput_fn);
				data.addString(old_file_fn);

				output->Write(data.getDataPtr(), data.getDataSize() );
			}
		}
	}
}

std::string BackupServerPrepareHash::hash(IFile *f)
{
	f->Seek(0);
	unsigned char buf[4096];
	_u32 rc;

	sha512_init(&ctx);
	do
	{
		rc=f->Read((char*)buf, 4096);
		if(rc>0)
			sha512_update(&ctx, buf, rc);
	}
	while(rc==4096);
	
	std::string ret;
	ret.resize(64);
	sha512_final(&ctx, (unsigned char*)&ret[0]);
	return ret;
}

std::string BackupServerPrepareHash::hash_with_patch(IFile *f, IFile *patch)
{
	sha512_init(&ctx);
	
	chunk_patcher.ApplyPatch(f, patch);

	std::string ret;
	ret.resize(64);
	sha512_final(&ctx, (unsigned char*)&ret[0]);
	return ret;
}

void BackupServerPrepareHash::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed)
{
	sha512_update(&ctx, (const unsigned char*)buf, (unsigned int)bsize);
}

bool BackupServerPrepareHash::isWorking(void)
{
	return working;
}

unsigned int adler32(unsigned int adler, const char *buf, unsigned int len);

std::string BackupServerPrepareHash::build_chunk_hashs(IFile *f, IFile *hashoutput, INotEnoughSpaceCallback *cb, bool ret_sha2, IFile *copy, bool modify_inplace)
{
	f->Seek(0);

	hashoutput->Seek(0);
	_i64 fsize=f->Size();
	if(!writeRepeatFreeSpace(hashoutput, (char*)&fsize, sizeof(_i64), cb))
		return "";

	sha512_ctx ctx;
	if(ret_sha2)
		sha512_init(&ctx);

	_i64 n_chunks=c_checkpoint_dist/c_small_hash_dist;
	char buf[c_small_hash_dist];
	char copy_buf[c_small_hash_dist];
	_i64 copy_write_pos=0;
	char zbuf[big_hash_size]={};
	_i64 hashoutputpos=sizeof(_i64);
	for(_i64 pos=0;pos<fsize;)
	{
		_i64 epos=pos+c_checkpoint_dist;
		MD5 big_hash;
		_i64 hashoutputpos_start=hashoutputpos;
		writeRepeatFreeSpace(hashoutput, zbuf, big_hash_size, cb);
		hashoutputpos+=big_hash_size;
		for(;pos<epos && pos<fsize;pos+=c_small_hash_dist)
		{
			_u32 r=f->Read(buf, c_small_hash_dist);
			_u32 small_hash=adler32(adler32(0, NULL, 0), buf, r);
			big_hash.update((unsigned char*)buf, r);
			if(!writeRepeatFreeSpace(hashoutput, (char*)&small_hash, small_hash_size, cb))
				return "";

			hashoutputpos+=small_hash_size;

			if(ret_sha2)
			{
				sha512_update(&ctx, (unsigned char*)buf, r);
			}
			if(copy!=NULL)
			{
				if(modify_inplace)
				{
					_u32 copy_r=copy->Read(copy_buf, c_small_hash_dist);

					if(copy_r!=r || memcmp(copy_buf, buf, r)!=0)
					{
						copy->Seek(copy_write_pos);
						if(!writeRepeatFreeSpace(copy, buf, r, cb) )
							return "";
					}

					copy_write_pos+=r;
				}
				else
				{
					if(!writeRepeatFreeSpace(copy, buf, r, cb) )
						return "";
				}
			}
		}
		hashoutput->Seek(hashoutputpos_start);
		big_hash.finalize();
		if(!writeRepeatFreeSpace(hashoutput, (const char*)big_hash.raw_digest_int(),  big_hash_size, cb))
			return "";

		hashoutput->Seek(hashoutputpos);
	}

	if(ret_sha2)
	{
		std::string ret;
		ret.resize(64);
		sha512_final(&ctx, (unsigned char*)&ret[0]);
		return ret;
	}
	else
	{
		return "k";
	}
}

bool BackupServerPrepareHash::writeRepeatFreeSpace(IFile *f, const char *buf, size_t bsize, INotEnoughSpaceCallback *cb)
{
	if( cb==NULL)
		return writeFileRepeat(f, buf, bsize);

	int rc=f->Write(buf, (_u32)bsize);
	if(rc!=bsize)
	{
		if(cb!=NULL && cb->handle_not_enough_space(f->getFilenameW()) )
		{
			_u32 written=rc;
			do
			{
				rc=f->Write(buf+written, (_u32)bsize-written);
				written+=rc;
			}
			while(written<bsize && rc>0);

			if(rc==0) return false;
		}
		else
		{
			return false;
		}
	}
	return true;
}

bool BackupServerPrepareHash::writeFileRepeat(IFile *f, const char *buf, size_t bsize)
{
	_u32 written=0;
	_u32 rc;
	int tries=50;
	do
	{
		rc=f->Write(buf+written, (_u32)(bsize-written));
		written+=rc;
		if(rc==0)
		{
			Server->wait(10000);
			--tries;
		}
	}
	while(written<bsize && (rc>0 || tries>0) );

	if(rc==0)
	{
		return false;
	}

	return true;
}

bool BackupServerPrepareHash::hasError(void)
{
	volatile bool r=has_error;
	has_error=false;
	return r;
}

#endif //CLIENT_ONLY
