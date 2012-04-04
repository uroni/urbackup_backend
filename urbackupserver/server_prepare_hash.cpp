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

#ifndef CLIENT_ONLY

#include "server_prepare_hash.h"
#include "../urbackupcommon/fileclient/data.h"
#include "../Interface/Server.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../stringtools.h"
#include "server_log.h"
#include "../urbackupcommon/os_functions.h"
#include "../fileservplugin/chunk_settings.h"
#include "../md5.h"

BackupServerPrepareHash::BackupServerPrepareHash(IPipe *pPipe, IPipe *pExitpipe, IPipe *pOutput, IPipe *pExitpipe_hash, int pClientid)
{
	pipe=pPipe;
	exitpipe=pExitpipe;
	output=pOutput;
	exitpipe_hash=pExitpipe_hash;
	clientid=pClientid;
	working=false;
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
		if(data=="exitnow")
		{
			output->Write("exitnow");
			std::string t;
			exitpipe_hash->Read(&t);
			Server->destroy(exitpipe_hash);
			exitpipe->Write("ok");
			Server->Log("server_prepare_hash Thread finished");
			delete this;
			return;
		}
		else if(data=="exit")
		{
			output->Write("exit");
			Server->destroy(exitpipe);
			Server->Log("server_prepare_hash Thread finished");
			delete this;
			return;
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

			IFile *tf=Server->openFile(os_file_prefix()+Server->ConvertToUnicode(temp_fn), MODE_READ);

			if(tf==NULL)
			{
				ServerLogger::Log(clientid, "Error opening file \""+temp_fn+"\" from pipe for reading", LL_ERROR);
			}
			else
			{
				ServerLogger::Log(clientid, "PT: Hashing file \""+ExtractFileName(tfn)+"\"", LL_DEBUG);
				std::string h=hash(tf);

				Server->destroy(tf);
				
				CWData data;
				data.addString(temp_fn);
				data.addInt(backupid);
				data.addChar(incremental);
				data.addString(tfn);
				data.addString(h);

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
	sha512_ctx ctx;

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

bool BackupServerPrepareHash::isWorking(void)
{
	return working;
}

unsigned int adler32(unsigned int adler, const char *buf, unsigned int len);

std::string BackupServerPrepareHash::build_chunk_hashs(IFile *f, IFile *hashoutput)
{
	f->Seek(0);

	hashoutput->Seek(0);
	_i64 fsize=f->Size();
	hashoutput->Write((char*)&fsize, sizeof(_i64));

	sha512_ctx ctx;
	sha512_init(&ctx);

	_i64 n_chunks=c_checkpoint_dist/c_small_hash_dist;
	char buf[c_small_hash_dist];
	char zbuf[big_hash_size]={};
	_i64 hashoutputpos=sizeof(_i64);
	for(_i64 pos=0;pos<fsize;)
	{
		_i64 epos=pos+c_checkpoint_dist;
		MD5 big_hash;
		_i64 hashoutputpos_start=hashoutputpos;
		hashoutput->Write(zbuf, big_hash_size);
		hashoutputpos+=big_hash_size;
		for(;pos<epos && pos<fsize;pos+=c_small_hash_dist)
		{
			_u32 r=f->Read(buf, c_small_hash_dist);
			_u32 small_hash=adler32(adler32(0, NULL, 0), buf, r);
			big_hash.update((unsigned char*)buf, r);
			hashoutput->Write((char*)&small_hash, small_hash_size);
			hashoutputpos+=small_hash_size;

			sha512_update(&ctx, (unsigned char*)buf, r);
		}
		hashoutput->Seek(hashoutputpos_start);
		big_hash.finalize();
		hashoutput->Write((const char*)big_hash.raw_digest_int(),  big_hash_size);
		hashoutput->Seek(hashoutputpos);
	}

	std::string ret;
	ret.resize(64);
	sha512_final(&ctx, (unsigned char*)&ret[0]);
	return ret;
}

#endif //CLIENT_ONLY