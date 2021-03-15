#pragma once
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include <memory>
#include "../btrfs/btrfsplugin/IBackupFileSystem.h"
namespace
{
	class BackupUpdaterThread;
}

class LocalBackup : public IThread
{
	class PrefixedBackupFiles : public IBackupFileSystem
	{
	public:
		PrefixedBackupFiles(IBackupFileSystem* backup_files, std::string prefix) :
			backup_files(backup_files), prefix(std::move(prefix)) {}

		~PrefixedBackupFiles()
		{
			delete backup_files;
		}

		virtual bool hasError() override;
		virtual IFsFile* openFile(const std::string& path, int mode) override;
		virtual bool reflinkFile(const std::string& source, const std::string& dest) override;
		virtual bool createDir(const std::string& path) override;
		virtual bool deleteFile(const std::string& path) override;
		virtual EFileType getFileType(const std::string& path) override;
		virtual bool Flush() override;
		virtual std::vector<SBtrfsFile> listFiles(const std::string& path) override;
		virtual bool createSubvol(const std::string& path) override;
		virtual bool createSnapshot(const std::string& src_path, const std::string& dest_path) override;
		virtual bool rename(const std::string& src_name, const std::string& dest_name) override;
		
		bool renameToFinal();

	private:
		std::string prefix;
		IBackupFileSystem* backup_files;
	};

public:
	LocalBackup(IBackupFileSystem* backup_files, int64 local_process_id,
		int64 server_log_id, int64 server_status_id, int64 backupid,
		std::string server_token, std::string server_identity, int facet_id);
protected:
	void onStartBackup();
	void onBackupFinish();

	void updateProgressPc(int new_pc, int64 p_total_bytes, int64 p_done_bytes);
	void updateProgressDetails(const std::string& details);
	void updateProgressSuccess(bool b);
	void updateProgressSpeed(double n_speed_bpms);

	void prepareBackupFiles(const std::string& prefix);

	std::unique_ptr<PrefixedBackupFiles> backup_files;
	std::unique_ptr<IBackupFileSystem> orig_backup_files;
	std::string server_token;
	std::string server_identity;
	int64 local_process_id;
	int64 server_log_id;
	int64 server_status_id;
	int64 backupid;
	int facet_id;
private:

	BackupUpdaterThread* backup_updater_thread;
};
