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

#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
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

#define LoadActions LoadActions_urbackupclient
#define UnloadActions UnloadActions_urbackupclient
#endif

#include "../Interface/Action.h"
#include "../Interface/Database.h"
#include "../Interface/SessionMgr.h"
#include "../Interface/Pipe.h"
#include "../Interface/Query.h"
#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../Interface/SettingsReader.h"

#include "../fsimageplugin/IFSImageFactory.h"
#include "../cryptoplugin/ICryptoFactory.h"

#include "database.h"
#include "tokens.h"

#include "ClientService.h"
#include "client.h"
#include "../stringtools.h"
#include "ServerIdentityMgr.h"
#include "../urbackupcommon/os_functions.h"
#ifdef _WIN32
#include "DirectoryWatcherThread.h"
#include "win_sysvol.h"
#endif
#include "InternetClient.h"
#include <stdlib.h>
#include "file_permissions.h"

#include "../urbackupcommon/chunk_hasher.h"
#include "../urbackupcommon/WalCheckpointThread.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.h"

namespace
{
#include "backup_client_db.h"

	std::string get_backup_client_db_data()
	{
		size_t out_len;
		void* cdata = tinfl_decompress_mem_to_heap(backup_client_db_z, backup_client_db_z_len, &out_len, TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_COMPUTE_ADLER32);
		if (cdata == NULL)
		{
			return std::string();
		}

		std::string ret(reinterpret_cast<char*>(cdata), reinterpret_cast<char*>(cdata) + out_len);
		mz_free(cdata);
		return ret;
	}
}


PLUGIN_ID filesrv_pluginid;
IFSImageFactory *image_fak;
ICryptoFactory *crypto_fak;
std::string server_identity;
std::string server_token;

const unsigned short default_urbackup_serviceport=35623;

void init_mutex1(void);
bool testEscape(void);
void do_restore(void);
void restore_wizard(void);
void upgrade(void);
bool upgrade_client(void);
void parse_devnum_test();

std::string lang="en";
std::string time_format_str_de="%d.%m.%Y %H:%M";
std::string time_format_str="%d.%m.%Y %H:%M";

#ifdef _WIN32
const std::string pw_file="pw.txt";
const std::string pw_change_file="pw_change.txt";
const std::string new_file="new.txt";
#else
const std::string pw_file="urbackup/pw.txt";
const std::string pw_change_file="urbackup/pw_change.txt";
const std::string new_file="urbackup/new.txt";
#endif

THREADPOOL_TICKET indexthread_ticket;
THREADPOOL_TICKET internetclient_ticket;

namespace
{
	int64 roundUp(int64 numToRound, int64 multiple)
	{
		return ((numToRound + multiple - 1) / multiple) * multiple;
	}

	int64 roundDown(int64 numToRound, int64 multiple)
	{
		return ((numToRound / multiple) * multiple);
	}

	const int64 dm_block_size = 1 * 1024 * 1024;

	void print_ext(const IFsFile::SFileExtent& ext, const std::string& file_dm_block_dev, int64& lin_off)
	{
		int64 start = roundUp(ext.volume_offset, dm_block_size);
		int64 end = roundDown(ext.volume_offset + ext.size, dm_block_size);

		if (end - start >= dm_block_size)
		{
			int64 bcount = (end - start) / 512;
			std::cout << lin_off << " " << bcount << " linear " << file_dm_block_dev << " " << (start / 512) << std::endl;
			lin_off += bcount;
		}
	}

	void do_print_dm_file_extents(const std::string& fn)
	{
		std::auto_ptr<IFsFile> f(Server->openFile(fn, MODE_READ));
		if (f.get() == NULL)
		{
			std::cerr << "Error opening file " << fn << " " << os_last_error_str() << std::endl;
			exit(1);
		}

#ifdef FS_IOC_FSSETXATTR
		fsxattr attr = {};
#ifdef FS_XFLAG_IMMUTABLE
		attr.fsx_xflags |= FS_XFLAG_IMMUTABLE;
#endif
		ioctl(f->getOsHandle(), FS_IOC_FSSETXATTR, &attr);
#ifdef FS_XFLAG_NODUMP
		attr.fsx_xflags |= FS_XFLAG_NODUMP;
#endif
		ioctl(f->getOsHandle(), FS_IOC_FSSETXATTR, &attr);
#ifdef FS_XFLAG_NODEFRAG
		attr.fsx_xflags |= FS_XFLAG_NODEFRAG;
#endif
		ioctl(f->getOsHandle(), FS_IOC_FSSETXATTR, &attr);
#endif

		std::string file_dm_block_dev = Server->getServerParameter("file-dm-block-dev");


		int64 lin_off = 0;
		int64 pos = 0;
		bool more_data = true;
		
		IFsFile::SFileExtent last_ext;
		while (more_data)
		{
			std::vector<IFsFile::SFileExtent> exts = f->getFileExtents(pos, 0, more_data);

			for (size_t i = 0; i < exts.size(); ++i)
			{
				if (last_ext.offset == -1)
				{
					last_ext = exts[i];
				}
				else if (last_ext.offset + last_ext.size != exts[i].offset
					|| last_ext.volume_offset + last_ext.size != exts[i].volume_offset)
				{
					print_ext(last_ext, file_dm_block_dev, lin_off);
					last_ext = exts[i];
				}
				else
				{
					last_ext.size += exts[i].size;
				}

				pos = (std::max)(exts[i].offset + exts[i].size, pos);
			}
		}

		if (last_ext.offset != -1)
		{
			print_ext(last_ext, file_dm_block_dev, lin_off);
		}

		if ((lin_off * 512) < (f->Size() * 3) / 4)
		{
			std::cerr << "ERROR: Only " << PrettyPrintBytes(lin_off * 512) << " of " << PrettyPrintBytes(f->Size()) << " was usable" << std::endl;
			exit(2);
		}

		exit(0);
	}
}


DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

#ifdef _DEBUG
	parse_devnum_test();
#endif
	
	std::string rmtest=Server->getServerParameter("rmtest");
	if(!rmtest.empty())
	{
		os_remove_nonempty_dir(rmtest);
		return;
	}

	std::string ssltest = Server->getServerParameter("ssltest");
	if (!ssltest.empty())
	{
		IPipe* p = Server->ConnectSslStream("google.de", 443, 10000);

		p->Write("GET / HTTP/1.0\r\n\r\n");

		std::string ret;
		while (p->Read(&ret) > 0)
		{
			Server->Log("SSL_OUT: " + ret);
		}
		return;
	}

	std::string print_dm_file_extents = Server->getServerParameter("print-dm-file-extents");
	if (!print_dm_file_extents.empty())
	{
		do_print_dm_file_extents(print_dm_file_extents);
		return;
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

	//writeZeroblockdata();

#ifdef _WIN32
	wchar_t tmpp[MAX_PATH];
	DWORD l;
	if((l=GetTempPathW(MAX_PATH, tmpp))==0 || l>MAX_PATH )
	{
		wcscpy_s(tmpp,L"C:\\");
	}

	std::string w_tmp=Server->ConvertFromWchar(tmpp);

	if(!w_tmp.empty() && w_tmp[w_tmp.size()-1]=='\\')
	{
		w_tmp.erase(w_tmp.size()-1, 1);
	}

	os_remove_nonempty_dir(w_tmp+os_file_sep()+"urbackup_client_tmp");
	if(!os_create_dir(w_tmp+os_file_sep()+"urbackup_client_tmp"))
	{
		Server->wait(5000);
		os_create_dir(w_tmp+os_file_sep()+"urbackup_client_tmp");
	}
	Server->setTemporaryDirectory(w_tmp+os_file_sep()+"urbackup_client_tmp");
#endif

	str_map params;
	crypto_fak = (ICryptoFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("cryptoplugin", params));
	if (crypto_fak == NULL)
	{
		Server->Log("Error loading Cryptoplugin", LL_ERROR);
	}

	{
		str_map params;
		image_fak = (IFSImageFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("fsimageplugin", params));
		if (image_fak == NULL)
		{
			Server->Log("Error loading fsimageplugin", LL_ERROR);
		}
	}

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

#ifdef _WIN32
	std::string restore=Server->getServerParameter("allow_restore");
	if(restore=="default" && !Server->fileExists(Server->getServerWorkingDir()+"\\UrBackupClient.exe"))
	{
		//Client without tray icon
		Server->setServerParameter("allow_restore", "server-confirms");
	}
	else if(restore.empty())
	{
		Server->setServerParameter("allow_restore", "default");
	}

	Server->setSocketWindowSizes(3 * 1024 * 1024, 128 * 1024);
#endif

	init_chunk_hasher();

	ServerIdentityMgr::init_mutex();
#ifdef _WIN32
	DirectoryWatcherThread::init_mutex();
#endif

	if(getFile(pw_file).size()<5)
	{
		writestring(Server->getSessionMgr()->GenerateSessionIDWithUser("",""), pw_file);
	}
	if(getFile(pw_change_file).size()<5)
	{
		write_file_only_admin(Server->getSessionMgr()->GenerateSessionIDWithUser("",""), pw_change_file);
	}

	if( !FileExists("urbackup/backup_client.db") )
	{
		writestring(get_backup_client_db_data(), "urbackup/backup_client.db");
	}

#ifndef _DEBUG
	change_file_permissions_admin_only("urbackup/backup_client.db");
#endif

	str_map db_params;
	db_params["wal_autocheckpoint"] = "0";

	if(! Server->openDatabase("urbackup/backup_client.db", URBACKUPDB_CLIENT, db_params) )
	{
		Server->Log("Couldn't open Database backup_client.db", LL_ERROR);
		exit(1);
	}

	WalCheckpointThread::init_mutex();

	WalCheckpointThread* wal_checkpoint_thread = new WalCheckpointThread(10 * 1024 * 1024, 200 * 1024 * 1024,
		"urbackup" + os_file_sep() + "backup_client.db", URBACKUPDB_CLIENT);
	Server->createThread(wal_checkpoint_thread, "db checkpoint");

	if (!upgrade_client())
	{
		Server->Log("Upgrading client database failed. Startup failed.", LL_ERROR);
		exit(1);
	}

#ifdef _WIN32
	if( !FileExists("prefilebackup.bat") && FileExists("prefilebackup_new.bat") )
	{
		copy_file("prefilebackup_new.bat", "prefilebackup.bat");
		Server->deleteFile("prefilebackup_new.bat");
	}
#endif

#ifdef _WIN32
#define INITIAL_SETTINGS_PREFIX ""
#else
#define INITIAL_SETTINGS_PREFIX "urbackup/"
#endif

	if( !FileExists("urbackup/data/settings.cfg") && FileExists(INITIAL_SETTINGS_PREFIX "initial_settings.cfg") )
	{
		std::auto_ptr<ISettingsReader> settings_reader(Server->createFileSettingsReader(INITIAL_SETTINGS_PREFIX "initial_settings.cfg"));
		std::string access_keys;
		std::string client_access_keys;
		if (settings_reader->getValue("access_keys", &access_keys) && !access_keys.empty())
		{
			ClientDAO cd(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT));
			std::vector<std::string> toks;
			Tokenize(access_keys, toks, ";");
			for (size_t i = 0; i < toks.size(); ++i)
			{
				std::string username = getuntil(":", toks[i]);
				std::string access_key = getafter(":", toks[i]);
				if (username.size() > 1
					&& !access_key.empty())
				{
					bool is_token = username[0] == 't';
					username = username.substr(1);

					if (!is_token)
					{
						bool is_user = username[0] == 'u';

						os_create_dir(tokens::tokens_path);

						std::string token_path;
						if (is_user)
						{
							token_path = std::string(tokens::tokens_path) + os_file_sep() + "user_" + bytesToHex(username);
						}
						else
						{
							token_path = std::string(tokens::tokens_path) + os_file_sep() + "group_" + bytesToHex(username);
						}

						tokens::write_token(tokens::get_hostname(), is_user, username, token_path, cd, access_key);
					}
					else
					{
						client_access_keys += "key." + username + "=" +
							access_key + "\n";

						client_access_keys += "key_age." + username + "=" +
							convert(Server->getTimeSeconds()) + "\n";
					}
				}				
			}

			if (!client_access_keys.empty()
				&& !FileExists("urbackup/access_keys.properties"))
			{
				write_file_only_admin(client_access_keys, "urbackup/access_keys.properties");
			}
		}

		copy_file(INITIAL_SETTINGS_PREFIX "initial_settings.cfg", "urbackup/data/settings.cfg");
		Server->deleteFile(INITIAL_SETTINGS_PREFIX "initial_settings.cfg");
	}

#ifndef _DEBUG
	if(FileExists("urbackup/data/settings.cfg"))
	{
		change_file_permissions_admin_only("urbackup/data/settings.cfg");
	}

	if(FileExists("urbackup/data/filelist.ub"))
	{
		change_file_permissions_admin_only("urbackup/data/filelist.ub");
	}

#ifdef _WIN32
	change_file_permissions_admin_only("urbackup");
#endif
	change_file_permissions_admin_only("urbackup/data");

	if(FileExists("debug.log"))
	{
		change_file_permissions_admin_only("debug.log");
	}
#endif

	bool do_leak_check=(Server->getServerParameter("leak_check")=="true");

	ClientConnector::init_mutex();
	unsigned short urbackup_serviceport = default_urbackup_serviceport;
	if(!Server->getServerParameter("urbackup_serviceport").empty())
	{
		urbackup_serviceport = static_cast<unsigned short>(atoi(Server->getServerParameter("urbackup_serviceport").c_str()));
	}

	IServer::BindTarget serviceport_bind_target = IServer::BindTarget_All;
	if (Server->getServerParameter("internet_only_mode") == "true")
	{
		serviceport_bind_target = IServer::BindTarget_Localhost;
	}

	Server->StartCustomStreamService(new ClientService(), "urbackupserver", urbackup_serviceport, -1, serviceport_bind_target);

	filesrv_pluginid=Server->StartPlugin("fileserv", params);

	IndexThread *it=new IndexThread();
	if(!do_leak_check)
	{
		Server->createThread(it, "file indexing");
	}
	else
	{
		indexthread_ticket=Server->getThreadPool()->execute(it, "file indexing");
	}

	internetclient_ticket=InternetClient::start(do_leak_check);

#ifdef _WIN32
	cacheVolumes();
#endif

	Server->Log("Started UrBackupClient Backend...", LL_INFO);
	Server->wait(1000);
}

DLLEXPORT void UnloadActions(void)
{
	if(Server->getServerParameter("leak_check")=="true")
	{
		IndexThread::doStop();
		Server->getThreadPool()->waitFor(indexthread_ticket);
		ServerIdentityMgr::destroy_mutex();

		InternetClient::stop(internetclient_ticket);

		ClientConnector::destroy_mutex();

		Server->destroyAllDatabases();
	}
}

#ifdef STATIC_PLUGIN
namespace
{
	static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 10);
}
#endif

void upgrade_client1_2(IDatabase *db)
{
	db->Write("ALTER TABLE shadowcopies ADD vol TEXT");
}

void upgrade_client2_3(IDatabase *db)
{
	db->Write("ALTER TABLE shadowcopies ADD refs INTEGER");
	db->Write("ALTER TABLE shadowcopies ADD starttime DATE");
}

void upgrade_client3_4(IDatabase *db)
{
	db->Write("ALTER TABLE shadowcopies ADD starttoken TEXT");
}

void upgrade_client4_5(IDatabase *db)
{
	db->Write("DROP TABLE mdirs");
	db->Write("CREATE TABLE mdirs ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT )");
	db->Write("DROP TABLE mdirs_backup");
	db->Write("CREATE TABLE mdirs_backup ( id INTEGER, name TEXT )");
	db->Write("CREATE TABLE mfiles ( dir_id INTEGER, name TEXT );");
	db->Write("CREATE TABLE mfiles_backup ( dir_id INTEGER, name TEXT );");
	db->Write("CREATE INDEX IF NOT EXISTS mfiles_backup_idx ON mfiles_backup( dir_id ASC )");
	db->Write("DELETE FROM files");	
}

void upgrade_client5_6(IDatabase *db)
{
	db->Write("DELETE FROM files");
}

void upgrade_client6_7(IDatabase *db)
{
	db->Write("DELETE FROM files");
}

void upgrade_client7_8(IDatabase *db)
{
	db->Write("DELETE FROM files");
}

void upgrade_client8_9(IDatabase *db)
{
	db->Write("DELETE FROM files");
}

void upgrade_client9_10(IDatabase *db)
{
	db->Write("CREATE TABLE filehashes (name TEXT, filesize INTEGER, modifytime INTEGER, hashdata BLOB)");
	db->Write("CREATE UNIQUE INDEX filehashes_idx ON filehashes (name ASC)");
}

void update_client10_11(IDatabase *db)
{
	db->Write("DROP TABLE filehashes");
	db->Write("DROP INDEX IF EXISTS filehashes_idx");
	db->Write("DELETE FROM files");
}

void update_client11_12(IDatabase *db)
{
	db_results res = db->Read("SELECT MAX(tvalue) AS c FROM misc WHERE tkey='last_backup_filetime'");
	db->Write("DELETE FROM misc WHERE tkey='last_backup_filetime'");
	if(!res.empty())
	{
		db->Write("INSERT INTO misc (tkey, tvalue) VALUES ('last_backup_filetime', '"+res[0]["c"]+"')");
	}

	db_results misc = db->Read("SELECT tkey, tvalue FROM misc");
	db->Write("DROP TABLE misc");
	db->Write("CREATE TABLE misc (tkey TEXT UNIQUE, tvalue TEXT)");

	IQuery* q_insert = db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES (?, ?)");
	for(size_t i=0;i<misc.size();++i)
	{
		q_insert->Bind(misc[i]["tkey"]);
		q_insert->Bind(misc[i]["tvalue"]);
		q_insert->Write();
		q_insert->Reset();
	}
}

void update_client12_13(IDatabase *db)
{
	db->Write("ALTER TABLE backupdirs ADD optional INTEGER");
	db->Write("UPDATE backupdirs SET optional=0 WHERE optional IS NULL");
}

void update_client13_14(IDatabase *db)
{
	db->Write("ALTER TABLE journal_data ADD frn_high INTEGER");
	db->Write("ALTER TABLE journal_data ADD parent_frn_high INTEGER");
	db->Write("ALTER TABLE map_frn ADD frn_high INTEGER");
	db->Write("ALTER TABLE map_frn ADD pid_high INTEGER");
	db->Write("DELETE FROM journal_data");
	db->Write("DELETE FROM map_frn");
	db->Write("DELETE FROM journal_ids");
	db->Write("DROP INDEX IF EXISTS frn_index");
	db->Write("DROP INDEX IF EXISTS frn_pid_index");
	db->Write("CREATE INDEX IF NOT EXISTS frn_index ON map_frn( frn ASC, frn_high ASC )");
	db->Write("CREATE INDEX IF NOT EXISTS frn_pid_index ON map_frn( pid ASC, pid_high ASC )");
}

void update_client14_15(IDatabase *db)
{
	db->Write("DELETE FROM journal_data");
	db->Write("DELETE FROM map_frn");
	db->Write("DELETE FROM journal_ids");
}

void update_client15_16(IDatabase *db)
{
	db->Write("DELETE FROM files");
}

void update_client16_17(IDatabase *db)
{
	db->Write("CREATE TABLE continuous_watch_queue (id INTEGER PRIMARY KEY, data_size INTEGER, data BLOB)");
	db->Write("ALTER TABLE backupdirs ADD tgroup INTEGER");
	db->Write("UPDATE backupdirs SET tgroup=0 WHERE tgroup IS NULL");
	db->Write("CREATE TABLE fileaccess_tokens (id INTEGER PRIMARY KEY, accountname TEXT, token TEXT, is_user INTEGER)");
	db->Write("CREATE UNIQUE INDEX fileaccess_tokens_unique ON fileaccess_tokens(accountname, is_user)");
}

void update_client17_18(IDatabase* db)
{
	db->Write("DROP TABLE mfiles");
	db->Write("DROP TABLE mfiles_backup");
}

void update_client18_19(IDatabase* db)
{
	db->Write("CREATE TABLE token_group_memberships (uid INTEGER REFERENCES fileaccess_tokens(id) ON DELETE CASCADE, gid INTEGER REFERENCES fileaccess_tokens(id) ON DELETE CASCADE)");
	db->Write("CREATE UNIQUE INDEX token_group_memberships_unique ON token_group_memberships(uid, gid)");
}

void update_client19_20(IDatabase* db)
{
	db->Write("ALTER TABLE backupdirs ADD symlinked INTEGER DEFAULT 0");
	db->Write("UPDATE backupdirs SET symlinked=0 WHERE symlinked IS NULL");
}

void update_client20_21(IDatabase* db)
{
	db->Write("ALTER TABLE shadowcopies ADD clientsubname TEXT");
	db->Write("UPDATE shadowcopies SET clientsubname='' WHERE clientsubname IS NULL");
}

void update_client21_22(IDatabase* db)
{
	db->Write("ALTER TABLE files ADD tgroup INTEGER");
	db->Write("UPDATE files SET tgroup=0 WHERE tgroup IS NULL");
}

void update_client22_23(IDatabase* db)
{
	db->Write("DROP INDEX files_idx");
	db->Write("DELETE FROM files");
	db->Write("CREATE UNIQUE INDEX files_idx ON files (name ASC, tgroup)");
	db->Write("UPDATE backupdirs SET optional=38 WHERE optional=0");
}

void update_client23_24(IDatabase* db)
{
	db->Write("ALTER TABLE backupdirs ADD reset_keep INTEGER DEFAULT 0");
	db->Write("UPDATE backupdirs SET reset_keep=0 WHERE reset_keep IS NULL");
	db->Write("CREATE TABLE virtual_client_group_offsets (id INTEGER PRIMARY KEY, virtual_client TEXT UNIQUE, group_offset INTEGER)");
}

void update_client24_25(IDatabase* db)
{
	db->Write("CREATE TABLE hardlinks (vol TEXT, frn_high INTEGER, frn_low INTEGER, parent_frn_high INTEGER, parent_frn_low INTEGER,"
		"PRIMARY KEY(vol, frn_high, frn_low, parent_frn_high, parent_frn_low) ) WITHOUT ROWID");
}

void update_client25_26(IDatabase* db)
{
#ifdef _WIN32
	if (os_get_file_type("urbctctl.exe")!=0)
	{
		system("urbctctl.exe reset all");
	}
#endif
}

void update_client26_27(IDatabase* db)
{
	db->Write("ALTER TABLE files ADD generation INTEGER DEFAULT 0");
}

void update_client27_28(IDatabase* db)
{
	ClientConnector::updateDefaultDirsSetting(db, true, 0);
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
	int ver=watoi(res_v[0]["tvalue"]);
	int old_v;
	int max_v = 28;

	if (ver > max_v)
	{
		Server->Log("Client database version (" + convert(ver) + ") bigger than latest known version (" + convert(max_v) + ")."
			" This client version cannot run with this database", LL_ERROR);
		return false;
	}

	db->Write("PRAGMA journal_mode=WAL");

	if (ver == max_v)
	{
		return true;
	}
	
	db->BeginWriteTransaction();

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
			case 3:
				upgrade_client3_4(db);
				++ver;
				break;
			case 4:
				upgrade_client4_5(db);
				++ver;
				break;
			case 5:
				upgrade_client5_6(db);
				++ver;
				break;
			case 6:
				upgrade_client6_7(db);
				++ver;
				break;
			case 7:
				upgrade_client7_8(db);
				++ver;
				break;
			case 8:
				upgrade_client8_9(db);
				++ver;
				break;
			case 9:
				upgrade_client9_10(db);
				++ver;
				break;
			case 10:
				update_client10_11(db);
				++ver;
				break;
			case 11:
				update_client11_12(db);
				++ver;
				break;
			case 12:
				update_client12_13(db);
				++ver;
				break;
			case 13:
				update_client13_14(db);
				++ver;
				break;
			case 14:
				update_client14_15(db);
				++ver;
				break;
			case 15:
				update_client15_16(db);
				++ver;
				break;
			case 16:
				update_client16_17(db);
				++ver;
				break;
			case 17:
				update_client17_18(db);
				++ver;
				break;
			case 18:
				update_client18_19(db);
				++ver;
				break;
			case 19:
				update_client19_20(db);
				++ver;
				break;
			case 20:
				update_client20_21(db);
				++ver;
				break;
			case 21:
				update_client21_22(db);
				++ver;
				break;
			case 22:
				update_client22_23(db);
				++ver;
				break;
			case 23:
				update_client23_24(db);
				++ver;
				break;
			case 24:
				update_client24_25(db);
				++ver;
				break;
			case 25:
				update_client25_26(db);
				++ver;
				break;
			case 26:
				update_client26_27(db);
				++ver;
				break;
			case 27:
				update_client27_28(db);
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

	db->EndTransaction();
	
	db->destroyAllQueries();
	return true;
}
