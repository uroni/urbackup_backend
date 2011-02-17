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
	bool overwrite;
	bool allow_overwrite;
	bool client_overwrite;
	bool autoshutdown;
	int startup_backup_delay;
	bool autoupdate_clients;
};

class ServerSettings
{
public:
	ServerSettings(IDatabase *db, int pClientid=-1);
	~ServerSettings(void);

	void update(void);

	void doUpdate(void);
	
	SSettings *getSettings(void);

	static void init_mutex(void);
	static void updateAll(void);
private:
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