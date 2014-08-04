#pragma once

#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"
#include "ChunkPatcher.h"
#include "server_prepare_hash.h"
#include "FileCache.h"
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

	bool findFileAndLink(const std::wstring &tfn, IFile *tf, std::wstring& hash_fn, const std::string &sha2, _i64 t_filesize,
		const std::string &hashoutput_fn, bool &tries_once, std::wstring &ff_last, bool &hardlink_limit, const FileMetadata& metadata);

	void addFileSQL(int backupid, char incremental, const std::wstring &fp, const std::wstring &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize);

	void copyFromTmpTable(bool force);

private:
	void prepareSQL(void);
	void addFile(int backupid, char incremental, IFile *tf, const std::wstring &tfn,
			std::wstring hash_fn, const std::string &sha2, bool diff_file, const std::string &orig_fn, const std::string &hashoutput_fn, int64 t_filesize,
			const FileMetadata& metadata);
	std::wstring findFileHash(const std::string &pHash, _i64 filesize, int &backupid, std::wstring &hashpath, bool& cache_hit);
	std::wstring findFileHashTmp(const std::string &pHash, _i64 filesize, int &backupid, std::wstring &hashpath);
	bool copyFile(IFile *tf, const std::wstring &dest);
	bool copyFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest, const FileMetadata& metadata);
	bool freeSpace(int64 fs, const std::wstring &fp);
	
	void addFileTmp(int backupid, const std::wstring &fp, const std::wstring &hash_path, const std::string &shahash, _i64 filesize);
	void deleteFileSQL(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid);
	void deleteFileTmp(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid);
	void copyFilesFromTmp(void);
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

	IQuery *q_find_file_hash;
	IQuery *q_delete_files_tmp;
	IQuery *q_add_file;
	IQuery *q_del_file;
	IQuery *q_move_del_file;
	IQuery *q_del_file_tmp;
	IQuery *q_copy_files;
	IQuery *q_copy_files_to_new;
	IQuery *q_delete_all_files_tmp;
	IQuery *q_count_files_tmp;

	ServerBackupDao* backupdao;

	IPipe *pipe;

	IDatabase *db;

	int tmp_count;
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

	int copy_limit;

	FileCache *filecache;

	std::wstring backupfolder;
	bool old_backupfolders_loaded;
	std::vector<std::wstring> old_backupfolders;
};