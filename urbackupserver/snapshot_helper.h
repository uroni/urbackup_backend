#include <string>

class SnapshotHelper
{
public:
	static bool isAvailable(void);
	static bool isSubvolume(std::wstring clientname, std::wstring name);
	static bool createEmptyFilesystem(std::wstring clientname, std::wstring name);
	static bool snapshotFileSystem(std::wstring clientname, std::wstring old_name, std::wstring snapshot_name);
	static bool removeFilesystem(std::wstring clientname, std::wstring name);
	static void setSnapshotHelperCommand(std::string helper_command);
private:
	static std::string helper_name;
};