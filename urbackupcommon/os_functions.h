#ifndef OS_FUNCTIONS_H
#define OS_FUNCTIONS_H

#include <vector>
#include <string>

#include "../Interface/Types.h"

void getMousePos(int &x, int &y);

struct SFile
{
	std::wstring name;
	int64 size;
	int64 last_modified;
	bool isdir;

	bool operator<(const SFile &other) const
	{
		return name < other.name;
	}
};

std::vector<SFile> getFiles(const std::wstring &path, bool *has_error=NULL);

void removeFile(const std::wstring &path);

void moveFile(const std::wstring &src, const std::wstring &dst);

bool isDirectory(const std::wstring &path);

int64 os_atoi64(const std::string &str);

void* os_start_transaction();

bool os_finish_transaction(void* transaction);

bool os_create_dir(const std::wstring &dir, void* transaction=NULL);

bool os_create_dir(const std::string &dir, void* transaction=NULL);

bool os_create_hardlink(const std::wstring &linkname, const std::wstring &fname, bool use_ioref, bool* too_many_links, void* transaction=NULL);

int64 os_free_space(const std::wstring &path);

int64 os_total_space(const std::wstring &path);

bool os_remove_nonempty_dir(const std::wstring &path);

bool os_remove_dir(const std::string &path);

bool os_remove_symlink_dir(const std::wstring &path);

std::wstring os_file_sep(void);

std::string os_file_sepn(void);

bool os_link_symbolic(const std::wstring &target, const std::wstring &lname);

bool os_directory_exists(const std::wstring &path);

#ifndef OS_FUNC_NO_NET
bool os_lookuphostname(std::string pServer, unsigned int *dest);
#endif

std::wstring os_file_prefix(std::wstring path);

bool os_file_truncate(const std::wstring &fn, int64 fsize);

std::string os_strftime(std::string fs);

bool os_create_dir_recursive(std::wstring fn);

std::wstring os_get_final_path(std::wstring path);

bool os_rename_file(std::wstring src, std::wstring dst);

#endif //OS_FUNCTIONS_H