#pragma once
#include "Backup.h"
#include "../Interface/Types.h"
#include "dao/ServerBackupDao.h"
#include <string>
#include "server_settings.h"
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include <map>
#include "../urbackupcommon/file_metadata.h"
#include "server_log.h"

class ClientMain;
class BackupServerHash;
class BackupServerPrepareHash;
class ServerPingThread;
class FileIndex;
namespace server {
class FileMetadataDownloadThread;
}

namespace
{
	const unsigned int status_update_intervall=1000;
	const unsigned int eta_update_intervall=60000;
}

struct SContinuousSequence
{
	SContinuousSequence()
		: id(-1), next(-1)
	{

	}

	SContinuousSequence(int64 id, int64 next)
		: id(id), next(next)
	{

	}
	int64 id;
	int64 next;
};

class FileBackup : public Backup
{
public:
	FileBackup(ClientMain* client_main, int clientid, std::wstring clientname, LogAction log_action, bool is_incremental, int group, bool use_tmpfiles, std::wstring tmpfile_path, bool use_reflink, bool use_snapshots);
	~FileBackup();

	bool getResult();
	bool hasEarlyError();
	bool hasDiskError();

	int getBackupid()
	{
		return backupid;
	}

	std::map<std::wstring, SContinuousSequence> getContinuousSequences()
	{
		return continuous_sequences;
	}

	static std::wstring convertToOSPathFromFileClient(std::wstring path);

protected:
	virtual bool doBackup();

	virtual bool doFileBackup() = 0;

	ServerBackupDao::SDuration interpolateDurations(const std::vector<ServerBackupDao::SDuration>& durations);
	bool request_filelist_construct(bool full, bool resume, int group, bool with_token, bool& no_backup_dirs, bool& connect_fail);
	void logVssLogdata();
	bool getTokenFile(FileClient &fc, bool hashed_transfer );
	std::string clientlistName( int group, bool new_list=false );
	void createHashThreads(bool use_reflink);
	void destroyHashThreads();
	_i64 getIncrementalSize(IFile *f, const std::vector<size_t> &diffs, bool all=false);
	void calculateEtaFileBackup( int64 &last_eta_update, int64& eta_set_time, int64 ctime, FileClient &fc, FileClientChunked* fc_chunked,
		int64 linked_bytes, int64 &last_eta_received_bytes, double &eta_estimated_speed, _i64 files_size );
	bool hasChange(size_t line, const std::vector<size_t> &diffs);
	std::wstring fixFilenameForOS(const std::wstring& fn);	
	bool link_file(const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path,
		const std::wstring &os_path, const std::string& sha2, _i64 filesize, bool add_sql, const FileMetadata& metadata);
	void sendBackupOkay(bool b_okay);
	void notifyClientBackupSuccessfull(void);
	void waitForFileThreads();
	bool verify_file_backup(IFile *fileentries);
	void save_debug_data(const std::wstring& rfn, const std::string& local_hash, const std::string& remote_hash);
	std::string getSHA256(const std::wstring& fn);
	std::string getSHA512(const std::wstring& fn);
	bool constructBackupPath(bool with_hashes, bool on_snapshot, bool create_fs);
	bool constructBackupPathCdp();
	std::string systemErrorInfo();
	void saveUsersOnClient();
	void createUserViews(IFile* file_list_f);
	bool createUserView(IFile* file_list_f, const std::vector<int>& ids, std::string accoutname, const std::vector<size_t>& identical_permission_roots);
	std::vector<size_t> findIdenticalPermissionRoots(IFile* file_list_f, const std::vector<int>& ids);
	void deleteBackup();
	bool createSymlink(const std::wstring& name, size_t depth, const std::wstring& symlink_target, const std::wstring& dir_sep, bool isdir);
	bool startFileMetadataDownloadThread();
	bool stopFileMetadataDownloadThread();

	int group;
	bool use_tmpfiles;
	std::wstring tmpfile_path;
	bool use_reflink;
	bool use_snapshots;
	bool with_hashes;
	bool cdp_path;

	std::wstring backuppath;
	std::wstring dir_pool_path;
	std::wstring backuppath_hashes;
	std::wstring backuppath_single;

	IPipe *hashpipe;
	IPipe *hashpipe_prepare;
	BackupServerHash *bsh;
	THREADPOOL_TICKET bsh_ticket;
	BackupServerPrepareHash *bsh_prepare;
	THREADPOOL_TICKET bsh_prepare_ticket;
	std::auto_ptr<BackupServerHash> local_hash;

	ServerPingThread* pingthread;
	THREADPOOL_TICKET pingthread_ticket;

	std::map<std::wstring, SContinuousSequence> continuous_sequences;

	std::auto_ptr<FileIndex> fileindex;

	bool disk_error;

	int backupid;

    std::auto_ptr<server::FileMetadataDownloadThread> metadata_download_thread;
	THREADPOOL_TICKET metadata_download_thread_ticket;
};
