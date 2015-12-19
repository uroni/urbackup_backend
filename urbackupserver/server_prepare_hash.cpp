/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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
#include "../common/adler32.h"
#include "../urbackupcommon/file_metadata.h"

BackupServerPrepareHash::BackupServerPrepareHash(IPipe *pPipe, IPipe *pOutput, int pClientid, logid_t logid)
	: logid(logid)
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

			int incremental;
			rd.getInt(&incremental);

			char with_hashes;
			rd.getChar(&with_hashes);

			std::string tfn;
			rd.getStr(&tfn);

			std::string hashpath;
			rd.getStr(&hashpath);

			std::string hashoutput_fn;
			rd.getStr(&hashoutput_fn);

			bool diff_file=!hashoutput_fn.empty();

			std::string old_file_fn;
			rd.getStr(&old_file_fn);

			int64 t_filesize;
			rd.getInt64(&t_filesize);

			FileMetadata metadata;
			metadata.read(rd);

			IFile *tf=Server->openFile(os_file_prefix((temp_fn)), MODE_READ);
			IFile *old_file=NULL;
			if(diff_file)
			{
				old_file=Server->openFile(os_file_prefix((old_file_fn)), MODE_READ);
				if(old_file==NULL)
				{
					ServerLogger::Log(logid, "Error opening file \""+old_file_fn+"\" from pipe for reading. File: old_file ec="+convert(os_last_error()), LL_ERROR);
					has_error=true;
					if(tf!=NULL) Server->destroy(tf);
					continue;
				}
			}

			if(tf==NULL)
			{
				ServerLogger::Log(logid, "Error opening file \""+temp_fn+"\" from pipe for reading file. File: temp_fn ec="+convert(os_last_error()), LL_ERROR);
				has_error=true;
				if(old_file!=NULL)
				{
					Server->destroy(old_file);
				}
			}
			else
			{
				ServerLogger::Log(logid, "PT: Hashing file \""+ExtractFileName(tfn)+"\"", LL_DEBUG);
				std::string h;
				if(!diff_file)
				{
					h=hash_sha(tf);
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
				data.addInt(incremental);
				data.addChar(with_hashes);
				data.addString(tfn);
				data.addString(hashpath);
				data.addString(h);
				data.addString(hashoutput_fn);
				data.addString(old_file_fn);
				data.addInt64(t_filesize);
				metadata.serialize(data);

				output->Write(data.getDataPtr(), data.getDataSize() );
			}
		}
	}
}

std::string BackupServerPrepareHash::hash_sha(IFile *f)
{
	f->Seek(0);
	unsigned char buf[32768];
	_u32 rc;

	sha_def_ctx local_ctx;
	sha_def_init(&local_ctx);
	do
	{
		rc=f->Read((char*)buf, 32768);
		if(rc>0)
			sha_def_update(&local_ctx, buf, rc);
	}
	while(rc>0);
	
	std::string ret;
	ret.resize(SHA_DEF_DIGEST_SIZE);
	sha_def_final(&local_ctx, (unsigned char*)&ret[0]);
	return ret;
}

std::string BackupServerPrepareHash::hash_with_patch(IFile *f, IFile *patch)
{
	sha_def_init(&ctx);
	
	chunk_patcher.ApplyPatch(f, patch);

	std::string ret;
	ret.resize(SHA_DEF_DIGEST_SIZE);
	sha_def_final(&ctx, (unsigned char*)&ret[0]);
	return ret;
}

void BackupServerPrepareHash::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed)
{
	sha_def_update(&ctx, (const unsigned char*)buf, (unsigned int)bsize);
}

bool BackupServerPrepareHash::isWorking(void)
{
	return working;
}

bool BackupServerPrepareHash::hasError(void)
{
	volatile bool r=has_error;
	has_error=false;
	return r;
}

#endif //CLIENT_ONLY
