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

#include "server_channel.h"

#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "database.h"
#include "server_get.h"
#include "../stringtools.h"
#include "os_functions.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "server_status.h"
#include "server_settings.h"
#include "capa_bits.h"
#include <memory.h>

const unsigned short serviceport=35623;
extern std::string server_identity;
extern IFSImageFactory *image_fak;

ServerChannelThread::ServerChannelThread(BackupServerGet *pServer_get, int clientid) :
server_get(pServer_get), clientid(clientid), settings(NULL)
{
	do_exit=false;
	mutex=Server->createMutex();
	input=NULL;
	if(clientid!=-1)
	{
		combat_mode=false;
	}
	else
	{
		combat_mode=true;
	}
}

ServerChannelThread::~ServerChannelThread(void)
{
	delete settings;
}

void ServerChannelThread::operator()(void)
{
	unsigned int lastpingtime=0;
	lasttime=0;

	settings=new ServerSettings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER), clientid);

	while(do_exit==false)
	{
		if(input==NULL)
		{
			IPipe *np=Server->ConnectStream(inet_ntoa(server_get->getClientaddr().sin_addr), serviceport, 10000);
			if(np==NULL)
			{
				Server->Log("Connecting Channel to ClientService failed - CONNECT error -55", LL_DEBUG);
				Server->wait(10000);
			}
			else
			{
				{
					IScopedLock lock(mutex);
					input=np;
				}
				if(combat_mode)
				{
					tcpstack.Send(input, server_identity+"CHANNEL");
				}
				else
				{
					tcpstack.Send(input, server_identity+"1CHANNEL capa="+nconvert(constructCapabilities()));
				}
				lasttime=Server->getTimeMS();
				lastpingtime=lasttime;
			}
		}
		else
		{
			if(Server->getTimeMS()-lasttime>180000)
			{
				Server->Log("Resetting channel because of timeout.", LL_DEBUG);
				IScopedLock lock(mutex);
				Server->destroy(input);
				input=NULL;
				tcpstack.reset();
			}
			/*if(Server->getTimeMS()-lastpingtime>60000 && input!=NULL)
			{
				size_t rc=tcpstack.Send(input, "PING");
				if(rc==0)
				{
					Server->destroy(input);
					input=NULL;
					tcpstack.reset();
				}
				lastpingtime=Server->getTimeMS();
			}*/

			if(input!=NULL)
			{
				std::string ret;
				size_t rc=input->Read(&ret, 80000);
				if(rc>0)
				{
					tcpstack.AddData((char*)ret.c_str(), ret.size());

					size_t packetsize;
					char *pck=tcpstack.getPacket(&packetsize);
					if(pck!=NULL && packetsize>0)
					{
						ret=pck;
						delete [] pck;
						lasttime=Server->getTimeMS();
						std::string r=processMsg(ret);
						if(!r.empty())
							tcpstack.Send(input, r);
					}

					bool was_updated;
					settings->getSettings(&was_updated);
					if(was_updated && !combat_mode)
					{
						IScopedLock lock(mutex);
						Server->destroy(input);
						input=NULL;
						tcpstack.reset();
					}
				}
				else if(rc==0)
				{
					if(input->hasError())
					{
						Server->Log("Lost channel connection to client. has_error=true", LL_DEBUG);
						IScopedLock lock(mutex);
						Server->destroy(input);
						input=NULL;
						tcpstack.reset();
						Server->wait(1000);
					}
					else
					{
						Server->Log("Lost channel connection to client. has_error=false", LL_DEBUG);
						Server->wait(1000);
					}
				}
			}
		}
	}

	if(input!=NULL)
	{
		Server->destroy(input);
	}
}

void ServerChannelThread::doExit(void)
{
	IScopedLock lock(mutex);
	do_exit=true;
	if(input!=NULL)
	{
		input->shutdown();
	}
}

std::string ServerChannelThread::processMsg(const std::string &msg)
{
	if(msg=="ERR")
	{
		combat_mode=true;
	}
	else if(msg=="START BACKUP INCR")
	{
		server_get->sendToPipe("START BACKUP INCR");
	}
	else if(msg=="START BACKUP FULL")
	{
		server_get->sendToPipe("START BACKUP FULL");
	}
	else if(msg=="PING")
	{
		return "PONG";
	}
	else if(msg=="UPDATE SETTINGS")
	{
		server_get->sendToPipe("UPDATE SETTINGS");
	}
	else if(msg=="START IMAGE FULL")
	{
		server_get->sendToPipe("START IMAGE FULL");
	}
	else if(msg=="START IMAGE INCR")
	{
		server_get->sendToPipe("START IMAGE INCR");
	}
	else if(msg=="GET BACKUPCLIENTS")
	{
		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db_results res=db->Read("SELECT name,id FROM clients");
		std::string clients;
		for(size_t i=0;i<res.size();++i)
		{
			clients+=Server->ConvertToUTF8(res[i][L"id"])+"|"+Server->ConvertToUTF8(res[i][L"name"])+"\n";
		}
		tcpstack.Send(input, clients);
		ServerStatus::updateActive();
	}
	else if(msg.find("GET BACKUPIMAGES ")==0 )
	{
		std::wstring name=Server->ConvertToUnicode(msg.substr(17));
		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		//TODO language
		IQuery *q=db->Prepare("SELECT id,strftime('%s', backuptime) AS timestamp, strftime('%d.%m.%Y %H:%M',backuptime) AS backuptime, letter FROM (backup_images INNER JOIN (SELECT * FROM clients WHERE name=?) b ON backup_images.clientid=b.id) a WHERE a.complete=1 AND length(a.letter)<=2 ORDER BY backuptime DESC");
		q->Bind(name);
		db_results res=q->Read();
		std::string r;
		q=db->Prepare("SELECT id,strftime('%s', backuptime) AS timestamp, strftime('%d.%m.%Y %H:%M',backuptime) AS backuptime FROM (backup_images INNER JOIN (SELECT * FROM assoc_images WHERE img_id=?) b ON backup_images.id=b.assoc_id) a WHERE a.complete=1 ORDER BY backuptime DESC");
		for(size_t i=0;i<res.size();++i)
		{
			r+=Server->ConvertToUTF8(res[i][L"id"])+"|"+Server->ConvertToUTF8(res[i][L"timestamp"])+"|"+Server->ConvertToUTF8(res[i][L"backuptime"])+"|"+Server->ConvertToUTF8(res[i][L"letter"])+"\n";
			
			q->Bind(watoi(res[i][L"id"]));
			db_results res2=q->Read();
			q->Reset();
			for(size_t j=0;j<res2.size();++j)
			{
				r+="#|"+Server->ConvertToUTF8(res2[j][L"id"])+"|"+Server->ConvertToUTF8(res2[j][L"timestamp"])+"|"+Server->ConvertToUTF8(res2[j][L"backuptime"])+"\n";
			}
		}
		tcpstack.Send(input, r);

		db->destroyAllQueries();

		ServerStatus::updateActive();
	}
	else if(msg.find("DOWNLOAD IMAGE ")==0)
	{
		std::string s_params=msg.substr(15);
		str_map params;
		ParseParamStr(s_params, &params);

		int img_id=watoi(params[L"img_id"]);

		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		IQuery *q=db->Prepare("SELECT path, version FROM backup_images WHERE id=? AND strftime('%s', backuptime)=?");
		q->Bind(img_id);
		q->Bind(params[L"time"]);
		db_results res=q->Read();
		if(res.empty())
		{
			_i64 r=-1;
			input->Write((char*)&r, sizeof(_i64));
		}
		else
		{
			int img_version=watoi(res[0][L"version"]);
			if(params[L"mbr"]==L"true")
			{
				IFile *f=Server->openFile(os_file_prefix()+res[0][L"path"]+L".mbr", MODE_READ);
				if(f==NULL)
				{
					_i64 r=-1;
					input->Write((char*)&r, sizeof(_i64));
				}
				else
				{
					_i64 r=f->Size();
					input->Write((char*)&r, sizeof(_i64));
					char buf[4096];
					_u32 rc;
					do
					{
						rc=f->Read(buf, 4096);
						if(rc>0)
						{
							bool b=input->Write(buf, rc);
							if(!b)
							{
								Server->Log("Writing to output pipe failed processMsg-2", LL_ERROR);
								Server->destroy(f);
								db->destroyAllQueries();
								Server->destroy(input);
								input=NULL;
								return "";
							}
							lasttime=Server->getTimeMS();
						}
					}
					while(rc>0);
					Server->destroy(f);
					db->destroyAllQueries();
					return "";
				}
			}

			uint64 offset=0;
			str_map::iterator it1=params.find(L"offset");
			if(it1!=params.end())
				offset=(uint64)os_atoi64(wnarrow(it1->second));

			ServerStatus::updateActive();

			lasttime=Server->getTimeMS();

			IVHDFile *vhdfile=image_fak->createVHDFile(res[0][L"path"], true, 0);
			if(!vhdfile->isOpen())
			{
				_i64 r=-1;
				input->Write((char*)&r, sizeof(_i64));
			}
			else
			{
				int skip=1024*512;

				if(img_version==0)
					skip=512*512;

				_i64 r=(_i64)vhdfile->getSize()-skip;
				input->Write((char*)&r, sizeof(_i64));
				unsigned int blocksize=vhdfile->getBlocksize();
				char buffer[4096];
				size_t read;
				uint64 currpos=offset;
				_i64 currblock=(currpos+skip)%blocksize;

				vhdfile->Seek(skip);
				/*vhdfile->Read(buffer, 512, read);
				if(read!=512)
				{
					Server->Log("Error: Could not read 512 bytes", LL_ERROR);
					image_fak->destroyVHDFile(vhdfile);
					db->destroyAllQueries();
					return "";
				}

				input->Write(buffer, (_u32)read);*/

				unsigned int last_update_time=Server->getTimeMS();

				bool is_ok=true;
				do
				{
					if(vhdfile->has_sector())
					{
						is_ok=vhdfile->Read(buffer, 4096, read);
						if(read<4096)
						{
							Server->Log("Padding zero bytes...", LL_WARNING);
							memset(&buffer[read], 0, 4096-read);
							read=4096;
						}
						
						input->Write((char*)&currpos, sizeof(uint64));
						bool b=input->Write(buffer, (_u32)read);
						if(!b)
						{
							Server->Log("Writing to output pipe failed processMsg-1", LL_ERROR);
							image_fak->destroyVHDFile(vhdfile);
							db->destroyAllQueries();
							Server->destroy(input);
							input=NULL;
							return "";
						}
						lasttime=Server->getTimeMS();
					}
					else
					{
						if(Server->getTimeMS()-lasttime>30000)
						{
							input->Write((char*)&currpos, sizeof(uint64));
							memset(buffer, 0, 4096);
							input->Write(buffer, (_u32)4096);
							lasttime=Server->getTimeMS();
						}
						read=4096;
						vhdfile->Seek(skip+currpos+4096);
					}					
					currpos+=read;

					if(Server->getTimeMS()-last_update_time>60000)
					{
						last_update_time=Server->getTimeMS();
						ServerStatus::updateActive();
					}
				}
				while( is_ok && (_i64)currpos<r );
				if((_i64)currpos>=r)
				{
					input->Write((char*)&currpos, sizeof(uint64));
				}
			}
			image_fak->destroyVHDFile(vhdfile);
		}
		db->destroyAllQueries();
	}
	else if(msg=="ERR")
	{
		Server->wait(60000);
	}
	return "";
}

int ServerChannelThread::constructCapabilities(void)
{
	int capa=0;
	SSettings *cs=settings->getSettings();

	if(!cs->allow_overwrite)
		capa|=DONT_SHOW_SETTINGS;
	if(!cs->allow_pause)
		capa|=DONT_ALLOW_PAUSE;
	if(!cs->allow_starting_file_backups)
		capa|=DONT_ALLOW_STARTING_FILE_BACKUPS;
	if(!cs->allow_starting_image_backups)
		capa|=DONT_ALLOW_STARTING_IMAGE_BACKUPS;
	if(!cs->allow_config_paths)
		capa|=DONT_ALLOW_CONFIG_PATHS;
	if(!cs->allow_log_view)
		capa|=DONT_SHOW_LOGS;
	if(cs->no_images)
		capa|=DONT_DO_IMAGE_BACKUPS;

	return capa;
}

#endif //CLIENT_ONLY