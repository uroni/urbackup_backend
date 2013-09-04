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

#include "server_channel.h"

#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "database.h"
#include "server_get.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "server_status.h"
#include "server_settings.h"
#include "../urbackupcommon/capa_bits.h"
#include "serverinterface/helper.h"
#include <memory.h>
#include <algorithm>

const unsigned short serviceport=35623;
extern std::string server_identity;
extern IFSImageFactory *image_fak;

namespace
{
	IDatabase* getDatabase(void)
	{
		Helper helper(Server->getThreadID(), NULL, NULL);
		return helper.getDatabase();
	}

	bool needs_login(void)
	{
		db_results res=getDatabase()->Read("SELECT count(*) AS c FROM settings_db.si_users");
		if(watoi(res[0][L"c"])>0)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
}

ServerChannelThread::ServerChannelThread(BackupServerGet *pServer_get, int clientid, bool internet_mode) :
server_get(pServer_get), clientid(clientid), settings(NULL), internet_mode(internet_mode)
{
	do_exit=false;
	mutex=Server->createMutex();
	input=NULL;
	combat_mode=false;
	tcpstack.setAddChecksum(internet_mode);
}

ServerChannelThread::~ServerChannelThread(void)
{
	delete settings;
	Server->destroy(mutex);
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
			IPipe *np=server_get->getClientCommandConnection();
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
					salt=ServerSettings::generateRandomAuthKey();
					tcpstack.Send(input, server_identity+"1CHANNEL capa="+nconvert(constructCapabilities())+"&salt="+salt);
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
	if(msg=="ERR" && combat_mode==false)
	{
		tcpstack.Send(input, server_identity+"CHANNEL");
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
	else if(msg.find("LOGIN ")==0 && !internet_mode)
	{
		std::string s_params=msg.substr(6);
		str_map params;
		ParseParamStr(s_params, &params);
		login(params);
	}
	else if(msg=="GET BACKUPCLIENTS" && !internet_mode && hasDownloadImageRights() )
	{
		GET_BACKUPCLIENTS();
	}
	else if(msg.find("GET BACKUPIMAGES ")==0 && !internet_mode && hasDownloadImageRights())
	{
		std::wstring name=Server->ConvertToUnicode(msg.substr(17));
		GET_BACKUPIMAGES(name);
	}
	else if(msg.find("DOWNLOAD IMAGE ")==0 && !internet_mode && hasDownloadImageRights())
	{
		std::string s_params=msg.substr(15);
		str_map params;
		ParseParamStr(s_params, &params);

		DOWNLOAD_IMAGE(params);
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
	if(cs->no_images || (internet_mode && !cs->internet_image_backups))
		capa|=DONT_DO_IMAGE_BACKUPS;
	if(internet_mode && !cs->internet_full_file_backups)
		capa|=DONT_DO_FULL_FILE_BACKUPS;

	return capa;
}

void ServerChannelThread::login(str_map& params)
{
	if(needs_login())
	{
		str_nmap PARAMS;
		str_map GET;

		if(!session.empty())
		{
			GET[L"ses"]=session;
		}

		Helper helper(Server->getThreadID(), &GET, &PARAMS);

		if(session.empty())
		{
			session=helper.generateSession(L"anonymous");
			GET[L"ses"]=session;
			helper.update(Server->getThreadID(), &GET, &PARAMS);
		}

		helper.getSession()->mStr[L"rnd"]=widen(salt);

		int user_id;
		if(helper.checkPassword(params[L"username"], params[L"password"], &user_id))
		{
			helper.getSession()->id=user_id;
			tcpstack.Send(input, "ok");
		}
		else
		{
			helper.getSession()->id=0;
			tcpstack.Send(input, "err");
		}
	}
	else
	{
		tcpstack.Send(input, "ok");
	}
}

bool ServerChannelThread::hasDownloadImageRights()
{
	if(!needs_login())
	{
		all_client_rights=true;
		return true;
	}

	str_map GET;
	GET[L"ses"]=session;
	Helper helper(Server->getThreadID(), &GET, NULL);

	if(helper.getSession()->id==0)
	{
		all_client_rights=false;
		return false;
	}
	
	client_right_ids=helper.clientRights("download_image", all_client_rights);

	if(all_client_rights)
	{
		return true;
	}

	return std::find(client_right_ids.begin(), client_right_ids.end(), helper.getSession()->id)!=client_right_ids.end();
}

void ServerChannelThread::GET_BACKUPCLIENTS(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	std::string t_where="";
	if(!all_client_rights)
	{
		t_where=" WHERE 1=2";
		for(size_t i=0;i<client_right_ids.size();++i)
		{
			t_where+=" OR id="+nconvert(client_right_ids[i]);
		}
	}

	db_results res=db->Read("SELECT name,id FROM clients"+t_where);
	std::string clients;
	for(size_t i=0;i<res.size();++i)
	{
		clients+=Server->ConvertToUTF8(res[i][L"id"])+"|"+Server->ConvertToUTF8(res[i][L"name"])+"\n";
	}
	tcpstack.Send(input, clients);
	ServerStatus::updateActive();
}

void ServerChannelThread::GET_BACKUPIMAGES(const std::wstring& clientname)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	//TODO language
	IQuery *q=db->Prepare("SELECT backupid AS id, strftime('%s', backuptime) AS timestamp, strftime('%d.%m.%Y %H:%M',backuptime) AS backuptime, letter, clientid FROM ((SELECT id AS backupid, clientid, backuptime, letter, complete FROM backup_images) c INNER JOIN (SELECT id FROM clients WHERE name=?) b ON c.clientid=b.id) a WHERE a.complete=1 AND length(a.letter)<=2 ORDER BY backuptime DESC");
	q->Bind(clientname);
	db_results res=q->Read();

	for(size_t i=0;i<res.size();++i)
	{
		if(!all_client_rights &&
			std::find(client_right_ids.begin(), client_right_ids.end(), watoi(res[i][L"clientid"]))==client_right_ids.end())
		{
			tcpstack.Send(input, "0|0|0|NO RIGHTS");
			db->destroyAllQueries();
			return;
		}
	}

	std::string r;
	q=db->Prepare("SELECT backupid AS id,strftime('%s', backuptime) AS timestamp, strftime('%d.%m.%Y %H:%M',backuptime) AS backuptime FROM ((SELECT id AS backupid, clientid, backuptime, letter, complete FROM backup_images) c INNER JOIN (SELECT assoc_id, img_id FROM assoc_images WHERE img_id=?) b ON backupid=b.assoc_id) a WHERE a.complete=1 ORDER BY backuptime DESC");
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

void ServerChannelThread::DOWNLOAD_IMAGE(str_map& params)
{
	int img_id=watoi(params[L"img_id"]);

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	IQuery *q=db->Prepare("SELECT path, version, clientid FROM backup_images WHERE id=? AND strftime('%s', backuptime)=?");
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
		if( !all_client_rights &&
			std::find(client_right_ids.begin(), client_right_ids.end(), watoi(res[0][L"clientid"]))==client_right_ids.end())
		{
			_i64 r=-1;
			input->Write((char*)&r, sizeof(_i64));
			return;
		}


		int img_version=watoi(res[0][L"version"]);
		if(params[L"mbr"]==L"true")
		{
			IFile *f=Server->openFile(os_file_prefix(res[0][L"path"]+L".mbr"), MODE_READ);
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
							return;
						}
						lasttime=Server->getTimeMS();
					}
				}
				while(rc>0);
				Server->destroy(f);
				db->destroyAllQueries();
				return;
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
						return;
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