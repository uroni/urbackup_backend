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

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif

#include <vector>

#ifndef STATIC_PLUGIN
#define DEF_SERVER
#endif
#include "../Interface/Server.h"

#ifndef STATIC_PLUGIN
IServer *Server;
#else
#include "../StaticPluginRegistration.h"

extern IServer* Server;

#define LoadActions LoadActions_urbackupserver
#define UnloadActions UnloadActions_urbackupserver
#endif

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
#include "../luaplugin/ILuaInterpreter.h"
#include "../clouddrive/IClouddriveFactory.h"

#include "database.h"
#include "actions.h"
#include "serverinterface/actions.h"
#include "serverinterface/helper.h"
SStartupStatus startup_status;
#include "server.h"
#include "ImageMount.h"


#include "../stringtools.h"
#include "server_status.h"
#include "server_log.h"
#include "server_cleanup.h"
#include "ClientMain.h"
#include "server_archive.h"
#include "server_settings.h"
#include "server_update_stats.h"
#include "../urbackupcommon/os_functions.h"
#include "InternetServiceConnector.h"
#include "filedownload.h"
#include "apps/cleanup_cmd.h"
#include "apps/repair_cmd.h"
#include "apps/export_auth_log.h"
#include "apps/skiphash_copy.h"
#include "apps/patch.h"
#include "create_files_index.h"
#include "server_dir_links.h"
#include "server_channel.h"
#include "DataplanDb.h"
#include "Alerts.h"
#include "Mailer.h"
#include "../urbackupcommon/settingslist.h"

#include <stdlib.h>
#include "../Interface/DatabaseCursor.h"
#include <set>
#include "apps/check_files_index.h"
#include "../fileservplugin/IFileServ.h"
#include "../fileservplugin/IFileServFactory.h"
#include "restore_client.h"
#include "../urbackupcommon/WalCheckpointThread.h"
#include "FileMetadataDownloadThread.h"
#include "../urbackupcommon/chunk_hasher.h"
#include "LogReport.h"
#include "WebSocketConnector.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.h"

namespace
{
#include "backup_server_db.h"

	std::string get_backup_server_db_data()
	{
		size_t out_len;
		void* cdata = tinfl_decompress_mem_to_heap(backup_server_db_z, backup_server_db_z_len, &out_len, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32);
		if (cdata == NULL)
		{
			return std::string();
		}

		std::string ret(reinterpret_cast<char*>(cdata), reinterpret_cast<char*>(cdata) + out_len);
		mz_free(cdata);
		return ret;
	}
}

IPipe *server_exit_pipe=NULL;
IFSImageFactory *image_fak;
ICryptoFactory *crypto_fak;
IClouddriveFactory* clouddrive_fak;
IUrlFactory *url_fak=NULL;
IFileServ* fileserv=NULL;
ILuaInterpreter* lua_interpreter = NULL;

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
int md5sum_check();
int blockalign();
void init_server_pubkey();

std::string lang="en";
std::string time_format_str="%Y-%m-%d %H:%M";
std::string time_format_str_de=time_format_str;

THREADPOOL_TICKET tt_cleanup_thread;
THREADPOOL_TICKET tt_automatic_archive_thread;
bool is_leak_check=false;

const size_t sqlite_data_allocation_chunk_size = 50*1024*1024; //50MB

#ifdef _WIN32
const std::string new_file="new.txt";
#else
const std::string new_file="urbackup/new.txt";
#endif

void open_server_database(bool init_db)
{
	if( !FileExists("urbackup/backup_server.db") && init_db )
	{
		writestring(get_backup_server_db_data(), "urbackup/backup_server.db");
	}

	std::string sqlite_mmap_huge = Server->getServerParameter("sqlite_mmap_huge");
	std::string sqlite_mmap_medium = Server->getServerParameter("sqlite_mmap_medium");
	std::string sqlite_mmap_small = Server->getServerParameter("sqlite_mmap_small");

	str_map params;
	params["wal_autocheckpoint"] = "0";

	if (!sqlite_mmap_medium.empty())
	{
		params["mmap_size"] = sqlite_mmap_medium;
	}
		
	if(! Server->openDatabase("urbackup/backup_server.db", URBACKUPDB_SERVER, params) )
	{
		Server->Log("Couldn't open Database backup_server.db. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_medium.empty())
	{
		params.erase(params.find("mmap_size"));
	}

	Server->setDatabaseAllocationChunkSize(URBACKUPDB_SERVER, sqlite_data_allocation_chunk_size);

	if(!Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER))
	{
		Server->Log("Couldn't open backup server database. Exiting. Expecting database at \""+
			Server->getServerWorkingDir()+os_file_sep()+"urbackup"+os_file_sep()+"backup_server.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_huge.empty())
	{
		params["mmap_size"] = sqlite_mmap_huge;
	}

	if (!Server->openDatabase("urbackup/backup_server_files.db", URBACKUPDB_SERVER_FILES, params))
	{
		Server->Log("Couldn't open Database backup_server_files.db. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_files.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_huge.empty())
	{
		params.erase(params.find("mmap_size"));
	}

	str_map params_nil;

	if (!Server->openDatabase("urbackup/backup_server_files.db", URBACKUPDB_SERVER_FILES_DEL, params_nil))
	{
		Server->Log("Couldn't open Database backup_server_files.db (2). Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_files.db\"", LL_ERROR);
		exit(1);
	}

	Server->setDatabaseAllocationChunkSize(URBACKUPDB_SERVER_FILES, sqlite_data_allocation_chunk_size);

	if (!Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES))
	{
		Server->Log("Couldn't open backup server database. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_files.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_small.empty())
	{
		params["mmap_size"] = sqlite_mmap_small;
	}

	if (!Server->openDatabase("urbackup/backup_server_link_journal.db", URBACKUPDB_SERVER_LINK_JOURNAL, params))
	{
		Server->Log("Couldn't open Database backup_server_link_journal.db. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_link_journal.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_small.empty())
	{
		params.erase(params.find("mmap_size"));
	}

	Server->setDatabaseAllocationChunkSize(URBACKUPDB_SERVER_LINK_JOURNAL, sqlite_data_allocation_chunk_size);

	if (!Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINK_JOURNAL))
	{
		Server->Log("Couldn't open backup server database. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_link_journal.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_medium.empty())
	{
		params["mmap_size"] = sqlite_mmap_small;
	}

	if (!Server->openDatabase("urbackup/backup_server_links.db", URBACKUPDB_SERVER_LINKS, params))
	{
		Server->Log("Couldn't open Database backup_server_links.db. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_links.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_medium.empty())
	{
		params.erase(params.find("mmap_size"));
	}

	Server->setDatabaseAllocationChunkSize(URBACKUPDB_SERVER_LINKS, sqlite_data_allocation_chunk_size);

	if (!Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS))
	{
		Server->Log("Couldn't open backup server database. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_links.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_small.empty())
	{
		params["mmap_size"] = sqlite_mmap_small;
	}

	if (!Server->openDatabase("urbackup/backup_server_settings.db", URBACKUPDB_SERVER_SETTINGS, params))
	{
		Server->Log("Couldn't open Database backup_server_settings.db. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_settings.db\"", LL_ERROR);
		exit(1);
	}

	if (!sqlite_mmap_small.empty())
	{
		params.erase(params.find("mmap_size"));
	}

	Server->setDatabaseAllocationChunkSize(URBACKUPDB_SERVER_SETTINGS, sqlite_data_allocation_chunk_size);

	if (!Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_SETTINGS))
	{
		Server->Log("Couldn't open backup server database. Exiting. Expecting database at \"" +
			Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_settings.db\"", LL_ERROR);
		exit(1);
	}
	
	Server->destroyDatabases(Server->getThreadID());
}

void open_settings_database()
{
	std::string aname="urbackup/backup_server_settings.db";

	Server->attachToDatabase(aname, "settings_db", URBACKUPDB_SERVER);	
}

void start_wal_checkpoint_threads()
{
	WalCheckpointThread* wal_checkpoint_thread = new WalCheckpointThread(100 * 1024 * 1024, 1000 * 1024 * 1024,
		"urbackup" + os_file_sep() + "backup_server_files.db", URBACKUPDB_SERVER_FILES);
	Server->createThread(wal_checkpoint_thread, "files checkpoint");

	wal_checkpoint_thread = new WalCheckpointThread(10 * 1024 * 1024, 100 * 1024 * 1024,
		"urbackup" + os_file_sep() + "backup_server.db", URBACKUPDB_SERVER, "main");
	Server->createThread(wal_checkpoint_thread, "main checkpoint");

	wal_checkpoint_thread = new WalCheckpointThread(10 * 1024 * 1024, 100 * 1024 * 1024,
		"urbackup" + os_file_sep() + "backup_server_settings.db", URBACKUPDB_SERVER);
	Server->createThread(wal_checkpoint_thread, "settings checkpoint");

	wal_checkpoint_thread = new WalCheckpointThread(10 * 1024 * 1024, 100 * 1024 * 1024,
		"urbackup" + os_file_sep() + "backup_server_link_journal.db", URBACKUPDB_SERVER_LINK_JOURNAL);
	Server->createThread(wal_checkpoint_thread, "lnk jour checkpoint");

	wal_checkpoint_thread = new WalCheckpointThread(100 * 1024 * 1024, 1000 * 1024 * 1024,
		"urbackup" + os_file_sep() + "backup_server_links.db", URBACKUPDB_SERVER_LINKS);
	Server->createThread(wal_checkpoint_thread, "lnk checkpoint");
}

bool attach_other_dbs(IDatabase* db)
{
	if (!db->Write("ATTACH DATABASE 'urbackup/backup_server_files.db' AS files_db"))
	{
		return false;
	}

	if (!db->Write("ATTACH DATABASE 'urbackup/backup_server_link_journal.db' AS link_journal_db"))
	{
		return false;
	}

	if (!db->Write("ATTACH DATABASE 'urbackup/backup_server_links.db' AS links_db"))
	{
		return false;
	}

	return true;
}

void detach_other_dbs(IDatabase* db)
{
	db->Write("DETACH DATABASE files_db");
	db->Write("DETACH DATABASE link_journal_db");
	db->Write("DETACH DATABASE links_db");
}

#include "../urbackupcommon/WebSocketPipe.h"

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
		os_remove_nonempty_dir(rmtest);
		return;
	}

	if (Server->getServerParameter("run-test") == "true")
	{
		exit(0);
	}

	std::string download_file=Server->getServerParameter("download_file");
	if(!download_file.empty())
	{
		/* For attaching debugger
		std::cout << "Process id: " << GetCurrentProcessId() << std::endl;
		system("pause");*/
		unsigned int tcpport=43001;
		std::string s_tcpport=Server->getServerParameter("tcpport");
		if(!s_tcpport.empty()) tcpport=atoi(s_tcpport.c_str());
		int method=0;
		std::string s_method=Server->getServerParameter("method");
		if(!s_method.empty()) method=atoi(s_method.c_str());

		FileDownload dl(Server->getServerParameter("servername"), tcpport);

		int predicted_filesize = atoi(Server->getServerParameter("predicted_filesize", "-1").c_str());
		Server->Log("Starting file download... (predicted_filesize="+convert(predicted_filesize)+")");
		dl.filedownload(download_file, Server->getServerParameter("dstfn"), method, predicted_filesize, SQueueStatus_NoQueue);
		exit(1);
	}

	std::string download_file_csv=Server->getServerParameter("download_file_csv");
	if(!download_file_csv.empty())
	{
		/* For attaching debugger
		std::cout << "Process id: " << GetCurrentProcessId() << std::endl;
		system("pause");*/
		unsigned int tcpport=43001;
		std::string s_tcpport=Server->getServerParameter("tcpport");
		if(!s_tcpport.empty()) tcpport=atoi(s_tcpport.c_str());

		FileDownload dl(Server->getServerParameter("servername"), tcpport);

		dl.filedownload(download_file_csv);
		exit(1);
	}

	init_mutex1();
	ServerLogger::init_mutex();
	init_dir_link_mutex();
	WalCheckpointThread::init_mutex();

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
		else if(app=="check_metadata")
		{
			rc=server::check_metadata();
		}
		else if(app=="skiphash_copy")
		{
			rc=skiphash_copy_file();
		}
		else if (app == "md5sum_check")
		{
			rc = md5sum_check();
		}
		else if (app == "patch_hash")
		{
			rc = patch_hash();
		}
		else if (app == "hash")
		{
			std::unique_ptr<IFsFile> f(Server->openFile(Server->getServerParameter("hash_file"), MODE_READ_SEQUENTIAL));
			if (f.get() != NULL)
			{
				std::string h = BackupServerPrepareHash::calc_hash(f.get(), Server->getServerParameter("hash_method"));
				Server->Log("Hash: " + base64_encode(reinterpret_cast<const unsigned char*>(h.data()), static_cast<unsigned int>(h.size())), LL_INFO);
			}
			else
			{
				Server->Log("Cannot open file to hash (hash_file) \"" + Server->getServerParameter("hash_file") + "\" for reading. " + os_last_error_str(), LL_ERROR);
			}
		}
		else if (app == "blockalign")
		{
			rc = blockalign();
		}
		else
		{
			rc=100;
			Server->Log("App not found. Available apps: cleanup, remove_unknown, cleanup_database, repair_database, defrag_database, export_auth_log, check_fileindex, skiphash_copy, md5sum_check, hash, blockalign");
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

	{
		str_map params;
		clouddrive_fak = reinterpret_cast<IClouddriveFactory*>(Server->getPlugin(Server->getThreadID(), Server->StartPlugin("clouddriveplugin", params)));
		if (clouddrive_fak == nullptr)
		{
			Server->Log("Error loading clouddriveplugin", LL_ERROR);
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

	if(FileExists("urbackup/server_ident_ecdsa409k1.pub") && crypto_fak!=NULL)
	{
		std::string signature;
		if(!crypto_fak->signData(getFile("urbackup/server_ident_ecdsa409k1.priv"), "test", signature))
		{
			Server->Log("Server ECDSA identity broken. Regenerating...", LL_ERROR);
			Server->deleteFile("urbackup/server_ident_ecdsa409k1.pub");
			Server->deleteFile("urbackup/server_ident_ecdsa409k1.priv");
		}
	}

	if(!FileExists("urbackup/server_ident_ecdsa409k1.pub") && crypto_fak!=NULL)
	{
		Server->Log("Generating Server private/public ECDSA key...", LL_INFO);
		crypto_fak->generatePrivatePublicKeyPair("urbackup/server_ident_ecdsa409k1");
	}

	if(FileExists("urbackup/server_ident.pub") && crypto_fak!=NULL)
	{
		std::string signature;
		if(!crypto_fak->signDataDSA(getFile("urbackup/server_ident.priv"), "test", signature))
		{
			Server->Log("Server DSA identity broken. Regenerating...", LL_ERROR);
			Server->deleteFile("urbackup/server_ident.pub");
			Server->deleteFile("urbackup/server_ident.priv");
		}
	}

	if(!FileExists("urbackup/server_ident.pub") && crypto_fak!=NULL)
	{
		Server->Log("Generating Server private/public DSA key...", LL_INFO);
		crypto_fak->generatePrivatePublicKeyPairDSA("urbackup/server_ident");
	}
	if((server_token=getFile("urbackup/server_token.key")).size()<5)
	{
		Server->Log("Generating Server token...", LL_INFO);
		std::string token=ServerSettings::generateRandomAuthKey(20);
		writestring(token, "urbackup/server_token.key");
		server_token=token;
	}

	init_server_pubkey();

	Server->deleteFile("urbackup/shutdown_now");


	{
		str_map params;
		image_fak=(IFSImageFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("fsimageplugin", params));
		if( image_fak==NULL )
		{
			Server->Log("Error loading fsimageplugin", LL_ERROR);
		}
	}

	{
		str_map params;
		IFileServFactory* fileserv_fak=(IFileServFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("fileserv", params));
		if( fileserv_fak==NULL )
		{
			Server->Log("Error loading fileservplugin. File restores won't work.", LL_ERROR);
		}
		else
		{
			if (!fileserv_fak->optainBackupPrivileges())
			{
				Server->Log("Error optaining backup SYSTEM privileges. Restoring symlinks may not work.", LL_ERROR);
			}
			fileserv = fileserv_fak->createFileServNoBind(std::string(), false, false, true);
		}
	}

	{
		str_map params;
		lua_interpreter = dynamic_cast<ILuaInterpreter*>(Server->getPlugin(Server->getThreadID(), Server->StartPlugin("lua", params)));
		if (lua_interpreter == NULL)
		{
			Server->Log("Lua plugin missing. Alerts won't work.", LL_WARNING);
		}
	}

	
	open_server_database(true);
	

	ServerStatus::init_mutex();
	ServerSettings::init_mutex();
	ClientMain::init_mutex();
	DataplanDb::init();
	init_log_report();
	ServerChannelThread::init_mutex();

	open_settings_database();
	
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
			Server->Log("Backup verification successful.", LL_INFO);
			Server->deleteFile("verification_result.txt");
			exit(0);
		}
	}

	Server->destroyAllDatabases();

	startup_status.mutex=Server->createMutex();
	{
		IScopedLock lock(startup_status.mutex);
		startup_status.upgrading_database=true;
	}

	ADD_ACTION(login);
		
	upgrade();


	std::vector<DATABASE_ID> dbs;
	dbs.push_back(URBACKUPDB_SERVER);
	dbs.push_back(URBACKUPDB_SERVER_FILES);
	dbs.push_back(URBACKUPDB_SERVER_LINKS);
	dbs.push_back(URBACKUPDB_SERVER_LINK_JOURNAL);

	for (size_t i = 0; i < dbs.size(); ++i)
	{
		IDatabase *db = Server->getDatabase(Server->getThreadID(), dbs[i]);
		db->Write("PRAGMA journal_mode=WAL");
	}
		
	if( FileExists("urbackup/backupfolder")
#ifndef _WIN32
		|| FileExists("/etc/urbackup/backupfolder")
#endif
		)
	{
		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db_results res=db->Read("SELECT value FROM settings_db.settings WHERE key='backupfolder' AND clientid=0");
		if(res.empty())
		{
			IQuery *q=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('backupfolder', ?, 0)", false);
			std::string bf=getFile("urbackup/backupfolder");
#ifndef _WIN32
			if (bf.empty())
			{
				bf = getFile("/etc/urbackup/backupfolder");
			}
#endif
			if(linecount(bf)>0)
				bf=getline(0, bf);
			q->Bind(trim(bf));
			q->Write();
			db->destroyQuery(q);
			ServerSettings::updateAll();
		}
	}

	if(!create_files_index(startup_status))
	{
		Server->Log("Could not create or open file entry index. Exiting.", LL_ERROR);
		exit(1);
	}

	{
		IScopedLock lock(startup_status.mutex);
		startup_status.upgrading_database=false;
	}

	std::string set_admin_pw=Server->getServerParameter("set_admin_pw");
	if(!set_admin_pw.empty())
	{
		std::string set_admin_username = Server->getServerParameter("set_admin_username");

		if(set_admin_username.empty())
		{
			set_admin_username = "admin";
		}

		std::string new_salt=ServerSettings::generateRandomAuthKey(20);

		const size_t pbkdf2_rounds = 10000;

		std::string password_md5 = strlower(crypto_fak->generatePasswordHash(hexToBytes(Server->GenerateHexMD5(new_salt+set_admin_pw)),
				new_salt, pbkdf2_rounds));

		IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

		IQuery* q_read = db->Prepare("SELECT password_md5 FROM settings_db.si_users WHERE name=?");
		q_read->Bind(set_admin_username);
		db_results res = q_read->Read();
		q_read->Reset();

		if(res.empty())
		{
			IQuery *q=db->Prepare("INSERT INTO settings_db.si_users (name, password_md5, salt, pbkdf2_rounds) VALUES (?,?,?,?)");
			q->Bind(set_admin_username);
			q->Bind(password_md5);
			q->Bind(new_salt);
			q->Bind(pbkdf2_rounds);
			q->Write();
			q->Reset();

			Server->Log("User account \""+set_admin_username+"\" not present. Created admin user account with specified password.", LL_INFO);
		}
		else
		{
			IQuery *q=db->Prepare("UPDATE si_users SET password_md5=?, salt=?, pbkdf2_rounds=? WHERE name=?");
			q->Bind(password_md5);
			q->Bind(new_salt);
			q->Bind(pbkdf2_rounds);
			q->Bind(set_admin_username);
			q->Write();
			q->Reset();

			Server->Log("Changed admin password.", LL_INFO);
		}

		{
			IQuery *q=db->Prepare("SELECT id FROM si_users WHERE name=?");
			q->Bind(set_admin_username);
			db_results res = q->Read();
			q->Reset();
			if(!res.empty())
			{
				updateRights(watoi(res[0]["id"]), "idx=0&0_domain=all&0_right=all", db);

				Server->Log("Updated admin rights.", LL_INFO);
			}
		}

		db->destroyAllQueries();

		exit(0);
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
	ADD_ACTION(add_client);
	ADD_ACTION(restore_prepare_wait);
	ADD_ACTION(scripts);
	ADD_ACTION(status_check);
	ADD_ACTION(restore_image);

	if(Server->getServerParameter("allow_shutdown")=="true")
	{
		ADD_ACTION(shutdown);
	}

	replay_directory_link_journal();

	Server->Log("Started UrBackup...", LL_INFO);

	
	str_map params;
	url_fak=(IUrlFactory*)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("url", params));
	if(url_fak==NULL)
	{
		Server->Log("Error loading IUrlFactory", LL_INFO);
	}

    ServerChannelThread::initOffset();

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(crypto_fak==NULL 
		&& db->Read("SELECT * FROM settings_db.si_users WHERE pbkdf2_rounds>0").size()>0)
	{
		Server->Log("Encrypted user passwords need cryptoplugin. Cannot run without cryptoplugin", LL_ERROR);
		exit(1);
	}

	server_exit_pipe=Server->createMemoryPipe();
	BackupServer *backup_server=new BackupServer(server_exit_pipe);
	Server->createThread(backup_server, "client discovery");
	Server->wait(500);

	InternetServiceConnector::init_mutex();

	{
		ServerSettings settings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER));
		if(Server->getServerParameter("internet_mode_disabled")!="1")
		{
			std::string tmp=Server->getServerParameter("internet_port", "");
			unsigned int port;
			if(!tmp.empty())
			{
				port = atoi(tmp.c_str());
			}
			else
			{
				port = 55415;
			}

			std::unique_ptr<ISettingsReader> settings_db(Server->createDBSettingsReader(db, "settings_db.settings",
				"SELECT value FROM settings_db.settings WHERE key=? AND clientid=0"));

			std::string port_str = settings_db->getValue("internet_server_bind_port", std::string());
			if (!port_str.empty())
			{
				port = watoi(port_str);
			}

			IServer::BindTarget internet_bind_target = IServer::BindTarget_All;

			if (Server->getServerParameter("internet_localhost_only") == "1")
			{
				internet_bind_target = IServer::BindTarget_Localhost;
			}

			InternetService* internet_service = new InternetService(backup_server);
			Server->StartCustomStreamService(internet_service, "InternetService", port, internet_bind_target);

			if (Server->getServerParameter("internet_disable_websocket") != "1")
			{
				Server->addWebSocket(new WebSocketConnector(internet_service, "socket"));
				Server->addWebSocket(new WebSocketConnector(internet_service, ""));
			}
		}
	}

	init_chunk_hasher();
	ServerCleanupThread::initMutex();
	ServerAutomaticArchive::initMutex();
	ServerCleanupThread *server_cleanup=new ServerCleanupThread(CleanupAction());
	Mailer::init();
	Server->createThread(new Alerts, "alerts");

	is_leak_check=(Server->getServerParameter("leak_check")=="true");

	if(is_leak_check)
	{
		tt_cleanup_thread=Server->getThreadPool()->execute(server_cleanup, "backup cleanup");
		tt_automatic_archive_thread=Server->getThreadPool()->execute(new ServerAutomaticArchive, "backup archival");
	}
	else
	{
		Server->createThread(server_cleanup, "backup cleanup");
		Server->createThread(new ServerAutomaticArchive, "backup archival");
	}

	Server->createThread(new ImageMount, "image umount");

	Server->setLogCircularBufferSize(20);

	start_wal_checkpoint_threads();

	Server->clearDatabases(Server->getThreadID());

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
		WalCheckpointThread::destroy_mutex();
		destroy_dir_link_mutex();
		Server->wait(1000);
	}

	if(shutdown_ok)
	{
		ClientMain::destroy_mutex();
	}

	std::vector<DATABASE_ID> db_ids;
	db_ids.push_back(URBACKUPDB_SERVER);
	db_ids.push_back(URBACKUPDB_SERVER_FILES);
	db_ids.push_back(URBACKUPDB_SERVER_LINKS);
	db_ids.push_back(URBACKUPDB_SERVER_LINK_JOURNAL);

	if (!shutdown_ok)
	{
		for (size_t i = 0; i < db_ids.size(); ++i)
		{
			IDatabase *db = Server->getDatabase(Server->getThreadID(), db_ids[i]);
			db->BeginWriteTransaction();
		}
	}
	else
	{
		Server->destroyAllDatabases();
	}
	FileIndex::stop_accept();
	FileIndex::flush();
}

#ifdef STATIC_PLUGIN
namespace
{
	static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 20);
}
#endif

void update_file(IQuery *q_space_get, IQuery* q_space_update, IQuery *q_file_update, db_results &curr_r)
{
	_i64 filesize=os_atoi64(curr_r[0]["filesize"]);

	std::map<int, int> client_c;
	for(size_t i=0;i<curr_r.size();++i)
	{
		int cid=watoi(curr_r[i]["clientid"]);
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
			q_file_update->Bind(os_atoi64(curr_r[i]["id"]));
			q_file_update->Write();
			q_file_update->Reset();
		}
		else
		{
			q_file_update->Bind(0);
			q_file_update->Bind(os_atoi64(curr_r[i]["id"]));
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
			_i64 used=os_atoi64(res[0]["bytes_used_files"]);
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

	std::string filesize;
	std::string shhash;
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
			if(shhash.empty() || (res[j]["shahash"]!=shhash || res[j]["filesize"]!=filesize )  )
			{
				if(!curr_r.empty())
				{
					update_file(q_space_get, q_space_update, q_file_update, curr_r);
				}
				curr_r.clear();
				shhash=res[j]["shhash"];
				filesize=res[j]["filesize"];
				curr_r.push_back(res[j]);
			}

			int pc=(int)(((float)j/(float)res.size())*100.f+0.5f);
			if(pc!=last_pc)
			{
				Server->Log(convert(pc)+"%", LL_INFO);
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
		q->Bind(res[i]["id"]);
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
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("allow_starting_full_file_backups");
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
		q_insert->Write();
		q_insert->Reset();
	}

	res=db->Read("SELECT value, clientid FROM settings_db.settings WHERE key='allow_starting_image_backups'");
	q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?, ?, ?)");
	for(size_t i=0;i<res.size();++i)
	{
		q_insert->Bind("allow_starting_incr_image_backups");
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("allow_starting_full_image_backups");
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
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
		q_insert->Bind(res[i]["t_right"]);
		q_insert->Bind(res[i]["clientid"]);
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
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("backup_window_full_file");
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("backup_window_incr_image");
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
		q_insert->Write();
		q_insert->Reset();

		q_insert->Bind("backup_window_full_image");
		q_insert->Bind(res[i]["value"]);
		q_insert->Bind(res[i]["clientid"]);
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

	std::unique_ptr<IFile> db_file(Server->openFile("urbackup/backup_server.db", MODE_READ));

	if(db_file.get() && Server->getServerParameter("override_freespace_check")==std::string())
	{
		int64 free_space = os_free_space(Server->getServerWorkingDir()+os_file_sep()+"urbackup");

		int64 missing_free_space = db_file->Size()-free_space;
		if(free_space<db_file->Size())
		{
			Server->Log("UrBackup needs "+PrettyPrintBytes(missing_free_space)+" more free space for the upgrade process. Please free up this amount of space at \""+Server->getServerWorkingDir()+os_file_sep()+"urbackup\"", LL_ERROR);
			return false;
		}

		std::string tmpdir = db->getTempDirectoryPath();
		if(!tmpdir.empty())
		{
			free_space = os_free_space(tmpdir);

			int64 missing_free_space = db_file->Size()-free_space;
			if(free_space<db_file->Size())
			{
				Server->Log("UrBackup needs "+PrettyPrintBytes(missing_free_space)+" more free space for the upgrade process at the temporary location. "
					"Please free up this amount of space at \""+tmpdir+"\"", LL_ERROR);
				return false;
			}
		}
	}

	db_file.reset();

	if(!db->Write("ALTER TABLE files RENAME TO files_bck"))
	{
		return false;
	}

	if(!db->Write("DROP INDEX files_idx") ||
		!db->Write("DROP INDEX files_did_count") ||
		!db->Write("DROP INDEX files_backupid") )
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

	if (!db->Write("CREATE TABLE files_db.files ("
		"id INTEGER PRIMARY KEY,"
		"backupid INTEGER,"
		"fullpath TEXT,"
		"shahash BLOB,"
		"filesize INTEGER,"
		"created INTEGER DEFAULT (CAST(strftime('%s','now') as INTEGER)),"
		"rsize INTEGER, clientid INTEGER, incremental INTEGER, hashpath TEXT, next_entry INTEGER, prev_entry INTEGER, pointed_to INTEGER)"))
	{
		return false;
	}

	IDatabaseCursor* cur = db->Prepare("SELECT rowid, backupid, fullpath, shahash, filesize, strftime('%s', created) AS created, rsize, clientid, incremental, hashpath FROM files_bck")->Cursor();

	IQuery* q_insert_file = db->Prepare("INSERT INTO files_db.files (id, backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, hashpath, next_entry, prev_entry, pointed_to) "
		"VALUES (?,?,?,?,?,?,?,?,?,?,0,0,0)");
	db_single_result res;
	while (cur->next(res))
	{
		q_insert_file->Bind(watoi64(res["rowid"]));
		q_insert_file->Bind(watoi(res["backupid"]));
		q_insert_file->Bind(res["fullpath"]);
		q_insert_file->Bind(res["shahash"].c_str(), static_cast<_u32>(res["shahash"].size()));
		q_insert_file->Bind(watoi64(res["filesize"]));
		q_insert_file->Bind(watoi64(res["created"]));
		q_insert_file->Bind(watoi64(res["rsize"]));
		q_insert_file->Bind(watoi(res["clientid"]));
		q_insert_file->Bind(watoi(res["incremental"]));
		q_insert_file->Bind(res["hashpath"]);
		q_insert_file->Write();
		q_insert_file->Reset();
	}

	if (cur->has_error())
	{
		return false;
	}

	if(!db->Write("DROP TABLE files_bck"))
	{
		return false;
	}

	if(!db->Write("CREATE INDEX files_db.files_backupid ON files (backupid)"))
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

bool update37_38()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("CREATE TABLE log_data (id INTEGER PRIMARY KEY, logid INTEGER REFERENCES logs(id) ON DELETE CASCADE, data TEXT)");
	b &= db->Write("CREATE INDEX log_data_idx ON log_data (logid)");

	IQuery* q_ins=db->Prepare("INSERT INTO log_data (logid, data) VALUES (?, ?)");

	db_results res;
	do 
	{
		res=db->Read("SELECT l.id AS id, l.logdata AS logdata FROM logs l WHERE NOT EXISTS (SELECT id FROM log_data WHERE logid = l.id) LIMIT 100");
		for(size_t i=0;i<res.size();++i)
		{
			q_ins->Bind(res[i]["id"]);
			q_ins->Bind(res[i]["logdata"]);
			q_ins->Write();
			q_ins->Reset();
		}
	} while (!res.empty());

	b &= db->Write("UPDATE logs SET logdata = NULL");

	return b;
}

bool update38_39()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("CREATE TABLE users_on_client (id INTEGER PRIMARY KEY, clientid INTEGER REFERENCES clients(id) ON DELETE CASCADE, username TEXT)");
	b &= db->Write("CREATE UNIQUE INDEX users_on_client_unique ON users_on_client(clientid, username)");

	b &= db->Write("CREATE TABLE tokens_on_client (id INTEGER PRIMARY KEY, clientid INTEGER REFERENCES clients(id) ON DELETE CASCADE, token TEXT)");
	b &= db->Write("CREATE UNIQUE INDEX tokens_on_client_unique ON tokens_on_client(clientid, token)");

	b &= db->Write("CREATE TABLE user_tokens (id INTEGER PRIMARY KEY, username TEXT, token TEXT)");
	b &= db->Write("CREATE UNIQUE INDEX user_tokens_unique ON user_tokens(username, token)");

	return b;
}

bool update39_40()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE logs ADD restore INTEGER");
	b &= db->Write("UPDATE logs SET restore=0 WHERE restore IS NULL");
	b &= db->Write("CREATE TABLE restores (id INTEGER PRIMARY KEY, clientid INTEGER REFERENCES clients(id) ON DELETE CASCADE, created DATE DEFAULT CURRENT_TIMESTAMP, finished DATE, done INTEGER, path TEXT, identity TEXT, success INTEGER)");

	return b;
}

bool upgrade40_41()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE settings_db.si_users ADD pbkdf2_rounds INTEGER");
	b &= db->Write("UPDATE settings_db.si_users SET pbkdf2_rounds=0");

	if(crypto_fak!=NULL)
	{
		db_results res = db->Read("SELECT id, password_md5, salt FROM settings_db.si_users");

		const size_t pbkdf2_rounds = 10000;

		IQuery* q_update = db->Prepare("UPDATE settings_db.si_users SET password_md5=?, pbkdf2_rounds=? WHERE id=?");

		for(size_t i=0;i<res.size();++i)
		{
			std::string password_md5 = strlower(crypto_fak->generatePasswordHash(hexToBytes((res[i]["password_md5"])),
				(res[i]["salt"]), pbkdf2_rounds));

			q_update->Bind(password_md5);
			q_update->Bind(pbkdf2_rounds);
			q_update->Bind(res[i]["id"]);
			q_update->Write();
			q_update->Reset();
		}
	}	

	return b;
}

bool upgrade41_42()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE clients ADD virtualmain TEXT");

	return b;
}

bool upgrade42_43()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("CREATE TABLE settings_db.access_tokens ("
		"clientid INTEGER,"
		"tokenhash BLOB)");

	b &= db->Write("CREATE UNIQUE INDEX settings_db.access_tokens_idx ON access_tokens(tokenhash)");

	return b;
}

bool upgrade43_44()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE restores ADD image INTEGER DEFAULT 0");

	b &= db->Write("ALTER TABLE restores ADD letter TEXT DEFAULT ''");

	return b;
}

bool upgrade44_45()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if (!db->Read("SELECT * FROM sqlite_master WHERE name = 'files' AND type = 'table'").empty())
	{
		db->Write("DROP TABLE IF EXISTS files_db.files");

		if (!db->Write("CREATE TABLE files_db.files ("
			"id INTEGER PRIMARY KEY,"
			"backupid INTEGER,"
			"fullpath TEXT,"
			"shahash BLOB,"
			"filesize INTEGER,"
			"created INTEGER DEFAULT (CAST(strftime('%s','now') as INTEGER)),"
			"rsize INTEGER, clientid INTEGER, incremental INTEGER, hashpath TEXT, next_entry INTEGER, prev_entry INTEGER, pointed_to INTEGER)"))
		{
			return false;
		}

		IDatabaseCursor* cur = db->Prepare("SELECT id, backupid, fullpath, shahash, filesize, strftime('%s', created) AS created, rsize, clientid, incremental, hashpath FROM files")->Cursor();

		IQuery* q_insert_file = db->Prepare("INSERT INTO files_db.files (id, backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, hashpath, next_entry, prev_entry, pointed_to) "
			"VALUES (?,?,?,?,?,?,?,?,?,?,0,0,0)");
		db_single_result res;
		while (cur->next(res))
		{
			q_insert_file->Bind(watoi64(res["id"]));
			q_insert_file->Bind(watoi(res["backupid"]));
			q_insert_file->Bind(res["fullpath"]);
			q_insert_file->Bind(res["shahash"].c_str(), static_cast<_u32>(res["shahash"].size()));
			q_insert_file->Bind(watoi64(res["filesize"]));
			q_insert_file->Bind(watoi64(res["created"]));
			q_insert_file->Bind(watoi64(res["rsize"]));
			q_insert_file->Bind(watoi(res["clientid"]));
			q_insert_file->Bind(watoi(res["incremental"]));
			q_insert_file->Bind(res["hashpath"]);
			q_insert_file->Write();
			q_insert_file->Reset();
		}

		if (cur->has_error())
		{
			return false;
		}

		if (!db->Write("DROP TABLE files"))
		{
			return false;
		}

		if (!db->Write("CREATE INDEX files_db.files_backupid ON files (backupid)"))
		{
			return false;
		}
	}

	if (!db->Write("CREATE TABLE files_db.files_incoming_stat (id INTEGER PRIMARY KEY, filesize INTEGER, clientid INTEGER, backupid INTEGER, existing_clients TEXT, direction INTEGER, incremental INTEGER)"))
	{
		return false;
	}

	if (!db->Write("DROP TABLE files_incoming_stat"))
	{
		return false;
	}

	if (!db->Write("CREATE TABLE links_db.directory_links ("
		"id INTEGER PRIMARY KEY,"
		"clientid INTGER,"
		"name TEXT,"
		"target TEXT)"))
	{
		return false;
	}

	IDatabaseCursor* cur = db->Prepare("SELECT id, clientid, name, target FROM directory_links")->Cursor();

	IQuery* q_insert_link = db->Prepare("INSERT INTO links_db.directory_links (id, clientid, name, target) "
		"VALUES (?, ?, ?, ?)");
	db_single_result res;
	while (cur->next(res))
	{
		q_insert_link->Bind(res["id"]);
		q_insert_link->Bind(res["clientid"]);
		q_insert_link->Bind(res["name"]);
		q_insert_link->Bind(res["target"]);
		q_insert_link->Write();
		q_insert_link->Reset();
	}

	if (!db->Write("CREATE INDEX links_db.directory_links_idx ON directory_links (clientid, name)"))
	{
		return false;
	}

	if (!db->Write("CREATE INDEX links_db.directory_links_target_idx ON directory_links (clientid, target)"))
	{
		return false;
	}

	if (!db->Write("CREATE TABLE link_journal_db.directory_link_journal ("
		"id INTEGER PRIMARY KEY,"
		"linkname TEXT,"
		"linktarget TEXT)"))
	{
		return false;
	}

	if (!db->Write("DROP TABLE directory_links"))
	{
		return false;
	}

	if (!db->Write("DROP TABLE directory_link_journal"))
	{
		return false;
	}

	return true;
}

bool upgrade45_46()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("DROP TABLE user_tokens");

	b &= db->Write("CREATE TABLE user_tokens (id INTEGER PRIMARY KEY, username TEXT, tgroup TEXT, clientid INTEGER DEFAULT 0, token TEXT, created DATE DEFAULT CURRENT_TIMESTAMP)");
	b &= db->Write("CREATE UNIQUE INDEX user_tokens_unique ON user_tokens(username, clientid, token, tgroup)");

	return b;
}

bool upgrade46_47()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE clients ADD last_filebackup_issues INTEGER DEFAULT 0");
	b &= db->Write("ALTER TABLE clients ADD os_simple TEXT");
	b &= db->Write("ALTER TABLE clients ADD os_version_str TEXT");
	b &= db->Write("ALTER TABLE clients ADD client_version_str TEXT");

	return b;
}

bool upgrade47_48()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE clients ADD groupid INTEGER DEFAULT 0");

	b &= db->Write("UPDATE clients SET groupid=0 WHERE groupid IS NULL");
	
	b &= db->Write("CREATE TABLE settings_db.si_client_groups ("
		"id INTEGER PRIMARY KEY,"
		"name TEXT)");

	return b;
}

bool upgrade48_49()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE backups ADD synctime INTEGER");

	b &= db->Write("ALTER TABLE backup_images ADD synctime INTEGER");

	return b;
}

bool upgrade49_50()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("UPDATE settings_db.settings SET value='0' WHERE value='-1' "
		"AND (key='local_speed' OR key='internet_speed' OR key='global_local_speed' OR key='global_internet_speed')");

	return b;
}

bool upgrade50_51()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE backup_images ADD archived INTEGER");
	b &= db->Write("UPDATE backup_images SET archived=0 WHERE archived IS NULL");
	b &= db->Write("ALTER TABLE backup_images ADD archive_timeout INTEGER");
	b &= db->Write("ALTER TABLE backup_images ADD mounttime INTEGER");
	b &= db->Write("UPDATE backup_images SET mounttime=0 WHERE mounttime IS NULL");
	b &= db->Write("UPDATE settings_db.settings SET value='0' WHERE value='-1' "
		"AND (key='local_speed' OR key='internet_speed' OR key='global_local_speed' OR key='global_internet_speed')");

	ServerSettings::updateAll();

	return b;
}

bool upgrade51_52()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE backups ADD delete_pending INTEGER");
	b &= db->Write("ALTER TABLE backup_images ADD delete_pending INTEGER");

	return b;
}

bool upgrade52_53()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE settings_db.automatic_archival ADD letters TEXT");
	b &= db->Write("UPDATE settings_db.automatic_archival SET letters='' WHERE letters IS NULL");
	b &= db->Write("UPDATE settings_db.automatic_archival SET letters='ALL' WHERE backup_types=4 OR backup_types=8 OR backup_types=12");

	return b;
}

bool upgrade53_54()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('restore_authkey','"+ServerSettings::generateRandomAuthKey()+"', 0)");

	return b;
}

bool upgrade54_55()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE clients ADD file_ok INTEGER");
	b &= db->Write("ALTER TABLE clients ADD image_ok INTEGER");
	b &= db->Write("ALTER TABLE clients ADD alerts_state BLOB");
	b &= db->Write("ALTER TABLE clients ADD alerts_next_check INTEGER");
	b &= db->Write("ALTER TABLE clients ADD created INTEGER");
	b &= db->Write("UPDATE clients SET created=0");

	b &= db->Write("UPDATE clients SET file_ok=0, image_ok=0");

	b &= db->Write("CREATE TABLE alert_scripts (id INTEGER PRIMARY KEY, "
		"name TEXT, script TEXT)");
	b &= db->Write("CREATE TABLE alert_script_params(id INTEGER PRIMARY KEY, "
		"script_id INTEGER REFERENCES alert_scripts(id) ON DELETE CASCADE, "
		"idx INTEGER, name TEXT, label TEXT, default_value TEXT, has_translation INTEGER DEFAULT 0, type TEXT)");

	IQuery *q = db->Prepare("INSERT INTO alert_scripts (id, name, script) VALUES (?, ?, '')");
	if (q != NULL)
	{
		q->Bind(1);
		q->Bind("Default");
		b &= q->Write();
	}
	else
	{
		b = false;
	}

	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (1, 0, 'alert_file_mult', 'alert_file_mult', '3', 1, 'num')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (1, 1, 'alert_image_mult', 'alert_image_mult', '3', 1, 'num')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (1, 2, 'alert_emails', 'alert_emails', '', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (1, 3, 'alert_important', 'alert_important', '0', 1, 'bool')");

	b &= db->Write("CREATE TABLE mail_queue (id INTEGER PRIMARY KEY, send_to TEXT, subject TEXT, message TEXT, next_try INTEGER, retry_count INTEGER DEFAULT 0)");

	return b;
}

bool upgrade55_56()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (1, 4, 'alert_mail_ok', 'alert_mail_ok', '1', 1, 'bool')");

	return b;
}

bool upgrade56_57()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("CREATE TABLE moved_clients (id INTEGER PRIMARY KEY, "
		"from_name TEXT, "
		"to_name TEXT)");

	b &= db->Write("CREATE INDEX moved_clients_from_idx ON moved_clients(from_name)");

	b &= db->Write("ALTER TABLE clients ADD uid TEXT");
	
	b &= db->Write("CREATE TABLE mounted_backup_images (id INTEGER PRIMARY KEY, "
		"backupid INTEGER REFERENCES backup_images(id) ON DELETE CASCADE, "
		"partition INTEGER,"
		"mounttime INTEGER)");

	return b;
}

const int64 script_id_default = 1;
const int64 script_id_pulseway = 100000;

bool upgrade57_58()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE alert_scripts ADD global_state BLOB");

	int64 scriptid = script_id_pulseway;
	b &= db->Write("INSERT INTO alert_scripts (id, name, script) VALUES (" + convert(scriptid) + ", 'Pulseway API', '')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 0, 'username', 'pulseway_username', '', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 1, 'password', 'pulseway_password', '', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 2, 'instance_id', 'pulseway_instance_id', '', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 3, 'instance_name', 'pulseway_instance_name', '', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 4, 'instance_group', 'pulseway_instance_group', 'Default', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 5, 'instance_description', 'pulseway_instance_description', '', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 6, 'refresh_interval_minutes', 'pulseway_refresh_interval_minutes', '15', 1, 'num')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 7, 'instance_notify_when_offline', 'pulseway_instance_notify_when_offline', '1', 1, 'bool')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 8, 'api_url', 'pulseway_api_url', 'https://api.pulseway.com/v2', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 9, 'alert_file_mult', 'alert_file_mult', '3', 1, 'num')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 10, 'alert_image_mult', 'alert_image_mult', '3', 1, 'num')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 11, 'alert_emails', 'alert_emails', '', 1, 'str')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 12, 'alert_important', 'alert_important', '0', 1, 'bool')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 13, 'priority', 'pulseway_priority', 'normal|low|elevated|critical', 1, 'choice')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(scriptid) + ", 14, 'alert_ok', 'pulseway_alert_ok', '1', 1, 'bool')");

	return b;
}

bool upgrade58_59()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(script_id_pulseway) + ", 15, 'alert_nag_interval', 'pulseway_alert_nag_interval', '0|h', 1, 'interval')");
	b &= db->Write("INSERT INTO alert_script_params (script_id, idx, name, label, default_value, has_translation, type) VALUES (" + convert(script_id_default) + ", 5, 'alert_nag_interval', 'alert_nag_interval', '0|h', 1, 'interval')");

	return b;
}

bool upgrade59_60()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;

	b &= db->Write("ALTER TABLE settings_db.settings ADD value_client TEXT");
	b &= db->Write("ALTER TABLE settings_db.settings ADD use INTEGER");
	b &= db->Write("UPDATE settings_db.settings SET use="+convert(c_use_value));

	IQuery* q_get = db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=? AND key=?");
	IQuery* q_update_use = db->Prepare("UPDATE settings_db.settings SET use=? WHERE clientid=?");
	IQuery* q_update_use_key = db->Prepare("UPDATE settings_db.settings SET use=? WHERE clientid=? AND key=?");

	db_results res_clients = db->Read("SELECT id FROM clients");
	for (size_t i = 0; i < res_clients.size(); ++i)
	{
		int clientid = watoi(res_clients[i]["id"]);
		
		q_get->Bind(clientid);
		q_get->Bind("overwrite");
		db_results res = q_get->Read();
		q_get->Reset();
		if (res.empty() || res[0]["value"] != "true")
		{
			q_update_use->Bind(c_use_group);
			q_update_use->Bind(clientid);
			q_update_use->Write();
			q_update_use->Reset();
		}

		q_get->Bind(clientid);
		q_get->Bind("client_set_settings");
		res = q_get->Read();
		q_get->Reset();
		if (!res.empty() && res[0]["value"] == "true")
		{
			std::vector<std::string> settings = getClientConfigurableSettingsList();
			for (size_t j = 0; j < settings.size(); ++j)
			{
				q_update_use_key->Bind(c_use_value_client);
				q_update_use_key->Bind(clientid);
				q_update_use_key->Bind(settings[j]);
				q_update_use_key->Write();
				q_update_use_key->Reset();
			}
		}
	}

	IQuery* q_update_use_key_only = db->Prepare("UPDATE settings_db.settings SET use=? WHERE key=?");
	std::vector<std::string> settings = getLocalizedSettingsList();
	for (size_t i = 0; i < settings.size(); ++i)
	{
		q_update_use_key_only->Bind(c_use_value);
		q_update_use_key_only->Bind(settings[i]);
	}

	return b;
}

std::string archiveSettingsParamStr(IDatabase* db, int clientid, std::string prefix)
{
	db_results res = db->Read("SELECT id, interval, interval_unit, length, length_unit, backup_types, clientid, archive_window, letters FROM settings_db.automatic_archival WHERE clientid=" + convert(clientid));

	IQuery* q_set_uuid = db->Prepare("UPDATE settings_db.automatic_archival SET uuid=? WHERE id=?");

	std::string ret;
	for (size_t i = 0; i < res.size(); ++i)
	{
		std::string uuid;
		uuid.resize(16);
		Server->secureRandomFill(&uuid[0], uuid.size());

		std::string idx = "_" + prefix + convert(i);
		if (!ret.empty()) ret += "&";
		ret += "every"+ idx +"=" + res[i]["interval"];
		ret += "&every_unit" + idx + "=" + res[i]["interval_unit"];
		ret += "&for" + idx + "=" + res[i]["interval_unit"];
		ret += "&for_unit" + idx + "=" + res[i]["interval_unit"];
		ret += "&backup_types" + idx + "=" + ServerAutomaticArchive::getBackupType(watoi(res[i]["backup_types"]));
		ret += "&archive_window" + idx + "=" + res[i]["archive_window"];
		ret += "&letters" + idx + "=" + res[i]["letters"];
		ret += "&uuid"+idx+"=" + bytesToHex(uuid);

		q_set_uuid->Bind(uuid);
		q_set_uuid->Bind(res[i]["id"]);
		q_set_uuid->Write();
		q_set_uuid->Reset();
	}

	return ret;
}

bool upgrade60_61()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if (!db->Write("ALTER TABLE settings_db.automatic_archival ADD uuid BLOB"))
	{
		return false;
	}

	bool b = true;
	IQuery* q_insert_setting = db->Prepare("INSERT INTO settings_db.settings (key, value, clientid, use) VALUES (?, ?, ?, ?)");

	db_results res_groups = db->Read("SELECT id FROM settings_db.si_client_groups");
	db_single_result res_global;
	res_global["id"] = "0";
	res_groups.push_back(res_global);

	for (size_t i = 0; i < res_groups.size(); ++i)
	{
		int group_id = watoi(res_groups[i]["id"]);

		q_insert_setting->Bind("archive");
		q_insert_setting->Bind(archiveSettingsParamStr(db, group_id*-1, group_id==0 ? "d" : "g"));
		q_insert_setting->Bind(group_id*-1);
		q_insert_setting->Bind(c_use_value);
		b &= q_insert_setting->Write();
		q_insert_setting->Reset();
	}

	IQuery* q_get = db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=? AND key=?");
	db_results res_clients = db->Read("SELECT id FROM clients");
	for (size_t i = 0; i < res_clients.size(); ++i)
	{
		int clientid = watoi(res_clients[i]["id"]);
		int r_clientid = clientid;
		int group_id = 0;
		q_get->Bind(clientid);
		q_get->Bind("group_id");
		db_results res = q_get->Read();
		q_get->Reset();
		if (!res.empty())
		{
			group_id = watoi(res[0]["value"])*-1;
		}

		q_get->Bind(clientid);
		q_get->Bind("overwrite");
		res = q_get->Read();
		q_get->Reset();
		if (res.empty() || res[0]["value"] != "true")
			r_clientid = group_id;

		q_get->Bind(clientid);
		q_get->Bind("overwrite_archive_settings");
		res = q_get->Read();
		q_get->Reset();
		if (res.empty() || res[0]["value"] != "true")
			r_clientid = group_id;

		std::string archive_params;
		int archive_use = c_use_value;
		if ( r_clientid <= 0 )
		{
			archive_use = c_use_group;
		}
		else
		{
			archive_params = archiveSettingsParamStr(db, clientid, "c");
		}

		q_insert_setting->Bind("archive");
		q_insert_setting->Bind(archive_params);
		q_insert_setting->Bind(clientid);
		q_insert_setting->Bind(archive_use);
		b &= q_insert_setting->Write();
		q_insert_setting->Reset();

		q_insert_setting->Bind("archive_update");
		q_insert_setting->Bind("1");
		q_insert_setting->Bind(clientid);
		q_insert_setting->Bind(0);
		b &= q_insert_setting->Write();
		q_insert_setting->Reset();
	}

	return b;
}

bool upgrade61_62()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	bool b= db->Write("ALTER TABLE clients ADD capa INTEGER DEFAULT 0");
	b &= db->Write("UPDATE clients SET capa=0 WHERE capa IS NULL");
	return b;
}

bool upgrade62_63()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	bool b = db->Write("ALTER TABLE clients ADD with_hashes INTEGER DEFAULT 0");
	b &= db->Write("UPDATE clients SET with_hashes=0 WHERE with_hashes IS NULL");
	return b;
}

bool upgrade63_64()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	bool b = db->Write("ALTER TABLE settings_db.settings ADD use_last_modified INTEGER");
	b &= db->Write("UPDATE settings_db.settings SET use_last_modified=" + convert(Server->getTimeSeconds()));
	b &= db->Write("UPDATE settings_db.settings SET value_client=value");
	return b;
}

bool upgrade64_65()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	db_results res_port = db->Read("SELECT value FROM settings_db.settings WHERE key='internet_server_port' AND clientid=0");

	bool b = true;
	if (!res_port.empty()
		&& res_port[0]["value"] != "55415")
	{
		b &= db->Write(std::string("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('internet_server_bind_port', '")+convert(watoi(res_port[0]["value"]))+"', 0)");
	}
	
	return b;
}

bool upgrade65_66()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool b = true;
	b &= db->Write("ALTER TABLE backups ADD incremental_ref INTEGER");
	b &= db->Write("ALTER TABLE backups ADD deletion_protected INTEGER");
	b &= db->Write("ALTER TABLE backups ADD delete_client_pending INTEGER");
	b &= db->Write("UPDATE backups SET incremental_ref=0");
	b &= db->Write("UPDATE backups SET deletion_protected=0");
	b &= db->Write("UPDATE backups SET delete_client_pending=0");
	b &= db->Write("ALTER TABLE clients ADD perm_uid BLOB");

	db_results res_clients = db->Read("SELECT id FROM clients");

	IQuery* q_update = db->Prepare("UPDATE clients SET perm_uid=? WHERE id=?");
	if (q_update == nullptr)
		return false;

	for (db_single_result res : res_clients)
	{
		std::vector<char> uid(16);
		Server->secureRandomFill(uid.data(), uid.size());

		q_update->Bind(uid.data(), uid.size());
		q_update->Bind(res["id"]);
		b &= q_update->Write();
		q_update->Reset();
	}

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
	
	int ver=watoi(res_v[0]["tvalue"]);
	int old_v;
	int max_v=66;
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

		if (!attach_other_dbs(db))
		{
			Server->Log("Attaching other databases failed", LL_ERROR);
			return;
		}
	}

	db_results cache_res;
	if(db->getEngineName()=="sqlite"
		&& ver>15)
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerBackupDao backup_dao(db);
		size_t update_stats_cachesize = 200 * 1024;
		ServerBackupDao::CondString setting = backup_dao.getClientSetting("update_stats_cachesize", 0);
		if (setting.exists
			&& !setting.value.empty())
		{
			update_stats_cachesize = watoi64(setting.value);
		}
		db->Write("PRAGMA cache_size = -"+convert(update_stats_cachesize));
	}
	
	IQuery *q_update=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='db_version'");
	do
	{
		if(ver<max_v)
		{
		    Server->Log("Upgrading database to version "+convert(ver+1), LL_WARNING);
		}
		if(ver>max_v)
		{
			Server->Log("Current UrBackup database version is "+convert(ver)+". This UrBackup"
				" server version only supports databases up to version "+convert(max_v)+"."
				" You need a newer UrBackup server version to work with this database.", LL_ERROR);
			exit(4);
		}
		db->BeginWriteTransaction();
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
				if(!upgrade35_36())
				{
					has_error=true;
				}
				++ver;
				break;
			case 36:
				if(!upgrade36_37())
				{
					has_error=true;
				}
				++ver;
				break;
			case 37:
				if(!update37_38())
				{
					has_error=true;
				}
				++ver;
				break;
			case 38:
				if(!update38_39())
				{
					has_error=true;
				}
				++ver;
				break;
			case 39:
				if(!update39_40())
				{
					has_error=true;
				}
				++ver;
				break;
			case 40:
				if(!upgrade40_41())
				{
					has_error=true;
				}
				++ver;
				break;
			case 41:
				if(!upgrade41_42())
				{
					has_error=true;
				}
				++ver;
				break;
			case 42:
				if(!upgrade42_43())
				{
					has_error=true;
				}
				++ver;
				break;
			case 43:
				if (!upgrade43_44())
				{
					has_error = true;
				}
				++ver;
				break;
			case 44:
				if (!upgrade44_45())
				{
					has_error = true;
				}
				++ver;
				break;
			case 45:
				if (!upgrade45_46())
				{
					has_error = true;
				}
				++ver;
			case 46:
				if (!upgrade46_47())
				{
					has_error = true;
				}
				++ver;
				break;
			case 47:
				if (!upgrade47_48())
				{
					has_error = true;
				}
				++ver;
				break;
			case 48:
				if (!upgrade48_49())
				{
					has_error = true;
				}
				++ver;
				break;
			case 49:
				if (!upgrade49_50())
				{
					has_error = true;
				}
				++ver;
				break;
			case 50:
				if (!upgrade50_51())
				{
					has_error = true;
				}
				++ver;
				break;
			case 51:
				if (!upgrade51_52())
				{
					has_error = true;
				}
				++ver;
				break;
			case 52:
				if (!upgrade52_53())
				{
					has_error = true;
				}
				++ver;
				break;
			case 53:
				if (!upgrade53_54())
				{
					has_error = true;
				}
				++ver;
				break;
			case 54:
				if (!upgrade54_55())
				{
					has_error = true;
				}
				++ver;
				break;
			case 55:
				if (!upgrade55_56())
				{
					has_error = true;
				}
				++ver;
				break;
			case 56:
				if (!upgrade56_57())
				{
					has_error = true;
				}
				++ver;
				break;
			case 57:
				if (!upgrade57_58())
				{
					has_error = true;
				}
				++ver;
				break;
			case 58:
				if (!upgrade58_59())
				{
					has_error = true;
				}
				++ver;
				break;
			case 59:
				if (!upgrade59_60())
				{
					has_error = true;
				}
				++ver;
				break;
			case 60:
				if (!upgrade60_61())
				{
					has_error = true;
				}
				++ver;
				break;
			case 61:
				if (!upgrade61_62())
				{
					has_error = true;
				}
				++ver;
				break;
			case 62:
				if (!upgrade62_63())
				{
					has_error = true;
				}
				++ver;
				break;
			case 63:
				if (!upgrade63_64())
				{
					has_error = true;
				}
				++ver;
				break;
			case 64:
				if (!upgrade64_65())
				{
					has_error = true;
				}
				++ver;
				break;
			case 65:
				if (!upgrade65_66())
				{
					has_error = true;
				}
				++ver;
				break;
			default:
				break;
		}

		if(has_error ||
			(Server->getFailBits() & IServer::FAIL_DATABASE_CORRUPTED) ||
			(Server->getFailBits() & IServer::FAIL_DATABASE_IOERR) ||
			(Server->getFailBits() & IServer::FAIL_DATABASE_FULL) )
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

			if(ver==36)
			{
				if(db->EndTransaction())
				{
					db->Write("PRAGMA page_size = 4096");

					db->Write("VACUUM");

					db->BeginWriteTransaction();
				}
				else
				{
					Server->Log("Upgrading database failed. Ending transaction failed. Shutting down server.", LL_ERROR);
					exit(5);
				}
				
			}

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
		detach_other_dbs(db);
		Server->Log("Done.", LL_WARNING);
	}

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+cache_res[0]["cache_size"]);
		db->freeMemory();
	}
	
	Server->clearDatabases(Server->getThreadID());
}
