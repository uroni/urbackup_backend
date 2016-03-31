#ifndef OS_FUNCTIONS_H
#define OS_FUNCTIONS_H

#include <vector>
#include <string>

#include "../Interface/Types.h"

void getMousePos(int &x, int &y);

struct SFile
{
	SFile() :
		size(0), last_modified(0), 
		usn(0), created(0), accessed(0),
		isdir(false), issym(false),
		isspecialf(false)
	{

	}

	std::string name;
	int64 size;
	int64 last_modified;
	int64 usn;
	int64 created;
	int64 accessed;
	bool isdir;
	bool issym;
	bool isspecialf;

	bool operator<(const SFile &other) const
	{
		return name < other.name;
	}
};

std::vector<SFile> getFilesWin(const std::string &path, bool *has_error=NULL, bool exact_filesize=true, bool with_usn=false, bool ignore_other_fs=false);

std::vector<SFile> getFiles(const std::string &path, bool *has_error=NULL, bool ignore_other_fs=false);

SFile getFileMetadataWin(const std::string &path, bool with_usn);

SFile getFileMetadata(const std::string &path);

void removeFile(const std::string &path);

void moveFile(const std::string &src, const std::string &dst);

bool isDirectory(const std::string &path, void* transaction=NULL);

int64 os_atoi64(const std::string &str);

bool os_create_dir(const std::string &dir);

bool os_create_dir(const std::string &dir);

bool os_create_hardlink(const std::string &linkname, const std::string &fname, bool use_ioref, bool* too_many_links);

int64 os_free_space(const std::string &path);

int64 os_total_space(const std::string &path);

typedef bool(*os_symlink_callback_t)(const std::string& linkname, void* userdata);

bool os_remove_nonempty_dir(const std::string &path, os_symlink_callback_t symlink_callback=NULL, void* userdata=NULL, bool delete_root=true);

bool os_remove_dir(const std::string &path);

bool os_remove_dir(const std::string &path);

bool os_remove_symlink_dir(const std::string &path);

std::string os_file_sep(void);

bool os_link_symbolic(const std::string &target, const std::string &lname, void* transaction=NULL, bool* isdir=NULL);

bool os_get_symlink_target(const std::string &lname, std::string &target);

bool os_is_symlink(const std::string &lname);

bool os_directory_exists(const std::string &path);

#ifndef OS_FUNC_NO_NET
bool os_lookuphostname(std::string pServer, unsigned int *dest);
#endif

std::string os_file_prefix(std::string path);

bool os_file_truncate(const std::string &fn, int64 fsize);

std::string os_strftime(std::string fs);

bool os_create_dir_recursive(std::string fn);

std::string os_get_final_path(std::string path);

bool os_rename_file(std::string src, std::string dst, void* transaction=NULL);

void* os_start_transaction();

bool os_finish_transaction(void* transaction);

int64 os_last_error();

int64 os_to_windows_filetime(int64 unix_time);

bool os_set_file_time(const std::string& fn, int64 created, int64 last_modified, int64 accessed);

bool copy_file(const std::string &src, const std::string &dst, bool flush = false);

class IFile;
bool copy_file(IFile *fsrc, IFile *fdst);

bool os_path_absolute(const std::string& path);

enum EFileType
{
	EFileType_File = 1,
	EFileType_Directory = 2,
	EFileType_Symlink = 4
};

int os_get_file_type(const std::string &path);

int os_popen(const std::string& cmd, std::string& ret);

int64 os_last_error(std::string& message);

std::string os_last_error_str();

struct SPrioInfoInt;

struct SPrioInfo
{
	SPrioInfo();
	~SPrioInfo();

	SPrioInfoInt* prio_info;
};

bool os_enable_background_priority(SPrioInfo& prio_info);

bool os_disable_background_priority(SPrioInfo& prio_info);

class ScopedBackgroundPrio
{
public:
	ScopedBackgroundPrio(bool enabled=true)
	{
		if (enabled)
		{
			background_prio = os_enable_background_priority(prio_info);
		}
		else
		{
			background_prio = false;
		}
	}

	void enable()
	{
		if (!background_prio)
		{
			background_prio = os_enable_background_priority(prio_info);
		}
	}

	~ScopedBackgroundPrio()
	{
		if (background_prio)
		{
			os_disable_background_priority(prio_info);
		}
	}
private:
	bool background_prio;
	SPrioInfo prio_info;
};


#endif //OS_FUNCTIONS_H