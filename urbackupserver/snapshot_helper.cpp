#include "snapshot_helper.h"
#include "../Interface/Server.h"
#include <stdlib.h>

const std::string helper_name="urbackup_snapshot_helper";

bool SnapshotHelper::isAvailable(void)
{
	int rc=system((helper_name+" test").c_str());
	return rc==0;
}

bool SnapshotHelper::createEmptyFilesystem(std::wstring clientname, std::wstring name)
{
	int rc=system((helper_name+" create \""+Server->ConvertToUTF8(clientname)+"\" \""+Server->ConvertToUTF8(name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::snapshotFileSystem(std::wstring clientname, std::wstring old_name, std::wstring snapshot_name)
{
	int rc=system((helper_name+" snapshot \""+Server->ConvertToUTF8(clientname)+"\" \""+Server->ConvertToUTF8(old_name)+"\" \""+Server->ConvertToUTF8(snapshot_name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::removeFilesystem(std::wstring clientname, std::wstring name)
{
	int rc=system((helper_name+" remove \""+Server->ConvertToUTF8(clientname)+"\" \""+Server->ConvertToUTF8(name)+"\"").c_str());
	return rc==0;
}