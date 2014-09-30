#include "../Interface/Types.h"
#include "../Interface/Thread.h"
#include "dao/ServerBackupDao.h"
#include "FileIndex.h"
#include <memory>

class IQuery;
class IDatabase;
class ServerSettings;

struct SDelInfo
{
	_i64 delsize;
	int clientid;
	int incremental;
};

class ServerUpdateStats : public IThread
{
public:
	ServerUpdateStats(bool image_repair_mode=false, bool interruptible=false);

	void operator()(void);

	static void repairImages(void);

private:

	void update_files(void);
	void update_images(void);

	void createQueries(void);
	void destroyQueries(void);

	std::map<int, _i64> getFilebackupSizesClients(void);
	void add(const std::vector<int>& subset, int64 num, std::map<int, _i64> &data);
	void updateSizes(std::map<int, _i64> & size_data);
	void add(std::map<int, _i64> &data, int backupid, _i64 filesize);
	void add_del(std::map<int, SDelInfo> &data, int backupid, _i64 filesize, int clientid, int incremental);
	void updateBackups(std::map<int, _i64> &data);
	void updateDels(std::map<int, SDelInfo> &data);

	bool repairImagePath(str_map img);

	void measureSpeed(void);

	bool image_repair_mode;
	bool interruptible;

	IQuery *q_get_images;
	IQuery *q_update_images_size;
    IQuery *q_get_sizes;
	IQuery *q_size_update;
	IQuery *q_update_backups;
	IQuery *q_get_backup_size;
	IQuery *q_get_del_size;
	IQuery *q_add_del_size;
	IQuery *q_update_del_size;
	IQuery *q_save_client_hist;
	IQuery *q_set_file_backup_null;
	IQuery *q_create_hist;
	IQuery *q_get_all_clients;

	IDatabase *db;

	size_t num_updated_files;

	std::auto_ptr<ServerBackupDao> backupdao;
	std::auto_ptr<FileIndex> fileindex;
};