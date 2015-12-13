#include <string>
#include <vector>

std::string map_file(std::string fn, const std::string& identity);
void add_share_path(const std::string &name, const std::string &path, const std::string& identity);
void remove_share_path(const std::string &name, const std::string& identity);
