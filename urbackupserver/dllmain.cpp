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

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif

#include <vector>

#define DEF_SERVER
#include "../Interface/Server.h"
IServer *Server;

#include "../Interface/Action.h"
#include "../Interface/Database.h"
#include "../Interface/SessionMgr.h"
#include "../Interface/Pipe.h"
#include "../Interface/Query.h"

#include "../Interface/Thread.h"
#include "../Interface/File.h"

#include "../fsimageplugin/IFSImageFactory.h"
#include "../cryptoplugin/ICryptoFactory.h"
#include "../urlplugin/IUrlFactory.h"

#include "database.h"
#include "actions.h"
#include "serverinterface/actions.h"
#include "serverinterface/helper.h"
SStartupStatus startup_status;
#include "server.h"


#include "../stringtools.h"
#include "server_status.h"
#include "server_log.h"
#include "server_cleanup.h"
#include "server_get.h"
#include "server_archive.h"
#include "server_settings.h"
#include "server_update_stats.h"
#include "../urbackupcommon/os_functions.h"
#include "InternetServiceConnector.h"
#include "filedownload.h"
#include "apps/cleanup_cmd.h"
#include "apps/repair_cmd.h"
#include "apps/export_auth_log.h"
#include "create_files_index.h"
#include "server_dir_links.h"

#include <stdlib.h>
#include "../Interface/DatabaseCursor.h"
#include <set>
#include "apps/check_files_index.h"

IPipe *server_exit_pipe=NULL;
IFSImageFactory *image_fak;
ICryptoFactory *crypto_fak;
IUrlFactory *url_fak=NULL;

std::string server_identity;
std::string server_token;

const unsigned short serviceport=35623;


#define ADD_ACTION(x) { IAction *na=new Actions::x;\
						Server->AddAction( na );\
						gActions.push_back(na); } 

std::vector<IAction*> gActions;

void init_mutex1(void);
void destroy_mutex1(void);
void writeZeroblockdata(void);
bool testEscape(void);
void upgrade(void);
bool test_amatch(void);
bool test_amatch(void);
bool verify_hashes(std::string arg);
void updateRights(int t_userid, std::string s_rights, IDatabase *db);
void open_settings_database_full(bool use_berkeleydb);

std::string lang="en";
std::string time_format_str="%Y-%m-%d %H:%M";
std::string time_format_str_de=time_format_str;

THREADPOOL_TICKET tt_cleanup_thread;
THREADPOOL_TICKET tt_automatic_archive_thread;
bool is_leak_check=false;

#ifdef _WIN32
const std::string new_file="new.txt";
#else
const std::string new_file="urbackup/new.txt";
#endif

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

void open_server_database(bool &use_berkeleydb, bool init_db)
{
	std::string bdb_config="mutex_set_max 1000000\r\nset_tx_max 500000\r\nset_lg_regionmax 10485760\r\nset_lg_bsize 4194304\r\nset_lg_max 20971520\r\nset_lk_max_locks 100000\r\nset_lk_max_lockers 10000\r\nset_lk_max_objects 100000\r\nset_cachesize 0 104857600 1";
	use_berkeleydb=false;

	if( !FileExists("urbackup/backup_server.bdb") && !FileExists("urbackup/backup_server.db") && FileExists("urbackup/backup_server.db.template") )
	{
		if(init_db)
		{
			copy_file(L"urbackup/backup_server.db.template", L"urbackup/backup_server.db");
		}
	}
		
	if( !FileExists("urbackup/backup_server.db") && !FileExists("urbackup/backup_server.bdb") && FileExists("urbackup/backup_server_init.sql") )
	{
		bool init=false;
		std::string engine="sqlite";
		std::string db_fn="urbackup/backup_server.db";
		if(Server->hasDatabaseFactory("bdb") )
		{
			os_create_dir(L"urbackup/backup_server.bdb-journal");
			writestring(bdb_config, "urbackup/backup_server.bdb-journal/DB_CONFIG");
			engine="bdb";
			db_fn="urbackup/backup_server.bdb";
			use_berkeleydb=true;
		}
			
		if(! Server->openDatabase(db_fn, URBACKUPDB_SERVER, engine) )
		{
			Server->Log("Couldn't open Database "+db_fn+". Exiting.", LL_ERROR);
			exit(1);
		}

		if(init_db)
		{
			IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
			db->Import("urbackup/backup_server_init.sql");
		}
	}
	else
	{			
		if(Server->hasDatabaseFactory("bdb") )
		{
			use_berkeleydb=true;

			Server->Log("Warning: Switching to Berkley DB", LL_WARNING);
			if(! Server->openDatabase("urbackup/backup_server.db", URBACKUPDB_SERVER_TMP) )
			{
				Server->Log("Couldn't open Database backup_server.db. Exiting.", LL_ERROR);
				exit(1);
			}

			IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_TMP);
			Server->deleteFile("urbackup/backup_server.dat");
			if(db->Dump("urbackup/backup_server.dat"))
			{
				Server->destroyAllDatabases();

				os_create_dir(L"urbackup/backup_server.bdb-journal");
				writestring(bdb_config, "urbackup/backup_server.bdb-journal/DB_CONFIG");

				if(! Server->openDatabase("urbackup/backup_server.bdb", URBACKUPDB_SERVER, "bdb") )
				{
					Server->Log("Couldn't open Database backup_server.bdb. Exiting.", LL_ERROR);
					exit(1);
				}
				db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
				if(db->Import("urbackup/backup_server.dat") )
				{
					Server->deleteFile("urbackup/backup_server.dat");
					rename("urbackup/backup_server.db", "urbackup/backup_server_old_sqlite.db");
				}
				else
				{
					Server->Log("Importing data into new BerkleyDB database failed. Exiting.", LL_ERROR);
					exit(1);
				}
			}
			else
			{
				Server->Log("Dumping Database failed. Exiting", LL_ERROR);
				exit(1);
			}
		}
		else
		{
			if(! Server->openDatabase("urbackup/backup_server.db", URBACKUPDB_SERVER) )
			{
				Server->Log("Couldn't open Database backup_server.db. Exiting.", LL_ERROR);
				exit(1);
			}
		}
	}

	if(!Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER))
	{
		Server->Log(L"Couldn't open backup server database. Exiting. Expecting database at \""+
			Server->getServerWorkingDir()+os_file_sep()+L"urbackup"+os_file_sep()+L"backup_server.db\"", LL_ERROR);
		exit(1);
	}
	else
	{
		Server->destroyDatabases(Server->getThreadID());
	}
}

void open_settings_database(bool use_berkeleydb)
{
	std::string aname="urbackup/backup_server_settings.db";
	if(use_berkeleydb)
		aname="urbackup/backup_server_settings.bdb";

	Server->attachToDatabase(aname, "settings_db", URBACKUPDB_SERVER);
}

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	/*if(!testEscape())
	{
		Server->Log("Escape test failed! Stopping.", LL_ERROR);
		return;
	}*/
	/*if(!test_amatch())
	{
		Server->Log("Amatch test failed! Stopping.", LL_ERROR);
		return;
	}*/
	/*if(!test_amatch())
	{
		Server->Log("Amatch test failed! Stopping.", LL_ERROR);
		return;
	}*/
	
	std::string rmtest=Server->getServerParameter("rmtest");
	if(!rmtest.empty())
	{
		os_remove_nonempty_dir(widen(rmtest));
		return;
	}

	std::string download_file=Server->getServerParameter("download_file");
	if(!download_file.empty())
	{
		FileDownload dl;
		unsigned int tcpport=43001;
		std::string s_tcpport=Server->getServerParameter("tcpport");
		if(!s_tcpport.empty()) tcpport=atoi(s_tcpport.c_str());
		int method=0;
		std::string s_method=Server->getServerParameter("method");
		if(!s_method.empty()) method=atoi(s_method.c_str());
		Server->Log("Starting file download...");
		dl.filedownload(download_file, Server->getServerParameter("servername"), Server->getServerParameter("dstfn"), tcpport, method);
		exit(1);
	}

	init_mutex1();
	ServerLogger::init_mutex();
	init_dir_link_mutex();

	std::string app=Server->getServerParameter("app", "");

	if(!app.empty())
	{
		int rc=0;

		if(app=="cleanup")
		{
			rc=cleanup_cmd();
		}
		else if(app=="remove_unknown")
		{
			rc=remove_unknown();
		}
		else if(app=="cleanup_database")
		{
			rc=cleanup_database();
		}
		else if(app=="repair_database")
		{
			rc=repair_cmd();
		}
		else if(app=="defrag_database")
		{
			rc=defrag_database();
		}
		else if(app=="export_auth_log")
		{
			rc=export_auth_log();
		}
		else if(app=="check_fileindex")
		{
			rc=check_files_index();
		}
		else
		{
			rc=100;
			Server->Log("App not found. Available apps: cleanup, remove_unknown, cleanup_database, repair_database, defrag_database, export_auth_log, check_fileindex");
		}
		exit(rc);
	}

#ifdef _WIN32
	char t_lang[20];
	GetLocaleInfoA(LOCALE_SYSTEM_DEFAULT,LOCALE_SISO639LANGNAME ,t_lang,sizeof(t_lang));
	lang=t_lang;
#endif

	if(lang=="de")
	{
		time_format_str=time_format_str_de;
	}

	{
		str_map params;
		crypto_fak=(ICryptoFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("cryptoplugin", params));
		if( crypto_fak==NULL )
		{
			Server->Log("Error loading Cryptoplugin. Internet service will not work.", LL_ERROR);
		}
	}

	//writeZeroblockdata();

	
	if((server_identity=getFile("urbackup/server_ident.key")).size()<5)
	{
		Server->Log("Generating Server identity...", LL_INFO);
		std::string ident="#I"+ServerSettings::generateRandomAuthKey(20)+"#";
		writestring(ident, "urbackup/server_ident.key");
		server_identity=ident;
	}
	if(!FileExists("urbackup/server_ident.pub") && crypto_fak!=NULL)
	{
		Server->Log("Generating Server private/public key...", LL_INFO);
		crypto_fak->generatePrivatePublicKeyPair("urbackup/server_ident");
	}
	if((server_token=getFile("urbackup/server_token.key")).size()<5)
	{
		Server->Log("Generating Server token...", LL_INFO);
		std::string token=ServerSettings::generateRandomAuthKey(20);
		writestring(token, "urbackup/server_token.key");
		server_token=token;
	}

	Server->deleteFile("urbackup/shutdown_now");


	{
		str_map params;
		image_fak=(IFSImageFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("fsimageplugin", params));
		if( image_fak==NULL )
		{
			Server->Log("Error loading fsimageplugin", LL_ERROR);
		}
	}

	
	bool use_berkeleydb;
	open_server_database(use_berkeleydb, true);

	std::string arg_verify_hashes=Server->getServerParameter("verify_hashes");
	if(!arg_verify_hashes.empty())
	{
		if(!verify_hashes(arg_verify_hashes))
		{
			Server->Log("Backup verification failed! See verification_result.txt for more info.", LL_ERROR);
			exit(1);
		}
		else
		{
			Server->Log("Backup verification successfull.", LL_INFO);
			Server->deleteFile("verification_result.txt");
			exit(0);
		}
	}

	ServerStatus::init_mutex();
	ServerSettings::init_mutex();
	BackupServerGet::init_mutex();

	open_settings_database(use_berkeleydb);
	open_settings_database_full(use_berkeleydb);

	Server->destroyAllDatabases();

	startup_status.mutex=Server->createMutex();
	{
		IScopedLock lock(startup_status.mutex);
		startup_status.upgrading_database=true;
	}

	ADD_ACTION(login);
		
	upgrade();

	if(!use_berkeleydb)
	{
		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db->Write("PRAGMA journal_mode=WAL");
	}

	if(!Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER))
	{
		Server->Log(L"Couldn't open backup server settings database. Exiting. Expecting database at \""+
			Server->getServerWorkingDir()+os_file_sep()+L"urbackup"+os_file_sep()+L"backup_server_settings.db\"", LL_ERROR);
		exit(1);
	}
		
	if( FileExists("urbackup/backupfolder") )
	{
		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db_results res=db->Read("SELECT value FROM settings_db.settings WHERE key='backupfolder' AND clientid=0");
		if(res.empty())
		{
			IQuery *q=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('backupfolder', ?, 0)", false);
			std::string bf=getFile("urbackup/backupfolder");
			if(linecount(bf)>0)
				bf=getline(0, bf);
			q->Bind(trim(bf));
			q->Write();
			db->destroyQuery(q);
		}
	}

	if(!create_files_index(startup_status))
	{
		Server->Log(L"Could not create or open file entry index. Exiting.", LL_ERROR);
		exit(1);
	}

	{
		IScopedLock lock(startup_status.mutex);
		startup_status.upgrading_database=false;
	}

	std::string set_admin_pw=Server->getServerParameter("set_admin_pw");
	if(!set_admin_pw.empty())
	{
		std::string rnd=ServerSettings::generateRandomAuthKey(20);

		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db_results res=db->Read("SELECT password_md5 FROM settings_db.si_users WHERE name='admin'");
		if(res.empty())
		{
			IQuery *q=db->Prepare("INSERT INTO settings_db.si_users (name, password_md5, salt) VALUES (?,?,?)");
			q->Bind("admin");
			q->Bind(Server->GenerateHexMD5(rnd+set_admin_pw));
			q->Bind(rnd);
			q->Write();
			q->Reset();
		}
		else
		{
			IQuery *q=db->Prepare("UPDATE si_users SET password_md5=?, salt=? WHERE name='admin'");
			q->Bind(Server->GenerateHexMD5(rnd+set_admin_pw));
			q->Bind(rnd);
			q->Write();
			q->Reset();
		}

		Server->Log("Changed admin password.", LL_INFO);

		{
			db_results res=db->Read("SELECT id FROM si_users WHERE name='admin'");
			if(!res.empty())
			{
				updateRights(watoi(res[0][L"id"]), "idx=0&0_domain=all&0_right=all", db);

				Server->Log("Updated admin rights.", LL_INFO);
			}
		}

		db->destroyAllQueries();

		exit(1);
	}
		

	ADD_ACTION(server_status);
	ADD_ACTION(progress);
	ADD_ACTION(salt);
	ADD_ACTION(lastacts);
	ADD_ACTION(piegraph);
	ADD_ACTION(usagegraph);
	ADD_ACTION(usage);
	ADD_ACTION(users);
	ADD_ACTION(status);
	ADD_ACTION(backups);
	ADD_ACTION(settings);
	ADD_ACTION(logs);
	ADD_ACTION(getimage);
	ADD_ACTION(download_client);
	ADD_ACTION(livelog);
	ADD_ACTION(start_backup);

	if(Server->getServerParameter("allow_shutdown")=="true")
	{
		ADD_ACTION(shutdown);
	}

	{
		ServerBackupDao backup_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER));
		replay_directory_link_journal(backup_dao);
	}

	Server->Log("Started UrBackup...", LL_INFO);

	
	str_map params;
	url_fak=(IUrlFactory*)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("url", params));
	if(url_fak==NULL)
	{
		Server->Log("Error loading IUrlFactory", LL_INFO);
	}

	server_exit_pipe=Server->createMemoryPipe();
	BackupServer *backup_server=new BackupServer(server_exit_pipe);
	Server->createThread(backup_server);
	Server->wait(500);

	InternetServiceConnector::init_mutex();

	{
		ServerSettings settings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER));
		if(settings.getSettings()->internet_mode_enabled)
		{
			std::string tmp=Server->getServerParameter("internet_port", "");
			unsigned int port;
			if(!tmp.empty())
			{
				port=atoi(tmp.c_str());
			}
			else
			{
				port=settings.getSettings()->internet_server_port;
			}
			Server->StartCustomStreamService(new InternetService, "InternetService", port);
		}
	}

	ServerCleanupThread::initMutex();
	ServerAutomaticArchive::initMutex();
	ServerCleanupThread *server_cleanup=new ServerCleanupThread(CleanupAction());

	is_leak_check=(Server->getServerParameter("leak_check")=="true");

	if(is_leak_check)
	{
		tt_cleanup_thread=Server->getThreadPool()->execute(server_cleanup);
		tt_automatic_archive_thread=Server->getThreadPool()->execute(new ServerAutomaticArchive);
	}
	else
	{
		Server->createThread(server_cleanup);
		Server->createThread(new ServerAutomaticArchive);
	}

	Server->setLogCircularBufferSize(20);

	Server->Log("UrBackup Server start up complete.", LL_INFO);
}

DLLEXPORT void UnloadActions(void)
{
	unsigned int wtime=500;
	if(is_leak_check)
		wtime=10000;

	bool shutdown_ok=false;
	if(server_exit_pipe!=NULL)
	{
		std::string msg="exit";
		int64 starttime=Server->getTimeMS();
		while(msg!="ok" && Server->getTimeMS()-starttime<wtime)
		{
			server_exit_pipe->Write("exit");
			Server->wait(100);
			server_exit_pipe->Read(&msg, 0);
		}

		if(msg=="ok")
		{
			Server->destroy(server_exit_pipe);
			BackupServer::cleanupThrottlers();
			shutdown_ok=true;
		}
	}
	
	ServerLogger::destroy_mutex();

	if(is_leak_check)
	{
		std::vector<THREADPOOL_TICKET> tickets;
		tickets.push_back(tt_automatic_archive_thread);
		tickets.push_back(tt_cleanup_thread);

		ServerCleanupThread::doQuit();
		ServerAutomaticArchive::doQuit();

		Server->getThreadPool()->waitFor(tickets);

		ServerCleanupThread::destroyMutex();
		ServerAutomaticArchive::destroyMutex();

		if(!shutdown_ok)
		{
			Server->Log("Could not shut down server. Leaks expected.", LL_ERROR);
		}

		InternetServiceConnector::destroy_mutex();
		destroy_mutex1();
		Server->destroy(startup_status.mutex);
		Server->Log("Deleting cached server settings...", LL_INFO);
		ServerSettings::clear_cache();
		ServerSettings::destroy_mutex();
		ServerStatus::destroy_mutex();
		destroy_dir_link_mutex();
		Server->wait(1000);
	}

	if(shutdown_ok)
	{
		BackupServerGet::destroy_mutex();
	}

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("PRAGMA wal_checkpoint");
	if(!shutdown_ok)
		db->BeginTransaction();
	else
		Server->destroyAllDatabases();
}

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

void upgrade9_10(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE si_users ADD report_mail TEXT");
	db->Write("ALTER TABLE si_users ADD report_loglevel INTEGER");
	db->Write("ALTER TABLE si_users ADD report_sendonly INTEGER");
}

void upgrade10_11(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE files ADD clientid INTEGER");
	db->Write("UPDATE files SET clientid=(SELECT clientid FROM backups WHERE backups.id=backupid)");
}

void upgrade11_12(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("DROP INDEX files_idx");
	db->Write("CREATE INDEX files_idx ON files (shahash, filesize, clientid)");
	db->Write("CREATE INDEX files_did_count ON files (did_count)");
}

void upgrade12_13(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE files ADD incremental INTEGER");
	db->Write("UPDATE files SET incremental=(SELECT incremental FROM backups WHERE backups.id=backupid)");
}

void upgrade13_14(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE INDEX files_backupid ON files (backupid)");
}

void upgrade14_15(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE settings_db.settings ("
			  "key TEXT,"
			  "value TEXT , clientid INTEGER);");
	db->Write("CREATE TABLE settings_db.si_users"
				"("
				"id INTEGER PRIMARY KEY,"
				"name TEXT,"
				"password_md5 TEXT,"
				"salt TEXT,"
				"report_mail TEXT,"
				"report_loglevel INTEGER,"
				"report_sendonly INTEGER"
				");");
	db->Write("CREATE TABLE settings_db.si_permissions"
				"("
				"clientid INTEGER REFERENCES si_users(id) ON DELETE CASCADE,"
				"t_right TEXT,"
				"t_domain TEXT"
				");");
	db->Write("INSERT INTO settings_db.settings SELECT * FROM settings");
	db->Write("INSERT INTO settings_db.si_users SELECT * FROM si_users");
	db->Write("INSERT INTO settings_db.si_permissions SELECT * FROM si_permissions");
	db->Write("DROP TABLE settings");
	db->Write("DROP TABLE si_users");
	db->Write("DROP TABLE si_permissions");
}

void upgrade15_16(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE settings_db.extra_clients ( id INTEGER PRIMARY KEY, hostname TEXT, lastip INTEGER)");
	db->Write("INSERT INTO settings_db.extra_clients SELECT * FROM extra_clients");
	db->Write("DROP TABLE extra_clients");
}

void upgrade16_17(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db_results res=db->Read("SELECT id FROM clients");
	IQuery *q=db->Prepare("INSERT INTO settings_db.settings (key,value, clientid) VALUES ('internet_authkey',?,?)", false);
	for(size_t i=0;i<res.size();++i)
	{
		std::string key=ServerSettings::generateRandomAuthKey();
		q->Bind(key);
		q->Bind(res[i][L"id"]);
		q->Write();
		q->Reset();
	}
	db->destroyQuery(q);
}

void upgrade17_18(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE files ADD hashpath TEXT");
	db->Write("ALTER TABLE files_del ADD hashpath TEXT");
}

void upgrade18_19(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE backups ADD archived INTEGER");
	db->Write("UPDATE backups SET archived=0 WHERE archived IS NULL");
}

void upgrade19_20(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE settings_db.automatic_archival ( id INTEGER PRIMARY KEY, next_archival INTEGER, interval INTEGER, interval_unit TEXT, length INTEGER, length_unit TEXT, backup_types INTEGER, clientid INTEGER)");
	db->Write("ALTER TABLE backups ADD archive_timeout INTEGER");
}

void upgrade20_21(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE settings_db.automatic_archival ADD archive_window TEXT");
	db->Write("UPDATE settings_db.automatic_archival SET archive_window='*;*;*;*' WHERE archive_window IS NULL");
}

void upgrade21_22(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE INDEX files_del_idx ON files_del (shahash, filesize, clientid)");
}

void upgrade22_23(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("INSERT INTO misc (tkey, tvalue) VALUES ('files_cache', 'none')");
	db->Write("CREATE TABLE files_new ( backupid INTEGER, fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, created DATE DEFAULT CURRENT_TIMESTAMP, rsize INTEGER, clientid INTEGER, incremental INTEGER)");
}

void upgrade23_24(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db_results res=db->Read("SELECT value, clientid FROM settings_db.settings WHERE key='allow_starting_file_backups'");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?, ?, ?)");
	for(size_t i=0;i<res.size();++i)
	{
		q_insert->Bind("allow_starting_incr_file_backups");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("allow_starting_full_file_backups");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();
	}

	res=db->Read("SELECT value, clientid FROM settings_db.settings WHERE key='allow_starting_image_backups'");
	q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?, ?, ?)");
	for(size_t i=0;i<res.size();++i)
	{
		q_insert->Bind("allow_starting_incr_image_backups");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("allow_starting_full_image_backups");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();
	}
}

void upgrade24_25(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE backups ADD size_calculated INTEGER");
	db->Write("UPDATE backups SET size_calculated=0 WHERE size_calculated IS NULL");
}

void upgrade25_26(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db_results res=db->Read("SELECT clientid, t_right FROM settings_db.si_permissions WHERE t_domain='settings'");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.si_permissions (t_domain, t_right, clientid) VALUES ('client_settings', ?, ?)");
	for(size_t i=0;i<res.size();++i)
	{
		q_insert->Bind(res[i][L"t_right"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();
	}
}

void upgrade26_27(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db_results res=db->Read("SELECT value, clientid FROM settings_db.settings WHERE key='backup_window'");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?, ?, ?)");
	for(size_t i=0;i<res.size();++i)
	{
		q_insert->Bind("backup_window_incr_file");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("backup_window_full_file");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("backup_window_incr_image");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("backup_window_full_image");
		q_insert->Bind(res[i][L"value"]);
		q_insert->Bind(res[i][L"clientid"]);
		q_insert->Write();
		q_insert->Reset();
	}
}

void upgrade27_28()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE INDEX settings_db.settings_idx ON settings (key, clientid)");
	db->Write("CREATE INDEX settings_db.si_users_idx ON si_users (name)");
	db->Write("CREATE INDEX settings_db.si_permissions_idx ON si_permissions (clientid, t_domain)");
}

void upgrade28_29()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE directory_links ("
		"id INTEGER PRIMARY KEY,"
		"clientid INTGER,"
		"name TEXT,"
		"target TEXT)");
	db->Write("CREATE INDEX directory_links_idx ON directory_links (clientid, name)");
	db->Write("CREATE INDEX directory_links_target_idx ON directory_links (clientid, target)");
	db->Write("CREATE TABLE directory_link_journal ("
		"id INTEGER PRIMARY KEY,"
		"linkname TEXT,"
		"linktarget TEXT)");
}

void upgrade29_30()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE settings_db.login_access_log ("
		"id INTEGER PRIMARY KEY,"
		"logintime DATE DEFAULT CURRENT_TIMESTAMP,"
		"username TEXT,"
		"ip TEXT,"
		"method INTEGER)");
}

void upgrade30_31()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE settings_db.old_backupfolders ("
		"id INTEGER PRIMARY KEY,"
		"backupfolder TEXT UNIQUE)");
}

void upgrade31_32()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE backups ADD resumed INTEGER");
	db->Write("UPDATE backups SET resumed=0 WHERE resumed IS NULL");
	db->Write("ALTER TABLE logs ADD resumed INTEGER");
	db->Write("UPDATE logs SET resumed=0 WHERE resumed IS NULL");
}

void upgrade32_33()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE TABLE orig_client_settings ("
		"id INTEGER PRIMARY KEY,"
		"clientid INTEGER UNIQUE,"
		"data TEXT )");
}

void upgrade33_34()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("ALTER TABLE backups ADD indexing_time_ms INTEGER");
	db->Write("UPDATE backups SET indexing_time_ms=0 WHERE indexing_time_ms IS NULL");
}

void upgrade34_35()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db->Write("CREATE INDEX IF NOT EXISTS clients_hist_id_created_idx ON clients_hist_id (created)");
}

bool upgrade35_36()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if(!db->Write("CREATE TEMPORARY TABLE files_bck ("
		"id INTEGER PRIMARY KEY,"
		"backupid INTEGER,"
		"fullpath TEXT,"
		"shahash BLOB,"
		"filesize INTEGER,"
		"created DATE DEFAULT CURRENT_TIMESTAMP"
		", rsize INTEGER, clientid INTEGER, incremental INTEGER, hashpath TEXT);"))
	{
		return false;
	}

	if(!db->Write("INSERT INTO files_bck SELECT rowid, backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, hashpath FROM files"))
	{
		return false;
	}

	if(!db->Write("DROP INDEX files_idx") ||
		!db->Write("DROP INDEX files_did_count") ||
		!db->Write("DROP INDEX files_backupid") ||
		!db->Write("DROP TABLE files"))
	{
		return false;
	}

	if(!db->Write("CREATE TABLE files ("
		"id INTEGER PRIMARY KEY,"
		"backupid INTEGER,"
		"fullpath TEXT,"
		"shahash BLOB,"
		"filesize INTEGER,"
		"created DATE DEFAULT CURRENT_TIMESTAMP"
		", rsize INTEGER, clientid INTEGER, incremental INTEGER, hashpath TEXT, next_entry INTEGER, prev_entry INTEGER, pointed_to INTEGER);"))
	{
		return false;
	}

	if(!db->Write("INSERT INTO files SELECT id, backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, hashpath, 0 AS next_entry, 0 AS prev_entry, 0 AS pointed_to FROM files_bck"))
	{
		return false;
	}

	if(!db->Write("DROP TABLE files_bck"))
	{
		return false;
	}

	if(!db->Write("DROP TABLE files_del"))
	{
		return false;
	}

	if(!db->Write("DROP TABLE files_new"))
	{
		return false;
	}

	if(!db->Write("CREATE INDEX files_backupid ON files (backupid)"))
	{
		return false;
	}

	if(!db->Write("CREATE TABLE files_incoming_stat (id INTEGER PRIMARY KEY, filesize INTEGER, clientid INTEGER, backupid INTEGER, existing_clients TEXT, direction INTEGER, incremental INTEGER)"))
	{
		return false;
	}

	return true;
}

bool upgrade36_37()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	bool b = true;
	b &= db->Write("ALTER TABLE backups ADD tgroup INTEGER");
	b &= db->Write("UPDATE backups SET tgroup=0 WHERE tgroup IS NULL");
	return b;
}


void upgrade(void)
{
	Server->destroyAllDatabases();
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	IQuery *qp=db->Prepare("SELECT tvalue FROM misc WHERE tkey='db_version'");
	if(qp==NULL)
	{
		Server->Log("Importing data...");
		db->Import("urbackup/backup_server.dat");
		qp=db->Prepare("SELECT tvalue FROM misc WHERE tkey='db_version'");
	}
	if(qp==NULL)
	{
		return;
	}
	db_results res_v=qp->Read();
	if(res_v.empty())
		return;
	
	int ver=watoi(res_v[0][L"tvalue"]);
	int old_v;
	int max_v=36;
	{
		IScopedLock lock(startup_status.mutex);
		startup_status.target_db_version=max_v;
		startup_status.curr_db_version=ver;
	}
	bool do_upgrade=false;
	if(ver<max_v)
	{
		do_upgrade=true;
		Server->Log("Upgrading...", LL_WARNING);
		Server->Log("Converting database to delete journal mode...", LL_WARNING);
		db->Write("PRAGMA journal_mode=DELETE");
	}

	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));
	}
	
	IQuery *q_update=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='db_version'");
	do
	{
		if(ver<max_v)
		{
		    Server->Log("Upgrading database to version "+nconvert(ver+1), LL_WARNING);
		}
		db->BeginTransaction();
		old_v=ver;
		bool has_error=false;
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
			case 9:
				upgrade9_10();
				++ver;
				break;
			case 10:
				upgrade10_11();
				++ver;
				break;
			case 11:
				upgrade11_12();
				++ver;
				break;
			case 12:
				upgrade12_13();
				++ver;
				break;
			case 13:
				upgrade13_14();
				++ver;
				break;
			case 14:
				upgrade14_15();
				++ver;
				break;
			case 15:
				upgrade15_16();
				++ver;
				break;
			case 16:
				upgrade16_17();
				++ver;
				break;
			case 17:
				upgrade17_18();
				++ver;
				break;
			case 18:
				upgrade18_19();
				++ver;
				break;
			case 19:
				upgrade19_20();
				++ver;
				break;
			case 20:
				upgrade20_21();
				++ver;
				break;
			case 21:
				upgrade21_22();
				++ver;
				break;
			case 22:
				upgrade22_23();
				++ver;
				break;
			case 23:
				upgrade23_24();
				++ver;
				break;
			case 24:
				upgrade24_25();
				++ver;
				break;
			case 25:
				upgrade25_26();
				++ver;
				break;
			case 26:
				upgrade26_27();
				++ver;
				break;
			case 27:
				upgrade27_28();
				++ver;
				break;
			case 28:
				upgrade28_29();
				++ver;
				break;
			case 29:
				upgrade29_30();
				++ver;
				break;
			case 30:
				upgrade30_31();
				++ver;
				break;
			case 31:
				upgrade31_32();
				++ver;
				break;
			case 32:
				upgrade32_33();
				++ver;
				break;
			case 33:
				upgrade33_34();
				++ver;
				break;
			case 34:
				upgrade34_35();
				++ver;
				break;
			case 35:
				upgrade35_36();
				++ver;
				break;
			case 35:
				if(!upgrade35_36())
				{
					has_error=true;
				}
				++ver;
				break;
			default:
				break;
		}

		if(has_error)
		{
			db->Write("ROLLBACK");
			Server->Log("Upgrading database failed. Shutting down server.", LL_ERROR);
			exit(5);
		}
		
		if(ver!=old_v)
		{
			q_update->Bind(ver);
			q_update->Write();
			q_update->Reset();

			{
				IScopedLock lock(startup_status.mutex);
				startup_status.curr_db_version=ver;
			}
		}
		
		db->EndTransaction();
	}
	while(old_v<ver);
	
	if(do_upgrade)
	{
		Server->Log("Done.", LL_WARNING);
	}

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+wnarrow(cache_res[0][L"cache_size"]));
		db->freeMemory();
	}
	
	db->destroyAllQueries();
}
