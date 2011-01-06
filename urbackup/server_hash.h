#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "os_functions.h"

struct SBackupEntry
{
	int score;
	int id;
	int clientid;
	int incremental;

	bool operator<(const SBackupEntry &other) const
	{
		return other.score<score;
	}
};

class BackupServerHash : public IThread
{
public:
	BackupServerHash(IPipe *pPipe, IPipe *pExitpipe, int pClientid, std::wstring pBackuppath);
	~BackupServerHash(void);

	void operator()(void);
	
	bool isWorking(void);

private:
	void prepareSQL(void);
	void addFile(unsigned int backupid, IFile *tf, const std::wstring &tfn, const std::string &sha2);
	std::wstring findFileHash(const std::string &pHash, _i64 filesize, int &backupid);
	std::vector<SBackupEntry> getBackups(void);
	std::wstring getBackupPath(int backupid);
	void deleteBackup(int backupid);
	void deleteBackupSQL(int backupid);
	bool copyFile(IFile *tf, const std::wstring &dest);
	bool freeSpace(int64 fs, const std::wstring &fp);
	void addFileSQL(int backupid, const std::wstring &fp, const std::string &shahash, _i64 filesize, _i64 rsize);
	void deleteFileSQL(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid);
	void copyFilesFromTmp(void);
	int countFilesInTmp(void);

	IQuery *q_find_file_hash;
	IQuery *q_get_backups;
	IQuery *q_get_backup_path;
	IQuery *q_delete_files;
	IQuery *q_delete_files_tmp;
	IQuery *q_delete_backup;
	IQuery *q_add_file;
	IQuery *q_del_file;
	IQuery *q_del_file_tmp;
	IQuery *q_copy_files;
	IQuery *q_delete_all_files_tmp;
	IQuery *q_count_files_tmp;

	IPipe *pipe;
	IPipe *exitpipe;

	IDatabase *db;

	int tmp_count;
	int link_logcnt;
	int space_logcnt;

	std::wstring backuppath;

	int clientid;
	
	volatile bool working;
};