#ifndef FILE_PERMISSIONS_H_
#define FILE_PERMISSIONS_H_

#include <string>

bool write_file_only_admin(const std::string& data, const std::string& fn);
bool change_file_permissions_admin_only(const std::string& filename);

#endif //FILE_PERMISSIONS_H_