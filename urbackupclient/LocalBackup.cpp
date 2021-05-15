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
#include <memory>
#include <set>

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
					ClientConnector::updateRestorePc(local_process_id, backup_id, status_id, curr_pc, server_token, std::string(), -1, total_bytes, done_bytes, speed_bpms);

					if (Server->getTimeMS() - last_fn_time > 61000)
					{
					}

					update_cond->wait(&lock, 60000);
				}
			}
			ClientConnector::updateRestorePc(local_process_id, backup_id, status_id, 101, server_token, std::string(), -1, total_bytes, success ? total_bytes : -2, 0);
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
		std::auto_ptr<IMutex> update_mutex;
		std::auto_ptr<ICondition> update_cond;
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

LocalBackup::LocalBackup(IBackupFileSystem* p_backup_files, int64 local_process_id, int64 server_log_id, 
	int64 server_status_id, int64 backupid, std::string server_token, std::string server_identity, int facet_id)
	: backup_updater_thread(NULL),
	server_log_id(server_log_id), server_status_id(server_status_id),
	backupid(backupid), server_token(std::move(server_token)), server_identity(std::move(server_identity)),
	local_process_id(local_process_id), facet_id(facet_id),
	orig_backup_files(p_backup_files)
{
	
}

void LocalBackup::onStartBackup()
{
	backup_updater_thread = new BackupUpdaterThread(local_process_id, backupid, server_status_id, server_token);
}

void LocalBackup::onBackupFinish()
{
	backup_updater_thread->stop();
	backup_updater_thread = NULL;
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

unsigned int LocalBackup::PrefixedBackupFiles::getFileType(const std::string& path)
{
	return backup_files->getFileType(prefix+path);
}

bool LocalBackup::PrefixedBackupFiles::Flush()
{
	return backup_files->Flush();
}

bool LocalBackup::PrefixedBackupFiles::renameToFinal()
{
	return backup_files->rename(prefix.substr(0, prefix.size() - 1),
		prefix.substr(0, prefix.size() - 5));
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

std::vector<SBtrfsFile> LocalBackup::PrefixedBackupFiles::listFiles(const std::string& path)
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
