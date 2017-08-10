#include "../Interface/Service.h"
#include "../Interface/CustomClient.h"
#include "../Interface/Server.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/internet_pipe_capabilities.h"
#include "server_settings.h"
#include <queue>

class IMutex;
class ICondition;
class IInternetServicePipe;
class ICompressedPipe;
class IECDHKeyExchange;

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
	ISS_USED
};


class InternetServiceConnector;

struct SClientData
{
	std::vector<InternetServiceConnector*> spare_connections;
	int64 last_seen;
	std::string endpoint_name;
};

struct SOnetimeToken
{
	SOnetimeToken(const std::string &clientname)
		: clientname(clientname)
	{
		created=Server->getTimeMS();
		token=ServerSettings::generateRandomBinaryKey();
	}
	std::string token;
	int64 created;
	std::string clientname;
};

const char SERVICE_COMMANDS=0;
const char SERVICE_FILESRV=1;

class InternetServiceConnector : public ICustomClient
{
public:
	InternetServiceConnector(void);
	~InternetServiceConnector(void);
	virtual void Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName);

	virtual bool Run(IRunOtherCallback* run_other);
	virtual void ReceivePackets(IRunOtherCallback* run_other);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static IPipe *getConnection(const std::string &clientname, char service, int timeoutms=-1);
	static std::vector<std::pair<std::string, std::string> > getOnlineClients(void);
	

	bool connectStart();
	bool Connect(char service, int timems);
	void stopConnecting(void);
	void freeConnection(void);

	virtual bool wantReceive(void);
	virtual bool closeSocket(void);

	IPipe *getISPipe(void);

private:
	void operator=(const InternetServiceConnector& other){}
	void operator()(const InternetServiceConnector& other){}
	InternetServiceConnector(const InternetServiceConnector& other){}

	void cleanup_pipes(bool remove_connection);

	std::string  generateOnetimeToken(const std::string &clientname);
	std::string getOnetimeToken(unsigned int id, std::string *cname);
	static void removeOldTokens(void);

	std::string getAuthkeyFromDB(const std::string &clientname, bool &db_timeout);

	bool hasClient(const std::string &clientname, bool &db_timeout);

	static std::map<std::string, SClientData> client_data;
	static IMutex *mutex;

	int state;

	THREAD_ID tid;
	IPipe *cs;
	IInternetServicePipe *is_pipe;
	ICompressedPipe *comp_pipe;
	int conn_version;
	IPipe *comm_pipe;
	IMutex *local_mutex;
	ICondition* connection_done_cond;
	IECDHKeyExchange* ecdh_key_exchange;
	int64 ecdh_key_exchange_age;

	CTCPStack tcpstack;

	int64 starttime;
	int64 lastpingtime;
	bool pinging;
	bool has_timeout;

	bool connect_start;
	volatile bool do_connect;
	volatile bool stop_connecting;
	bool is_connected;
	volatile bool free_connection;

	char target_service;

	std::string clientname;
	std::string challenge;
	std::string authkey;

	int compression_level;

	bool token_auth;

	std::string endpoint_name;

	static IMutex *onetime_token_mutex;
	static std::map<unsigned int, SOnetimeToken> onetime_tokens;
	static unsigned int onetime_token_id;
	static std::vector<std::pair<IECDHKeyExchange*, int64> > ecdh_key_exchange_buffer;

	static int64 last_token_remove;

	unsigned int client_ping_interval;
};
