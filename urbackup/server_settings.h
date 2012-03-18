#ifndef SERVER_SETTINS_H
#define SERVER_SETTINS_H

#include "../Interface/SettingsReader.h"
#include "../Interface/Database.h"
#include "../Interface/Mutex.h"

struct SSettings
{
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
	bool client_overwrite;
	bool autoshutdown;
	int startup_backup_delay;
	bool autoupdate_clients;
	int max_sim_backups;
	int max_active_clients;
	std::string backup_window;
	std::wstring computername;
	std::wstring exclude_files;
	std::wstring include_files;
	std::wstring default_dirs;
	std::string cleanup_window;
	bool allow_pause;
	bool allow_starting_file_backups;
	bool allow_starting_image_backups;
	bool allow_config_paths;
	bool allow_log_view;
	std::string image_letters;
	bool backup_database;
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

class ServerSettings
{
public:
	ServerSettings(IDatabase *db, int pClientid=-1);
	~ServerSettings(void);

	void update(void);

	void doUpdate(void);
	
	SSettings *getSettings(bool *was_updated=NULL);

	static void init_mutex(void);
	static void updateAll(void);

	std::vector<STimeSpan> getBackupWindow(void);
	std::vector<STimeSpan> getCleanupWindow(void);
	std::vector<std::string> getBackupVolumes(void);

private:
	std::vector<STimeSpan> getWindow(std::string window);
	float parseTimeDet(std::string t);
	STimeSpan parseTime(std::string t);
	int parseDayOfWeek(std::string dow);
	void readSettingsDefault(void);
	void readSettingsClient(void);

	SSettings settings;

	ISettingsReader *settings_default;
	ISettingsReader *settings_client;

	volatile bool do_update;

	int clientid;

	static std::vector<ServerSettings*> g_settings;
	static IMutex *g_mutex;
};

std::vector<std::wstring> getSettingsList(void);

#endif //SERVER_SETTINS_H