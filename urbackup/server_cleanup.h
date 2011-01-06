#include "../Interface/Database.h"
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"

class ServerSettings;


class ServerCleanupThread : public IThread
{
public:

	void operator()(void);

	void do_cleanup(void);
	bool do_cleanup(int64 minspace);
	void cleanup_images(int64 minspace=-1);
	void cleanup_files(int64 minspace=-1);

	void createQueries(void);
	void destroyQueries(void);

	size_t getImagesFullNum(int clientid, int &backupid_top, const std::vector<int> &notit);
	size_t getImagesIncrNum(int clientid, int &backupid_top, const std::vector<int> &notit);

	size_t getFilesFullNum(int clientid, int &backupid_top);
	size_t getFilesIncrNum(int clientid, int &backupid_top);

	void removeImage(int backupid);
	bool findUncompleteImageRef(int backupid);

	void deleteFileBackup(const std::wstring &backupfolder, int clientid, int backupid);

	static void updateStats(void);
	static void initMutex(void);
private:

	void removeImageSize(int backupid);

	bool hasEnoughFreeSpace(int64 minspace, ServerSettings *settings);

	IDatabase *db;

	IQuery *q_incomplete_images;
	IQuery *q_remove_image;
	IQuery *q_get_clients;
	IQuery *q_get_full_num_images;
	IQuery *q_get_image_refs;
	IQuery *q_get_image_path;
	IQuery *q_get_incr_num_images;
	IQuery *q_get_full_num_files;
	IQuery *q_get_incr_num_files;
	IQuery *q_get_clientname;
	IQuery *q_get_backuppath;
	IQuery *q_delete_files;
	IQuery *q_remove_file_backup;
	IQuery *q_get_filebackup_info;
	IQuery *q_get_image_info;
	IQuery *q_move_files;
	IQuery *q_remove_image_size;
	IQuery *q_del_image_stats;
	IQuery *q_image_stats_stop;

	static IMutex *mutex;
	static ICondition *cond;

	static bool update_stats;
	
	std::vector<int> removeerr;
};