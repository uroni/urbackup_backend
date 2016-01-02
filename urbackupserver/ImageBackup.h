#pragma once
#include "Backup.h"
#include "../Interface/Types.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "server_status.h"

class ServerVHDWriter;
class IFile;
class ServerPingThread;

class ImageBackup : public Backup
{
public:
	ImageBackup(ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname,
		LogAction log_action, bool incremental, std::string letter, std::string server_token);

	int getBackupId()
	{
		return backupid;
	}

protected:
	virtual bool doBackup();

	bool doImage(const std::string &pLetter, const std::string &pParentvhd, int incremental, int incremental_ref,
		bool transfer_checksum, std::string image_file_format, bool transfer_bitmap);
	unsigned int writeMBR(ServerVHDWriter* vhdfile, uint64 volsize);
	int64 updateNextblock(int64 nextblock, int64 currblock, sha256_ctx* shactx, unsigned char* zeroblockdata,
		bool parent_fn, ServerVHDWriter* parentfile, IFile* hashfile, IFile* parenthashfile, unsigned int blocksize,
		int64 mbr_offset, int64 vhd_blocksize, bool &warned_about_parenthashfile_error, int64 empty_vhdblock_start);
	SBackup getLastImage(const std::string &letter, bool incr);
	std::string constructImagePath(const std::string &letter, std::string image_file_format, bool full_backup);
	std::string getMBR(const std::string &dl);

	std::string letter;

	bool synthetic_full;

	int backupid;

	std::string backuppath_single;

	ServerPingThread* pingthread;
	THREADPOOL_TICKET pingthread_ticket;
};