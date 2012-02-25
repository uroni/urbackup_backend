#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"

class BackupServerHash : public IThread
{
public:
	BackupServerHash(IPipe *pPipe, IPipe *pExitpipe, int pClientid);
	~BackupServerHash(void);

	void operator()(void);
	
	bool isWorking(void);

	bool hasError(void);

private:
	void prepareSQL(void);
	void addFile(int backupid, char incremental, IFile *tf, const std::wstring &tfn, const std::string &sha2);
	std::wstring findFileHash(const std::string &pHash, _i64 filesize, int &backupid);
	std::wstring findFileHashTmp(const std::string &pHash, _i64 filesize, int &backupid);
	bool copyFile(IFile *tf, const std::wstring &dest);
	bool freeSpace(int64 fs, const std::wstring &fp);
	void addFileSQL(int backupid, char incremental, const std::wstring &fp, const std::string &shahash, _i64 filesize, _i64 rsize);
	void addFileTmp(int backupid, const std::wstring &fp, const std::string &shahash, _i64 filesize);
	void deleteFileSQL(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid);
	void deleteFileTmp(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid);
	void copyFilesFromTmp(void);
	int countFilesInTmp(void);

	std::map<std::pair<std::string, _i64>, std::vector<std::pair<int, std::wstring> > > files_tmp;

	IQuery *q_find_file_hash;
	IQuery *q_delete_files_tmp;
	IQuery *q_add_file;
	IQuery *q_del_file;
	IQuery *q_move_del_file;
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


	int clientid;

	volatile bool working;
	volatile bool has_error;
};