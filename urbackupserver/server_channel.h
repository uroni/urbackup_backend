#include "../Interface/Thread.h"
#include "../Interface/Pipe.h"
#include "../Interface/Mutex.h"
#include "fileclient/socket_header.h"
#include "../urbackupcommon/fileclient/tcpstack.h"

class BackupServerGet;

class ServerSettings;
namespace {
class SessionKeepaliveThread;
}

class ServerChannelThread : public IThread
{
public:
	ServerChannelThread(BackupServerGet *pServer_get, int clientid, bool internet_mode, const std::string& identiy);
	~ServerChannelThread(void);

	void operator()(void);

	std::string processMsg(const std::string &msg);

	void doExit(void);

private:
	int64 lasttime;
	int clientid;

	int constructCapabilities(void);

	bool hasDownloadImageRights(void);

	void LOGIN(str_map& params);
	void SALT(str_map& params);

	void GET_BACKUPCLIENTS(void);
	void GET_BACKUPIMAGES(const std::wstring& clientname);
	void DOWNLOAD_IMAGE(str_map& params);

	BackupServerGet *server_get;
	IPipe *exitpipe;
	IPipe *input;
	CTCPStack tcpstack;

	ServerSettings *settings;

	IMutex *mutex;

	volatile bool do_exit;
	bool combat_mode;
	bool internet_mode;

	std::string salt;
	std::wstring session;
	std::vector<int> client_right_ids;
	bool all_client_rights;

	int img_id_offset;

	std::string identity;
	std::string client_addr;

	SessionKeepaliveThread* keepalive_thread;
};