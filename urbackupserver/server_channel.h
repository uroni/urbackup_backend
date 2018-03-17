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
}

class ServerChannelThread : public IThread
{
public:
	ServerChannelThread(ClientMain *client_main, const std::string& clientname, int clientid, bool internet_mode, 
		bool allow_restore, std::vector<std::string> allow_restore_clientids,
		const std::string& identiy, std::string server_token, const std::string& virtual_client);
	~ServerChannelThread(void);

	void operator()(void);

	std::string processMsg(const std::string &msg);

	void doExit(void);

    static void initOffset();

	bool isOnline();

private:
	int64 lasttime;
	int clientid;

	int constructCapabilities(void);

	bool hasDownloadImageRights(void);

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

	void reset();

	bool has_restore_permission(const std::string& clientname, int clientid);

	std::string get_clientname(IDatabase* db, int clientid);

	ClientMain *client_main;
	IPipe *exitpipe;
	IPipe *input;
	CTCPStack tcpstack;

	ServerSettings *settings;

	IMutex *mutex;

	volatile bool do_exit;
	bool internet_mode;
	bool allow_restore;
	std::vector<std::string> allow_restore_clients;
	bool allow_shutdown;

	std::string salt;
	std::string session;
	std::vector<int> client_right_ids;
	bool all_client_rights;

    static int img_id_offset;

	std::string client_addr;

	SessionKeepaliveThread* keepalive_thread;

	std::string clientname;

	std::string virtual_client;

	std::string last_fileaccesstokens;

	std::string server_token;

	std::vector<THREADPOOL_TICKET> fileclient_threads;
};
