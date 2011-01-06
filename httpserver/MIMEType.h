#include <map>
#include <string>

class MIMEType
{
public:
	static std::string getMIMEType(const std::string &extension);
	static void addMIMEType( const std::string &extension, const std::string &mimetype);

private:
	static std::map<std::string, std::string> types;
};

void add_default_mimetypes(void);