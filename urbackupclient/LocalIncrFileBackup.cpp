#include "LocalIncrFileBackup.h"
#include <ctime>

bool LocalIncrFileBackup::prepareBackuppath()
{
	const std::time_t t = std::time(nullptr);
	char mbstr[100];
	if (!std::strftime(mbstr, sizeof(mbstr), "%y%m%d-%H%M", std::localtime(&t)))
		return false;

	std::string prefix = std::string(mbstr);

	std::vector<SBtrfsFile> existing_backups = orig_backup_files->listFiles("");

	if (!orig_backup_files->createDir(prefix))
		return false;

	backup_files->createDir(".hashes");
	backup_files->createDir(getBackupInternalDataDir());

	return (backup_files->getFileType(getBackupInternalDataDir()) & EFileType_Directory) > 0;
}
