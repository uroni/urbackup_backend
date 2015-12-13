#include <string>

class SnapshotHelper
{
public:
	static bool isAvailable(void);
	static bool isSubvolume(std::string clientname, std::string name);
	static bool createEmptyFilesystem(std::string clientname, std::string name);
	static bool snapshotFileSystem(std::string clientname, std::string old_name, std::string snapshot_name);
	static bool removeFilesystem(std::string clientname, std::string name);
	static void setSnapshotHelperCommand(std::string helper_command);
private:
	static std::string helper_name;
};