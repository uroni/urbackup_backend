#include <string>
#include <vector>

std::wstring map_file(std::wstring fn, bool append_urd=false, std::wstring *udir=NULL);
void add_share_path(const std::string &name, const std::wstring &path);
void remove_share_path(const std::string &name);
std::vector<std::string> get_maps(void);
