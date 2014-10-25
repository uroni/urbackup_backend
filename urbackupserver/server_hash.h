#pragma once

#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"
#include "ChunkPatcher.h"
#include "server_prepare_hash.h"
#include "FileIndex.h"
#include "dao/ServerBackupDao.h"
#include <vector>


class FileMetadata;

struct STmpFile
{
	STmpFile(int backupid, std::wstring fp, std::wstring hashpath)
		: backupid(backupid), fp(fp), hashpath(hashpath)
	{
	}
	STmpFile(void) {}

	int backupid;
	std::wstring fp;
	std::wstring hashpath;
};

class BackupServerHash : public IThread, public INotEnoughSpaceCallback, public IChunkPatcherCallback
{
public:

	enum EAction
	{
		EAction_LinkOrCopy,
		EAction_Copy
	};

	BackupServerHash(IPipe *pPipe, int pClientid, bool use_snapshots, bool use_reflink, bool use_tmpfiles);
	~BackupServerHash(void);

	void operator()(void);
	
	bool isWorking(void);

	bool hasError(void);

	virtual bool handle_not_enough_space(const std::wstring &path);

	virtual void next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed);

	void setupDatabase(void);
	void deinitDatabase(void);

	bool findFileAndLink(const std::wstring &tfn, IFile *tf, std::wstring& hash_fn, const std::string &sha2, _i64 t_filesize, const std::string &hashoutput_fn, 
		bool copy_from_hardlink_if_failed, bool &tries_once, std::wstring &ff_last, bool &hardlink_limit, bool &copied_file, int64& entryid, int& entryclientid, int64& rsize, int64& next_entry,
		const FileMetadata& metadata, const FileMetadata& parent_metadata);

	void addFileSQL(int backupid, int clientid, int incremental, const std::wstring &fp, const std::wstring &hash_path,
		const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid, int64 next_entry, bool update_fileindex);

	static void addFileSQL(ServerBackupDao& backupdao, FileIndex& fileindex, int backupid, int clientid, int incremental, const std::wstring &fp,
		const std::wstring &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid,
		int64 next_entry, bool update_fileindex);

	static void deleteFileSQL(ServerBackupDao& backupdao, FileIndex& fileindex, const char* pHash, _i64 filesize, _i64 rsize, int clientid, int backupid, int incremental, int64 id, int64 prev_id, int64 next_id, int pointed_to,
		bool use_transaction, bool del_entry);

private:
	void addFile(int backupid, int incremental, IFile *tf, const std::wstring &tfn,
			std::wstring hash_fn, const std::string &sha2, const std::string &orig_fn, const std::string &hashoutput_fn, int64 t_filesize,
			const FileMetadata& metadata, const FileMetadata& parent_metadata);
			
	struct SFindState
	{
		SFindState()
			: state(0) {}

		int state;
		int64 orig_prev;
		ServerBackupDao::SFindFileEntry prev;
		std::map<int, int64> entryids;
		std::map<int, int64>::iterator client;
	};

	ServerBackupDao::SFindFileEntry findFileHash(const std::string &pHash, _i64 filesize, int clientid, SFindState& state);

	bool copyFile(IFile *tf, const std::wstring &dest);
	bool copyFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest, const FileMetadata& metadata);
	bool freeSpace(int64 fs, const std::wstring &fp);
	
	int countFilesInTmp(void);
	IFile* openFileRetry(const std::wstring &dest, int mode);
	bool patchFile(IFile *patch, const std::wstring &source, const std::wstring &dest, const std::wstring hash_output, const std::wstring hash_dest,
		const FileMetadata& metadata);

	bool createChunkHashes(IFile *tf, const std::wstring hash_fn);
	
	bool replaceFile(IFile *tf, const std::wstring &dest, const std::wstring &orig_fn);
	bool replaceFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest, const std::wstring &orig_fn,
		const FileMetadata& metadata);

	bool renameFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest, const FileMetadata& metadata);
	bool renameFile(IFile *tf, const std::wstring &dest);

	bool correctPath(std::wstring& ff, std::wstring& f_hashpath);

	std::map<std::pair<std::string, _i64>, std::vector<STmpFile> > files_tmp;

	ServerBackupDao* backupdao;

	IPipe *pipe;

	IDatabase *db;

	int link_logcnt;
	int space_logcnt;


	int clientid;

	volatile bool working;
	volatile bool has_error;

	IFile *chunk_output_fn;
	ChunkPatcher chunk_patcher;

	bool use_snapshots;
	bool use_reflink;
	bool use_tmpfiles;
	_i64 chunk_patch_pos;

	_i64 cow_filesize;

	int copy_limit;

	FileIndex *fileindex;

	std::wstring backupfolder;
	bool old_backupfolders_loaded;
	std::vector<std::wstring> old_backupfolders;
};