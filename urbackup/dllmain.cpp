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

#include <vector>

#define DEF_SERVER
#include "../Interface/Server.h"
#include "../Interface/Action.h"
#include "../Interface/Database.h"
#include "../Interface/SessionMgr.h"
#include "../Interface/Pipe.h"
#include "../Interface/Query.h"
#include "../Interface/Thread.h"
#include "../Interface/File.h"

#include "../fsimageplugin/IFSImageFactory.h"
#include "../pychart/IPychartFactory.h"
#include "../downloadplugin/IDownloadFactory.h"
#include "../cryptoplugin/ICryptoFactory.h"

IServer *Server;

#include "database.h"
#include "actions.h"
#include "serverinterface/actions.h"

#include "server.h"
#include "ClientService.h"
#include "client.h"
#include "../stringtools.h"
#include "server_status.h"
#include "server_log.h"
#include "server_cleanup.h"
#include "server_get.h"
#include "ServerIdentityMgr.h"
#include "os_functions.h"
#ifdef _WIN32
#include "DirectoryWatcherThread.h"
#endif
#include <stdlib.h>

PLUGIN_ID filesrv_pluginid;
IPipe *server_exit_pipe=NULL;
IFSImageFactory *image_fak;
IPychartFactory *pychart_fak;
IDownloadFactory *download_fak;
ICryptoFactory *crypto_fak;
std::string server_identity;
std::string server_token;

bool is_backup_client=true;
const unsigned short serviceport=35623;


#define ADD_ACTION(x) { IAction *na=new Actions::x;\
						Server->AddAction( na );\
						gActions.push_back(na); } 

std::vector<IAction*> gActions;

void init_mutex1(void);
void writeZeroblockdata(void);
bool testEscape(void);
void upgrade_1(void);
void do_restore(void);
void restore_wizard(void);
void upgrade(void);
bool upgrade_client(void);

bool is_server=false;

std::string lang="en";
std::string time_format_str_de="%d.%m.%Y %H:%M";
std::string time_format_str="%m/%d/%Y %H:%M";

bool copy_file(const std::wstring &src, const std::wstring &dst)
{
	IFile *fsrc=Server->openFile(src, MODE_READ);
	if(fsrc==NULL) return false;
	IFile *fdst=Server->openFile(dst, MODE_WRITE);
	if(fdst==NULL)
	{
		Server->destroy(fsrc);
		return false;
	}
	char buf[4096];
	size_t rc;
	while( (rc=(_u32)fsrc->Read(buf, 4096))>0)
	{
		fdst->Write(buf, (_u32)rc);
	}
	
	Server->destroy(fsrc);
	Server->destroy(fdst);
	return true;
}

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	/*if(!testEscape())
	{
		Server->Log("Escape test failed! Stopping.", LL_ERROR);
		return;
	}*/
	
	std::string rmtest=Server->getServerParameter("rmtest");
	if(!rmtest.empty())
	{
		os_remove_nonempty_dir(widen(rmtest));
		return;
	}

#ifndef CLIENT_ONLY
	init_mutex1();
	ServerLogger::init_mutex();
#endif

#ifdef _WIN32
	char t_lang[20];
	GetLocaleInfoA(LOCALE_SYSTEM_DEFAULT,LOCALE_SISO639LANGNAME ,t_lang,sizeof(t_lang));
	lang=t_lang;
#endif

	if(lang=="de")
	{
		time_format_str=time_format_str_de;
	}

	//writeZeroblockdata();

	if(Server->getServerParameter("restore_mode")=="true")
	{
		Server->setServerParameter("max_worker_clients", "1");
	}
	if(Server->getServerParameter("restore")=="true")
	{
		do_restore();
		exit(10);
		return;
	}
	if(Server->getServerParameter("restore_wizard")=="true")
	{
		restore_wizard();
		exit(10);
		return;
	}

	bool both=false;
	if( Server->getServerParameter("server")=="true" )
	{
		Server->Log("Starting as server...");
		is_backup_client=false;
		both=false;
	}
	else if( Server->getServerParameter("server")=="both" )
	{
		Server->Log("Starting as both...");
		both=true;
	}
	else
	{
		Server->Log("Starting as client...");
	}


#ifndef CLIENT_ONLY
	if(both || (!both && !is_backup_client))
	{
		if((server_identity=getFile("urbackup/server_ident.key")).size()<5)
		{
			Server->Log("Generating Server identity...", LL_INFO);
			std::string ident="#I";
			for(size_t i=0;i<30;++i)
			{
				ident+=getRandomChar();
			}
			ident+='#';
			writestring(ident, "urbackup/server_ident.key");
			server_identity=ident;
		}
		if((server_token=getFile("urbackup/server_token.key")).size()<5)
		{
			Server->Log("Generating Server token...", LL_INFO);
			std::string token;
			for(size_t i=0;i<30;++i)
			{
				token+=getRandomChar();
			}
			writestring(token, "urbackup/server_token.key");
			server_token=token;
		}
		is_server=true;

		Server->deleteFile("urbackup/shutdown_now");
	}
#endif

	if(both)
	{
		is_backup_client=true;
	}

	{
		str_map params;
		image_fak=(IFSImageFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("fsimageplugin", params));
		if( image_fak==NULL )
		{
			Server->Log("Error loading fsimageplugin", LL_ERROR);
		}
	}


	if(is_backup_client)
	{
		ServerIdentityMgr::init_mutex();
#ifdef _WIN32
		DirectoryWatcherThread::init_mutex();
#endif

		if(getFile("pw.txt").size()<5)
		{
			writestring(wnarrow(Server->getSessionMgr()->GenerateSessionIDWithUser(L"",L"")), "pw.txt");
		}

		if(! Server->openDatabase("urbackup/backup_client.db", URBACKUPDB_CLIENT) )
		{
			Server->Log("Couldn't open Database backup_client.db", LL_ERROR);
			return;
		}

		if(FileExists("new.txt") )
		{
			Server->Log("Upgrading...", LL_WARNING);
			Server->deleteFile("new.txt");
			if(!upgrade_client())
			{
				IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
				db->Write("DELETE FROM files");			
				db->Write("CREATE TABLE IF NOT EXISTS logdata (id INTEGER PRIMARY KEY,logid INTEGER,loglevel INTEGER,message TEXT,idx INTEGER);");
				db->Write("CREATE TABLE IF NOT EXISTS  logs ( id INTEGER PRIMARY KEY, ttime DATE DEFAULT CURRENT_TIMESTAMP);");
				db->Write("CREATE TABLE IF NOT EXISTS shadowcopies ( id INTEGER PRIMARY KEY, vssid BLOB, ssetid BLOB, target TEXT, path TEXT);");
				db->Write("CREATE TABLE IF NOT EXISTS mdirs_backup ( name TEXT );");
				db->Write("ALTER TABLE shadowcopies ADD tname TEXT;");
				db->Write("ALTER TABLE shadowcopies ADD orig_target TEXT;");
				db->Write("ALTER TABLE shadowcopies ADD filesrv INTEGER;");
				db->Write("CREATE TABLE IF NOT EXISTS journal_ids ( id INTEGER PRIMARY KEY, device_name TEXT, journal_id INTEGER, last_record INTEGER);");
				db->Write("ALTER TABLE journal_ids ADD index_done INTEGER;");
				db->Write("UPDATE journal_ids SET index_done=0 WHERE index_done IS NULL");
				db->Write("CREATE TABLE IF NOT EXISTS map_frn ( id INTEGER PRIMARY KEY, name TEXT, pid INTEGER, frn INTEGER, rid INTEGER)");
				db->Write("CREATE INDEX IF NOT EXISTS frn_index ON map_frn( frn ASC )");
				db->Write("CREATE INDEX IF NOT EXISTS frn_pid_index ON map_frn( pid ASC )");
				db->Write("CREATE TABLE IF NOT EXISTS journal_data ( id INTEGER PRIMARY KEY, device_name TEXT, journal_id INTEGER, usn INTEGER, reason INTEGER, filename TEXT, frn INTEGER, parent_frn INTEGER, next_usn INTEGER)");
				db->Write("DELETE FROM journal_ids");
				db->Write("DELETE FROM journal_data");
				db->Write("DELETE FROM map_frn");
				db->Write("CREATE INDEX IF NOT EXISTS logdata_index ON logdata( logid ASC )");
				db->Write("ALTER TABLE logdata ADD ltime DATE;");
				db->Write("CREATE TABLE IF NOT EXISTS del_dirs ( name TEXT );");
				db->Write("CREATE TABLE IF NOT EXISTS del_dirs_backup ( name TEXT );");
				db->Write("ALTER TABLE journal_data ADD attributes INTEGER;");
				db->Write("ALTER TABLE backupdirs ADD server_default INTEGER;");
				db->Write("UPDATE backupdirs SET server_default=0 WHERE server_default IS NULL");
				db->Write("CREATE TABLE IF NOT EXISTS misc (tkey TEXT, tvalue TEXT);");
				db->Write("INSERT INTO misc (tkey, tvalue) VALUES ('db_version', '1');");
				upgrade_client();
			}
		}
	}

#ifndef CLIENT_ONLY
	if(both || (!both && !is_backup_client) )
	{
		if( !FileExists("urbackup/backup_server.db") && FileExists("urbackup/backup_server.db.template") )
		{
			//Copy file
			copy_file(L"urbackup/backup_server.db.template", L"urbackup/backup_server.db");
		}
		if(! Server->openDatabase("urbackup/backup_server.db", URBACKUPDB_SERVER) )
		{
			Server->Log("Couldn't open Database backup_server.db", LL_ERROR);
			return;
		}
		else
		{
			IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
			db->Write("PRAGMA journal_mode=WAL");
		}

		ServerStatus::init_mutex();
		ServerSettings::init_mutex();
		BackupServerGet::init_mutex();

		ADD_ACTION(server_status);
		ADD_ACTION(progress);
		ADD_ACTION(login);
		ADD_ACTION(salt);
		ADD_ACTION(generate_templ);
		ADD_ACTION(lastacts);
		ADD_ACTION(piegraph);
		ADD_ACTION(usagegraph);
		ADD_ACTION(usage);
		ADD_ACTION(users);
		ADD_ACTION(status);
		ADD_ACTION(backups);
		ADD_ACTION(settings);
		ADD_ACTION(logs);
		ADD_ACTION(isimageready);
		ADD_ACTION(getimage);
		ADD_ACTION(google_chart);
		
		upgrade();
	}
#endif

	Server->Log("Started UrBackup...", LL_INFO);

	if(is_backup_client)
	{
		ClientConnector::init_mutex();
		Server->StartCustomStreamService(new ClientService(), "urbackupserver", serviceport);

		str_map params;
		filesrv_pluginid=Server->StartPlugin("fileserv", params);

		crypto_fak=(ICryptoFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("cryptoplugin", params));
		if( crypto_fak==NULL )
		{
			Server->Log("Error loading Cryptoplugin", LL_ERROR);
		}

		IndexThread *it=new IndexThread();
		Server->createThread(it);

		Server->wait(1000);
	}
#ifndef CLIENT_ONLY
	if(both || (!both && !is_backup_client))
	{
		str_map params;
		pychart_fak=(IPychartFactory*)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("pychart", params));
		if(pychart_fak==NULL)
		{
			Server->Log("Error loading IPychartFactory", LL_ERROR);
		}
		download_fak=(IDownloadFactory*)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("download", params));
		if(download_fak==NULL)
		{
			Server->Log("Error loading IDownloadFactory", LL_ERROR);
		}


		server_exit_pipe=Server->createMemoryPipe();
		BackupServer *backup_server=new BackupServer(server_exit_pipe);
		Server->createThread(backup_server);
		Server->wait(500);

		ServerCleanupThread::initMutex();
		ServerCleanupThread *server_cleanup=new ServerCleanupThread();
		Server->createThread(server_cleanup);
	}
#endif
}

DLLEXPORT void UnloadActions(void)
{
	if(server_exit_pipe!=NULL)
	{
		std::string msg="exit";
		while(msg!="ok")
		{
			server_exit_pipe->Write(msg);
			Server->wait(100);
			server_exit_pipe->Read(&msg);
		}

		Server->destroy(server_exit_pipe);
	}
	
#ifndef CLIENT_ONLY
	ServerLogger::destroy_mutex();
	if(is_server)
	{		
		BackupServerGet::destroy_mutex();

		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db->Write("PRAGMA wal_checkpoint");
		Server->destroyAllDatabases();
		db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db->Write("PRAGMA journal_mode=DELETE");
	}
#endif
}

#ifndef CLIENT_ONLY

void update_file(IQuery *q_space_get, IQuery* q_space_update, IQuery *q_file_update, db_results &curr_r)
{
	_i64 filesize=os_atoi64(wnarrow(curr_r[0][L"filesize"]));

	std::map<int, int> client_c;
	for(size_t i=0;i<curr_r.size();++i)
	{
		int cid=watoi(curr_r[i][L"clientid"]);
		std::map<int, int>::iterator it=client_c.find(cid);
		if(it==client_c.end())
		{
			client_c.insert(std::pair<int, int>(cid, 1));
		}
		else
		{
			++it->second;
		}

		if(i==0)
		{
			q_file_update->Bind(filesize);
			q_file_update->Bind(os_atoi64(wnarrow(curr_r[i][L"id"])));
			q_file_update->Write();
			q_file_update->Reset();
		}
		else
		{
			q_file_update->Bind(0);
			q_file_update->Bind(os_atoi64(wnarrow(curr_r[i][L"id"])));
			q_file_update->Write();
			q_file_update->Reset();
		}
	}


	for(std::map<int, int>::iterator it=client_c.begin();it!=client_c.end();++it)
	{
		q_space_get->Bind(it->first);
		db_results res=q_space_get->Read();
		q_space_get->Reset();
		if(!res.empty())
		{
			_i64 used=os_atoi64(wnarrow(res[0][L"bytes_used_files"]));
			used+=filesize/client_c.size();
			q_space_update->Bind(used);
			q_space_update->Bind(it->first);
			q_space_update->Write();
			q_space_update->Reset();
		}
	}
}

void upgrade_1(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE files ADD rsize INTEGER");
	db->Write("ALTER TABLE files ADD did_count INTEGER");
	db->Write("ALTER TABLE clients ADD bytes_used_files INTEGER");
	db->Write("ALTER TABLE clients ADD bytes_used_images INTEGER");
	db->Write("UPDATE clients SET bytes_used_files=0 WHERE bytes_used_files IS NULL");
	db->Write("UPDATE clients SET bytes_used_images=0 WHERE bytes_used_images IS NULL");
	db->Write("UPDATE files SET did_count=1 WHERE did_count IS NULL");

	IQuery *q_read=db->Prepare("SELECT files.rowid AS id, shahash, filesize, clientid FROM (files INNER JOIN backups ON files.backupid=backups.id) WHERE rsize IS NULL ORDER BY shahash DESC LIMIT 10000");
	IQuery *q_space_get=db->Prepare("SELECT bytes_used_files FROM clients WHERE id=?");
	IQuery *q_space_update=db->Prepare("UPDATE clients SET bytes_used_files=? WHERE id=?");
	IQuery *q_file_update=db->Prepare("UPDATE files SET rsize=? WHERE rowid=?");

	std::wstring filesize;
	std::wstring shhash;
	db_results curr_r;
	int last_pc=0;
	Server->Log("Updating client space usage...", LL_INFO);
	db_results res;
	do
	{
		res=q_read->Read();	
		q_read->Reset();
		for(size_t j=0;j<res.size();++j)
		{
			if(shhash.empty() || (res[j][L"shahash"]!=shhash || res[j][L"filesize"]!=filesize )  )
			{
				if(!curr_r.empty())
				{
					update_file(q_space_get, q_space_update, q_file_update, curr_r);
				}
				curr_r.clear();
				shhash=res[j][L"shhash"];
				filesize=res[j][L"filesize"];
				curr_r.push_back(res[j]);
			}

			int pc=(int)(((float)j/(float)res.size())*100.f+0.5f);
			if(pc!=last_pc)
			{
				Server->Log(nconvert(pc)+"%", LL_INFO);
				last_pc=pc;
			}
		}
	}
	while(!res.empty());

	if(!curr_r.empty())
	{
		update_file(q_space_get, q_space_update, q_file_update, curr_r);
	}

	db->destroyAllQueries();
}

void upgrade1_2(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE logs ADD errors INTEGER");
	db->Write("ALTER TABLE logs ADD warnings INTEGER");
	db->Write("ALTER TABLE logs ADD infos INTEGER");
	db->Write("ALTER TABLE logs ADD image INTEGER");
	db->Write("ALTER TABLE logs ADD incremental INTEGER");
}

void upgrade2_3(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE INDEX IF NOT EXISTS clients_hist_created_idx ON clients_hist (created)");
}

void upgrade3_4(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE INDEX IF NOT EXISTS logs_created_idx ON logs (created)");
}

void upgrade4_5(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE extra_clients ( id INTEGER PRIMARY KEY, hostname TEXT, lastip INTEGER)");	
}

void upgrade5_6(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE files_del ADD is_del INTEGER");
	db->Write("UPDATE files_del SET is_del=1 WHERE is_del IS NULL");
}

void upgrade6_7(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE backup_images ADD version INTEGER");
	db->Write("UPDATE backup_images SET version=0 WHERE version IS NULL");
}

void upgrade7_8(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE clients ADD delete_pending INTEGER");
	db->Write("UPDATE clients SET delete_pending=0 WHERE delete_pending IS NULL");
}

void upgrade8_9(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE backup_images ADD letter TEXT");
	db->Write("UPDATE backup_images SET letter='C:' WHERE letter IS NULL");
	db->Write("CREATE TABLE assoc_images ( img_id INTEGER REFERENCES backup_images(id) ON DELETE CASCADE, assoc_id INTEGER REFERENCES backup_images(id) ON DELETE CASCADE)");
}

void upgrade(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db_results res_v=db->Read("SELECT tvalue FROM misc WHERE tkey='db_version'");
	if(res_v.empty())
		return;
	
	int ver=watoi(res_v[0][L"tvalue"]);
	int old_v;
	int max_v=7;
	bool do_upgrade=false;
	if(ver<max_v)
	{
		do_upgrade=true;
		Server->Log("Upgrading...", LL_WARNING);
	}
	
	IQuery *q_update=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='db_version'");
	do
	{
		old_v=ver;
		switch(ver)
		{
			case 1:
				upgrade1_2();
				++ver;
				break;
			case 2:
				upgrade2_3();
				++ver;
				break;
			case 3:
				upgrade3_4();
				++ver;
				break;
			case 4:
				upgrade4_5();
				++ver;
				break;
			case 5:
				upgrade5_6();
				++ver;
				break;
			case 6:
				upgrade6_7();
				++ver;
				break;
			case 7:
				upgrade7_8();
				++ver;
				break;
			case 8:
				upgrade8_9();
				++ver;
				break;
			default:
				break;
		}
		
		if(ver!=old_v)
		{
			q_update->Bind(ver);
			q_update->Write();
			q_update->Reset();
		}
	}
	while(old_v<ver);
	
	if(do_upgrade)
	{
		Server->Log("Done.", LL_WARNING);
	}
	
	db->destroyAllQueries();
}

#endif //CLIENT_ONLY

void upgrade_client1_2(IDatabase *db)
{
	db->Write("ALTER TABLE shadowcopies ADD vol TEXT");
}

void upgrade_client2_3(IDatabase *db)
{
	db->Write("ALTER TABLE shadowcopies ADD refs INTEGER");
	db->Write("ALTER TABLE shadowcopies ADD starttime DATE");
}

bool upgrade_client(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT tvalue FROM misc WHERE tkey='db_version'");
	if(q==NULL)
		return false;
	db_results res_v=q->Read();
	if(res_v.empty())
		return false;
	
	int ver=watoi(res_v[0][L"tvalue"]);
	int old_v;
	
	IQuery *q_update=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='db_version'");
	do
	{
		old_v=ver;
		switch(ver)
		{
			case 1:
				upgrade_client1_2(db);
				++ver;
				break;
			case 2:
				upgrade_client2_3(db);
				++ver;
				break;
			default:
				break;
		}
		
		if(ver!=old_v)
		{
			q_update->Bind(ver);
			q_update->Write();
			q_update->Reset();
		}
	}
	while(old_v<ver);
	
	db->destroyAllQueries();
	return true;
}