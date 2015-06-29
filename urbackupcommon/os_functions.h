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
		usn(0), created(0),
		isdir(false), issym(false),
		isspecial(false)
	{

	}

	std::wstring name;
	int64 size;
	int64 last_modified;
	int64 usn;
	int64 created;
	bool isdir;
	bool issym;
	bool isspecial;

	bool operator<(const SFile &other) const
	{
		return name < other.name;
	}
};

std::vector<SFile> getFilesWin(const std::wstring &path, bool *has_error=NULL, bool exact_filesize=true, bool with_usn=false);

std::vector<SFile> getFiles(const std::wstring &path, bool *has_error=NULL);

SFile getFileMetadataWin(const std::wstring &path, bool with_usn);

SFile getFileMetadata(const std::wstring &path);

void removeFile(const std::wstring &path);

void moveFile(const std::wstring &src, const std::wstring &dst);

bool isDirectory(const std::wstring &path, void* transaction=NULL);

int64 os_atoi64(const std::string &str);

bool os_create_dir(const std::wstring &dir);

bool os_create_dir(const std::string &dir);

bool os_create_hardlink(const std::wstring &linkname, const std::wstring &fname, bool use_ioref, bool* too_many_links);

bool os_create_hardlink(const std::string &linkname, const std::string &fname, bool use_ioref, bool* too_many_link);

int64 os_free_space(const std::wstring &path);

int64 os_total_space(const std::wstring &path);

typedef bool(*os_symlink_callback_t)(const std::wstring& linkname, void* userdata);

bool os_remove_nonempty_dir(const std::wstring &path, os_symlink_callback_t symlink_callback=NULL, void* userdata=NULL, bool delete_root=true);

bool os_remove_dir(const std::string &path);

bool os_remove_dir(const std::wstring &path);

bool os_remove_symlink_dir(const std::wstring &path);

std::wstring os_file_sep(void);

std::string os_file_sepn(void);

bool os_link_symbolic(const std::wstring &target, const std::wstring &lname, void* transaction=NULL, bool* isdir=NULL);

bool os_get_symlink_target(const std::wstring &lname, std::wstring &target);

bool os_is_symlink(const std::wstring &lname);

bool os_directory_exists(const std::wstring &path);

#ifndef OS_FUNC_NO_NET
bool os_lookuphostname(std::string pServer, unsigned int *dest);
#endif

std::wstring os_file_prefix(std::wstring path);

bool os_file_truncate(const std::wstring &fn, int64 fsize);

std::string os_strftime(std::string fs);

bool os_create_dir_recursive(std::wstring fn);

std::wstring os_get_final_path(std::wstring path);

bool os_rename_file(std::wstring src, std::wstring dst, void* transaction=NULL);

void* os_start_transaction();

bool os_finish_transaction(void* transaction);

int64 os_last_error();

int64 os_to_windows_filetime(int64 unix_time);

bool os_set_file_time(const std::wstring& fn, int64 created, int64 last_modified);

bool copy_file(const std::wstring &src, const std::wstring &dst);

bool os_path_absolute(const std::wstring& path);

enum EFileType
{
	EFileType_File = 1,
	EFileType_Directory = 2,
	EFileType_Symlink = 4
};

int os_get_file_type(const std::wstring &path);

int os_popen(const std::string& cmd, std::string& ret);

#endif //OS_FUNCTIONS_H