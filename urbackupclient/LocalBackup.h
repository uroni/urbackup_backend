#pragma once
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include <memory>
#include "../Interface/BackupFileSystem.h"
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
		virtual int getFileType(const std::string& path) override;
		virtual std::vector<SFile> listFiles(const std::string& path) override;
		virtual bool createSubvol(const std::string& path) override;
		virtual bool createSnapshot(const std::string& src_path, const std::string& dest_path) override;
		virtual bool rename(const std::string& src_name, const std::string& dest_name) override;
		virtual bool removeDirRecursive(const std::string& path) override;
		virtual bool directoryExists(const std::string& path) override;
		virtual bool linkSymbolic(const std::string& target, const std::string& lname) override;
		virtual bool copyFile(const std::string& src, const std::string& dst, bool flush, std::string* error_str) override;
		
		bool renameToFinal();

		std::string getPrefix() {
			return prefix;
		}

		IBackupFileSystem* root() {
			return backup_files;
		}

		virtual bool sync(const std::string& path) override;
		virtual bool deleteSubvol(const std::string& path) override;
		virtual int64 totalSpace() override;
		virtual int64 freeSpace() override;
		virtual int64 freeMetadataSpace() override;
		virtual int64 unallocatedSpace() override;
		virtual bool forceAllocMetadata() override;
		virtual bool balance(int usage, size_t limit, bool metadata, bool& enospc, size_t& relocated) override;
		virtual std::string fileSep() override;
		virtual std::string filePath(IFile* f) override;
		virtual bool getXAttr(const std::string& path, const std::string& key, std::string& value) override;
		virtual bool setXAttr(const std::string& path, const std::string& key, const std::string& val) override;
		virtual std::string getName() override;
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

	bool createSymlink(const std::string& name, size_t depth, const std::string& symlink_target, const std::string& dir_sep, bool isdir);

	std::string fixFilenameForOS(std::string fn) {
		return fn;
	}

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
