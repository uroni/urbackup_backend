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

struct SClientSumCacheItem
{
	SClientSumCacheItem(int clientid, _i64 s_rsize)
		: clientid(clientid), s_rsize(s_rsize) {}

	int clientid;
	_i64 s_rsize;
};

struct SNumFilesClientCacheItem
{
	SNumFilesClientCacheItem(const std::wstring &shahash, _i64 filesize, int clientid)
		: shahash(shahash), filesize(filesize), clientid(clientid) {}
	std::wstring shahash;
	_i64 filesize;
	int clientid;

	bool operator<(const SNumFilesClientCacheItem &other) const
	{
		std::pair<std::wstring, std::pair<_i64, int> > t1(shahash, std::pair<_i64, int>(filesize, clientid) );
		std::pair<std::wstring, std::pair<_i64, int> > t2(other.shahash, std::pair<_i64, int>(other.filesize, other.clientid) );
		return t1<t2;
	}
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

	std::map<int, _i64> calculateSizeDeltas(const std::wstring &pShaHash, _i64 filesize, _i64 *rsize, bool with_del);
	std::map<int, _i64> getSizes(void);
	void add(std::map<int, _i64> &data, std::map<int, _i64> &delta, int mod=1);
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
	IQuery *q_get_ncount_files;
	IQuery *q_get_ncount_files_num;
	IQuery *q_has_client;
	IQuery *q_has_client_del;
	IQuery *q_mark_done;
	IQuery *q_mark_done_bulk_files;
	IQuery *q_get_clients;
	IQuery *q_get_sizes;
	IQuery *q_size_update;
	IQuery *q_get_delfiles;
	IQuery *q_get_delfiles_num;
	IQuery *q_del_delfile;
	IQuery *q_del_delfile_bulk;
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
	IQuery *q_get_clients_del;

	IDatabase *db;

	size_t num_updated_files;

	std::map<std::pair<std::wstring, _i64>, std::vector<SClientSumCacheItem> > client_sum_cache;
	std::map<SNumFilesClientCacheItem, _i64> already_deleted_bytes;
	std::vector<SClientSumCacheItem> getClientSum(const std::wstring &shahash, _i64 filesize, bool with_del);
	void invalidateClientSum(const std::wstring &shahash, _i64 filesize);

	std::map<SNumFilesClientCacheItem, size_t> files_num_clients_cache;
	std::map<SNumFilesClientCacheItem, size_t> files_num_clients_del_cache;
	size_t getFilesNumClient(const SNumFilesClientCacheItem &item);
	size_t getFilesNumClientDel(const SNumFilesClientCacheItem &item);
};