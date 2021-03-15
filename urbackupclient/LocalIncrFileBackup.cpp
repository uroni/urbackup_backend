#include "LocalIncrFileBackup.h"
#include <ctime>

bool LocalIncrFileBackup::prepareBackuppath()
{
	const std::time_t t = std::time(nullptr);
	char mbstr[100];
	if (!std::strftime(mbstr, sizeof(mbstr), "%y%m%d-%H%M.new", std::localtime(&t)))
		return false;

	std::string prefix = std::string(mbstr);

	std::vector<SBtrfsFile> existing_backups = orig_backup_files->listFiles("");

	if (existing_backups.empty())
		return false;

	std::string last_backuppath;
	for (std::vector<SBtrfsFile>::reverse_iterator it = existing_backups.rbegin();it!=existing_backups.rend();++it)
	{
		if (it->name.find(".new") == std::string::npos)
		{
			last_backuppath = it->name;
			break;
		}
	}

	if (last_backuppath.empty())
		return false;

	if (!orig_backup_files->createSnapshot(last_backuppath, prefix))
		return false;

	backup_files->createDir(".hashes");
	backup_files->createDir(getBackupInternalDataDir());

	return (backup_files->getFileType(getBackupInternalDataDir()) & EFileType_Directory) > 0;
}
