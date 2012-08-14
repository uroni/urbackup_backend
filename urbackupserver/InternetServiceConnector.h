#include "../Interface/Service.h"
#include "../Interface/CustomClient.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/internet_pipe_capabilities.h"
#include <queue>

class IMutex;
class ICondition;
class InternetServicePipe;
class CompressedPipe;

class InternetService : public IService
{
	virtual ICustomClient* createClient();
	virtual void destroyClient( ICustomClient * pClient);
};

enum InternetServiceState
{
	ISS_AUTH,
	ISS_AUTHED,
	ISS_CAPA,
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
	static void destroy_mutex(void);

	static IPipe *getConnection(const std::string &clientname, char service, int timeoutms=-1);
	static std::vector<std::string> getOnlineClients(void);

	bool Connect(ICondition *n_cond, ICondition *stop_cond, char service);
	void stopConnecting(void);
	void stopConnectingAndWait(void);
	bool isConnected(void);
	void freeConnection(void);
	bool hasTimeout(void);

	virtual bool wantReceive(void);
	virtual bool closeSocket(void);

	IPipe *getISPipe(void);
	
	void localWait(ICondition *cond, int timems);
private:

	void cleanup_pipes(void);
	void cleanup(void);

	static std::map<std::string, SClientData> client_data;
	static IMutex *mutex;

	int state;

	THREAD_ID tid;
	IPipe *cs;
	InternetServicePipe *is_pipe;
	CompressedPipe *comp_pipe;
	IPipe *comm_pipe;
	IMutex *local_mutex;
	ICondition * volatile connection_done_cond;
	ICondition * volatile connection_stop_cond;

	CTCPStack tcpstack;

	unsigned int starttime;
	unsigned int lastpingtime;
	bool pinging;
	volatile bool has_timeout;

	volatile bool do_connect;
	volatile bool stop_connecting;
	volatile bool is_connected;
	volatile bool free_connection;

	volatile char target_service;

	std::string clientname;
	std::string challenge;
	std::string authkey;

	int compression_level;
};
