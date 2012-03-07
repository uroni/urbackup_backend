#include "../Interface/Service.h"
#include "../Interface/CustomClient.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include <queue>

class IMutex;
class ICondition;
class InternetServicePipe;

class InternetService : public IService
{
	virtual ICustomClient* createClient();
	virtual void destroyClient( ICustomClient * pClient);
};

enum InternetServiceState
{
	ISS_AUTH,
	ISS_AUTHED,
	ISS_CONNECTING,
	ISS_USED,
	ISS_QUIT
};

class InternetServiceConnector;

struct SClientData
{
	std::queue<InternetServiceConnector*> spare_connections;
	unsigned int last_seen;
};

const char SERVICE_COMMANDS=0;
const char SERVICE_FILESRV=1;

class InternetServiceConnector : public ICustomClient
{
public:
	InternetServiceConnector(void);
	~InternetServiceConnector(void);
	virtual void Init(THREAD_ID pTID, IPipe *pPipe);

	virtual bool Run(void);
	virtual void ReceivePackets(void);

	static void init_mutex(void);

	static IPipe *getConnection(const std::string &clientname, char service, int timeoutms=-1);
	static std::vector<std::string> getOnlineClients(void);

	bool Connect(ICondition *n_cond, char service);
	bool stopConnecting(void);
	bool isConnected(void);
	void freeConnection(void);

	virtual bool wantReceive(void);
	virtual bool closeSocket(void);

	IPipe *getISPipe(void);
private:

	static std::map<std::string, SClientData> client_data;
	static IMutex *mutex;

	int state;

	THREAD_ID tid;
	IPipe *cs;
	InternetServicePipe *is_pipe;
	IMutex *local_mutex;
	ICondition * volatile connection_done_cond;

	CTCPStack tcpstack;

	unsigned int starttime;
	unsigned int lastpingtime;
	bool pinging;

	volatile bool do_connect;
	volatile bool stop_connecting;
	volatile bool is_connected;
	volatile bool free_connection;

	volatile char target_service;

	std::string clientname;
	std::string challenge;
};