/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2017 Martin Raiber
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
#include "copy_storage.h"
#include "dao/ServerBackupDao.h"
#include "dao/ServerCleanupDao.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#ifdef NO_EMBEDDED_LMDB
#include <lmdb.h>
#else
#include "lmdb/lmdb.h"
#endif
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "database.h"
#include "server_dir_links.h"
#include "server_log.h"
#include "server_status.h"

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#else
#include <Windows.h>
#endif

#if defined(_WIN32) || defined(__APPLE__) || defined(__FreeBSD__)
#define stat64 stat
#endif

namespace
{
	std::string getBackupfolder(IDatabase *db)
	{
		db_results res = db->Read("SELECT value FROM settings_db.settings WHERE key='backupfolder' AND clientid=0");
		if (!res.empty())
		{
			return res[0]["value"];
		}
		return std::string();
	}

	void deleteIncomplete(const std::string& dest_folder)
	{
		std::vector<SFile> clients = getFiles(dest_folder);

		for (size_t i = 0; i < clients.size(); ++i)
		{
			if (!clients[i].isdir)
				continue;

			std::vector<SFile> backups = getFiles(dest_folder + os_file_sep() + clients[i].name);

			for (size_t j = 0; j < backups.size(); ++j)
			{
				if (backups[j].name.find("_incomplete") != std::string::npos)
				{
					if (backups[j].isdir)
					{
						Server->Log("Deleting incomplete folder \"" + dest_folder + os_file_sep() + clients[i].name + os_file_sep() + backups[j].name + "\"...");
						os_remove_nonempty_dir(os_file_prefix(dest_folder + os_file_sep() + clients[i].name + os_file_sep() + backups[j].name));
					}
					else
					{
						Server->Log("Deleting incomplete file \"" + dest_folder + os_file_sep() + clients[i].name + os_file_sep() + backups[j].name+"\"...");
						Server->deleteFile(os_file_prefix(dest_folder + os_file_sep() + clients[i].name + os_file_sep() + backups[j].name));
					}
				}

				if (backups[j].name == ".directory_pool")
				{
					std::vector<SFile> pool1 = getFiles(dest_folder + os_file_sep() + clients[i].name+os_file_sep()+ backups[j].name);

					for (size_t k = 0; k < pool1.size(); ++k)
					{
						std::vector<SFile> pool2 = getFiles(dest_folder + os_file_sep() + clients[i].name + os_file_sep() + backups[j].name
																+os_file_sep()+pool1[k].name);

						for (size_t l = 0; l < pool2.size(); ++l)
						{
							if (pool2[l].name.find("_incomplete") != std::string::npos)
							{
								Server->Log("Deleting incomplete symlink pool folder \"" + dest_folder + os_file_sep() + clients[i].name + os_file_sep() + backups[j].name
									+ os_file_sep() + pool1[k].name + os_file_sep() + pool2[l].name + "\"...");
								os_remove_nonempty_dir(os_file_prefix(dest_folder + os_file_sep() + clients[i].name + os_file_sep() + backups[j].name
									+ os_file_sep() + pool1[k].name + os_file_sep() + pool2[l].name));
							}
						}
					}
				}
			}
		}
	}

	bool openMdb(const std::string& dst_folder, MDB_env*& env)
	{
		if (!os_directory_exists(dst_folder + os_file_sep() + "inode_db"))
		{
			if (!os_create_dir(dst_folder + os_file_sep() + "inode_db"))
			{
				Server->Log("Error creating directory \"" + dst_folder + os_file_sep() + "inode_db" + "\". " + os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		int rc = mdb_env_create(&env);
		if(rc!=0)
		{
			Server->Log("Error creating mdb env (" + (std::string)mdb_strerror(rc)+")", LL_ERROR);
			return false;
		}

		rc = mdb_env_set_maxreaders(env, 4094);

		if (rc)
		{
			Server->Log("LMDB: Failed to set max readers (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
			mdb_env_close(env);
			return false;
		}

		uint64 envsize = 1ULL * 1024 * 1024 * 1024 * 1024; //1TB

		int64 freespace = os_free_space(dst_folder);
		if (freespace > 0
			&& static_cast<uint64>(freespace)<envsize)
		{
			envsize = static_cast<uint64>(0.9*freespace);
		}

		rc = mdb_env_set_mapsize(env, envsize);

		if (rc)
		{
			Server->Log("LMDB: Failed to set map size (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
			mdb_env_close(env);
			return false;
		}

		unsigned int flags = MDB_NOSUBDIR | MDB_NOMETASYNC;
		rc = mdb_env_open(env, (dst_folder + os_file_sep() + "inode_db" + os_file_sep() + "inode_db.lmdb").c_str(), flags, 0664);

		if (rc)
		{
			Server->Log("LMDB: Failed to open LMDB database file (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
			mdb_env_close(env);
			return false;
		}

		return true;
	}

	class ScopedCloseLmdbEnv
	{
	public:
		ScopedCloseLmdbEnv(MDB_env* env)
			: env(env)
		{}
		~ScopedCloseLmdbEnv() {
			mdb_env_close(env);
		}

	private:
		MDB_env* env;
	};

	bool getFileInode(const std::string& fpath, int64& inode)
	{
#ifndef _WIN32
		struct stat64 statbuf;
		int rc = stat64(fpath.c_str(), &statbuf);

		if (rc != 0)
		{
			Server->Log("Error with stat of " + fpath + " errorcode: " + convert(errno), LL_ERROR);
			return false;
		}

		inode = statbuf.st_ino;
		return true;
#else
		HANDLE hFile = CreateFileW(Server->ConvertToWchar(os_file_prefix(fpath)).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			Server->Log("Error opening file " + fpath + ". "+os_last_error_str(), LL_ERROR);
			return false;
		}

		BY_HANDLE_FILE_INFORMATION fileInformation;
		BOOL b = GetFileInformationByHandle(hFile, &fileInformation);
		CloseHandle(hFile);
		if(!b)
		{
			Server->Log("Error getting file information of " + fpath + ". " + os_last_error_str(), LL_ERROR);
			return false;
		}

		LARGE_INTEGER li;
		li.HighPart = fileInformation.nFileIndexHigh;
		li.LowPart = fileInformation.nFileIndexLow;

		inode = li.QuadPart;

		return true;
#endif
	}

	std::string remove_incomplete_folder(const std::string& path)
	{
		std::vector<std::string> toks;
		Tokenize(path, toks, os_file_sep());

		std::string ret;
		std::string inc_str = "_incomplete";
		for (size_t i = 0; i < toks.size(); ++i)
		{
			if (i != 0)
			{
				ret += os_file_sep();
			}
			if (toks[i].size() > inc_str.size() + 1
				&& toks[i].find(inc_str) == toks[i].size() - inc_str.size())
			{
				ret += toks[i].substr(0, toks[i].size() - inc_str.size());
			}
			else
			{
				ret += toks[i];
			}
		}

		return ret;
	}

	bool copy_filebackup(const std::string& src_folder, const std::string& dst_folder, const std::string& pool_dest, MDB_txn* txn, MDB_dbi dbi, bool ignore_copy_errors)
	{
		bool has_error = false;
		std::vector<SFile> files = getFiles(os_file_prefix(src_folder), &has_error);

		if (has_error)
		{
			Server->Log("Error listing files is directory \"" + src_folder + "\". " + os_last_error_str(), LL_ERROR);
			return false;
		}

		for (size_t i = 0; i < files.size(); ++i)
		{
			if (files[i].issym)
			{
				std::string sym_target;
				if (!os_get_symlink_target(os_file_prefix(src_folder + os_file_sep() + files[i].name), sym_target))
				{
					Server->Log("Error getting symlink target of \"" + src_folder + os_file_sep() + files[i].name + "\". " + os_last_error_str(), LL_ERROR);
					return false;
				}

				std::string directory_pool = ExtractFileName(ExtractFilePath(ExtractFilePath(sym_target, os_file_sep()), os_file_sep()), os_file_sep());

				if (directory_pool == ".directory_pool")
				{
					std::string pool_path = pool_dest + os_file_sep() + ExtractFileName(ExtractFilePath(sym_target, os_file_sep()), os_file_sep())
						+ os_file_sep() + ExtractFileName(sym_target, os_file_sep());
					if (!os_directory_exists(pool_path)
						&& !os_directory_exists(pool_path + "_incomplete"))
					{
						if (!os_create_dir_recursive(pool_path + "_incomplete"))
						{
							Server->Log("Error creating pool path \"" + pool_path + "_incomplete\". "+os_last_error_str(), LL_ERROR);
							return false;
						}

						if (!os_directory_exists(os_file_prefix(sym_target)))
						{
							Server->Log("Directory pool path target \"" + sym_target + "\" does not exist", LL_ERROR);
							if (!ignore_copy_errors)
							{
								return false;
							}
						}
						else
						{
							if (!copy_filebackup(sym_target, pool_path + "_incomplete", pool_dest, txn, dbi, ignore_copy_errors))
							{
								return false;
							}

							//os_sync(pool_path + "_incomplete");

							if (!os_rename_file(pool_path + "_incomplete", pool_path, NULL))
							{
								Server->Log("Error renaming to \"" + pool_path + "\". " + os_last_error_str(), LL_ERROR);
								return false;
							}
						}
					}
				}
				
				bool isdir = files[i].isdir;
				if (!os_link_symbolic(sym_target, os_file_prefix(dst_folder + os_file_sep() + files[i].name), NULL, &isdir))
				{
					Server->Log("Error creating symlink at \"" + dst_folder + os_file_sep() + files[i].name + "\". " + os_last_error_str(), LL_ERROR);
					return false;
				}
			}
			else if (files[i].isdir)
			{
				if (!os_create_dir(os_file_prefix(dst_folder + os_file_sep() + files[i].name)))
				{
					Server->Log("Error creating folder \"" + dst_folder + os_file_sep() + files[i].name + "\". "+os_last_error_str(), LL_ERROR);
					return false;
				}

				if (!copy_filebackup(src_folder + os_file_sep() + files[i].name, dst_folder + os_file_sep() + files[i].name, pool_dest, txn, dbi, ignore_copy_errors))
				{
					return false;
				}
			}
			else
			{
				int64 inode;
				if (!getFileInode(src_folder + os_file_sep() + files[i].name, inode))
				{
					Server->Log("Error getting inode of file " + src_folder + os_file_sep() + files[i].name, LL_ERROR);
					return false;
				}

				MDB_val mdb_tkey;
				mdb_tkey.mv_data = static_cast<void*>(&inode);
				mdb_tkey.mv_size = sizeof(inode);

				MDB_val mdb_tval;

				int rc = mdb_get(txn, dbi, &mdb_tkey, &mdb_tval);

				if (rc == 0)
				{
					std::string hl_source(reinterpret_cast<char*>(mdb_tval.mv_data), mdb_tval.mv_size);

					if (os_get_file_type(os_file_prefix(hl_source))==0)
					{
						hl_source = remove_incomplete_folder(hl_source);
					}

					if (!os_create_hardlink(os_file_prefix(dst_folder + os_file_sep() + files[i].name),
						os_file_prefix(hl_source), false, NULL))
					{
						Server->Log("Error creating hard link at \"" + src_folder + os_file_sep() + files[i].name+"\" to \""+ hl_source+"\"", LL_ERROR);
						return false;
					}
				}
				else if (rc == MDB_NOTFOUND)
				{
					std::string hl_source = dst_folder + os_file_sep() + files[i].name;

					std::string error_str;
					if (!copy_file(os_file_prefix(src_folder + os_file_sep() + files[i].name), os_file_prefix(hl_source), false, &error_str))
					{
						Server->Log("Error copying file from \"" + src_folder + os_file_sep() + files[i].name + "\" to \"" + dst_folder + os_file_sep() + files[i].name + "\". " + error_str, LL_ERROR);
						if (!ignore_copy_errors)
						{
							return false;
						}
					}

					mdb_tval.mv_data = &hl_source[0];
					mdb_tval.mv_size = hl_source.size();

					rc = mdb_put(txn, dbi, &mdb_tkey, &mdb_tval, 0);

					if (rc != 0)
					{
						Server->Log("LMDB: mdb_put failed (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
						return false;
					}
				}
				else
				{
					Server->Log("LMDB: mdb_get failed (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
					return false;
				}
			}
		}

		return true;
	}

	bool copy_folder_contents(const std::string& src, const std::string& dst)
	{
		std::vector<SFile> files = getFiles(src);

		for (size_t i = 0; i < files.size(); ++i)
		{
			if (files[i].isdir)
			{
				Server->Log("Found folder " + src + os_file_sep() + files[i].name, LL_ERROR);
				return false;
			}

			std::string error_str;
			if (!copy_file(src + os_file_sep() + files[i].name,
				dst + os_file_sep() + files[i].name, false, &error_str))
			{
				Server->Log("Error copying \"" + src + os_file_sep() + files[i].name + "\" to \"" + dst + os_file_sep() + files[i].name + "\"", LL_ERROR);
				return false;
			}
		}

		return true;
	}

	bool copy_with_ext(std::string src, std::string dst, std::string ext)
	{
		if (!FileExists(src + ext))
		{
			return true;
		}

		std::string error_str;
		if (!copy_file(src + ext,
			dst + ext, false, &error_str))
		{
			Server->Log("Error copying \"" + src +ext + "\" to \"" + dst +ext + "\"", LL_ERROR);
			return false;
		}

		return true;
	}

	bool rename_with_ext(std::string dst_incomplete, std::string dst, std::string ext)
	{
		Server->deleteFile(dst + ext);
		if(!os_rename_file(dst_incomplete +ext, dst+ext))
		{
			Server->Log("Error renaming \"" + dst_incomplete + ext + "\" to \"" + dst + ext + "\"", LL_ERROR);
			return false;
		}

		return true;
	}

	bool copy_image_backup(const std::string& src, const std::string& dst_folder)
	{
		std::string src_name_incomplete = ExtractFileName(src)+"_incomplete";

		if (!copy_with_ext(src, dst_folder + os_file_sep() + src_name_incomplete, ""))
		{
			return false;
		}
		
		if (!copy_with_ext(src, dst_folder + os_file_sep() + src_name_incomplete, ".mbr"))
		{
			return false;
		}

		if (!copy_with_ext(src, dst_folder + os_file_sep() + src_name_incomplete, ".hash"))
		{
			return false;
		}

		if (!copy_with_ext(src, dst_folder + os_file_sep() + src_name_incomplete, ".bitmap"))
		{
			return false;
		}

		if (!copy_with_ext(src, dst_folder + os_file_sep() + src_name_incomplete, ".cbitmap"))
		{
			return false;
		}

		if (!copy_with_ext(src, dst_folder + os_file_sep() + src_name_incomplete, ".sync"))
		{
			return false;
		}

		//os_sync(dst_folder);

		std::string src_name = ExtractFileName(src);

		if (FileExists(dst_folder + os_file_sep() + src_name_incomplete)
			&& !rename_with_ext(dst_folder + os_file_sep() + src_name_incomplete,
			dst_folder + os_file_sep() + src_name, ".mbr"))
		{
			return false;
		}

		if (FileExists(dst_folder + os_file_sep() + src_name_incomplete+".hash")
			&& !rename_with_ext(dst_folder + os_file_sep() + src_name_incomplete,
			dst_folder + os_file_sep() + src_name, ".hash"))
		{
			return false;
		}

		if (FileExists(dst_folder + os_file_sep() + src_name_incomplete + ".bitmap")
			&& !rename_with_ext(dst_folder + os_file_sep() + src_name_incomplete,
			dst_folder + os_file_sep() + src_name, ".bitmap"))
		{
			return false;
		}

		if (FileExists(dst_folder + os_file_sep() + src_name_incomplete + ".cbitmap")
			&& !rename_with_ext(dst_folder + os_file_sep() + src_name_incomplete,
			dst_folder + os_file_sep() + src_name, ".cbitmap"))
		{
			return false;
		}

		if (FileExists(dst_folder + os_file_sep() + src_name_incomplete + ".sync")
			&& !rename_with_ext(dst_folder + os_file_sep() + src_name_incomplete,
				dst_folder + os_file_sep() + src_name, ".sync"))
		{
			return false;
		}

		if (!rename_with_ext(dst_folder + os_file_sep() + src_name_incomplete,
			dst_folder + os_file_sep() + src_name, ""))
		{
			return false;
		}

		return true;
	}

	void update_process_pcdone(size_t n_backups, size_t processed_backups,
		ScopedProcess& storage_migration)
	{
		if (n_backups > 0)
		{
			int pcdone = (int)((processed_backups*100.f) / n_backups + 0.5f);

			ServerStatus::setProcessPcDone(std::string(), storage_migration.getStatusId(), pcdone);
		}
	}
}

int copy_storage(const std::string& dest_folder, bool ignore_copy_errors)
{
	logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
	ScopedProcess storage_migration(std::string(), sa_storage_migration, std::string(), logid, false, LOG_CATEGORY_CLEANUP);

	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if (db == NULL)
	{
		ServerLogger::Log(logid, "Could not open files database", LL_ERROR);
		return 1;
	}

	std::string backupfolder = getBackupfolder(db);

	if (backupfolder.empty())
	{
		ServerLogger::Log(logid, "Error getting backupfolder", LL_ERROR);
		return 1;
	}

	ServerLogger::Log(logid, "Deleting incomplete transfers...");
	deleteIncomplete(dest_folder);

	MDB_env* env = NULL;
	if(!openMdb(dest_folder, env))
	{
		ServerLogger::Log(logid, "Error opening inode db", LL_ERROR);
		return 1;
	}

	ScopedCloseLmdbEnv close_env(env);

	ServerBackupDao backup_dao(db);
	ServerCleanupDao cleanup_dao(db);

	std::vector<int> clientids = backup_dao.getClientIds();

	size_t n_backups = 0;

	for (size_t i = 0; i < clientids.size(); ++i)
	{
		ServerCleanupDao::CondString clientname = cleanup_dao.getClientName(clientids[i]);
		if (!clientname.exists)
		{
			continue;
		}

		std::vector<ServerCleanupDao::SFileBackupInfo> file_backups = cleanup_dao.getFileBackupsOfClient(clientids[i]);

		for (size_t j = 0; j < file_backups.size(); ++j)
		{
			if (file_backups[j].done == 0)
			{
				continue;
			}

			if (os_directory_exists(os_file_prefix(dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path)))
			{
				continue;
			}

			if (!os_directory_exists(os_file_prefix(backupfolder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path)))
			{
				continue;
			}

			++n_backups;
		}

		std::vector<ServerCleanupDao::SImageBackupInfo> image_backups = cleanup_dao.getImageBackupsOfClient(clientids[i]);

		for (size_t j = 0; j < image_backups.size(); ++j)
		{
			ServerCleanupDao::SImageBackupInfo& ibackup = image_backups[j];

			if (ibackup.complete == 0)
			{
				continue;
			}

			if (!FileExists(ibackup.path))
			{
				continue;
			}

			std::string ibackup_name = ExtractFileName(ExtractFilePath(ibackup.path));

			if (ibackup_name.find("Image_") != std::string::npos)
			{
				std::string ibackup_dest = dest_folder + os_file_sep() + clientname.value + os_file_sep() + ibackup_name;

				if (!os_directory_exists(ibackup_dest))
				{
					++n_backups;
				}
			}
			else
			{
				std::string dst_name = dest_folder + os_file_sep() + clientname.value + os_file_sep() + ExtractFileName(ibackup.path);

				if (!FileExists(dst_name))
				{
					++n_backups;
				}
			}
		}
	}

	ServerStatus::setProcessPcDone(std::string(), storage_migration.getStatusId(),
		0);

	size_t processed_backups = 0;

	for (size_t i = 0; i < clientids.size(); ++i)
	{
		ServerCleanupDao::CondString clientname = cleanup_dao.getClientName(clientids[i]);
		if (!clientname.exists)
		{
			continue;
		}

		if (!os_directory_exists(dest_folder + os_file_sep() + clientname.value)
			&& !os_create_dir(dest_folder + os_file_sep() + clientname.value))
		{
			ServerLogger::Log(logid, "Error creating folder \"" + dest_folder + os_file_sep() + clientname.value + "\". "+os_last_error_str(), LL_ERROR);
			return 1;
		}

		ServerLogger::Log(logid, "Copying backups of client \"" + clientname.value + "\"...", LL_INFO);

		std::vector<ServerCleanupDao::SFileBackupInfo> file_backups = cleanup_dao.getFileBackupsOfClient(clientids[i]);

		for (size_t j = 0; j < file_backups.size(); ++j)
		{
			if (file_backups[j].done == 0)
			{
				continue;
			}

			if (os_directory_exists(os_file_prefix(dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path)))
			{
				continue;
			}

			if (!os_directory_exists(os_file_prefix(backupfolder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path)))
			{
				ServerLogger::Log(logid, "Backup id " + convert(file_backups[j].id) + " path " + file_backups[j].path + " of client \"" + clientname.value + "\" does not exist on current storage. Skipping.", LL_WARNING);
				continue;
			}

			IScopedLock client_lock(NULL);
			dir_link_lock_client_mutex(clientids[i], client_lock);

			ServerLogger::Log(logid, "Copying backup id " + convert(file_backups[j].id) + " path " + file_backups[j].path + " of client \"" + clientname.value + "\"...", LL_INFO);

			MDB_txn* txn = NULL;
			int rc = mdb_txn_begin(env, NULL, 0, &txn);

			if (rc)
			{
				ServerLogger::Log(logid, "LMDB: Failed to open transaction handle (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
				return 1;
			}

			MDB_dbi dbi;
			rc = mdb_dbi_open(txn, NULL, 0, &dbi);

			if (rc)
			{
				ServerLogger::Log(logid, "LMDB: Failed to open database (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
				mdb_txn_abort(txn);
				return 1;
			}

			std::string pool_dest = dest_folder + os_file_sep() + clientname.value + os_file_sep() + ".directory_pool";

			if (!os_create_dir(dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path + "_incomplete"))
			{
				ServerLogger::Log(logid, "Error creating folder \""+ dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path + "_incomplete\". "+os_last_error_str(), LL_ERROR);
				mdb_txn_abort(txn);
				return 1;
			}

			if (!copy_filebackup(backupfolder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path,
				dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path+"_incomplete", pool_dest, txn, dbi, ignore_copy_errors))
			{
				ServerLogger::Log(logid, "Copying backup id " + convert(file_backups[j].id) + " path " + file_backups[j].path + " of client \"" + clientname.value + "\" failed.", LL_ERROR);
				mdb_txn_abort(txn);
				continue;
			}

			//os_sync(dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path + "_incomplete");

			if (!os_rename_file(os_file_prefix(dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path + "_incomplete"),
				os_file_prefix(dest_folder + os_file_sep() + clientname.value + os_file_sep() + file_backups[j].path)))
			{
				ServerLogger::Log(logid, "Error renaming folder after copying. " + os_last_error_str(), LL_ERROR);
				mdb_txn_abort(txn);
				return 1;
			}

			rc = mdb_txn_commit(txn);

			if (rc)
			{
				ServerLogger::Log(logid, "LMDB: mdb_txn_commit failed (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
				return 1;
			}

			++processed_backups;

			update_process_pcdone(n_backups, processed_backups, storage_migration);
		}

		std::vector<ServerCleanupDao::SImageBackupInfo> image_backups = cleanup_dao.getImageBackupsOfClient(clientids[i]);

		for (size_t j = 0; j < image_backups.size(); ++j)
		{
			ServerCleanupDao::SImageBackupInfo& ibackup = image_backups[j];

			if (ibackup.complete == 0)
			{
				continue;
			}

			if (!FileExists(ibackup.path))
			{
				ServerLogger::Log(logid, "Image backup id " + convert(image_backups[j].id) + " path " + image_backups[j].path + " of client \"" + clientname.value + "\" does not exist on current storage. Skipping.", LL_WARNING);
				continue;
			}

			std::string ibackup_name = ExtractFileName(ExtractFilePath(ibackup.path));

			if (ibackup_name.find("Image_") != std::string::npos)
			{
				std::string ibackup_dest = dest_folder + os_file_sep() + clientname.value + os_file_sep() + ibackup_name;

				if (os_directory_exists(ibackup_dest))
				{
					continue;
				}

				ServerLogger::Log(logid, "Copying image backup id " + convert(image_backups[j].id) + " path " + image_backups[j].path + " (whole parent folder) of client \"" + clientname.value + "\"...", LL_INFO);

				if (!os_create_dir(ibackup_dest + "_incomplete"))
				{
					ServerLogger::Log(logid, "Error creating folder \"" + ibackup_dest+"_incomplete" + "\". "+os_last_error_str(), LL_ERROR);
					return 1;
				}

				if (!copy_folder_contents(ExtractFilePath(ibackup.path), ibackup_dest + "_incomplete"))
				{
					ServerLogger::Log(logid, "Copying image backup id " + convert(image_backups[j].id) + " path " + image_backups[j].path + " of client \"" + clientname.value + "\" failed.", LL_ERROR);
					continue;
				}

				if (!os_rename_file(os_file_prefix(ibackup_dest + "_incomplete"),
					os_file_prefix(ibackup_dest)))
				{
					ServerLogger::Log(logid, "Error renaming image folder after copying. " + os_last_error_str(), LL_ERROR);
					return 1;
				}

				++processed_backups;

				update_process_pcdone(n_backups, processed_backups, storage_migration);
			}
			else
			{
				std::string dst_name = dest_folder + os_file_sep() + clientname.value + os_file_sep() + ExtractFileName(ibackup.path);

				if (FileExists(dst_name))
				{
					continue;
				}

				ServerLogger::Log(logid, "Copying image backup id " + convert(image_backups[j].id) + " path " + image_backups[j].path + " of client \"" + clientname.value + "\"...", LL_INFO);

				if (!copy_image_backup(ibackup.path, dest_folder + os_file_sep() + clientname.value))
				{
					ServerLogger::Log(logid, "Copying image backup id " + convert(image_backups[j].id) + " path " + image_backups[j].path + " of client \"" + clientname.value + "\" failed.", LL_ERROR);
					continue;
				}
			}
		}
	}

	ServerLogger::Log(logid, "Storage migration successfully finished.", LL_INFO);

	return 0;
}