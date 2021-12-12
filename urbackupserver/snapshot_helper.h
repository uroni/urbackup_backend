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

#ifdef _WIN32
	static std::string getBackupStoragePath();
	static bool createEmptyFilesystemWindows(std::string clientname, std::string name, std::string& errmsg);
	static bool removeFilesystemWindows(std::string clientname, std::string name);
	static int testWindows();
	static void setupWindows();
	static bool snapshotFileSystemWindows(std::string clientname, std::string old_name, std::string snapshot_name, std::string& errmsg);
	static bool isSubvolumeWindows(std::string clientname, std::string name);
#endif

	static std::string helper_name;
};