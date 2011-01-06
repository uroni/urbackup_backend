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
#include "fileclient/data.h"
#include "../Interface/Server.h"
#include "sha2/sha2.h"
#include "../stringtools.h"
#include "server_log.h"

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

			unsigned int backupid;
			rd.getUInt(&backupid);

			std::string tfn;
			rd.getStr(&tfn);

			IFile *tf=Server->openFile(Server->ConvertToUnicode(temp_fn), MODE_READ);

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
				data.addUInt(backupid);
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

#endif //CLIENT_ONLY