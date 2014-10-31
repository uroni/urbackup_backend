#ifndef SERVER_SETTINS_H
#define SERVER_SETTINS_H

#include "../Interface/SettingsReader.h"
#include "../Interface/Database.h"
#include "../Interface/Mutex.h"

namespace
{
	const char* image_file_format_default = "default";
	const char* image_file_format_vhd = "vhd";
	const char* image_file_format_vhdz = "vhdz";
	const char* image_file_format_cowraw = "cowraw";
}

struct SSettings
{
	int clientid;
	std::wstring backupfolder;
	std::wstring backupfolder_uncompr;
	int update_freq_incr;
	int update_freq_full;
	int update_freq_image_full;
	int update_freq_image_incr;
	int max_file_incr;
	int min_file_incr;
	int max_file_full;
	int min_file_full;
	int min_image_incr;
	int max_image_incr;
	int min_image_full;
	int max_image_full;
	bool no_images;
	bool no_file_backups;
	bool overwrite;
	bool allow_overwrite;
	bool autoshutdown;
	int startup_backup_delay;
	bool download_client;
	bool autoupdate_clients;
	int max_sim_backups;
	int max_active_clients;
	std::string backup_window_incr_file;
	std::string backup_window_full_file;
	std::string backup_window_incr_image;
	std::string backup_window_full_image;
	std::wstring computername;
	std::wstring exclude_files;
	std::wstring include_files;
	std::wstring default_dirs;
	std::string cleanup_window;
	bool allow_pause;
	bool allow_starting_full_file_backups;
	bool allow_starting_incr_file_backups;
	bool allow_starting_full_image_backups;
	bool allow_starting_incr_image_backups;
	bool allow_config_paths;
	bool allow_log_view;
	bool allow_tray_exit;
	std::string image_letters;
	bool backup_database;
	std::string internet_server;
	bool client_set_settings;
	unsigned short internet_server_port;
	std::string internet_authkey;
	bool internet_full_file_backups;
	bool internet_image_backups;
	bool internet_encrypt;
	bool internet_compress;
	int internet_compression_level;
	int local_speed;
	int internet_speed;
	int global_internet_speed;
	int global_local_speed;
	bool internet_mode_enabled;
	bool silent_update;
	bool use_tmpfiles;
	bool use_tmpfiles_images;
	std::wstring tmpdir;
	std::string local_full_file_transfer_mode;
	std::string internet_full_file_transfer_mode;
	std::string local_incr_file_transfer_mode;
	std::string internet_incr_file_transfer_mode;
	std::string local_image_transfer_mode;
	std::string internet_image_transfer_mode;
	size_t file_hash_collect_amount;
	size_t file_hash_collect_timeout;
	size_t file_hash_collect_cachesize;
	size_t update_stats_cachesize;
	std::string global_soft_fs_quota;
	std::string filescache_type;
	int64 filescache_size;
	int suspend_index_limit;
	std::string client_quota;
	bool end_to_end_file_backup_verification;
	bool internet_calculate_filehashes_on_client;
	bool use_incremental_symlinks;
	std::string image_file_format;
	bool trust_client_hashes;
	bool internet_connect_always;
	bool show_server_updates;
	bool verify_using_client_hashes;
	bool internet_readd_file_entries;
};

struct STimeSpan
{
	STimeSpan(void): dayofweek(-1) {}
	STimeSpan(int dayofweek, float start_hour, float stop_hour):dayofweek(dayofweek), start_hour(start_hour), stop_hour(stop_hour) {}
	STimeSpan(float start_hour, float stop_hour):dayofweek(0), start_hour(start_hour), stop_hour(stop_hour) {}
	int dayofweek;
	float start_hour;
	float stop_hour;
};

class ServerSettings;

struct SSettingsCacheItem
{
	SSettings* settings;
	size_t refcount;
	bool needs_update;
};

class ServerSettings
{
public:
	ServerSettings(IDatabase *db, int pClientid=-1);
	~ServerSettings(void);

	void update(bool force_update);

	void doUpdate(void);
	
	SSettings *getSettings(bool *was_updated=NULL);

	static void init_mutex(void);
	static void destroy_mutex(void);
	static void clear_cache();
	static void updateAll(void);
	static std::string generateRandomAuthKey(size_t len=10);
	static std::string generateRandomBinaryKey(void);

	std::vector<STimeSpan> getBackupWindowIncrFile(void);
	std::vector<STimeSpan> getBackupWindowFullFile(void);
	std::vector<STimeSpan> getBackupWindowIncrImage(void);
	std::vector<STimeSpan> getBackupWindowFullImage(void);

	std::vector<STimeSpan> getCleanupWindow(void);
	std::vector<std::string> getBackupVolumes(const std::string& all_volumes);

	std::string getImageFileFormat();

	int getUpdateFreqImageIncr();
	int getUpdateFreqFileIncr();
	int getUpdateFreqImageFull();
	int getUpdateFreqFileFull();

private:
	void operator=(const ServerSettings& other){};
	ServerSettings(const ServerSettings& other){};

	std::vector<STimeSpan> getWindow(std::string window);
	float parseTimeDet(std::string t);
	STimeSpan parseTime(std::string t);
	int parseDayOfWeek(std::string dow);
	void readSettingsDefault(void);
	void readSettingsClient(void);
	void readBoolClientSetting(const std::string &name, bool *output);
	void readStringClientSetting(const std::string &name, std::string *output);
	void readIntClientSetting(const std::string &name, int *output);
	void readSizeClientSetting(const std::string &name, size_t *output);
	void createSettingsReaders();
	void updateInternal(bool* was_updated);

	SSettingsCacheItem* settings_cache;
	SSettings* local_settings;

	ISettingsReader *settings_default;
	ISettingsReader *settings_client;
	IDatabase* db;

	volatile bool do_update;

	int clientid;

	static std::map<ServerSettings*, bool> g_settings;
	static std::map<int, SSettingsCacheItem> g_settings_cache;
	static IMutex *g_mutex;
};


#endif //SERVER_SETTINS_H
