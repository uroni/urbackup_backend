#include <string>

#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"

class IDatabase;

const int backup_type_incr_file=1;
const int backup_type_full_file=2;

class ServerAutomaticArchive : public IThread
{
public:
	void operator()(void);


	static int getBackupTypes(const std::wstring &backup_type_name);
	static std::wstring getBackupType(int backup_types);
	static void doQuit(void);
	static void initMutex(void);
	static void destroyMutex(void);

private:
	void archiveTimeout(void);
	void archiveBackups(void);
	int getNonArchivedFileBackup(int backup_types, int clientid);
	void archiveFileBackup(int backupid, int length);
	void updateInterval(int archiveid, int interval);

	bool isInArchiveWindow(const std::wstring &window_def);

	void copyArchiveSettings(int clientid);

	IDatabase *db;

	static volatile bool do_quit;
	static ICondition *cond;
	static IMutex *mutex;
};
