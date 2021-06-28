#pragma once
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include <memory>
#include "../Interface/BackupFileSystem.h"
namespace
{
	class BackupUpdaterThread;

	const unsigned int status_update_intervall = 1000;
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
		virtual IFile* getBackingFile() override;
		virtual std::string lastError() override;
		virtual std::vector<SChunk> getChunks() override;
	private:
		std::string prefix;
		IBackupFileSystem* backup_files;		
	};

public:
	LocalBackup(bool file, bool incr, int64 local_process_id,
		int64 server_log_id, int64 server_status_id, int64 backupid,
		std::string server_token, std::string server_identity, int facet_id,
		size_t max_backups, const std::string& dest_url, const std::string& dest_url_params,
		const str_map& dest_secret_params);
protected:
	bool onStartBackup();
	void onBackupFinish(bool image);

	void updateProgressPc(int new_pc, int64 p_total_bytes, int64 p_done_bytes);
	void updateProgressDetails(const std::string& details);
	void updateProgressSuccess(bool b);
	void updateProgressSpeed(double n_speed_bpms);

	void prepareBackupFiles(const std::string& prefix);

	bool createSymlink(const std::string& name, size_t depth, const std::string& symlink_target, const std::string& dir_sep, bool isdir);

	bool sync();

	bool sendLogBuffer();

	void log(const std::string& msg, int loglevel);

	void updateProgress(int64 ctime);

	void logIndexResult();

	std::string fixFilenameForOS(std::string fn) {
		return fn;
	}

	bool cleanupOldBackups(bool image);

	bool openFileSystem();

	void calculateBackupSpeed(int64 ctime);

	void calculateEta(int64 ctime);

	std::unique_ptr<PrefixedBackupFiles> backup_files;
	std::unique_ptr<IBackupFileSystem> orig_backup_files;
	std::string server_token;
	std::string server_identity;
	int64 local_process_id;
	int64 server_log_id;
	int64 server_status_id;
	int64 backupid;
	int facet_id;
	size_t max_backups;
	bool backup_success = false;
	std::string dest_url;
	std::string dest_url_params;
	str_map dest_secret_params;

	std::vector<std::pair<std::string, int> > log_buffer;

	int64 total_bytes = 0;
	int64 done_bytes = 0;
	int64 file_done_bytes = 0;
	int64 speed_set_time = 0;
	int64 last_speed_received_bytes = 0;
	double curr_speed_bpms = 0;
	int64 last_eta_update = 0;
	int64 last_eta_received_bytes = 0;
	int64 eta_set_time = 0;
	int64 eta_estimated_speed = 0;
	int64 curr_eta = 0;

	bool file;
	bool incr;

private:

	BackupUpdaterThread* backup_updater_thread;
};
