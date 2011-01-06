#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "os_functions.h"
#include <vector>

#ifndef GUID_DEFINED
#define GUID_DEFINED
#if defined(__midl)
typedef struct {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    byte           Data4[ 8 ];
} GUID;
#else
typedef struct _GUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[ 8 ];
} GUID;
#endif
#endif

struct SBackupDir
{
	int id;
	std::string tname;
	std::wstring path;
};

struct SShadowCopy
{
	int id;
	GUID vssid;
	GUID ssetid;
	std::wstring target;
	std::wstring path;
	std::wstring tname;
	std::wstring orig_target;
	bool filesrv;
};

class ClientDAO
{
public:
	ClientDAO(IDatabase *pDB);
	void prepareQueries(void);
	void destroyQueries(void);
	void restartQueries(void);

	bool getFiles(std::wstring path, std::vector<SFile> &data);

	void addFiles(std::wstring path, const std::vector<SFile> &data);
	void modifyFiles(std::wstring path, const std::vector<SFile> &data);
	bool hasFiles(std::wstring path);
	
	void removeAllFiles(void);

	void copyFromTmpFiles(void);

	std::vector<SBackupDir> getBackupDirs(void);

	std::vector<std::wstring> getChangedDirs(bool del=true);

	std::vector<SShadowCopy> getShadowcopies(void);
	int addShadowcopy(const SShadowCopy &sc);
	void deleteShadowcopy(int id);

	void deleteSavedChangedDirs(void);
	void restoreSavedChangedDirs(void);

	bool hasChangedGap(void);
	void deleteChangedDirs(void);

	std::vector<std::wstring> getGapDirs(void);

private:
	IDatabase *db;

	IQuery *q_get_files;
	IQuery *q_add_files;
	IQuery *q_get_dirs;
	IQuery *q_remove_all;
	IQuery *q_get_changed_dirs;
	IQuery *q_remove_changed_dirs;
	IQuery *q_modify_files;
	IQuery *q_has_files;
	IQuery *q_insert_shadowcopy;
	IQuery *q_get_shadowcopies;
	IQuery *q_remove_shadowcopies;
	IQuery *q_save_changed_dirs;
	IQuery *q_delete_saved_changed_dirs;
	IQuery *q_restore_saved_changed_dirs;
	IQuery *q_copy_from_tmp_files;
	IQuery *q_delete_tmp_files;
	IQuery *q_has_changed_gap;
};