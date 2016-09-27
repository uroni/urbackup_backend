#include <string>

class SnapshotHelper
{
public:
	static int isAvailable(void);
	static bool isSubvolume(std::string clientname, std::string name);
	static bool createEmptyFilesystem(std::string clientname, std::string name);
	static bool snapshotFileSystem(std::string clientname, std::string old_name, std::string snapshot_name);
	static bool removeFilesystem(std::string clientname, std::string name);
	static void setSnapshotHelperCommand(std::string helper_command);
	static bool makeReadonly(std::string clientname, std::string name);
private:
	static std::string helper_name;
};