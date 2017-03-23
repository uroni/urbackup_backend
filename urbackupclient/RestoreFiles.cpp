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

#include "RestoreFiles.h"
#include "ClientService.h"
#include "../stringtools.h"
#include "../urbackupcommon/filelist_utils.h"
#include "RestoreDownloadThread.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "clientdao.h"
#include "../Interface/Server.h"
#include <algorithm>
#include <stack>
#include "../urbackupcommon/chunk_hasher.h"
#include "database.h"
#include "FileMetadataDownloadThread.h"
#include "file_permissions.h"
#include <assert.h>
#include "../common/data.h"
#include "tokens.h"
#include <algorithm>

namespace
{
	const int64 restore_flag_no_overwrite = 1 << 0;
	const int64 restore_flag_no_reboot_overwrite = 1 << 1;
	const int64 restore_flag_ignore_overwrite_failures = 1 << 2;
	const int64 restore_flag_mapping_is_alternative = 1 << 3;
	const int64 restore_flag_open_all_files_first = 1 << 4;
	const int64 restore_flag_reboot_overwrite_all = 1 << 5;

	class RestoreUpdaterThread : public IThread
	{
	public:
		RestoreUpdaterThread(int64 local_process_id, int64 restore_id, int64 status_id, std::string server_token)
			: update_mutex(Server->createMutex()), stopped(false),
			update_cond(Server->createCondition()), curr_pc(-1),
			restore_id(restore_id), status_id(status_id), server_token(server_token),
			curr_fn_pc(-1), local_process_id(local_process_id), total_bytes(-1), done_bytes(0), speed_bpms(0), success(false),
			last_fn_time(0)
		{
			SRunningProcess new_proc;
			new_proc.id = local_process_id;
			new_proc.action = RUNNING_RESTORE_FILE;
			new_proc.server_id = status_id;
			new_proc.server_token = server_token;
			ClientConnector::addNewProcess(new_proc);
		}

		void operator()()
		{
			{
				IScopedLock lock(update_mutex.get());
				while (!stopped)
				{
					ClientConnector::updateRestorePc(local_process_id, restore_id, status_id, curr_pc, server_token, curr_fn, curr_fn_pc, total_bytes, done_bytes, speed_bpms);

					if (Server->getTimeMS() - last_fn_time > 61000)
					{
						curr_fn.clear();
						curr_fn_pc = -1;
					}

					update_cond->wait(&lock, 60000);
				}
			}
			ClientConnector::updateRestorePc(local_process_id, restore_id, status_id, 101, server_token, std::string(), -1, total_bytes, success ? total_bytes : -2, 0);
			delete this;
		}

		void stop()
		{
			IScopedLock lock(update_mutex.get());
			stopped = true;
			update_cond->notify_all();
		}

		void update_pc(int new_pc, int64 p_total_bytes, int64 p_done_bytes)
		{
			IScopedLock lock(update_mutex.get());
			curr_pc = new_pc;
			total_bytes = p_total_bytes;
			done_bytes = p_done_bytes;
			update_cond->notify_all();
		}

		void update_fn(const std::string& fn, int fn_pc)
		{
			IScopedLock lock(update_mutex.get());
			curr_fn = fn;
			curr_fn_pc = fn_pc;
			last_fn_time = Server->getTimeMS();
			update_cond->notify_all();
		}

		void update_speed(double n_speed_bpms)
		{
			IScopedLock lock(update_mutex.get());
			speed_bpms = n_speed_bpms;
			update_cond->notify_all();
		}

		void set_success(bool b)
		{
			IScopedLock lock(update_mutex.get());
			success = b;
		}

	private:
		std::auto_ptr<IMutex> update_mutex;
		std::auto_ptr<ICondition> update_cond;
		bool stopped;
		int curr_pc;
		std::string curr_fn;
		int curr_fn_pc;
		int64 restore_id;
		int64 status_id;
		std::string server_token;
		int64 local_process_id;
		int64 total_bytes;
		int64 done_bytes;
		double speed_bpms;
		bool success;
		int64 last_fn_time;
	};

	class ScopedRestoreUpdater
	{
	public:
		ScopedRestoreUpdater(int64 local_process_id, int64 restore_id, int64 status_id, std::string server_token)
			: restore_updater(new RestoreUpdaterThread(local_process_id, restore_id, status_id, server_token))
		{
			restore_updater_ticket = Server->getThreadPool()->execute(restore_updater, "restore progress");
		}

		void update_pc(int new_pc, int64 total_bytes, int64 done_bytes)
		{
			restore_updater->update_pc(new_pc, total_bytes, done_bytes);
		}

		void update_fn(const std::string& fn, int fn_pc)
		{
			restore_updater->update_fn(fn, fn_pc);
		}

		void update_speed(double speed_bpms)
		{
			restore_updater->update_speed(speed_bpms);
		}

		void set_success(bool b)
		{
			restore_updater->set_success(b);
		}

		~ScopedRestoreUpdater()
		{
			restore_updater->stop();
			Server->getThreadPool()->waitFor(restore_updater_ticket);
		}

	private:
		RestoreUpdaterThread* restore_updater;
		THREADPOOL_TICKET restore_updater_ticket;
	};

	const char ID_GRANT_ACCESS = 0;
	const char ID_DENY_ACCESS = 1;

	bool hasPermission(const std::string& fn, const std::vector<int64>& tids, ClientDAO* clientdao, tokens::ETokenRight right, tokens::TokenCache& cache)
	{
		std::string permissions = tokens::get_file_tokens(fn, clientdao, right, cache);

		CRData perm(permissions.data(),
			permissions.size());

		char action;
		while (perm.getChar(&action))
		{
			int64 pid;
			if (!perm.getVarInt(&pid))
			{
				return false;
			}

			switch (action)
			{
			case ID_GRANT_ACCESS:
				if (pid == 0 || std::binary_search(tids.begin(), tids.end(), pid))
				{
					return true;
				}
				break;
			case ID_DENY_ACCESS:
				if (pid == 0 || std::binary_search(tids.begin(), tids.end(), pid))
				{
					return false;
				}
				break;
			}
		}

		return false;
	}

	bool canModifyFile(const std::string& fn, const std::vector<int64>& tids, ClientDAO* clientdao, tokens::TokenCache& cache)
	{
		return hasPermission(fn, tids, clientdao, tokens::ETokenRight_Write, cache);
	}

	bool canDelete(const std::string& fn, const std::vector<int64>& tids, ClientDAO* clientdao, tokens::TokenCache& cache)
	{
		return hasPermission(fn, tids, clientdao, tokens::ETokenRight_Delete, cache);
	}

	bool canDeleteFromDir(const std::string& fn, const std::vector<int64>& tids, ClientDAO* clientdao, tokens::TokenCache& cache)
	{
		std::string dir = ExtractFilePath(fn, os_file_sep());
		if (dir.empty())
		{
			return false;
		}

		return hasPermission(dir, tids, clientdao, tokens::ETokenRight_Full, cache);
	}

	bool canAddFiles(const std::string& fn, const std::vector<int64>& tids, ClientDAO* clientdao, tokens::TokenCache& cache)
	{
		return hasPermission(fn, tids, clientdao, tokens::ETokenRight_Write, cache);
	}

	bool canCreateFile(const std::string& fn, const std::vector<int64>& tids, ClientDAO* clientdao, tokens::TokenCache& cache)
	{
		std::string dir = ExtractFilePath(fn, os_file_sep());
		if (dir.empty())
		{
			return false;
		}

		return canAddFiles(dir, tids, clientdao, cache);
	}

	bool canCreateDirRecursive(const std::string& fn, const std::vector<int64>& tids, ClientDAO* clientdao, tokens::TokenCache& cache)
	{
		std::string cd = fn;
		while (!os_directory_exists(os_file_prefix(cd)))
		{
			cd = ExtractFilePath(cd, os_file_sep());
		}

		if (!cd.empty())
		{
			return canCreateFile(cd, tids, clientdao, cache);
		}
		else
		{
			return false;
		}
	}
}


RestoreFiles::~RestoreFiles()
{
	for (std::map<std::string, std::pair<IFile*, int64> >::iterator it = cbt_hash_files.begin();
		it != cbt_hash_files.end(); ++it)
	{
		delete it->second.first;
	}
}

void RestoreFiles::operator()()
{
	if (client_token.empty())
	{
		log("Client token empty. See client log file for error details. "
			"Running a file backup may fix this issue.", LL_ERROR);
	}

	std::auto_ptr<RestoreFiles> delete_this(this);
	if (restore_declined)
	{
		log("Restore was declined by client", LL_ERROR);
		ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
		return;
	}


	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	FileClient fc(false, client_token, 3,
		true, this, NULL);

	log("Starting restore...", LL_INFO);

	{
		ScopedRestoreUpdater restore_updater(local_process_id, restore_id, status_id, server_token);
		curr_restore_updater = &restore_updater;

		if (!connectFileClient(fc))
		{
			log("Connecting for restore failed", LL_ERROR);
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			is_offline = true;
			return;
		}

		log("Loading file list...", LL_INFO);

		if (!downloadFilelist(fc))
		{
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			is_offline = true;
			return;
		}

		log("Calculating download size...", LL_INFO);

		int64 total_size = calculateDownloadSize();
		if (total_size == -1)
		{
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			return;
		}

		FileClient fc_metadata(false, client_token, 3,
			true, this, NULL);

		if (!connectFileClient(fc_metadata))
		{
			log("Connecting for file metadata failed", LL_ERROR);
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			is_offline = true;
			return;
		}

		std::auto_ptr<client::FileMetadataDownloadThread> metadata_thread(new client::FileMetadataDownloadThread(*this, fc_metadata, client_token));
		THREADPOOL_TICKET metadata_dl = Server->getThreadPool()->execute(metadata_thread.get(), "file restore metadata download");

		int64 starttime = Server->getTimeMS();

		while (Server->getTimeMS() - starttime < 60000)
		{
			if (fc_metadata.isDownloading())
			{
				break;
			}
		}

		if (!fc_metadata.isDownloading())
		{
			log("Error starting metadata download", LL_INFO);
			restore_failed(*metadata_thread.get(), metadata_dl);
			is_offline = true;
			return;
		}

		IndexThread::readPatterns(tgroup, clientsubname, exclude_dirs,
			include_dirs);

		std::map<std::string, IFsFile*> open_files;
		if (restore_flags & restore_flag_open_all_files_first)
		{
			log("Opening files...", LL_INFO);

			bool overwrite_failure = false;
			if (!openFiles(open_files, overwrite_failure))
			{
				for (std::map<std::string, IFsFile*>::iterator it = open_files.begin();
					it != open_files.end(); ++it)
				{
					Server->destroy(it->second);
				}

				if (overwrite_failure)
				{
					log("Restore finished successfully after overwrite failure. Nothing restored.", LL_INFO);
					metadata_thread->shutdown(false);
					Server->getThreadPool()->waitFor(metadata_dl);
					restore_updater.set_success(true);
					ClientConnector::restoreDone(log_id, status_id, restore_id, true, server_token);
				}
				else
				{
					restore_failed(*metadata_thread.get(), metadata_dl);
				}
				return;
			}
		}

		log("Downloading necessary file data...", LL_INFO);

		fc.setProgressLogCallback(this);

		restore_updater.update_pc(0, total_size, 0);

		if (!downloadFiles(fc, total_size, restore_updater, open_files))
		{
			restore_failed(*metadata_thread.get(), metadata_dl);
			is_offline = true;
			metadata_thread->applyMetadata(metadata_path_mapping);
			return;
		}

		curr_restore_updater = NULL;

		int64 transferred_bytes = metadata_thread->getTransferredBytes();

		int attempt = 0;
		int64 last_transfer_time = Server->getTimeMS();

		do
		{
			if (fc.InformMetadataStreamEnd(client_token, 0) == ERR_TIMEOUT)
			{
				fc.Reconnect();

				fc.InformMetadataStreamEnd(client_token, 0);
			}

			log("Waiting for metadata download stream to finish", LL_DEBUG);

			if (Server->getThreadPool()->waitFor(metadata_dl, 10000))
			{
				break;
			}

			int64 new_transferred_bytes = metadata_thread->getTransferredBytes();

			if (new_transferred_bytes > transferred_bytes)
			{
				last_transfer_time = Server->getTimeMS();
			}

			if (Server->getTimeMS() - last_transfer_time>140000)
			{
				log("No meta-data transfer in the last " + PrettyPrintTime(Server->getTimeMS() - last_transfer_time) + ". Shutting down meta-data tranfer.", LL_DEBUG);
				is_offline = true;
				metadata_thread->shutdown(true);
			}

			transferred_bytes = new_transferred_bytes;
			++attempt;

		} while (true);

		if (!metadata_thread->applyMetadata(metadata_path_mapping))
		{
			restore_failed(*metadata_thread.get(), metadata_dl);
			return;
		}

		restore_updater.set_success(true);
	}

	log("Restore finished successfully.", LL_INFO);

	ClientConnector::restoreDone(log_id, status_id, restore_id, true, server_token);

	if (request_restart)
	{
		log("Restore requested system restart to rename/delete locked files", LL_INFO);
		ClientConnector::requestRestoreRestart();
	}
}

IPipe * RestoreFiles::new_fileclient_connection( )
{
	return ClientConnector::getFileServConnection(server_token, 10000);
}

bool RestoreFiles::connectFileClient( FileClient& fc )
{
	IPipe* np = new_fileclient_connection();

	if(np!=NULL)
	{
		fc.Connect(np);
		return true;
	}
	else
	{
		return false;
	}
}

bool RestoreFiles::downloadFilelist( FileClient& fc )
{
	filelist = Server->openTemporaryFile();
	filelist_del.reset(filelist);

	if(filelist==NULL)
	{
		return false;
	}

	_u32 rc = fc.GetFile("clientdl_filelist", filelist, true, false, 0, false, 0);

	if(rc!=ERR_SUCCESS)
	{
		log("Error getting file list. Errorcode: "+FileClient::getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		return false;
	}

	return true;
}

int64 RestoreFiles::calculateDownloadSize()
{
	std::vector<char> buffer;
	buffer.resize(32768);

	FileListParser filelist_parser;

	_u32 read;
	SFile data;
	std::map<std::string, std::string> extra;

	int64 total_size = 0;

	filelist->Seek(0);

	do 
	{
		read = filelist->Read(buffer.data(), static_cast<_u32>(buffer.size()));

		for(_u32 i=0;i<read;++i)
		{
			if(filelist_parser.nextEntry(buffer[i], data, &extra))
			{
				if(data.size>0)
				{
					total_size+=data.size;
				}				
			}
		}

	} while (read>0);

	return total_size;
}

bool RestoreFiles::openFiles(std::map<std::string, IFsFile*>& open_files, bool& overwrite_failure)
{
	std::vector<char> buffer;
	buffer.resize(32768);

	FileListParser filelist_parser;

	_u32 read;
	SFile data;
	std::map<std::string, std::string> extra;

	size_t depth = 0;

	std::string restore_path;
	std::string alt_restore_path;

	size_t line = 0;

	bool has_error = false;

	filelist->Seek(0);

	std::stack<std::vector<std::string> > folder_files;
	folder_files.push(std::vector<std::string>());

	std::vector<int64> tids;
	ClientDAO client_dao(db);
	tokens::TokenCache token_cache;
	size_t skip_dir = std::string::npos;

	do
	{
		read = filelist->Read(buffer.data(), static_cast<_u32>(buffer.size()));

		for (_u32 i = 0; i<read && !has_error; ++i)
		{
			if (filelist_parser.nextEntry(buffer[i], data, &extra))
			{
				if (skip_dir != std::string::npos
					&& data.isdir)
				{
					if (data.name == "..")
					{
						--skip_dir;
						if (skip_dir == 0)
						{
							skip_dir = std::string::npos;
						}
						else
						{
							++line;
							continue;
						}
					}
					else
					{
						++skip_dir;
						++line;
						continue;
					}
				}
				else if (skip_dir != std::string::npos)
				{
					++line;
					continue;
				}

				FileMetadata metadata;
				metadata.read(extra);

				if (depth == 0
					&& (!data.isdir || data.name != "..")
					&& !metadata.orig_path.empty()
					&& !restore_path.empty()
					&& !has_error)
				{
					if (ExtractFilePath(metadata.orig_path, os_file_sep()) != restore_path)
					{
						folder_files.top().clear();
					}
				}

				str_map::iterator it_tids = extra.find("tids");
				if (it_tids != extra.begin())
				{
					std::vector<std::string> toks;
					TokenizeMail(it_tids->second, toks, ",");
					tids.clear();
					for (size_t j = 0; j < toks.size(); ++j)
					{
						tids.push_back(watoi64(toks[j]));
					}
					std::sort(tids.begin(), tids.end());
				}

				if (data.isdir)
				{
					if (data.name != "..")
					{
						bool set_orig_path = false;
						std::string restore_name = data.name;
						if (!metadata.orig_path.empty())
						{
							restore_path = metadata.orig_path;
							set_orig_path = true;
							restore_name = ExtractFileName(restore_path, os_file_sep());

							str_map::iterator it_alt_orig_path = extra.find("alt_orig_path");
							if (it_alt_orig_path != extra.end())
							{
								alt_restore_path = it_alt_orig_path->second;
							}
							else
							{
								alt_restore_path = restore_path;
							}
						}

						if (depth == 0)
						{
							if (extra.find("skip") != extra.end())
							{
								++line;
								continue;
							}

							bool win_root = false;

#ifdef _WIN32
							if (restore_path.size() <= 3)
							{
								win_root = true;
							}
#endif

							if (!win_root
								&& !canCreateDirRecursive(restore_path, tids, &client_dao, token_cache))
							{
								if (!(restore_flags & restore_flag_ignore_overwrite_failures))
								{
									log("Error creating directory \"" + restore_path + "\" recursively. No permission to create directory.", LL_ERROR);
									has_error = true;
								}
								skip_dir = 1;
							}
							else if (!win_root
								&& !os_directory_exists(os_file_prefix(restore_path)))
							{
								if (!os_create_dir_recursive(os_file_prefix(restore_path)))
								{
									log("Error recursively creating directory \"" + restore_path + "\". " + os_last_error_str(), LL_ERROR);
									has_error = true;
								}
							}
							else if (!canAddFiles(restore_path, tids, &client_dao, token_cache))
							{
								if (!(restore_flags & restore_flag_ignore_overwrite_failures))
								{
									log("No permissions to create files in \"" + restore_path + "\".", LL_ERROR);
									has_error = true;
								}
								skip_dir = 1;
							}
						}
						else
						{
							if (!set_orig_path)
							{
								restore_path += os_file_sep() + data.name;
								alt_restore_path += os_file_sep() + data.name;
							}

							if (extra.find("skip") != extra.end())
							{
								++line;
								continue;
							}

							if (!canCreateFile(restore_path, tids, &client_dao, token_cache))
							{
								if (!(restore_flags & restore_flag_ignore_overwrite_failures))
								{
									log("Error creating directory \"" + restore_path + "\". No permission to create directory.", LL_ERROR);
									has_error = true;
								}
								skip_dir = 1;
							}
							else if (!os_directory_exists(os_file_prefix(restore_path)))
							{
								if (!os_create_dir(os_file_prefix(restore_path))
									&& !createDirectoryWin(restore_path))
								{
									log("Error creating directory \"" + restore_path + "\". " + os_last_error_str(), LL_ERROR);
									has_error = true;
								}
							}
							else if (!canAddFiles(restore_path, tids, &client_dao, token_cache))
							{
								if (!(restore_flags & restore_flag_ignore_overwrite_failures))
								{
									log("No permissions to create files in \"" + restore_path + "\".", LL_ERROR);
									has_error = true;
								}
								skip_dir = 1;
							}
						}

#ifdef _WIN32
						std::string name_lower = strlower(restore_name);
#else
						std::string name_lower = restore_name;
#endif
						folder_files.top().push_back(name_lower);
						folder_files.push(std::vector<std::string>());

						++depth;
					}
					else
					{
						--depth;
						restore_path = ExtractFilePath(restore_path, os_file_sep());
						alt_restore_path = ExtractFilePath(alt_restore_path, os_file_sep());
						folder_files.pop();
					}
				}
				else
				{
					if (extra.find("skip") != extra.end())
					{
						++line;
						continue;
					}

					std::string restore_name;
					std::string local_fn;
					std::string alt_local_fn;
					if (!metadata.orig_path.empty())
					{
						local_fn = metadata.orig_path;
						restore_name = ExtractFileName(metadata.orig_path, os_file_sep());

						str_map::iterator it_alt_orig_path = extra.find("alt_orig_path");
						if (it_alt_orig_path != extra.end())
						{
							alt_local_fn = it_alt_orig_path->second;
							alt_restore_path = ExtractFilePath(alt_local_fn, os_file_sep());
						}
						else
						{
							alt_local_fn = local_fn;
						}
					}
					else
					{
						local_fn = restore_path + os_file_sep() + data.name;
						restore_name = data.name;
					}
					

#ifdef _WIN32
					std::string name_lower = strlower(restore_name);
#else
					std::string name_lower = restore_name;
#endif
					folder_files.top().push_back(name_lower);

					str_map::iterator it_orig_path = extra.find("orig_path");
					if (it_orig_path != extra.end())
					{
						local_fn = it_orig_path->second;
						restore_path = ExtractFilePath(local_fn, os_file_sep());
					}

					if (restore_flags & restore_flag_mapping_is_alternative)
					{
						if (restore_flags & restore_flag_no_overwrite)
						{
							if (os_get_file_type(os_file_prefix(alt_local_fn)) == 0)
							{
								os_create_dir_recursive(os_file_prefix(ExtractFilePath(alt_local_fn, os_file_sep())));
								local_fn = alt_local_fn;
							}
						}
					}

					if (os_get_file_type(os_file_prefix(local_fn)) != 0)
					{
						if (restore_flags & restore_flag_no_overwrite)
						{
							log("File \"" + local_fn + "\" does already exist and the restore is configured to not overwrite existing files.", LL_ERROR);
							return false;
						}
						else if (!canModifyFile(local_fn, tids, &client_dao, token_cache))
						{
							if (!(restore_flags & restore_flag_ignore_overwrite_failures))
							{
								log("Error opening file \"" + local_fn + "\" for restore. No permission to write to file.", LL_ERROR);
								return false;
							}
						}
						else
						{
							std::auto_ptr<IFsFile> orig_file(Server->openFile(os_file_prefix(local_fn), MODE_RW_RESTORE));

							if (orig_file.get() == NULL)
							{
								if (!(restore_flags & restore_flag_ignore_overwrite_failures))
								{
									log("Error opening file \"" + local_fn + "\" for restore. " + os_last_error_str(), LL_ERROR);
									return false;
								}
								else
								{
									overwrite_failure = true;
									log("Error opening file \"" + local_fn + "\" for restore. " + os_last_error_str()+". Restore is configured to ignore such errors.", LL_WARNING);
									return false;
								}
							}
							else
							{
								open_files[local_fn] = orig_file.release();
							}
						}
					}
					else
					{
						std::auto_ptr<IFsFile> restore_file(Server->openFile(os_file_prefix(local_fn), MODE_RW_CREATE_RESTORE));

						if (restore_file.get() == NULL)
						{
							log("Error opening new file \"" + local_fn + "\" for restore. " + os_last_error_str(), LL_ERROR);
							return false;
						}
						else
						{
							open_files[local_fn] = restore_file.release();
						}
					}
				}
				++line;
			}
		}

	} while (read>0 && !has_error);

	return true;
}

bool RestoreFiles::downloadFiles(FileClient& fc, int64 total_size, ScopedRestoreUpdater& restore_updater,
	std::map<std::string, IFsFile*>& open_files)
{
	std::vector<char> buffer;
	buffer.resize(32768);

	FileListParser filelist_parser;

	std::auto_ptr<FileClientChunked> fc_chunked = createFcChunked();

	if(fc_chunked.get()==NULL)
	{
		return false;
	}

	fc_chunked->setProgressLogCallback(this);

	_u32 read;
	SFile data;
	std::map<std::string, std::string> extra;

	size_t depth = 0;

	std::string restore_path;
	std::string alt_restore_path;
	std::string share_path;
	std::string server_path = "clientdl";

	std::auto_ptr<RestoreDownloadThread> restore_download(new RestoreDownloadThread(fc, *fc_chunked, client_token, metadata_path_mapping));
    THREADPOOL_TICKET restore_download_ticket = Server->getThreadPool()->execute(restore_download.get(), "file restore download");

	std::string curr_files_dir;
	std::vector<SFileAndHash> curr_files;

	size_t line=0;

	bool has_error=false;

	filelist->Seek(0);

	int64 laststatsupdate=Server->getTimeMS();
	int64 skipped_bytes = 0;
	int db_tgroup = 0;

	std::vector<size_t> folder_items;
	folder_items.push_back(0);

	std::stack<std::vector<std::string> > folder_files;
	folder_files.push(std::vector<std::string>());

	std::vector<std::string> deletion_queue;
	std::vector<std::pair<std::string, std::string> > rename_queue;

	bool single_item=false;
	std::vector<int64> tids;
	ClientDAO client_dao(db);
	tokens::TokenCache token_cache;
	size_t skip_dir = std::string::npos;
	bool skip_last_dir = false;

	do 
	{
		read = filelist->Read(buffer.data(), static_cast<_u32>(buffer.size()));

		for(_u32 i=0;i<read && !has_error;++i)
		{
			if(filelist_parser.nextEntry(buffer[i], data, &extra))
			{
				if (skip_dir != std::string::npos
					&& data.isdir)
				{
					if (data.name == "..")
					{
						--skip_dir;
						if (skip_dir == 0)
						{
							skip_dir = std::string::npos;
							skip_last_dir = true;
						}
						else
						{
							++line;
							continue;
						}
					}
					else
					{
						++skip_dir;
						++line;
						continue;
					}
				}
				else if (skip_dir != std::string::npos)
				{
					++line;
					continue;
				}
				else
				{
					skip_last_dir = false;
				}

				FileMetadata metadata;
				metadata.read(extra);

				str_map::iterator it_tids = extra.find("tids");
				if (it_tids != extra.begin())
				{
					std::vector<std::string> toks;
					TokenizeMail(it_tids->second, toks, ",");
					tids.clear();
					for (size_t j = 0; j < toks.size(); ++j)
					{
						tids.push_back(watoi64(toks[j]));
					}
					std::sort(tids.begin(), tids.end());
				}

				if ( depth==0
					&& (!data.isdir || data.name != "..") 
					&& !metadata.orig_path.empty()
					&& !restore_path.empty()
					&& !has_error
					&& !(os_get_file_type(os_file_prefix(restore_path)) & EFileType_Symlink) )
				{
					if (ExtractFilePath(metadata.orig_path, os_file_sep()) != restore_path)
					{
						if (!single_item && clean_other)
						{
							bool has_include_exclude = false;
							if (!removeFiles(restore_path, share_path, restore_download.get(), folder_files, deletion_queue, has_include_exclude,
											tids, &client_dao, token_cache))
							{
								has_error = true;
							}
						}

						folder_files.top().clear();
					}
				}
				else if(depth>=1 && data.isdir && data.name==".."					
					&& clean_other && !has_error
					&& !(os_get_file_type(os_file_prefix(restore_path)) & EFileType_Symlink)
					&& !skip_last_dir)
				{
					bool has_include_exclude = false;
					if (!removeFiles(restore_path, share_path, restore_download.get(), folder_files, deletion_queue, has_include_exclude,
									tids, &client_dao, token_cache))
					{
						has_error = true;
					}
				}

				if(depth==0 && extra.find("single_item")!=extra.end())
				{
					single_item=true;
				}
				else if(depth==0)
				{
					single_item = false;
				}

				if (depth == 0 && extra.find("server_path") != extra.end())
				{
					server_path = extra["server_path"];
				}

				int64 ctime=Server->getTimeMS();
				if(ctime-laststatsupdate>1000)
				{
					laststatsupdate=ctime;
					if(total_size==0)
					{
						restore_updater.update_pc(100, total_size, total_size);
					}
					else
					{
						int64 done_bytes = fc.getReceivedDataBytes(true) + fc_chunked->getReceivedDataBytes(true) + skipped_bytes;
						int pcdone = (std::min)(100,(int)(((float)done_bytes)/((float)total_size/100.f)+0.5f));
						restore_updater.update_pc(pcdone, total_size, done_bytes);
					}

					calculateDownloadSpeed(fc, fc_chunked.get());
				}

				if(!data.isdir || data.name!="..")
				{
					for(size_t j=0;j<folder_items.size();++j)
					{
						++folder_items[j];
					}
				}

				if(data.isdir)
				{
					if(data.name!="..")
					{
						bool set_orig_path = false;
						std::string restore_name = data.name;
                        if(!metadata.orig_path.empty())
						{
                            restore_path = metadata.orig_path;
							set_orig_path=true;
							restore_name = ExtractFileName(restore_path, os_file_sep());
						}

						bool set_share_path = false;
						str_map::iterator it_share_path = extra.find("share_path");
						if (it_share_path != extra.end())
						{
							share_path = greplace("/", os_file_sep(), it_share_path->second);
							set_share_path = true;
						}

						str_map::iterator it_alt_orig_path = extra.find("alt_orig_path");
						if (it_alt_orig_path != extra.end())
						{
							alt_restore_path = it_alt_orig_path->second;
						}

						if (extra.find("skip") != extra.end())
						{
							skip_dir = 1;
						}

						if(depth==0)
						{
							bool win_root = false;

#ifdef _WIN32
							if (restore_path.size() <= 3)
							{
								win_root = true;
							}
#endif
							if (extra.find("skip") == extra.end()
								&& !win_root)
							{
								if (!os_directory_exists(os_file_prefix(restore_path)))
								{
									if (!canCreateDirRecursive(restore_path, tids, &client_dao, token_cache))
									{
										if (!(restore_flags & restore_flag_ignore_overwrite_failures))
										{
											log("Error creating directory \"" + restore_path + "\" recursively. No permission to create directory.", LL_ERROR);
											has_error = true;
										}
										skip_dir = 1;
									}
									else if (!os_create_dir_recursive(os_file_prefix(restore_path)))
									{
										log("Error recursively creating directory \"" + restore_path + "\". " + os_last_error_str(), LL_ERROR);
										has_error = true;
									}
								}
								else if (!canAddFiles(restore_path, tids, &client_dao, token_cache))
								{
									if (!(restore_flags & restore_flag_ignore_overwrite_failures))
									{
										log("No permissions to create files in \"" + restore_path + "\".", LL_ERROR);
										has_error = true;
									}
									skip_dir = 1;
								}
							}
							

							if (!clientsubname.empty())
							{
								db_tgroup = -1;
								IQuery *q = db->Prepare("SELECT group_offset FROM virtual_client_group_offsets WHERE virtual_client=?", false);
								q->Bind(clientsubname);
								db_results res = q->Read();
								db->destroyQuery(q);

								if (!res.empty())
								{
									db_tgroup = watoi(res[0]["group_offset"]) + tgroup;
								}
							}
							else
							{
								db_tgroup = tgroup;
							}

							if (db_tgroup >= 0)
							{
								IQuery* q = db->Prepare("SELECT optional FROM backupdirs WHERE name=? AND tgroup=?", false);
								q->Bind(data.name);
								q->Bind(db_tgroup);
								db_results res = q->Read();
								db->destroyQuery(q);

								if (!res.empty() && (watoi(res[0]["optional"]) & EBackupDirFlag_ShareHashes) > 0)
								{
									db_tgroup = 0;
								}
								else
								{
									db_tgroup += 1;
								}
							}
						}
						else
						{
							if(!set_orig_path)
							{
								restore_path+=os_file_sep()+data.name;
								alt_restore_path += os_file_sep() + data.name;
							}

							if (extra.find("skip") == extra.end())
							{
								if (!os_directory_exists(os_file_prefix(restore_path)))
								{
									if (!os_create_dir(os_file_prefix(restore_path))
										&& !createDirectoryWin(restore_path))
									{
										log("Error creating directory \"" + restore_path + "\". " + os_last_error_str(), LL_ERROR);
										has_error = true;
									}
								}
								else if (!canAddFiles(restore_path, tids, &client_dao, token_cache))
								{
									if (!(restore_flags & restore_flag_ignore_overwrite_failures))
									{
										log("No permissions to create files in \"" + restore_path + "\".", LL_ERROR);
										has_error = true;
									}
									skip_dir = 1;
								}
							}
						}

						if (!set_share_path)
						{
							if (!share_path.empty())
							{
								share_path += os_file_sep();
							}

							share_path += data.name;
						}

#ifdef _WIN32
						std::string name_lower = strlower(restore_name);
#else
						std::string name_lower = restore_name;
#endif
						folder_files.top().push_back(name_lower);

						folder_items.push_back(0);
						folder_files.push(std::vector<std::string>());

						server_path+="/"+data.name;

						++depth;
					}
					else
					{
						--depth;			

                        restore_download->addToQueueFull(line, server_path, restore_path, 0,
                            metadata, false, true, folder_items.back(), NULL);

						server_path = ExtractFilePath(server_path, "/");
						restore_path = ExtractFilePath(restore_path, os_file_sep());
						alt_restore_path = ExtractFilePath(alt_restore_path, os_file_sep());
						share_path = ExtractFilePath(share_path, os_file_sep());

						folder_items.pop_back();
						folder_files.pop();
					}
				}
				else
				{

					std::string restore_name;
					std::string local_fn;
					std::string alt_local_fn;
					if (!metadata.orig_path.empty())
					{
						local_fn = metadata.orig_path;
						restore_name = ExtractFileName(metadata.orig_path, os_file_sep());
					}
					else
					{
						local_fn = restore_path + os_file_sep() + data.name;
						restore_name = data.name;
					}

					str_map::iterator it_alt_orig_path = extra.find("alt_orig_path");
					if (it_alt_orig_path != extra.end())
					{
						alt_local_fn = it_alt_orig_path->second;
						alt_restore_path = ExtractFilePath(alt_local_fn, os_file_sep());
					}
					else
					{
						alt_local_fn = alt_restore_path + os_file_sep() + data.name;
					}
					
#ifdef _WIN32
					std::string name_lower = strlower(restore_name);
#else
					std::string name_lower = restore_name;
#endif
					std::string server_fn = server_path + "/" + data.name;


					folder_files.top().push_back(name_lower);

					if (extra.find("skip") != extra.end())
					{
						++line;
						continue;
					}

					log("Restoring file \"" + local_fn + "\"...", LL_DEBUG);

					str_map::iterator it_orig_path = extra.find("orig_path");
					if(it_orig_path!=extra.end())
					{
                        local_fn = it_orig_path->second;
						restore_path = ExtractFilePath(local_fn, os_file_sep());
					}

					str_map::iterator it_share_path = extra.find("share_path");
					if (it_share_path != extra.end())
					{
						share_path = greplace("/", os_file_sep(), ExtractFilePath(it_share_path->second, "/"));
					}
				
					int orig_ftype = os_get_file_type(os_file_prefix(local_fn));
					if(orig_ftype !=0)
					{
						if(restore_path!=curr_files_dir)
						{
							curr_files_dir = restore_path;

#ifndef _WIN32
							std::string restore_path_lower = restore_path;
#else
							std::string restore_path_lower = strlower(restore_path);
#endif

							int64 generation;
							if(!client_dao.getFiles(restore_path_lower + os_file_sep(), db_tgroup, curr_files, generation))
							{
								curr_files.clear();
							}
						}

						std::string shahash;

						SFileAndHash search_file = {};
						search_file.name = restore_name;

						std::vector<SFileAndHash>::iterator it_file = std::lower_bound(curr_files.begin(), curr_files.end(), search_file);
						if(it_file!=curr_files.end() && it_file->name == restore_name)
						{
							SFile metadata = getFileMetadataWin(local_fn, true);
							if(!metadata.name.empty())
							{
								uint64 change_indicator = metadata.last_modified;
								if(metadata.usn!=0)
								{
									change_indicator = metadata.usn;
								}
								if(!metadata.isdir
									&& metadata.size==it_file->size
									&& change_indicator==it_file->change_indicator
									&& !it_file->hash.empty())
								{
									shahash = it_file->hash;
								}
							}							
						}

						std::auto_ptr<IFsFile> orig_file;
						bool use_open_fallback = true;

						if (restore_flags & restore_flag_open_all_files_first)
						{
							std::map<std::string, IFsFile*>::iterator it = open_files.find(local_fn);
							if (it == open_files.end())
							{
								log("Cannot find \"" + local_fn + "\" in open file list", LL_ERROR);
								has_error = true;
							}
							else
							{
								orig_file.reset(it->second);
								open_files.erase(it);

								if (orig_file->getFilename() != os_file_prefix(local_fn))
								{
									metadata_path_mapping[local_fn] = orig_file->getFilename();
								}
							}
							use_open_fallback = false;
						}
						else
						{
							if (!canModifyFile(local_fn, tids, &client_dao, token_cache))
							{
								int ll = LL_ERROR;
								if (!(restore_flags & restore_flag_ignore_overwrite_failures))
								{
									has_error = true;
									ll = LL_WARNING;
								}
								log("No permission to open \"" + local_fn + "\" for writing. Not restoring file.", ll);
								++line;
								continue;
							}

							if (!(restore_flags & restore_flag_reboot_overwrite_all))
							{
								orig_file.reset(Server->openFile(os_file_prefix(local_fn), MODE_RW_RESTORE));
							}

#ifdef _WIN32
							if (restore_flags & restore_flag_no_reboot_overwrite)
							{
								use_open_fallback = false;
							}
#endif
						}

#ifdef _WIN32
						if (orig_file.get() == NULL
							&& orig_ftype & EFileType_Symlink )
						{
							Server->deleteFile(os_file_prefix(local_fn));
							orig_file.reset(Server->openFile(os_file_prefix(local_fn), MODE_RW_CREATE_RESTORE));
						}

						if( (orig_file.get()==NULL
							&& use_open_fallback) )
						{
							size_t idx=0;
							std::string old_local_fn=local_fn;
							while(orig_file.get()==NULL && idx<100)
							{
								local_fn=old_local_fn+"_urbackup_restore_"+convert(idx);
								++idx;
								if (!Server->fileExists(os_file_prefix(local_fn)) )
								{
									orig_file.reset(Server->openFile(os_file_prefix(local_fn), MODE_RW_CREATE_RESTORE));
								}
							}

							if (orig_file.get() != NULL)
							{
								rename_queue.push_back(std::make_pair(local_fn, old_local_fn));
								metadata_path_mapping[old_local_fn] = local_fn;
								folder_files.top().push_back(strlower(ExtractFileName(local_fn, os_file_sep())));
								if (!change_file_permissions_admin_only(os_file_prefix(local_fn)))
								{
									log("Cannot change file permissions of \""+local_fn+"\" to allow only admin access. " + os_last_error_str(), LL_ERROR);
									has_error = true;
									orig_file.reset();
								}
							}
						}
#else
						if (orig_file.get() == NULL
							&& use_open_fallback)
						{
							log("Cannot open file \"" + local_fn + "\" for writing. Unlinking file and creating new one. " + os_last_error_str(), LL_INFO);

							int rc = unlink(local_fn.c_str());
							if (rc == 0)
							{
								orig_file.reset(Server->openFile(os_file_prefix(local_fn), MODE_RW_CREATE));
								if (!change_file_permissions_admin_only(os_file_prefix(local_fn)))
								{
									log("Cannot change file permissions of \"" + local_fn + "\" to allow only root access. " + os_last_error_str(), LL_ERROR);
									has_error = true;
									orig_file.reset();
								}
							}
						}
#endif

						if(orig_file.get()==NULL)
						{
							int ll = LL_ERROR;
							if (!(restore_flags & restore_flag_ignore_overwrite_failures))
							{
								has_error = true;
								ll = LL_WARNING;
							}
							log("Cannot open file \"" + local_fn + "\" for writing. Not restoring file. " + os_last_error_str(), ll);
						}
						else if (data.size == 0)
						{
							if (orig_file->Size() != 0
								&& !(orig_ftype & EFileType_Symlink) )
							{							
								if (!orig_file->Resize(0, false))
								{
									log("Cannot truncate file \"" + local_fn + "\" to zero bytes. " + os_last_error_str(), LL_ERROR);
									has_error = true;
								}
							}

							orig_file.reset();

							restore_download->addToQueueFull(line, server_fn, local_fn,
								data.size, metadata, false, true, 0, NULL);
						}
						else
						{		
							IFile* chunkhashes = Server->openTemporaryFile();

							if(chunkhashes==NULL)
							{
								log("Cannot open temporary file for chunk hashes of file \""+local_fn+"\". Not restoring file. " + os_last_error_str(), LL_ERROR);
								has_error=true;
							}
							else
							{
								std::auto_ptr<IHashFunc> hashf;
								std::auto_ptr<IHashFunc> hashf2;

								std::string hash_key;

								if (extra.find("shahash") != extra.end())
								{
									hashf.reset(new HashSha512);
									hashf2.reset(new HashSha512);
									hash_key = "shahash";
								}
								else
								{
									hashf.reset(new TreeHash(NULL));
									hashf2.reset(new TreeHash(NULL));
									hash_key = "thash";
								}

                                bool calc_hashes=false;
								if(shahash.empty())
								{
									log("Calculating hashes of file \""+local_fn+"\"...", LL_DEBUG);
									FsExtentIterator extent_iterator(orig_file.get(), 512*1024);

									std::pair<IFile*, int64> cbt_hash_file;
									if (hash_key == "thash")
									{
										cbt_hash_file = getCbtHashFile(local_fn);
									}

									if (build_chunk_hashs(orig_file.get(), chunkhashes, NULL, NULL, false, NULL,
										NULL, false, hashf.get(), &extent_iterator, cbt_hash_file))
									{
										calc_hashes = true;
										shahash = hashf->finalize();
									}
												
									IFile* tmp_f = Server->openTemporaryFile();
									ScopedDeleteFile del_tmp_f(tmp_f);
									if (build_chunk_hashs(orig_file.get(), tmp_f, NULL, NULL, false, NULL, NULL, false, hashf2.get()))
									{
										assert(shahash == hashf2->finalize());
									}
								}
								
								if(shahash!=base64_decode_dash(extra[hash_key]))
								{
                                    if(!calc_hashes)
                                    {
                                        log("Calculating hashes of file \""+local_fn+"\"...", LL_DEBUG);

										std::pair<IFile*, int64> cbt_hash_file;
										if (hash_key == "thash")
										{
											cbt_hash_file = getCbtHashFile(local_fn);
										}

										FsExtentIterator extent_iterator(orig_file.get());
                                        build_chunk_hashs(orig_file.get(), chunkhashes, NULL, NULL, false, NULL, NULL,
											false, hashf.get(), &extent_iterator, cbt_hash_file);

										IFile* tmp_f = Server->openTemporaryFile();
										ScopedDeleteFile del_tmp_f(tmp_f);
										if (build_chunk_hashs(orig_file.get(), tmp_f, NULL, NULL, false, NULL, NULL, false, hashf2.get()))
										{
											assert(hashf->finalize() == hashf2->finalize());
										}
                                    }

									IFsFile* r_orig_file = orig_file.release();
									restore_download->addToQueueChunked(line, server_fn, local_fn, 
										data.size, metadata, false, r_orig_file, chunkhashes);
								}
								else
								{
									skipped_bytes += data.size;

									restore_download->addToQueueFull(line, server_fn, local_fn, 
                                        data.size, metadata, false, true, 0, NULL);

									std::string tmpfn = chunkhashes->getFilename();
									delete chunkhashes;
									Server->deleteFile(tmpfn);
								}
							}
						}
					}
					else
					{
						IFsFile* orig_file = NULL;
						if (restore_flags & restore_flag_open_all_files_first)
						{
							std::map<std::string, IFsFile*>::iterator it = open_files.find(local_fn);
							if (it == open_files.end())
							{
								log("Cannot find \"" + local_fn + "\" in open file list", LL_ERROR);
								has_error = true;
							}
							else
							{
								open_files.erase(it);
							}
						}

						if (data.size == 0)
						{
							std::auto_ptr<IFile> touch_file;
							if (orig_file == NULL)
							{
								touch_file.reset(Server->openFile(os_file_prefix(local_fn), MODE_RW_CREATE_RESTORE));
							}
							else
							{
								touch_file.reset(orig_file);
							}

							if (touch_file.get() == NULL)
							{
								log("Cannot touch file \"" + local_fn + "\". " + os_last_error_str(), LL_ERROR);
								has_error = true;
							}
							else
							{
								restore_download->addToQueueFull(line, server_fn, local_fn,
									data.size, metadata, false, true, 0, NULL);
							}
						}
						else
						{
							restore_download->addToQueueFull(line, server_fn, local_fn,
								data.size, metadata, false, false, 0, orig_file);
						}
					}
				}
				++line;
			}
		}

	} while (read>0 && !has_error);

	if(!single_item && clean_other
		&& !has_error
		&& !skip_last_dir)
	{
		bool has_include_exclude = false;
		if(!removeFiles(restore_path, share_path, restore_download.get(), folder_files, deletion_queue, has_include_exclude,
						tids, &client_dao, token_cache))
		{
			has_error=true;
		}
	}

	if (!open_files.empty())
	{
		log("There are still " + convert(open_files.size()) + " open files.", LL_ERROR);
		for (std::map<std::string, IFsFile*>::iterator it = open_files.begin();
			it != open_files.end(); ++it)
		{
			Server->destroy(it->second);
		}
	}


    restore_download->queueStop();

    while(!Server->getThreadPool()->waitFor(restore_download_ticket, 1000))
    {
        if(total_size==0)
        {
			restore_updater.update_pc(100, total_size, total_size);
        }
        else
        {
			int64 done_bytes = fc.getReceivedDataBytes(true) + fc_chunked->getReceivedDataBytes(true) + skipped_bytes;
            int pcdone = (std::min)(100,(int)(((float)done_bytes)/((float)total_size/100.f)+0.5f));
			restore_updater.update_pc(pcdone, total_size, done_bytes);
        }

		calculateDownloadSpeed(fc, fc_chunked.get());
    }

#ifdef _WIN32
	if(!has_error && !restore_download->hasError())
	{
		if(!deletion_queue.empty())
		{
			request_restart = true;

			if(!deleteFilesOnRestart(deletion_queue))
			{
				has_error=true;
			}
		}

		if(!rename_queue.empty())
		{
			request_restart = true;
			if(!renameFilesOnRestart(rename_queue))
			{
				has_error=true;
			}
		}

		rename_queue = restore_download->getRenameQueue();
		if(!rename_queue.empty())
		{
			request_restart = true;
			if(!renameFilesOnRestart(rename_queue))
			{
				has_error=true;
			}
		}
	}
#endif

    if(restore_download->hasError())
    {
        log("Error while downloading files during restore", LL_ERROR);
        return false;
    }

    return !has_error;
}

void RestoreFiles::log( const std::string& msg, int loglevel )
{
	Server->Log(msg, loglevel);
	if(loglevel>=LL_INFO
		&& !is_offline)
	{
		ClientConnector::tochannelLog(log_id, msg, loglevel, server_token);
	}
}

std::auto_ptr<FileClientChunked> RestoreFiles::createFcChunked()
{
	IPipe* conn = new_fileclient_connection();

	if(conn==NULL)
	{
		return std::auto_ptr<FileClientChunked>();
	}

	return std::auto_ptr<FileClientChunked>(new FileClientChunked(conn, true, &tcpstack, this,
		NULL, client_token, NULL));
}

void RestoreFiles::log_progress(const std::string & fn, int64 total, int64 downloaded, int64 speed_bps)
{
	if (total > 0)
	{
		std::string cfn;
		if (next(fn, 0, "clientdl"))
		{
			cfn = getafter("/", fn);
		}
		else
		{
			cfn = fn;
		}

		curr_restore_updater->update_fn(cfn, (int)(((downloaded*100.f) / total) + 0.5f));
	}
}

void RestoreFiles::restore_failed(client::FileMetadataDownloadThread& metadata_thread, THREADPOOL_TICKET metadata_dl)
{
	log("Restore failed.", LL_INFO);

	metadata_thread.shutdown(false);

	Server->getThreadPool()->waitFor(metadata_dl);

	ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
}

bool RestoreFiles::removeFiles( std::string restore_path, std::string share_path, RestoreDownloadThread* restore_download,
	std::stack<std::vector<std::string> > &folder_files, std::vector<std::string> &deletion_queue, bool& has_include_exclude,
	const std::vector<int64>& tids, ClientDAO* clientdao, tokens::TokenCache& cache)
{
	bool ret=true;

	bool get_files_error;
	std::vector<SFile> files = getFiles(os_file_prefix(restore_path), &get_files_error, ignore_other_fs);

	if(get_files_error)
	{
		log("Error enumerating files in \""+restore_path+"\". "+os_last_error_str(), LL_ERROR);
		if ((os_get_file_type(os_file_prefix(restore_path)) & EFileType_Symlink) == 0)
		{
			ret = false;
		}
	}
	else
	{
		for(size_t j=0;j<files.size();++j)
		{
#ifdef _WIN32
			std::string fn_lower = strlower(files[j].name);
#else
			std::string fn_lower = files[j].name;
#endif

			if( (folder_files.empty()
				  || std::find(folder_files.top().begin(), folder_files.top().end(), fn_lower)==folder_files.top().end())
				&& !restore_download->isRenamedFile(restore_path + os_file_sep() + files[j].name) )
			{
				std::string cpath = restore_path+os_file_sep()+files[j].name;
				std::string csharepath = share_path + os_file_sep() + files[j].name;

				if (IndexThread::isExcluded(exclude_dirs, cpath)
					|| IndexThread::isExcluded(exclude_dirs, csharepath))
				{
					has_include_exclude = true;
					continue;
				}
				
				if (!IndexThread::isIncluded(include_dirs, cpath, NULL)
					&& !IndexThread::isIncluded(include_dirs, csharepath, NULL))
				{
					has_include_exclude = true;
					continue;
				}

				if(files[j].isdir
					&& !files[j].issym)
				{
					bool del_has_include_exclude = false;
					std::stack<std::vector<std::string> > dummy_folder_files;
					if (!canDeleteFromDir(cpath, tids, clientdao, cache))
					{
						log("No permission to delete files in directory \"" + restore_path + "\".", LL_DEBUG);
					}
					else if( removeFiles(cpath, csharepath, restore_download, dummy_folder_files, deletion_queue, del_has_include_exclude,
										 tids, clientdao, cache)
						&& !del_has_include_exclude)
					{
						log("Deleting directory \"" + restore_path + "\".", LL_DEBUG);
						if (!os_remove_dir(os_file_prefix(cpath)))
						{
							log("Error deleting directory \"" + restore_path + "\". " + os_last_error_str(), LL_WARNING);
#ifndef _WIN32
							ret = false;
#else
							deletion_queue.push_back(cpath);
#endif
						}
					}


				}
				else
				{
					log("Deleting file \"" + cpath + "\".", LL_DEBUG);

					if (!canDelete(cpath, tids, clientdao, cache))
					{
						log("No permission to delete file/directory \"" + cpath + "\".", LL_WARNING);
					}
					else if( (!files[j].isdir && !Server->deleteFile(os_file_prefix(cpath)) )
						|| (files[j].isdir && !os_remove_symlink_dir(os_file_prefix(cpath))) )
					{
						log("Error deleting file \""+ cpath +"\". "+os_last_error_str(), LL_WARNING);

#ifndef _WIN32
						ret=false;
#else
						deletion_queue.push_back(cpath);
#endif
					}
				}
			}
		}
	}

	return ret;
}

bool RestoreFiles::deleteFilesOnRestart( std::vector<std::string> &deletion_queue_dirs )
{
	for(size_t i=0;i<deletion_queue_dirs.size();++i)
	{
		int ftype = os_get_file_type(deletion_queue_dirs[i]);

		if(ftype & EFileType_Directory)
		{
			if(!deleteFolderOnRestart(deletion_queue_dirs[i]))
			{
				log("Error deleting folder "+deletion_queue_dirs[i]+" on restart", LL_ERROR);
				return false;
			}
		}
		else
		{
			if(!deleteFileOnRestart(deletion_queue_dirs[i]))
			{
				log("Error deleting file "+deletion_queue_dirs[i]+" on restart", LL_ERROR);
				return false;
			}
		}
	}

	return true;
}

bool RestoreFiles::deleteFileOnRestart( const std::string& fpath )
{
#ifndef _WIN32
	return false;
#else
	BOOL b = MoveFileExW(Server->ConvertToWchar(os_file_prefix(fpath)).c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	if(b!=TRUE)
	{
		log("Error deleting file "+fpath+" on restart. "+os_last_error_str(), LL_ERROR);
	}
	return b==TRUE;
#endif
}

bool RestoreFiles::deleteFolderOnRestart( const std::string& fpath )
{
	std::vector<SFile> files = getFiles(os_file_prefix(fpath), NULL, ignore_other_fs);

	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir)
		{
			if(!deleteFolderOnRestart(fpath + os_file_sep() + files[i].name))
			{
				return false;
			}
		}
		else
		{
			if(!deleteFileOnRestart(fpath + os_file_sep() + files[i].name))
			{
				return false;
			}
		}
	}

#ifndef _WIN32
	return false;
#else
	BOOL b = MoveFileExW(Server->ConvertToWchar(os_file_prefix(fpath)).c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	if(b!=TRUE)
	{
		log("Error deleting folder "+fpath+" on restart. "+os_last_error_str(), LL_ERROR);
	}
	return b==TRUE;
#endif
}

bool RestoreFiles::renameFilesOnRestart( std::vector<std::pair<std::string, std::string> >& rename_queue )
{
#ifndef _WIN32
	return false;
#else
	for(size_t i=0;i<rename_queue.size();++i)
	{
		BOOL b = MoveFileExW(Server->ConvertToWchar(os_file_prefix(rename_queue[i].first)).c_str(), 
			Server->ConvertToWchar(os_file_prefix(rename_queue[i].second)).c_str(), MOVEFILE_DELAY_UNTIL_REBOOT);
		if(b!=TRUE)
		{
			log("Error renaming "+rename_queue[i].first+" to "+rename_queue[i].second+" on Windows restart. "+os_last_error_str(), LL_ERROR);
			return false;
		}
	}
	return true;
#endif
}

void RestoreFiles::calculateDownloadSpeed(FileClient & fc, FileClientChunked * fc_chunked)
{
	int64 ctime = Server->getTimeMS();
	if (speed_set_time == 0)
	{
		speed_set_time = ctime;
	}

	if (ctime - speed_set_time>10000)
	{
		int64 received_data_bytes = fc.getTransferredBytes() + (fc_chunked != NULL ? fc_chunked->getTransferredBytes() : 0);

		int64 new_bytes = received_data_bytes - last_speed_received_bytes;
		int64 passed_time = ctime - speed_set_time;

		if (passed_time > 0)
		{
			speed_set_time = ctime;

			double speed_bpms = static_cast<double>(new_bytes) / passed_time;

			if (last_speed_received_bytes > 0)
			{
				curr_restore_updater->update_speed(speed_bpms);
			}

			last_speed_received_bytes = received_data_bytes;
		}
	}
}

bool RestoreFiles::createDirectoryWin(const std::string & dir)
{
#ifdef _WIN32
	if (GetLastError() == ERROR_ACCESS_DENIED)
	{
		std::string cdir = ExtractFilePath(dir, os_file_sep());
		while (!cdir.empty())
		{
			if (change_file_permissions_admin_only(os_file_prefix(cdir)))
			{
				if (os_create_dir(os_file_prefix(dir)))
				{
					return true;
				}
				else
				{
					if (GetLastError() != ERROR_ACCESS_DENIED)
					{
						return false;
					}
				}
			}

			cdir = ExtractFilePath(cdir, os_file_sep());
		}
	}
	return false;
#else
	return false;
#endif
}

std::pair<IFile*, int64> RestoreFiles::getCbtHashFile(const std::string & fn)
{
	std::string vol = fn;
	IndexThread::normalizeVolume(vol);
	vol = strlower(vol);

	std::map<std::string, std::pair<IFile*, int64> >::iterator it = cbt_hash_files.find(vol);
	if (it != cbt_hash_files.end())
	{
		return it->second;
	}

	CWData data;
	IPipe* localpipe = Server->createMemoryPipe();
	data.addChar(IndexThread::IndexThreadAction_SnapshotCbt);
	data.addVoidPtr(localpipe);
	data.addString(fn);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	std::string ret;
	localpipe->Read(&ret);
	localpipe->Write("exit");

	if (ret == "done")
	{
		IFile* cbt_hash_file = Server->openFile("urbackup\\hdat_file_" + conv_filename(vol) + ".dat", MODE_RW_CREATE_DEVICE);
		int64 cbt_hash_file_blocksize = -1;

#ifdef _WIN32
		DWORD sectors_per_cluster;
		DWORD bytes_per_sector;
		BOOL b = GetDiskFreeSpaceW((Server->ConvertToWchar(vol) + L"\\").c_str(),
			&sectors_per_cluster, &bytes_per_sector, NULL, NULL);
		if (!b)
		{
			Server->Log("Error in GetDiskFreeSpaceW. " + os_last_error_str(), LL_ERROR);
		}
		else
		{
			cbt_hash_file_blocksize = bytes_per_sector * sectors_per_cluster;
		}
#endif

		cbt_hash_files[vol] = std::make_pair(cbt_hash_file, cbt_hash_file_blocksize);
		return std::make_pair(cbt_hash_file, cbt_hash_file_blocksize);
	}
	else
	{
		IFile* nf = NULL;
		cbt_hash_files[vol] = std::make_pair(nf, -1);
		return std::make_pair(nf, -1);
	}
}
