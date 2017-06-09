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

#include "server_channel.h"

#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "database.h"
#include "ClientMain.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "server_status.h"
#include "server_settings.h"
#include "../urbackupcommon/capa_bits.h"
#include "serverinterface/helper.h"
#include "serverinterface/login.h"
#include <memory.h>
#include <algorithm>
#include <limits.h>
#include "../fileservplugin/IFileServ.h"
#include "server_log.h"
#include "restore_client.h"
#include "serverinterface/backups.h"
#include "dao/ServerBackupDao.h"

const unsigned short serviceport=35623;
extern IFSImageFactory *image_fak;
extern IFileServ* fileserv;

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
		if(watoi(res[0]["c"])>0)
		{
			return true;
		}
		else
		{
			return false;
		}
	}


	class SessionKeepaliveThread : public IThread
	{
	public:
		SessionKeepaliveThread(std::string session)
			: do_quit(false), session(session)
		{

		}

		void operator()()
		{
			while(!do_quit)
			{
				SUser* user=Server->getSessionMgr()->getUser(session, std::string());
				if(user!=NULL)
				{
					Server->getSessionMgr()->releaseUser(user);
				}
				Server->wait(10000);
			}

			delete this;
		}

		void doQuit() {
			do_quit=true;
		}

	private:
		volatile bool do_quit;
		std::string session;
	};

	class FileservClientThread : public IThread
	{
	public:
		FileservClientThread(IPipe* pipe, const char* p_extra_buffer, size_t extra_buffer_size)
			: pipe(pipe)
		{
			if(extra_buffer_size>0)
			{
				extra_buffer.assign(p_extra_buffer, p_extra_buffer+extra_buffer_size);
			}
		}

		void operator()()
		{
			fileserv->runClient(pipe, &extra_buffer);
			delete pipe;
			delete this;
		}

	private:
		IPipe* pipe;
		std::vector<char> extra_buffer;
	};
}

int ServerChannelThread::img_id_offset=0;

void ServerChannelThread::initOffset()
{
    IDatabase* db = getDatabase();
    ServerBackupDao backupdao(db);

    ServerBackupDao::CondString offset_str = backupdao.getMiscValue("img_id_offset");

    if(offset_str.exists)
    {
        img_id_offset = watoi(offset_str.value);
    }
    else
    {
        img_id_offset = Server->getRandomNumber()%((unsigned int)INT_MAX/2);
        backupdao.addMiscValue("img_id_offset", convert(img_id_offset));
    }
}

bool ServerChannelThread::isOnline()
{
	IScopedLock lock(mutex);
	return input != NULL;
}

ServerChannelThread::ServerChannelThread(ClientMain *client_main, const std::string& clientname, int clientid,
	bool internet_mode, bool allow_restore, const std::string& identity, std::string server_token, const std::string& virtual_client) :
	client_main(client_main), clientname(clientname), clientid(clientid), settings(NULL),
		internet_mode(internet_mode), allow_restore(allow_restore), keepalive_thread(NULL), server_token(server_token),
	virtual_client(virtual_client)
{
	do_exit=false;
	mutex=Server->createMutex();
	input=NULL;
	tcpstack.setAddChecksum(internet_mode);
}

ServerChannelThread::~ServerChannelThread(void)
{
	Server->destroy(mutex);
}

void ServerChannelThread::operator()(void)
{
	int64 lastpingtime=0;
	lasttime=0;

	settings=new ServerSettings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER), clientid);
	ScopedFreeObjRef<ServerSettings*> settings_free(settings);

	std::string curr_ident;

	while(do_exit==false)
	{
		if(input==NULL)
		{
			IPipe *np=client_main->getClientCommandConnection(10000, &client_addr);
			if(np==NULL)
			{
				Server->Log("Connecting Channel to "+clientname+" failed - CONNECT error -55", LL_DEBUG);
				Server->wait(10000);
			}
			else
			{
				{
					IScopedLock lock(mutex);
					input=np;
				}
				curr_ident = client_main->getIdentity();
				tcpstack.Send(input, curr_ident +"1CHANNEL capa="+convert(constructCapabilities())+"&token="+server_token+"&restore_version=1&virtual_client="+EscapeParamString(virtual_client));

				lasttime=Server->getTimeMS();
				lastpingtime=lasttime;
			}
		}
		else
		{
			if(Server->getTimeMS()-lasttime>180000)
			{
				Server->Log("Resetting channel to "+clientname+" because of timeout.", LL_DEBUG);
				reset();
			}

			if (curr_ident != client_main->getIdentity())
			{
				Server->Log("Resetting channel to " + clientname + " because session identity changed.", LL_DEBUG);
				reset();
			}

			if(input!=NULL)
			{
				std::string ret;
				size_t rc=input->Read(&ret, 80000);
				if(rc>0)
				{
					tcpstack.AddData((char*)ret.c_str(), ret.size());

					while(tcpstack.getPacket(ret) && !ret.empty())
					{
						if(ret!="PING")
						{
							Server->Log("Channel message: "+ret, LL_DEBUG);
						}
						lasttime=Server->getTimeMS();
						std::string r=processMsg(ret);
						if(!r.empty())
							tcpstack.Send(input, r);
					}

					bool was_updated;
					settings->getSettings(&was_updated);
					if(input!=NULL && was_updated)
					{
						Server->Log("Settings changed. Capabilities may have changed. Reconnecting channel...", LL_DEBUG);
						reset();
					}
				}
				else if(rc==0)
				{
					if(input->hasError())
					{
						Server->Log("Lost channel connection to "+clientname+". has_error=true", LL_DEBUG);
						reset();
						Server->wait(1000);
					}
					else
					{
						Server->Log("Lost channel connection to "+clientname+". has_error=false", LL_DEBUG);
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

	if(keepalive_thread!=NULL)
	{
		keepalive_thread->doQuit();
	}

	if (!fileclient_threads.empty())
	{
		Server->Log(clientname+"/server_channel: Waiting for fileclient threads...", LL_DEBUG);
		Server->getThreadPool()->waitFor(fileclient_threads);
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
		return std::string();
	}
	else if(msg=="START BACKUP INCR")
	{
		client_main->sendToPipe("START BACKUP INCR");
	}
	else if(msg=="START BACKUP FULL")
	{
		client_main->sendToPipe("START BACKUP FULL");
	}
	else if(msg=="PING")
	{
		client_main->refreshSessionIdentity();
		return "PONG";
	}
	else if(msg=="UPDATE SETTINGS")
	{
		client_main->sendToPipe("UPDATE SETTINGS");
	}
	else if(msg=="START IMAGE FULL")
	{
		client_main->sendToPipe("START IMAGE FULL");
	}
	else if(msg=="START IMAGE INCR")
	{
		client_main->sendToPipe("START IMAGE INCR");
	}
	else if (msg == "UPDATE ACCESS KEY")
	{
		client_main->sendToPipe("UPDATE ACCESS KEY");
	}
	else if (next(msg, 0, "METERED "))
	{
		std::string s_params = msg.substr(8);
		str_map params;
		ParseParamStrHttp(s_params, &params);
		client_main->setConnectionMetered(params["metered"] == "1");
	}
	else if(next(msg, 0, "LOGIN ") && allow_restore)
	{
		std::string s_params=msg.substr(6);
		str_map params;
		ParseParamStrHttp(s_params, &params);
		LOGIN(params);
	}
	else if(next(msg, 0, "SALT ") && allow_restore)
	{
		std::string s_params=msg.substr(5);
		str_map params;
		ParseParamStrHttp(s_params, &params);
		SALT(params);
	}
	else if(msg=="GET BACKUPCLIENTS" && allow_restore && hasDownloadImageRights() )
	{
		GET_BACKUPCLIENTS();
	}
	else if(next(msg, 0, "GET BACKUPIMAGES ") && allow_restore && hasDownloadImageRights())
	{
		std::string name=(msg.substr(17));
		GET_BACKUPIMAGES(name);
	}
    else if(next(msg, 0, "GET FILE BACKUPS TOKENS"))
    {
        std::string s_params;
		if(msg.size()>=24)
		{
			s_params = msg.substr(24);
		}		
        str_map params;
        ParseParamStrHttp(s_params, &params);

        GET_FILE_BACKUPS_TOKENS(params);
    }
	else if(next(msg, 0, "GET FILE BACKUPS ") && hasDownloadImageRights())
	{
		std::string name=(msg.substr(17));
		GET_FILE_BACKUPS(name);
	}
	else if(next(msg, 0, "GET FILE LIST TOKENS "))
	{
		std::string s_params=msg.substr(21);
		str_map params;
		ParseParamStrHttp(s_params, &params);

		GET_FILE_LIST_TOKENS(params);
	}
	else if(next(msg, 0, "START RESTORE TOKENS "))
	{
		std::string s_params=msg.substr(21);
		str_map params;
		ParseParamStrHttp(s_params, &params);
	}
	else if(next(msg, 0, "DOWNLOAD IMAGE ") && allow_restore && hasDownloadImageRights())
	{
		std::string s_params=msg.substr(15);
		str_map params;
		ParseParamStrHttp(s_params, &params);

		DOWNLOAD_IMAGE(params);
		Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER)->destroyAllQueries();
	}
	else if(next(msg, 0, "DOWNLOAD FILES TOKENS "))
	{
		std::string s_params=msg.substr(22);
		str_map params;
		ParseParamStrHttp(s_params, &params);

		DOWNLOAD_FILES_TOKENS(params);
	}
	else if(next(msg, 0, "DOWNLOAD FILES ") && hasDownloadImageRights())
	{
		std::string s_params=msg.substr(15);
		str_map params;
		ParseParamStrHttp(s_params, &params);

		DOWNLOAD_FILES(params);
	}
	else if(next(msg, 0, "CHANGES "))
	{
		client_main->addContinuousChanges(msg.substr(8));
	}
	else if(next(msg, 0, "FILESERV"))
	{
		if(fileserv!=NULL)
		{
			fileclient_threads.push_back(Server->getThreadPool()->execute(new FileservClientThread(input, tcpstack.getBuffer(), tcpstack.getBuffersize()), "fileserver"));
			input=NULL;
			tcpstack.reset();
		}
	}
	else if(next(msg, 0, "RESTORE PERCENT "))
	{
		std::string s_params=msg.substr(16);
		str_map params;
		ParseParamStrHttp(s_params, &params);

		RESTORE_PERCENT(params);
	}
	else if(next(msg, 0, "RESTORE DONE "))
	{
		std::string s_params=msg.substr(13);
		str_map params;
		ParseParamStrHttp(s_params, &params);

		RESTORE_DONE(params);
	}
	else if(next(msg, 0, "LOG "))
	{
		size_t first_sep = msg.find("-", 4);
		size_t second_sep = msg.find("-", first_sep+1);

		if(first_sep!=std::string::npos &&
			second_sep!=std::string::npos)
		{
			logid_t log_id = std::make_pair(os_atoi64(msg.substr(4, first_sep-4)), 0);
			int loglevel = atoi(msg.substr(first_sep+1, second_sep-first_sep-1).c_str());
			
			if(ServerLogger::hasClient(log_id, clientid))
			{
				ServerLogger::Log(log_id, msg.substr(second_sep+1), loglevel);
			}
		}
	}
	else
	{
		IScopedLock lock(mutex);
		Server->destroy(input);
		input=NULL;
		tcpstack.reset();
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
	if(!cs->allow_starting_full_file_backups && !cs->allow_starting_incr_file_backups)
		capa|=DONT_ALLOW_STARTING_FILE_BACKUPS;
	if(!cs->allow_starting_full_image_backups && !cs->allow_starting_incr_image_backups)
		capa|=DONT_ALLOW_STARTING_IMAGE_BACKUPS;
	if(!cs->allow_config_paths)
		capa|=DONT_ALLOW_CONFIG_PATHS;
	if(!cs->allow_log_view)
		capa|=DONT_SHOW_LOGS;
	if(cs->no_images || (internet_mode && !cs->internet_image_backups))
		capa|=DONT_DO_IMAGE_BACKUPS;
	if(internet_mode && !cs->internet_full_file_backups)
		capa|=DONT_DO_FULL_FILE_BACKUPS;
	if(!cs->allow_starting_full_file_backups)
		capa|=DONT_ALLOW_STARTING_FULL_FILE_BACKUPS;
	if(!cs->allow_starting_incr_file_backups)
		capa|=DONT_ALLOW_STARTING_INCR_FILE_BACKUPS;
	if(!cs->allow_starting_full_image_backups)
		capa|=DONT_ALLOW_STARTING_FULL_IMAGE_BACKUPS;
	if(!cs->allow_starting_incr_image_backups)
		capa|=DONT_ALLOW_STARTING_INCR_IMAGE_BACKUPS;
	if(!cs->allow_tray_exit)
		capa|=DONT_ALLOW_EXIT_TRAY_ICON;
	if(!cs->server_url.empty())
		capa|=ALLOW_TOKEN_AUTHENTICATION;
	if (!cs->allow_file_restore)
		capa |= DONT_ALLOW_FILE_RESTORE;
	if (!cs->allow_component_restore)
		capa |= DONT_ALLOW_COMPONENT_RESTORE;
	if (!cs->allow_component_restore)
		capa |= DONT_ALLOW_COMPONENT_CONFIG;

	return capa;
}

void ServerChannelThread::LOGIN(str_map& params)
{
	str_map PARAMS;
	str_map GET;

	if(!session.empty())
	{
		GET["ses"]=session;
	}

	Helper helper(Server->getThreadID(), &GET, &PARAMS);

	if(needs_login())
	{
		if(session.empty())
		{
			session=helper.generateSession("anonymous");
			GET["ses"]=session;
			helper.update(Server->getThreadID(), &GET, &PARAMS);

			if(keepalive_thread!=NULL)
			{
				keepalive_thread->doQuit();
			}
			keepalive_thread = new SessionKeepaliveThread(session);
			Server->getThreadPool()->execute(keepalive_thread, "restore keepalive");
		}

		helper.getSession()->mStr["rnd"]=salt;

		int user_id;
		if(helper.checkPassword(params["username"], params["password"], &user_id, false))
		{
			helper.getSession()->id=user_id;
			PARAMS["REMOTE_ADDR"]=client_addr;
			logSuccessfulLogin(helper, PARAMS, params["username"], LoginMethod_RestoreCD);
			tcpstack.Send(input, "ok");
		}
		else
		{
			helper.getSession()->id=-1;
			tcpstack.Send(input, "err");
			logFailedLogin(helper, PARAMS, params["username"], LoginMethod_RestoreCD);
		}
	}
	else
	{
		logSuccessfulLogin(helper, PARAMS, "anonymous", LoginMethod_RestoreCD);
		tcpstack.Send(input, "ok");
	}
}

void ServerChannelThread::SALT(str_map& params)
{
	if(needs_login())
	{
		std::string username=params["username"];

		if(username.empty())
		{
			tcpstack.Send(input, "err: username empty");
			return;
		}

		str_map PARAMS;
		str_map GET;

		if(!session.empty())
		{
			GET["ses"]=session;
		}

		Helper helper(Server->getThreadID(), &GET, &PARAMS);

		IQuery * q = helper.getDatabase()->Prepare("SELECT salt, pbkdf2_rounds FROM settings_db.si_users WHERE name=?");
		q->Bind(username);
		db_results res=q->Read();
		if(res.empty())
		{
			tcpstack.Send(input, "err: user not found");
		}
		else
		{
			salt=ServerSettings::generateRandomAuthKey();

			std::string pbkdf2_rounds="";
			if(res[0]["pbkdf2_rounds"]!="0")
			{
				pbkdf2_rounds+=";"+(res[0]["pbkdf2_rounds"]);
			}

			tcpstack.Send(input, "ok;"+(res[0]["salt"])+";"+salt+pbkdf2_rounds);
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
	str_map PARAMS;
	GET["ses"]=session;
	Helper helper(Server->getThreadID(), &GET, &PARAMS);

	if(helper.getSession()==NULL)
	{
		Server->Log("Channel session timeout", LL_ERROR);
		return false;
	}

	if(helper.getSession()==NULL)
	{
		Server->Log("Channel session timeout", LL_ERROR);
		return false;
	}

	if(helper.getSession()->id==-1)
	{
		all_client_rights=false;
		return false;
	}
	
	client_right_ids=helper.clientRights("download_image", all_client_rights);

	if(all_client_rights)
	{
		return true;
	}

	return !client_right_ids.empty();
}

int ServerChannelThread::getLastBackupid(IDatabase* db)
{
	IQuery* q = db->Prepare("SELECT id FROM backups WHERE clientid=? AND complete=1 ORDER BY backuptime DESC LIMIT 1");
	q->Bind(clientid);
	db_results res = q->Read();
	q->Reset();

	if (!res.empty())
	{
		return watoi(res[0]["id"]);
	}

	return 0;
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
			t_where+=" OR id="+convert(client_right_ids[i]);
		}
	}

	db_results res=db->Read("SELECT name,id FROM clients"+t_where);
	std::string clients;
	for(size_t i=0;i<res.size();++i)
	{
		clients+=(res[i]["id"])+"|"+(res[i]["name"])+"\n";
	}
	tcpstack.Send(input, clients);
	ServerStatus::updateActive();
}

void ServerChannelThread::GET_BACKUPIMAGES(const std::string& clientname)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	//TODO language
	IQuery *q=db->Prepare("SELECT backupid AS id, strftime('%s', backuptime) AS timestamp, strftime('%Y-%m-%d %H:%M',backuptime,'localtime') AS backuptime, letter, clientid FROM ((SELECT id AS backupid, clientid, backuptime, letter, complete FROM backup_images) c INNER JOIN (SELECT id FROM clients WHERE name=?) b ON c.clientid=b.id) a WHERE a.complete=1 AND length(a.letter)<=2 ORDER BY backuptime DESC");
	q->Bind(clientname);
	db_results res=q->Read();

	for(size_t i=0;i<res.size();++i)
	{
		if(!all_client_rights &&
			std::find(client_right_ids.begin(), client_right_ids.end(), watoi(res[i]["clientid"]))==client_right_ids.end())
		{
			tcpstack.Send(input, "0|0|0|NO RIGHTS");
			db->destroyAllQueries();
			return;
		}
	}

	std::string r;
	q=db->Prepare("SELECT backupid AS id,strftime('%s', backuptime) AS timestamp, strftime('%Y-%m-%d %H:%M',backuptime,'localtime') AS backuptime FROM ((SELECT id AS backupid, clientid, backuptime, letter, complete FROM backup_images) c INNER JOIN (SELECT assoc_id, img_id FROM assoc_images WHERE img_id=?) b ON backupid=b.assoc_id) a WHERE a.complete=1 ORDER BY backuptime DESC");
	for(size_t i=0;i<res.size();++i)
	{
		r+=convert(img_id_offset+watoi(res[i]["id"]))+"|"+(res[i]["timestamp"])+"|"+(res[i]["backuptime"])+"|"+(res[i]["letter"])+"\n";
			
		q->Bind(watoi(res[i]["id"]));
		db_results res2=q->Read();
		q->Reset();
		for(size_t j=0;j<res2.size();++j)
		{
			r+="#|"+convert(img_id_offset+watoi(res2[j]["id"]))+"|"+(res2[j]["timestamp"])+"|"+(res2[j]["backuptime"])+"\n";
		}
	}
	tcpstack.Send(input, r);

	db->destroyAllQueries();

	ServerStatus::updateActive();
}

void ServerChannelThread::GET_FILE_BACKUPS( const std::string& clientname )
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	IQuery *q=db->Prepare("SELECT backupid AS id, strftime('%s', backuptime) AS timestamp, strftime('%Y-%m-%d %H:%M',backuptime,'localtime') AS backuptime, clientid, tgroup FROM "
		"((SELECT id AS backupid, clientid, backuptime, complete, tgroup FROM backups) c INNER JOIN (SELECT id FROM clients WHERE name=?) b ON c.clientid=b.id) a "
		"WHERE a.complete=1 ORDER BY backuptime DESC");
	q->Bind(clientname);
	db_results res=q->Read();

	for(size_t i=0;i<res.size();++i)
	{
		if(!all_client_rights &&
			std::find(client_right_ids.begin(), client_right_ids.end(), watoi(res[i]["clientid"]))==client_right_ids.end())
		{
			tcpstack.Send(input, "0|0|0|NO RIGHTS");
			db->destroyAllQueries();
			return;
		}
	}

	std::string r;
	for(size_t i=0;i<res.size();++i)
	{
		r+=convert(img_id_offset+watoi(res[i]["id"]))+"|"+(res[i]["timestamp"])+"|"+(res[i]["backuptime"])+"|"+(res[i]["tgroup"])+"\n";
	}
	tcpstack.Send(input, r);

	db->destroyAllQueries();

	ServerStatus::updateActive();
}

void ServerChannelThread::GET_FILE_BACKUPS_TOKENS(str_map& params)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if(params.find("token_data")!=params.end())
	{
		last_fileaccesstokens = backupaccess::decryptTokens(db, params);
	}

	if(last_fileaccesstokens.empty())
	{
        tcpstack.Send(input, "err");
		db->destroyAllQueries();
		return;
	}

	int local_id_offset;
	if(params["with_id_offset"]=="false")
	{
		local_id_offset = 0;
	}
	else
	{
		local_id_offset = img_id_offset;
	}

	bool has_access;
	JSON::Array backups = backupaccess::get_backups_with_tokens(db, clientid, clientname, &last_fileaccesstokens, local_id_offset, has_access);

	if (!has_access)
	{
		tcpstack.Send(input, "err");
	}
	else
	{
		tcpstack.Send(input, backups.stringify(false));
	}
    
	db->destroyAllQueries();

	ServerStatus::updateActive();
}

void ServerChannelThread::GET_FILE_LIST_TOKENS(str_map& params)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if(params.find("token_data")!=params.end())
	{
		last_fileaccesstokens = backupaccess::decryptTokens(db, params);
	}

	JSON::Object ret;
	if(last_fileaccesstokens.empty())
	{
		tcpstack.Send(input, "err");
		db->destroyAllQueries();
		return;
	}

	int local_id_offset;
	if(params["with_id_offset"]=="false")
	{
		local_id_offset = 0;
	}
	else
	{
		local_id_offset = img_id_offset;
	}

	bool has_backupid=params.find("backupid")!=params.end();
	int backupid=0;
	if(has_backupid)
	{
		backupid = watoi(params["backupid"]);

		if (backupid == 0)
		{
			backupid = getLastBackupid(db);
		}
		else
		{
			backupid -= local_id_offset;
		}
	}

	std::string u_path=params["path"];

	if(!backupaccess::get_files_with_tokens(db, has_backupid? &backupid:NULL,
        clientid, clientname, &last_fileaccesstokens, u_path, local_id_offset, ret))
	{
		tcpstack.Send(input, "err");
	}
	else
	{
        tcpstack.Send(input, ret.get(std::string("files")).getArray().stringify(false));
	}

	db->destroyAllQueries();

	ServerStatus::updateActive();
}

namespace
{
	class ScopedDestroyVhdfile
	{
		IVHDFile* vhdfile;
	public:
		ScopedDestroyVhdfile(IVHDFile* vhdfile)
			: vhdfile(vhdfile) {}
		~ScopedDestroyVhdfile() {
			image_fak->destroyVHDFile(vhdfile);
		}
	};

	class ScopedSetRestoreDone
	{
		bool ok;
		ServerBackupDao& backup_dao;
		int64 restore_id;
	public:
		ScopedSetRestoreDone(ServerBackupDao& backup_dao, int64 restore_id)
			: backup_dao(backup_dao), ok(false), restore_id(restore_id)
		{}

		~ScopedSetRestoreDone() {
			backup_dao.setRestoreDone(ok ? 1 : 0, restore_id);
		}

		void set_ok() {
			ok = true;
		}
	};
}

void ServerChannelThread::DOWNLOAD_IMAGE(str_map& params)
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	const _u32 img_send_timeout = 30000;

	ServerBackupDao backup_dao(db);

	int img_id=watoi(params["img_id"])-img_id_offset;
	
	IQuery *q=db->Prepare("SELECT path, version, clientid, letter FROM backup_images WHERE id=? AND strftime('%s', backuptime)=?");
	q->Bind(img_id);
	q->Bind(params["time"]);
	db_results res=q->Read();
	if(res.empty())
	{
		Server->Log("Image to download not found (img_id=" + convert(img_id) + " time=" + params["time"] + ")", LL_DEBUG);
		_i64 r=-1;
		if (!input->Write((char*)&r, sizeof(_i64), img_send_timeout))
		{
			reset();
			return;
		}
	}
	else
	{
		if( !all_client_rights &&
			std::find(client_right_ids.begin(), client_right_ids.end(), watoi(res[0]["clientid"]))==client_right_ids.end())
		{
			Server->Log("No permission to download image of client with id " + res[0]["clientid"], LL_DEBUG);
			_i64 r=-1;
			if (!input->Write((char*)&r, sizeof(_i64), img_send_timeout))
			{
				reset();
			}
			return;
		}

		int img_version=watoi(res[0]["version"]);
		if(params["mbr"]=="true")
		{
			std::auto_ptr<IFile> f(Server->openFile(os_file_prefix(res[0]["path"]+".mbr"), MODE_READ));
			if(f.get()==NULL)
			{
				_i64 r=little_endian(-1);
				if (!input->Write((char*)&r, sizeof(_i64), img_send_timeout))
				{
					reset();
				}
				return;
			}
			else
			{
				_i64 r=little_endian(f->Size());
				if (!input->Write((char*)&r, sizeof(_i64), img_send_timeout))
				{
					reset();
					return;
				}
				char buf[4096];
				_u32 rc;
				do
				{
					rc=f->Read(buf, 4096);
					if(rc>0)
					{
						bool b=input->Write(buf, rc, img_send_timeout);
						if(!b)
						{
							Server->Log("Writing to output pipe failed processMsg-2", LL_ERROR);
							reset();
							return;
						}
						lasttime=Server->getTimeMS();
					}
				}
				while(rc>0);
				return;
			}
		}

		uint64 offset=0;
		str_map::iterator it1=params.find("offset");
		if(it1!=params.end())
			offset=(uint64)os_atoi64(it1->second);

		ServerStatus::updateActive();

		lasttime=Server->getTimeMS();

		std::string file_extension = strlower(findextension(res[0]["path"]));

		IVHDFile *vhdfile;
		if (file_extension == "raw")
		{
			vhdfile = image_fak->createVHDFile(res[0]["path"], true, 0, 2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_RawCowFile); 
		}
		else
		{
			vhdfile = image_fak->createVHDFile(res[0]["path"], true, 0);
		}

		ScopedDestroyVhdfile destroy_vhdfile(vhdfile);

		if(vhdfile==NULL 
			|| !vhdfile->isOpen())
		{
			_i64 r=-1;
			if (!input->Write((char*)&r, sizeof(_i64), img_send_timeout))
			{
				reset();
			}
		}
		else
		{
			std::string clientname = backup_dao.getClientnameByImageid(img_id).value;
			ScopedProcess restore_process(clientname, sa_restore_image, res[0]["letter"], logid_t(), false, watoi(res[0]["clientid"]));
			backup_dao.addRestore(watoi(res[0]["clientid"]), std::string(), std::string(), 1, res[0]["letter"]);
			ScopedSetRestoreDone restore_done(backup_dao, db->getLastInsertID());

			int skip=1024*512;

			if(img_version==0
				&& file_extension!="raw")
				skip=512*512;

			_i64 imgsize = (_i64)vhdfile->getSize() - skip;
			_i64 r=little_endian(imgsize);
			if (!input->Write((char*)&r, sizeof(_i64), img_send_timeout))
			{
				Server->Log("Sending image size failed", LL_DEBUG);
				reset();
				return;
			}
			unsigned int blocksize=vhdfile->getBlocksize();
			char buffer[4096];
			size_t read;
			uint64 currpos=offset;
			_i64 currblock=(currpos+skip)%blocksize;

			/*vhdfile->Read(buffer, 512, read);
			if(read!=512)
			{
				Server->Log("Error: Could not read 512 bytes", LL_ERROR);
				image_fak->destroyVHDFile(vhdfile);
				db->destroyAllQueries();
				return "";
			}

			input->Write(buffer, (_u32)read);*/

			int64 last_update_time=Server->getTimeMS();

			int64 used_bytes = vhdfile->usedSize();
			int64 used_transferred_bytes = 0;

			ServerStatus::setProcessPcDone(clientname, restore_process.getStatusId(), 0);
			int pcdone = 0;

			vhdfile->Seek(0);

			for (uint64 pos = skip; pos < skip + currpos; pos += blocksize)
			{
				vhdfile->Seek(pos);
				if (vhdfile->has_sector())
				{
					used_transferred_bytes += blocksize;
				}
			}


			if (params["with_used_bytes"] == "1")
			{
				_i64 r = little_endian(used_bytes - skip);
				if (!input->Write((char*)&r, sizeof(_i64)))
				{
					Server->Log("Sending used bytes failed", LL_DEBUG);
					reset();
					return;
				}
			}

			vhdfile->Seek(skip);

			bool is_ok=true;
			do
			{
				if(vhdfile->has_sector())
				{
					is_ok=vhdfile->Read(buffer, 4096, read);
					if(read<4096)
					{
						Server->Log("Padding "+convert(4096 - read)+" zero bytes during restore...", LL_WARNING);
						memset(&buffer[read], 0, 4096-read);
						read=4096;
					}
					if (!is_ok)
					{
						Server->Log("Error reading from VHD file during restore. "+os_last_error_str(), LL_ERROR);
					}
						
					uint64 currpos_endian = little_endian(currpos);
					bool b = input->Write((char*)&currpos_endian, sizeof(uint64), img_send_timeout, false);
					if (b)
					{
						b = input->Write(buffer, (_u32)read, img_send_timeout, false);
					}
					if(!b)
					{
						Server->Log("Writing to output pipe failed processMsg-1", LL_ERROR);
						reset();
						return;
					}
					used_transferred_bytes += read;
					lasttime=Server->getTimeMS();
				}
				else
				{
					if(Server->getTimeMS()-lasttime>30000)
					{
						uint64 currpos_endian = little_endian(currpos);
						bool b = input->Write((char*)&currpos_endian, sizeof(uint64), img_send_timeout, false);
						memset(buffer, 0, 4096);
						if (b)
						{
							 b = input->Write(buffer, (_u32)4096, img_send_timeout, true);
						}
						if (!b)
						{
							Server->Log("Sending keep-alive block failed", LL_DEBUG);
							reset();
							return;
						}
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

					if (used_bytes > 0)
					{
						int pcdone_new = static_cast<int>((used_transferred_bytes * 100) / used_bytes);
						if (pcdone_new != pcdone)
						{
							pcdone = pcdone_new;
							ServerStatus::setProcessPcDone(clientname, restore_process.getStatusId(), pcdone);
						}
					}					
				}
			}
			while( is_ok && (_i64)currpos<imgsize);
			if((_i64)currpos>=imgsize)
			{
				r = little_endian(0x7fffffffffffffffLL);
				if (!input->Write((char*)&r, sizeof(int64), img_send_timeout))
				{
					Server->Log("Sending image end failed", LL_WARNING);
					reset();
					return;
				}
			}
			else
			{
				if (!input->Flush())
				{
					reset();
					return;
				}
			}

			if (is_ok)
			{
				restore_done.set_ok();
			}
		}
	}
}

void ServerChannelThread::DOWNLOAD_FILES( str_map& params )
{
	int backupid=watoi(params["backupid"])-img_id_offset;

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	IQuery *q=db->Prepare("SELECT clientid FROM backups WHERE id=? AND strftime('%s', backuptime)=?");
	q->Bind(backupid);
	q->Bind(params["time"]);
	db_results res=q->Read();
	if(res.empty())
	{
		JSON::Object ret;
		ret.set("err", 2);
		tcpstack.Send(input, ret.stringify(false));
	}
	else
	{
		if( !all_client_rights &&
			std::find(client_right_ids.begin(), client_right_ids.end(), watoi(res[0]["clientid"]))==client_right_ids.end())
		{
			JSON::Object ret;
			ret.set("err", 4);
			tcpstack.Send(input, ret.stringify(false));
			return;
		}

		std::vector<std::pair<std::string, std::string> > map_paths;
		for (size_t i = 0; params.find("map_path_source" + convert(i)) != params.end(); ++i)
		{
			map_paths.push_back(std::make_pair(params["map_path_source" + convert(i)], params["map_path_target" + convert(i)]));
		}

		int64 restore_id;
		size_t status_id;
		logid_t log_id;

		bool clean_other = params["clean_other"] == "1";
		bool ignore_other_fs = params["ignore_other_fs"] != "0";
		bool follow_symlinks = params["follow_symlinks"] == "0";
		int64 restore_flags = 0;
		str_map::iterator restore_flags_it = params.find("restore_flags");
		if (restore_flags_it != params.end())
		{
			restore_flags = watoi64(restore_flags_it->second);
		}

		std::vector<std::string> tokens;

		if(create_clientdl_thread(backupid, clientname, clientid, restore_id, status_id, log_id,
			params["restore_token"], map_paths, clean_other, ignore_other_fs, follow_symlinks, restore_flags, tokens))
		{
			JSON::Object ret;
			ret.set("ok", true);
			ret.set("restore_id", restore_id);
			ret.set("status_id", status_id);
			ret.set("log_id", log_id.first);
			tcpstack.Send(input, ret.stringify(false));
		}
		else
		{
			JSON::Object ret;
			ret.set("err", 5);
			tcpstack.Send(input, ret.stringify(false));
		}
	}
}

void ServerChannelThread::DOWNLOAD_FILES_TOKENS(str_map& params)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if (params.find("token_data") != params.end())
	{
		last_fileaccesstokens = backupaccess::decryptTokens(db, params);
	}

	JSON::Object ret;
	do 
	{
		if(last_fileaccesstokens.empty())
		{
			ret.set("err", 1);
			break;
		}

		int local_id_offset;
		if (params["with_id_offset"] == "true")
		{
			local_id_offset = img_id_offset;
		}
		else
		{
			local_id_offset = 0;
		}

		bool has_backupid=params.find("backupid")!=params.end();
		int backupid=0;
		if(has_backupid)
		{
			backupid=watoi(params["backupid"]);

			if (backupid != 0)
			{
				backupid -= local_id_offset;
			}
		}
		else
		{
			ret.set("err", 2);
			break;
		}

		if (backupid == 0)
		{
			backupid = getLastBackupid(db);
		}

		std::string u_path=params["path"];

		std::string backupfolder = backupaccess::getBackupFolder(db);
		std::string backuppath = backupaccess::get_backup_path(db, backupid, clientid);

		if(backupfolder.empty())
		{
			ret.set("err", 3);
			break;
		}

		backupaccess::SPathInfo path_info = backupaccess::get_metadata_path_with_tokens(u_path, &last_fileaccesstokens,
			clientname, backupfolder, &backupid, backuppath);

		if(!path_info.can_access_path)
		{
			ret.set("err", 4);
			break;
		}

		std::vector<std::string> tokens;
		Tokenize(last_fileaccesstokens, tokens, ";");

		int64 restore_id;
		size_t status_id;
		logid_t log_id;

		std::string filename;
		if(path_info.is_file)
		{
			filename = ExtractFileName(path_info.full_path, os_file_sep());
			if(!filename.empty())
			{
				path_info.full_path = ExtractFilePath(path_info.full_path, os_file_sep());
				path_info.full_metadata_path = ExtractFilePath(path_info.full_metadata_path, os_file_sep());
				path_info.rel_path = ExtractFilePath(path_info.rel_path, os_file_sep());
			}
			else
			{
				ret.set("err", 5);
				break;
			}
		}

		if(filename.empty())
		{
			filename = params["filter"];
		}

		std::vector<std::pair<std::string, std::string> > map_paths;
		for (size_t i = 0; params.find("map_path_source" + convert(i)) != params.end(); ++i)
		{
			map_paths.push_back(std::make_pair(params["map_path_source" + convert(i)], params["map_path_target" + convert(i)]));
		}

		bool clean_other = params["clean_other"] == "1";
		bool ignore_other_fs = params["ignore_other_fs"] != "0";
		bool follow_symlinks= params["follow_symlinks"] == "0";
		int64 restore_flags = 0;
		str_map::iterator restore_flags_it = params.find("restore_flags");
		if (restore_flags_it != params.end())
		{
			restore_flags = watoi64(restore_flags_it->second);
		}
		THREADPOOL_TICKET ticket;

		if(!create_clientdl_thread(clientname, clientid, clientid, path_info.full_path, path_info.full_metadata_path, filename, 
			path_info.rel_path.empty(), path_info.rel_path, restore_id, status_id, log_id, params["restore_token"],
			map_paths, clean_other, ignore_other_fs, greplace(os_file_sep(), "/", path_info.rel_path), follow_symlinks, restore_flags,
			ticket, tokens, path_info.backup_tokens))
		{
			ret.set("err", 5);
			break;
		}

		ret.set("ok", true);
		ret.set("restore_id", restore_id);
		ret.set("status_id", status_id);
		ret.set("log_id", log_id.first);

		str_map::iterator it_process_id = params.find("process_id");
		if (it_process_id != params.end())
		{
			ret.set("process_id", watoi64(it_process_id->second));
		}
	} while (false);

    tcpstack.Send(input, ret.stringify(false));
	db->destroyAllQueries();	
}

void ServerChannelThread::RESTORE_PERCENT( str_map params )
{
	int64 status_id = watoi64(params["status_id"]);
	int64 restore_id = watoi64(params["id"]);
	int pc = watoi(params["pc"]);
	double speed_bpms = atof(params["speed_bpms"].c_str());
	std::string details = params["details"];
	int64 total_bytes = watoi64(params["total_bytes"]);
	int64 done_bytes = watoi64(params["done_bytes"]);
	int detail_pc = watoi(params["detail_pc"]);

	client_main->updateRestoreRunning(restore_id);

	ServerStatus::setProcessPcDone(clientname, status_id, pc);
	ServerStatus::setProcessSpeed(clientname, status_id, speed_bpms);
	ServerStatus::setProcessDoneBytes(clientname, status_id, done_bytes);
	ServerStatus::setProcessTotalBytes(clientname, status_id, total_bytes);
}

void ServerChannelThread::RESTORE_DONE( str_map params )
{
	int64 status_id = watoi64(params["status_id"]);
	logid_t log_id = std::make_pair(watoi64(params["log_id"]), 0);
	int64 restore_id = watoi64(params["id"]);
	bool success = params["success"]=="true";

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);

	ServerBackupDao::CondString restore_ident = backup_dao.getRestoreIdentity(restore_id, clientid);

	if(restore_ident.exists && !restore_ident.value.empty())
	{
		std::string ident = (restore_ident.value);
		ServerStatus::stopProcess(clientname, status_id);

		fileserv->removeIdentity(ident);
		fileserv->removeDir("clientdl_filelist", ident);
		size_t idx = 0;
		while (fileserv->removeDir("clientdl"+convert(idx), ident))
		{
			fileserv->removeMetadataCallback("clientdl"+convert(idx), ident);
			++idx;
		}		

		ServerBackupDao::CondString restore_path = backup_dao.getRestorePath(restore_id, clientid);

		if (!restore_path.exists
			|| restore_path.value != "windows_components_config"+os_file_sep())
		{
			int errors = 0;
			int warnings = 0;
			int infos = 0;
			std::string logdata = ServerLogger::getLogdata(log_id, errors, warnings, infos);

			backup_dao.saveBackupLog(clientid, errors, warnings, infos, 0,
				0, 0, 1);

			backup_dao.saveBackupLogData(db->getLastInsertID(), logdata);

			backup_dao.setRestoreDone(success ? 1 : 0, restore_id);
		}
		else
		{
			backup_dao.deleteRestore(restore_id);
		}	

		ServerLogger::reset(log_id);

		client_main->finishRestore(restore_id);
		ClientMain::cleanupRestoreShare(clientid, restore_ident.value);
	}
	
}

void ServerChannelThread::reset()
{
	IScopedLock lock(mutex);
	Server->destroy(input);
	input = NULL;
	tcpstack.reset();
}


