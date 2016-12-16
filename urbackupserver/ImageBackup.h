#pragma once
#include "Backup.h"
#include "../Interface/Types.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "server_status.h"

class IMutex;
class ServerVHDWriter;
class IFile;
class ServerPingThread;
class ScopedLockImageFromCleanup;
class ServerRunningUpdater;

class ImageBackup : public Backup
{
public:
	ImageBackup(ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname,
		LogAction log_action, bool incremental, std::string letter, std::string server_token, std::string details,
		bool set_complete, int64 snapshot_id, std::string snapshot_group_loginfo, int64 backup_starttime, bool scheduled);

	int getBackupId()
	{
		return backupid;
	}

	bool getNotFound()
	{
		return not_found;
	}

	std::string getLetter()
	{
		return letter;
	}

	int64 getBackupStarttime()
	{
		return backup_starttime;
	}

	struct SImageDependency
	{
		std::string volume;
		int64 snapshot_id;
	};

	std::vector<SImageDependency> getDependencies(bool reset);

protected:
	virtual bool doBackup();

	bool doImage(const std::string &pLetter, const std::string &pParentvhd, int incremental, int incremental_ref,
		bool transfer_checksum, std::string image_file_format, bool transfer_bitmap, bool transfer_prev_cbitmap);
	unsigned int writeMBR(ServerVHDWriter* vhdfile, uint64 volsize);
	int64 updateNextblock(int64 nextblock, int64 currblock, sha256_ctx* shactx, unsigned char* zeroblockdata,
		bool parent_fn, IFile* hashfile, IFile* parenthashfile, unsigned int blocksize,
		int64 mbr_offset, int64 vhd_blocksize, bool &warned_about_parenthashfile_error, int64 empty_vhdblock_start,
		ServerVHDWriter* vhdfile, int64 trim_add);
	SBackup getLastImage(const std::string &letter, bool incr);
	std::string constructImagePath(const std::string &letter, std::string image_file_format, std::string pParentvhd);
	std::string getMBR(const std::string &dl, bool& fatal_error);
	bool runPostBackupScript(bool incr, const std::string& path, const std::string &pLetter, bool success);
	void addBackupToDatabase(const std::string &pLetter, const std::string &pParentvhd,
		int incremental, int incremental_ref, const std::string& imagefn, ScopedLockImageFromCleanup& cleanup_lock,
		ServerRunningUpdater *running_updater);
	bool readShadowData(const std::string& shadowdata);

	std::string letter;

	bool synthetic_full;

	bool not_found;

	int backupid;

	std::string backuppath_single;

	ServerPingThread* pingthread;
	THREADPOOL_TICKET pingthread_ticket;

	std::vector<SImageDependency> dependencies;
	std::vector<SImageDependency> ret_dependencies;

	bool set_complete;

	int64 snapshot_id;

	std::auto_ptr<IMutex> mutex;

	std::string snapshot_group_loginfo;

	int64 backup_starttime;
};