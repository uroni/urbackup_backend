#include <vector>
#include <string>

class IndexFiles
{
public:
	static const std::vector<std::string>& getIndexFiles(void);
	static void addIndexFile( const std::string &fn);

private:
	static std::vector<std::string> files;
};

void add_default_indexfiles(void);