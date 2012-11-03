#include "../Interface/Database.h"
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include <vector>

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

	bool removeImage(int backupid, bool update_stat=true, int64 size_correction=0);
	bool findUncompleteImageRef(int backupid);

	void removeClient(int clientid);

	bool deleteFileBackup(const std::wstring &backupfolder, int clientid, int backupid);

	void deletePendingClients(void);

	static void updateStats(bool interruptible);
	static void initMutex(void);
	static void destroyMutex(void);

	void backup_database(void);

	static void doQuit(void);
private:

	bool deleteAndTruncateFile(std::wstring path);
	bool deleteImage(std::wstring path);
	void removeImageSize(int backupid);
	int64 getImageSize(int backupid);
	std::vector<int> getAssocImages(int backupid);

	int hasEnoughFreeSpace(int64 minspace, ServerSettings *settings);

	bool truncate_files_recurisve(std::wstring path);

	IDatabase *db;

	IQuery *q_incomplete_images;
	IQuery *q_remove_image;
	IQuery *q_get_clients_sortfiles;
	IQuery *q_get_clients_sortimages;
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
	IQuery *q_get_client_images;
	IQuery *q_get_client_filebackups;
	IQuery *q_get_assoc_img;
	IQuery *q_get_image_size;

	static IMutex *mutex;
	static ICondition *cond;

	static IMutex *a_mutex;

	static bool update_stats;
	static bool update_stats_interruptible;
	
	std::vector<int> removeerr;

	static volatile bool do_quit;
};
