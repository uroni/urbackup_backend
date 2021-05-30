#include "../Interface/Thread.h"
#include "../Interface/Pipe.h"
#include "../Interface/Mutex.h"
#include "../urbackupcommon/fileclient/socket_header.h"
#include "../urbackupcommon/fileclient/tcpstack.h"

class ClientMain;
class IDatabase;

class ServerSettings;
namespace {
class SessionKeepaliveThread;
class RestoreTokenKeepaliveThread;
}

class ServerChannelThread : public IThread
{
public:
	ServerChannelThread(ClientMain *client_main, const std::string& clientname, int clientid, bool internet_mode, 
		bool allow_restore,	std::string server_token, const std::string& virtual_client,
		ServerChannelThread* parent);
	~ServerChannelThread(void);

	void run();
	void operator()(void);

	std::string processMsg(const std::string &msg);

	void doExit(void);

    static void initOffset();

	static void init_mutex();

	bool isOnline();

	static void add_restore_token(const std::string& token, int backupid);
	static void remove_restore_token(const std::string& token);

	struct SRestoreToken
	{
		int backupid;
		int64 lasttime;
	};

	static bool get_restore_token(const std::string& token, SRestoreToken& ret);

private:
	int64 lasttime;
	int64 next_reauth_time;
	int64 reauth_tries;
	int clientid;

	int constructCapabilities(void);

	bool hasDownloadImageRights(const str_map& params=str_map());

	int getLastBackupid(IDatabase* db);

	void LOGIN(str_map& params);
	void SALT(str_map& params);

	void GET_BACKUPCLIENTS(void);
	void GET_BACKUPIMAGES(const std::string& clientname);
	void GET_FILE_BACKUPS(const std::string& clientname);
	void GET_FILE_BACKUPS_TOKENS(str_map& params);
	void GET_FILE_LIST_TOKENS(str_map& params);
	void DOWNLOAD_IMAGE(str_map& params);
	void DOWNLOAD_FILES(str_map& params);
	void DOWNLOAD_FILES_TOKENS(str_map& params);
	void RESTORE_PERCENT( str_map params );
	void RESTORE_DONE( str_map params );
	void BACKUP_PERCENT(str_map params);
	void BACKUP_DONE(str_map params);
	void STARTUP(str_map& params);

	void reset();

	bool has_restore_permission(const std::string& clientname, int clientid);

	std::string get_clientname(IDatabase* db, int clientid);

	void add_extra_channel();

	void remove_extra_channel();

	static void timeout_restore_tokens_used();

	ClientMain *client_main;
	IPipe *exitpipe;
	IPipe *input;
	CTCPStack tcpstack;

	ServerSettings *settings;

	IMutex *mutex;

	volatile bool do_exit;
	bool internet_mode;
	bool allow_restore;
	bool allow_shutdown;

	std::string salt;
	std::string session;
	std::vector<int> client_right_ids;
	bool all_client_rights;

    static int img_id_offset;

	std::string client_addr;

	SessionKeepaliveThread* keepalive_thread;
	RestoreTokenKeepaliveThread* restore_token_keepalive_thread;

	std::string clientname;

	std::string virtual_client;

	std::string last_fileaccesstokens;

	std::string server_token;

	std::vector<THREADPOOL_TICKET> fileclient_threads;

	ServerChannelThread* parent;
	std::vector<ServerChannelThread*> extra_channel_threads;

	static IMutex* restore_token_mutex;
	static std::map<std::string, SRestoreToken> restore_tokens;
	static std::map<std::string, SRestoreToken> restore_tokens_used;

	int64 startup_timestamp;
};
