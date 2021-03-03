#include "BtrfsFactory.h"
#include "BackupFileSystem.h"

IBackupFileSystem* BtrfsFactory::openBtrfsImage(const std::string& path)
{
	return new BtrfsBackupFileSystem(path);
}
