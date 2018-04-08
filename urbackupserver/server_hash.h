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
#include "dao/ServerFilesDao.h"
#include <vector>
#include <map>
#include "../urbackupcommon/chunk_hasher.h"
#include "server_log.h"
#include "../urbackupcommon/ExtentIterator.h"

class FileMetadata;
class MaxFileId;

const int64 link_file_min_size = 2048;

struct STmpFile
{
	STmpFile(int backupid, std::string fp, std::string hashpath)
		: backupid(backupid), fp(fp), hashpath(hashpath)
	{
	}
	STmpFile(void) {}

	int backupid;
	std::string fp;
	std::string hashpath;
};

class BackupServerHash : public IThread, public INotEnoughSpaceCallback, public IChunkPatcherCallback
{
public:

	enum EAction
	{
		EAction_LinkOrCopy,
		EAction_Copy
	};

	BackupServerHash(IPipe *pPipe, int pClientid, bool use_snapshots, bool use_reflink,
		bool use_tmpfiles, logid_t logid, bool snapshot_file_inplace, MaxFileId& max_file_id);
	~BackupServerHash(void);

	void operator()(void);
	
	bool isWorking(void);

	bool hasError(void);

	virtual bool handle_not_enough_space(const std::string &path);

	virtual void next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed, bool* is_sparse);

	virtual void next_sparse_extent_bytes(const char *buf, size_t bsize);

	virtual int64 chunk_patcher_pos();

	void setupDatabase(void);
	void deinitDatabase(void);

	bool findFileAndLink(const std::string &tfn, IFile *tf, std::string hash_fn, const std::string &sha2, _i64 t_filesize, const std::string &hashoutput_fn, 
		bool copy_from_hardlink_if_failed, bool &tries_once, std::string &ff_last, bool &hardlink_limit, bool &copied_file, int64& entryid, int& entryclientid, int64& rsize, int64& next_entry,
		FileMetadata& metadata, bool datch_dbs, ExtentIterator* extent_iterator);

	void addFileSQL(int backupid, int clientid, int incremental, const std::string &fp, const std::string &hash_path,
		const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid, int64 next_entry, bool update_fileindex);

	static void addFileSQL(ServerFilesDao& filesdao, FileIndex& fileindex, int backupid, int clientid, int incremental, const std::string &fp,
		const std::string &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid,
		int64 next_entry, bool update_fileindex);
		
		
	static void deleteFileSQL(ServerFilesDao& filesdao, FileIndex& fileindex, int64 id);

	struct SInMemCorrection
	{
		std::map<int64, int64> next_entries;
		std::map<int64, int64> prev_entries;
		std::map<int64, int> pointed_to;
		int64 max_correct;
		int64 min_correct;

		bool needs_correction(int64 id)
		{
			return id >= min_correct && id <= max_correct;
		}
	};

	static void deleteFileSQL(ServerFilesDao& filesdao, FileIndex& fileindex, const char* pHash, _i64 filesize, _i64 rsize, int clientid, int backupid, int incremental, int64 id, int64 prev_id, int64 next_id, int pointed_to,
		bool use_transaction, bool del_entry, bool detach_dbs, bool with_backupstat, SInMemCorrection* correction);

private:
	void addFile(int backupid, int incremental, IFile *tf, const std::string &tfn,
			std::string hash_fn, const std::string &sha2, const std::string &orig_fn, const std::string &hashoutput_fn, int64 t_filesize,
			FileMetadata& metadata, bool with_hashes, ExtentIterator* extent_iterator);
			
	struct SFindState
	{
		SFindState()
			: state(0) {}

		int state;
		int64 orig_prev;
		ServerFilesDao::SFindFileEntry prev;
		std::map<int, int64> entryids;
		std::map<int, int64>::iterator client;
	};

	ServerFilesDao::SFindFileEntry findFileHash(const std::string &pHash, _i64 filesize, int clientid, SFindState& state);

	bool copyFile(IFile *tf, const std::string &dest, ExtentIterator* extent_iterator);
	bool copyFileWithHashoutput(IFile *tf, const std::string &dest, const std::string hash_dest, ExtentIterator* extent_iterator);
	bool freeSpace(int64 fs, const std::string &fp);
	
	int countFilesInTmp(void);
	IFsFile* openFileRetry(const std::string &dest, int mode, std::string& errstr);
	bool patchFile(IFile *patch, const std::string &source, const std::string &dest, const std::string hash_output, const std::string hash_dest,
		_i64 tfilesize, ExtentIterator* extent_iterator);
	
	bool replaceFile(IFile *tf, const std::string &dest, const std::string &orig_fn, ExtentIterator* extent_iterator);
	bool replaceFileWithHashoutput(IFile *tf, const std::string &dest, const std::string hash_dest, const std::string &orig_fn, ExtentIterator* extent_iterator);

	bool renameFileWithHashoutput(IFile *tf, const std::string &dest, const std::string hash_dest, ExtentIterator* extent_iterator);
	bool renameFile(IFile *tf, const std::string &dest);

	bool correctPath(std::string& ff, std::string& f_hashpath);
	bool correctClientName(const std::string& backupfolder, std::string& ff, std::string& f_hashpath);

	bool punchHoleOrZero(IFile *tf, int64 offset, int64 size);

	std::map<std::pair<std::string, _i64>, std::vector<STmpFile> > files_tmp;

	ServerFilesDao* filesdao;

	IPipe *pipe;

	IDatabase *db;

	int link_logcnt;
	int space_logcnt;


	int clientid;

	volatile bool working;
	volatile bool has_error;

	IFsFile *chunk_output_fn;
	ChunkPatcher chunk_patcher;
	bool chunk_patcher_has_error;

	bool use_snapshots;
	bool use_reflink;
	bool use_tmpfiles;
	bool has_reflink;
	_i64 chunk_patch_pos;

	_i64 cow_filesize;

	FileIndex *fileindex;

	std::string backupfolder;
	bool old_backupfolders_loaded;
	std::vector<std::string> old_backupfolders;
	std::map<std::string, std::vector<std::string> > client_moved_to;

	logid_t logid;

	bool enabled_sparse;

	bool snapshot_file_inplace;

	MaxFileId& max_file_id;
};