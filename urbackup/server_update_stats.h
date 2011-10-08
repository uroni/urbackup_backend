#include "../Interface/Types.h"
#include "../Interface/Thread.h"

class IQuery;
class IDatabase;

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

	void update_files(void);
	void update_images(void);

	void createQueries(void);
	void destroyQueries(void);

	static void repairImages(void);
private:

	std::map<int, _i64> calculateSizeDeltas(const std::wstring &pShaHash, _i64 filesize, _i64 *rsize);
	std::map<int, _i64> getSizes(void);
	void add(std::map<int, _i64> &data, std::map<int, _i64> &delta, int mod=1);
	void updateSizes(std::map<int, _i64> & size_data);
	void add(std::map<int, _i64> &data, int backupid, _i64 filesize);
	void add_del(std::map<int, SDelInfo> &data, int backupid, _i64 filesize, int clientid, int incremental);
	void updateBackups(std::map<int, _i64> &data);
	void updateDels(std::map<int, SDelInfo> &data);

	bool repairImagePath(str_map img);

	bool image_repair_mode;
	bool interruptible;

	IQuery *q_get_images;
	IQuery *q_update_images_size;
	IQuery *q_get_ncount_files;
	IQuery *q_has_client;
	IQuery *q_mark_done;
	IQuery *q_get_clients;
	IQuery *q_get_sizes;
	IQuery *q_size_update;
	IQuery *q_get_delfiles;
	IQuery *q_del_delfile;
	IQuery *q_update_backups;
	IQuery *q_get_backup_size;
	IQuery *q_get_del_size;
	IQuery *q_add_del_size;
	IQuery *q_update_del_size;
	IQuery *q_save_client_hist;
	IQuery *q_set_file_backup_null;
	IQuery *q_transfer_bytes;
	IQuery *q_get_transfer;
	IQuery *q_create_hist;
	IQuery *q_get_all_clients;

	IDatabase *db;
};