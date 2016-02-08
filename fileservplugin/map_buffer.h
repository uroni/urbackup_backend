#include <string>
#include <vector>

std::string map_file(std::string fn, const std::string& identity, bool& allow_exec);
void add_share_path(const std::string &name, const std::string &path, const std::string& identity, bool allow_exec);
void remove_share_path(const std::string &name, const std::string& identity);
void register_fn_redirect(const std::string & source_fn, const std::string & target_fn);
