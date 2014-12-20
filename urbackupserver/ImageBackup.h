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
	ImageBackup(ClientMain* client_main, int clientid, std::wstring clientname, LogAction log_action, bool incremental, std::string letter);

	int getBackupId()
	{
		return backupid;
	}

protected:
	virtual bool doBackup();

	bool doImage(const std::string &pLetter, const std::wstring &pParentvhd, int incremental, int incremental_ref,
		bool transfer_checksum, std::string image_file_format);
	unsigned int writeMBR(ServerVHDWriter* vhdfile, uint64 volsize);
	int64 updateNextblock(int64 nextblock, int64 currblock, sha256_ctx* shactx, unsigned char* zeroblockdata,
		bool parent_fn, ServerVHDWriter* parentfile, IFile* hashfile, IFile* parenthashfile, unsigned int blocksize,
		int64 mbr_offset, int64 vhd_blocksize, bool &warned_about_parenthashfile_error);
	SBackup getLastImage(const std::string &letter, bool incr);
	std::wstring constructImagePath(const std::wstring &letter, std::string image_file_format);
	std::string getMBR(const std::wstring &dl);

	std::string letter;

	bool synthetic_full;

	int backupid;

	ServerPingThread* pingthread;
	THREADPOOL_TICKET pingthread_ticket;
};