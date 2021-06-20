/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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

#include "LocalBackup.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../stringtools.h"
#include "ClientService.h"
#include "../common/data.h"
#include "../clouddrive/IClouddriveFactory.h"
#include "FilesystemManager.h"
#include "client.h"
#include <memory>
#include <set>

extern IClouddriveFactory* clouddrive_fak;

namespace
{
	class BackupUpdaterThread : public IThread
	{
	public:
		BackupUpdaterThread(int64 local_process_id, int64 backup_id, int64 status_id, std::string server_token)
			: update_mutex(Server->createMutex()), stopped(false),
			update_cond(Server->createCondition()), curr_pc(-1),
			backup_id(backup_id), status_id(status_id), server_token(server_token),
			local_process_id(local_process_id), total_bytes(-1), done_bytes(0), speed_bpms(0), success(false),
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
					ClientConnector::updateLocalBackupPc(local_process_id, backup_id, status_id, curr_pc, server_token, std::string(), -1, total_bytes, done_bytes, speed_bpms);

					if (Server->getTimeMS() - last_fn_time > 61000)
					{
					}

					update_cond->wait(&lock, 60000);
				}
			}
			ClientConnector::updateLocalBackupPc(local_process_id, backup_id, status_id, 101, server_token, std::string(), -1, total_bytes, success ? total_bytes : -2, 0);
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

		void update_details(const std::string& details)
		{
			IScopedLock lock(update_mutex.get());
			curr_details = details;
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
		std::unique_ptr<IMutex> update_mutex;
		std::unique_ptr<ICondition> update_cond;
		bool stopped;
		int curr_pc;
		std::string curr_details;
		int64 backup_id;
		int64 status_id;
		std::string server_token;
		int64 local_process_id;
		int64 total_bytes;
		int64 done_bytes;
		double speed_bpms;
		bool success;
		int64 last_fn_time;
	};
}

LocalBackup::LocalBackup(int64 local_process_id, int64 server_log_id, 
	int64 server_status_id, int64 backupid, std::string server_token, std::string server_identity, int facet_id,
	size_t max_backups, const std::string& dest_url, const std::string& dest_url_params,
	const str_map& dest_secret_params)
	: backup_updater_thread(NULL),
	server_log_id(server_log_id), server_status_id(server_status_id),
	backupid(backupid), server_token(std::move(server_token)), server_identity(std::move(server_identity)),
	local_process_id(local_process_id), facet_id(facet_id),
	max_backups(max_backups),
	dest_url(dest_url), dest_url_params(dest_url_params), 
	dest_secret_params(dest_secret_params)
{
	
}

bool LocalBackup::onStartBackup()
{
	if (!openFileSystem())
		return false;

	backup_updater_thread = new BackupUpdaterThread(local_process_id, backupid, server_status_id, server_token);

	return true;
}

void LocalBackup::onBackupFinish(bool image)
{
	backup_updater_thread->stop();
	backup_updater_thread = NULL;

	log("Cleaning up old backups...", LL_INFO);

	if (!cleanupOldBackups(image))
	{
		log("Cleaning up old backups failed", LL_ERROR);
		backup_success = false;
	}

	if (backup_success &&
		clouddrive_fak != nullptr &&
		clouddrive_fak->isCloudFile(backup_files->getBackingFile()))
	{
		log("Calculating background delete data...", LL_INFO);

		int64 transid = clouddrive_fak->getCfTransid(backup_files->getBackingFile());
		if (!clouddrive_fak->runBackgroundWorker(backup_files->getBackingFile(),
			"urbackup/bg_del_" + conv_filename(server_token) + "_" + std::to_string(transid) + "_"+std::to_string(backupid)+".dat"))
		{
			log("Error generating background delete data", LL_ERROR);
			backup_success = false;
		}
	}

	if (!ClientConnector::localBackupDone(server_log_id, server_status_id, backupid,
		backup_success, server_token))
	{
		log("Error notifying server about local backup success", LL_WARNING);
	}

	log("Backup finished.", LL_INFO);

	int64 log_send_starttime = Server->getTimeMS();
	while (Server->getTimeMS() - log_send_starttime<3*60*1000)
	{
		if (sendLogBuffer())
		{
			break;
		}
		else
		{
			Server->wait(5000);
		}
	}
}

void LocalBackup::updateProgressPc(int new_pc, int64 p_total_bytes, int64 p_done_bytes)
{
	backup_updater_thread->update_pc(new_pc, p_total_bytes, p_done_bytes);
}

void LocalBackup::updateProgressDetails(const std::string& details)
{
	backup_updater_thread->update_details(details);
}

void LocalBackup::updateProgressSuccess(bool b)
{
	backup_updater_thread->set_success(b);
}

void LocalBackup::updateProgressSpeed(double n_speed_bpms)
{
	backup_updater_thread->update_speed(n_speed_bpms);
}

void LocalBackup::prepareBackupFiles(const std::string& prefix)
{
	backup_files = std::make_unique<PrefixedBackupFiles>(orig_backup_files.release(), prefix + "\\");
}

bool LocalBackup::createSymlink(const std::string& name, size_t depth, const std::string& symlink_target, const std::string& dir_sep, bool isdir)
{
	std::vector<std::string> toks;
	Tokenize(symlink_target, toks, dir_sep);

	std::string target;

	for (size_t i = 0; i < depth; ++i)
	{
		target += ".." + os_file_sep();
	}

	for (size_t i = 0; i < toks.size(); ++i)
	{
		std::set<std::string> emptyset;
		std::string emptypath;
		std::string component = fixFilenameForOS(toks[i]);

		if (component == ".." || component == ".")
			continue;

		target += component;

		if (i + 1 < toks.size())
		{
			target += os_file_sep();
		}
	}

	if (toks.empty()
		&& isdir)
	{
		target += ".symlink_void_dir";
	}

	if (toks.empty()
		&& !isdir)
	{
		target += ".symlink_void_file";
	}

	//return backup_files->createSymlink(target, os_file_prefix(name), NULL, &isdir);
	return false;
}

bool LocalBackup::sync()
{
	if (!backup_files->sync(std::string()))
	{
		log("Error syncing backup file system -2", LL_ERROR);
		return false;
	}

	IFile* backing_file = backup_files->getBackingFile();

	if (backing_file != nullptr &&
		!backing_file->Sync())
	{
		log("Error syncing backup file system backing file", LL_ERROR);
		return false;
	}

	return true;
}

bool LocalBackup::sendLogBuffer()
{
	size_t send_ok = std::string::npos;
	for (size_t i = 0; i < log_buffer.size(); ++i)
	{
		if (ClientConnector::tochannelLog(server_log_id,
			log_buffer[i].first, log_buffer[i].second, server_token,
			0))
		{
			send_ok = i;
		}
	}

	if (send_ok != std::string::npos)
	{
		log_buffer.erase(log_buffer.begin(), log_buffer.begin() + send_ok + 1);
	}

	return log_buffer.empty();
}

void LocalBackup::log(const std::string& msg, int loglevel)
{
	Server->Log(msg, loglevel);
	if (!sendLogBuffer() ||
		!ClientConnector::tochannelLog(server_log_id, msg,
			loglevel, server_token, 0))
	{
		if (log_buffer.size() > 100)
		{
			log_buffer.erase(log_buffer.begin());
		}
		log_buffer.push_back(std::make_pair(msg, loglevel));
	}
}

void LocalBackup::logIndexResult()
{
	CWData data;
	data.addChar(IndexThread::IndexThreadAction_GetLog);
	unsigned int result_id = IndexThread::getResultId();
	data.addUInt(result_id);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	std::string ret;
	IndexThread::getResult(result_id, 8000, ret);
	IndexThread::removeResult(result_id);


	if (!ret.empty())
	{
		std::vector<std::string> lines;
		Tokenize(ret, lines, "\n");

		for (std::string& line : lines)
		{
			size_t f1 = line.find("-");
			size_t f2 = line.find("-", f1 + 1);
			int ll = watoi(line.substr(0, f1));
			int64 times = watoi64(line.substr(f1+1, f2 - f1));

			log(line.substr(f2 + 1), ll);
		}
	}
}

bool LocalBackup::cleanupOldBackups(bool image)
{
	std::vector<SFile> backups = backup_files->root()->listFiles(std::string());

	size_t n_backups = 0;
	for (SFile& backup: backups)
	{
		if (backup.isdir && backup.name.find("Image") == std::string::npos && !image)
			++n_backups;
		else if (backup.isdir && backup.name.find("Image") != std::string::npos && image)
			++n_backups;
	}

	bool did_cleanup = false;

	while (n_backups > max_backups)
	{
		for (SFile& backup : backups)
		{
			bool del_backup = false;
			if (backup.isdir && backup.name.find("Image") == std::string::npos && !image)
				del_backup = true;
			else if (backup.isdir && backup.name.find("Image") != std::string::npos && image)
				del_backup = true;

			if (del_backup)
			{
				if (!backup_files->root()->deleteSubvol(backup.name))
				{
					log("Error deleting subvol \"" + backup.name + "\". " + backup_files->lastError(), LL_ERROR);
					return false;
				}
				did_cleanup = true;
				--n_backups;
				break;
			}
		}

		backups = backup_files->root()->listFiles(std::string());
	}

	if (did_cleanup)
	{
		if (!backup_files->root()->sync(std::string()))
		{
			log("Error syncing fs after cleanup. " + backup_files->lastError(), LL_ERROR);
			return false;
		}
	}

	return true;
}

bool LocalBackup::openFileSystem()
{
	if (!FilesystemManager::openFilesystemImage(dest_url, dest_url_params, dest_secret_params))
	{
		log("Error opening destination file system "+dest_url, LL_ERROR);
		return false;
	}	

	orig_backup_files.reset(FilesystemManager::getFileSystem(dest_url));

	if (!orig_backup_files)
	{
		log("Error getting destination file system " + dest_url, LL_ERROR);
		return false;
	}

	return true;
}

bool LocalBackup::PrefixedBackupFiles::hasError()
{
	return backup_files->hasError();
}

IFsFile* LocalBackup::PrefixedBackupFiles::openFile(const std::string& path, int mode)
{
	return backup_files->openFile(prefix + path, mode);
}

bool LocalBackup::PrefixedBackupFiles::reflinkFile(const std::string& source, const std::string& dest)
{
	return backup_files->reflinkFile(source, prefix+dest);
}

bool LocalBackup::PrefixedBackupFiles::createDir(const std::string& path)
{
	return backup_files->createDir(prefix + path);
}

bool LocalBackup::PrefixedBackupFiles::deleteFile(const std::string& path)
{
	return backup_files->deleteFile(prefix + path);
}

int LocalBackup::PrefixedBackupFiles::getFileType(const std::string& path)
{
	return backup_files->getFileType(prefix+path);
}

bool LocalBackup::PrefixedBackupFiles::renameToFinal()
{
	return backup_files->rename(prefix.substr(0, prefix.size() - 1),
		prefix.substr(0, prefix.size() - 5));
}

bool LocalBackup::PrefixedBackupFiles::sync(const std::string& path)
{
	return backup_files->sync(prefix+path);
}

bool LocalBackup::PrefixedBackupFiles::deleteSubvol(const std::string& path)
{
	return backup_files->deleteSubvol(prefix+path);
}

int64 LocalBackup::PrefixedBackupFiles::totalSpace()
{
	return backup_files->totalSpace();
}

int64 LocalBackup::PrefixedBackupFiles::freeSpace()
{
	return backup_files->freeSpace();
}

int64 LocalBackup::PrefixedBackupFiles::freeMetadataSpace()
{
	return backup_files->freeMetadataSpace();
}

int64 LocalBackup::PrefixedBackupFiles::unallocatedSpace()
{
	return backup_files->unallocatedSpace();
}

bool LocalBackup::PrefixedBackupFiles::forceAllocMetadata()
{
	return backup_files->forceAllocMetadata();
}

bool LocalBackup::PrefixedBackupFiles::balance(int usage, size_t limit, bool metadata, bool& enospc, size_t& relocated)
{
	return backup_files->balance(usage, limit, metadata, enospc, relocated);
}

std::string LocalBackup::PrefixedBackupFiles::fileSep()
{
	return backup_files->fileSep();
}

std::string LocalBackup::PrefixedBackupFiles::filePath(IFile* f)
{
	return backup_files->filePath(f);
}

bool LocalBackup::PrefixedBackupFiles::getXAttr(const std::string& path, const std::string& key, std::string& value)
{
	return backup_files->getXAttr(prefix + path, key, value);
}

bool LocalBackup::PrefixedBackupFiles::setXAttr(const std::string& path, const std::string& key, const std::string& val)
{
	return backup_files->setXAttr(prefix + path, key, val);
}

std::string LocalBackup::PrefixedBackupFiles::getName()
{
	return prefix + "|" + backup_files->getName();
}

IFile* LocalBackup::PrefixedBackupFiles::getBackingFile()
{
	return nullptr;
}

std::string LocalBackup::PrefixedBackupFiles::lastError()
{
	return std::string();
}

std::vector<IBackupFileSystem::SChunk> LocalBackup::PrefixedBackupFiles::getChunks()
{
	return backup_files->getChunks();
}

bool LocalBackup::PrefixedBackupFiles::copyFile(const std::string& src, const std::string& dst, bool flush, std::string* error_str)
{
	return backup_files->copyFile(prefix+src, prefix+dst, flush, error_str);
}

bool LocalBackup::PrefixedBackupFiles::removeDirRecursive(const std::string& path)
{
	return backup_files->removeDirRecursive(prefix+path);
}

bool LocalBackup::PrefixedBackupFiles::directoryExists(const std::string& path)
{
	return backup_files->directoryExists(prefix+path);
}

bool LocalBackup::PrefixedBackupFiles::linkSymbolic(const std::string& target, const std::string& lname)
{
	return backup_files->linkSymbolic(prefix+target, prefix+lname);
}

std::vector<SFile> LocalBackup::PrefixedBackupFiles::listFiles(const std::string& path)
{
	return backup_files->listFiles(prefix + path);
}

bool LocalBackup::PrefixedBackupFiles::createSubvol(const std::string& path)
{
	return backup_files->createSubvol(prefix + path);
}

bool LocalBackup::PrefixedBackupFiles::createSnapshot(const std::string& src_path, const std::string& dest_path)
{
	return backup_files->createSnapshot(prefix+src_path, prefix+dest_path);
}

bool LocalBackup::PrefixedBackupFiles::rename(const std::string& src_name, const std::string& dest_name)
{
	return backup_files->rename(prefix+src_name, prefix+dest_name);
}
