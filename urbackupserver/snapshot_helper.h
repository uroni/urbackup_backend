#include <string>

class SnapshotHelper
{
public:
	static int isAvailable(void);
	static bool isSubvolume(bool image, std::string clientname, std::string name);
	static bool createEmptyFilesystem(bool image, std::string clientname, std::string name, std::string& errmsg);
	static bool snapshotFileSystem(bool image, std::string clientname, std::string old_name, std::string snapshot_name, std::string& errmsg);
	static bool removeFilesystem(bool image, std::string clientname, std::string name);
	static void setSnapshotHelperCommand(std::string helper_command);
	static bool makeReadonly(bool image, std::string clientname, std::string name);
	static std::string getMountpoint(bool image, std::string clientname, std::string name);
private:
	static std::string helper_name;
};