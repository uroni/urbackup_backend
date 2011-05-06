#include <string>
#include <vector>

std::wstring map_file(std::wstring fn, bool append_urd=false, std::wstring *udir=NULL);
void add_share_path(const std::wstring &name, const std::wstring &path);
void remove_share_path(const std::wstring &name);
std::vector<std::wstring> get_maps(void);
