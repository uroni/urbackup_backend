#include <string>
#include <vector>

std::wstring map_file(std::wstring fn, const std::string& identity);
void add_share_path(const std::wstring &name, const std::wstring &path, const std::string& identity);
void remove_share_path(const std::wstring &name, const std::string& identity);
