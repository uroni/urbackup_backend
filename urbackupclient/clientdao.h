#pragma once

#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../urbackupcommon/os_functions.h"
#include <vector>
#include <memory.h>

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
	unsigned long  Data1;
	unsigned short Data2;
	unsigned short Data3;
	unsigned char  Data4[8];

#ifndef _WIN32
	bool operator==(const _GUID& other) const {
		return memcmp(this, &other, sizeof(unsigned long) + 2 * sizeof(unsigned short) + 8) == 0;
	}
	bool operator!=(const _GUID& other) const {
		return !(*this == other);
	}
#endif
} GUID;
#endif

std::string guidToString(GUID guid);
GUID randomGuid();

enum EBackupDirFlag
{
	EBackupDirFlag_None = 0,
	EBackupDirFlag_Optional = 1,
	EBackupDirFlag_FollowSymlinks = 2,
	EBackupDirFlag_SymlinksOptional = 4,
	EBackupDirFlag_OneFilesystem = 8,
	EBackupDirFlag_RequireSnapshot = 16,
	EBackupDirFlag_ShareHashes = 32,
	EBackupDirFlag_KeepFiles = 64,
};

struct SBackupDir
{
	int id;
	std::string tname;
	std::string path;
	int flags;
	int group;
	bool symlinked;
	bool symlinked_confirmed;
	bool server_default;
	bool reset_keep;
};

struct SShadowCopy
{
	SShadowCopy() : refs(0) {}
	int id;
	GUID vssid;
	GUID ssetid;
	std::string target;
	std::string path;
	std::string tname;
	std::string orig_target;
	std::string vol;
	std::string starttoken;
	std::string clientsubname;
	bool filesrv;
	int refs;
	int passedtime;
};

struct SFileAndHash
{
	std::string name;
	int64 size;
	uint64 change_indicator;
	bool isdir;
	std::string hash;
	bool issym;
	bool isspecialf;
	size_t nlinks;

	std::string symlink_target;

	std::string output_symlink_target;

	bool operator<(const SFileAndHash &other) const
	{
		return name < other.name;
	}

	bool operator==(const SFileAndHash &other) const
	{
		return name == other.name &&
			size == other.size &&
			change_indicator == other.change_indicator &&
			isdir == other.isdir &&
			hash == other.hash &&
			issym == other.issym &&
			isspecialf == other.isspecialf &&
			symlink_target == other.symlink_target;
	}
};

class ClientDAO
{
public:
	ClientDAO(IDatabase *pDB);
	~ClientDAO();
	void prepareQueries();
	void destroyQueries(void);
	void restartQueries(void);

	void prepareQueriesGen(void);
	void destroyQueriesGen(void);

	bool getFiles(std::string path, int tgroup, std::vector<SFileAndHash> &data);

	void addFiles(std::string path, int tgroup, const std::vector<SFileAndHash> &data);
	void modifyFiles(std::string path, int tgroup, const std::vector<SFileAndHash> &data);
	bool hasFiles(std::string path, int tgroup);
	
	void removeAllFiles(void);

	std::vector<SBackupDir> getBackupDirs(void);

	std::vector<std::string> getChangedDirs(const std::string& path, bool backup);

	std::vector<SShadowCopy> getShadowcopies(void);
	int addShadowcopy(const SShadowCopy &sc);
	void deleteShadowcopy(int id);
	int modShadowcopyRefCount(int id, int m);


	void deleteSavedChangedDirs(void);

	bool hasChangedGap(void);

	std::vector<std::string> getGapDirs(void);

	std::vector<std::string> getDelDirs(const std::string& path, bool del=true);
	void deleteSavedDelDirs(void);

	void removeDeletedDir(const std::string &dir, int tgroup);

	std::string getOldExcludePattern(void);
	void updateOldExcludePattern(const std::string &pattern);

	std::string getOldIncludePattern(void);
	void updateOldIncludePattern(const std::string &pattern);

	std::string getMiscValue(const std::string& key);
	void updateMiscValue(const std::string& key, const std::string& value);

	static const int c_is_group;
	static const int c_is_user;
	static const int c_is_system_user;

	//@-SQLGenFunctionsBegin
	struct CondInt64
	{
		bool exists;
		int64 value;
	};
	struct SToken
	{
		int64 id;
		std::string accountname;
		std::string token;
		int is_user;
	};


	void updateShadowCopyStarttime(int id);
	void updateFileAccessToken(const std::string& accountname, const std::string& token, int is_user);
	std::vector<SToken> getFileAccessTokens(void);
	CondInt64 getFileAccessTokenId2Alts(const std::string& accountname, int is_user_alt1, int is_user_alt2);
	CondInt64 getFileAccessTokenId(const std::string& accountname, int is_user);
	void updateGroupMembership(int64 uid, const std::string& accountname);
	std::vector<int> getGroupMembership(int uid);
	void addBackupDir(const std::string& name, const std::string& path, int server_default, int flags, int tgroup, int symlinked);
	void delBackupDir(int64 id);
	void setResetKeep(int val, int64 id);
	void resetHardlink(const std::string& vol, int64 frn_high, int64 frn_low);
	CondInt64 hasHardLink(const std::string& vol, int64 frn_high, int64 frn_low);
	void addHardlink(const std::string& vol, int64 frn_high, int64 frn_low, int64 parent_frn_high, int64 parent_frn_low);
	void resetAllHardlinks(void);
	//@-SQLGenFunctionsEnd

	static std::string escapeGlob(const std::string& input);

private:
	

	IDatabase *db;

	IQuery *q_get_files;
	IQuery *q_add_files;
	IQuery *q_get_dirs;
	IQuery *q_remove_all;
	IQuery *q_get_changed_dirs;
	IQuery *q_modify_files;
	IQuery *q_has_files;
	IQuery *q_insert_shadowcopy;
	IQuery *q_get_shadowcopies;
	IQuery *q_remove_shadowcopies;
	IQuery *q_save_changed_dirs;
	IQuery *q_delete_saved_changed_dirs;
	IQuery *q_has_changed_gap;
	IQuery *q_get_del_dirs;
	IQuery *q_del_del_dirs;
	IQuery *q_copy_del_dirs;
	IQuery *q_del_del_dirs_copy;
	IQuery *q_remove_del_dir;
	IQuery *q_get_shadowcopy_refcount;
	IQuery *q_set_shadowcopy_refcount;
	IQuery *q_get_pattern;
	IQuery *q_insert_pattern;
	IQuery *q_update_pattern;

	//@-SQLGenVariablesBegin
	IQuery* q_updateShadowCopyStarttime;
	IQuery* q_updateFileAccessToken;
	IQuery* q_getFileAccessTokens;
	IQuery* q_getFileAccessTokenId2Alts;
	IQuery* q_getFileAccessTokenId;
	IQuery* q_updateGroupMembership;
	IQuery* q_getGroupMembership;
	IQuery* q_addBackupDir;
	IQuery* q_delBackupDir;
	IQuery* q_setResetKeep;
	IQuery* q_resetHardlink;
	IQuery* q_hasHardLink;
	IQuery* q_addHardlink;
	IQuery* q_resetAllHardlinks;
	//@-SQLGenVariablesEnd

	bool with_files_tmp;
};
