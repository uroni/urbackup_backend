#include <vector>
#include <string>
#include <queue>
#include <set>
#include <map>

#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../Interface/Server.h"

class IMutex;
class IPipe;
class ISettingsReader;
class CTCPStack;
class ICustomClient;
class IScopedLock;
class ICondition;

struct SServerConnectionSettings
{
	std::string hostname;
	std::string proxy;
	unsigned short port;
};

struct SServerSettings
{
	std::vector<SServerConnectionSettings> servers;
	size_t selected_server;
	std::string clientname;
	std::string authkey;
	bool internet_compress;
	bool internet_encrypt;
	bool internet_connect_always;
};

class InternetClient : public IThread
{
public:
	InternetClient(int facet_id, const std::string& settings_fn)
		: facet_id(facet_id), settings_fn(settings_fn) {}

	static void init_mutex(void);
	static void destroy_mutex(void);
	static void hasLANConnection(void);
	static bool isConnected(void);
	static void setHasConnection(bool b);
	static int64 timeSinceLastLanConnection();

	void newConnection(void);
	void rmConnection(void);

	static std::vector<THREADPOOL_TICKET> start(bool use_pool=false);
	static void stop(std::vector<THREADPOOL_TICKET> tt);

	void operator()(void);

	void doUpdateSettings(void);
	bool tryToConnect(IScopedLock *lock);

	void setHasAuthErr(void);
	void resetAuthErr(void);

	static void updateSettings(void);

	void addOnetimeToken(const std::string &token);
	std::pair<unsigned int, std::string> getOnetimeToken(void);
	void clearOnetimeTokens();

	static std::string getStatusMsg(int facet_id);

	void setStatusMsg(const std::string& msg);

	static IPipe* connect(const SServerConnectionSettings& selected_settings, CTCPStack& tcpstack);

private:
	static IMutex* g_mutex;
	IMutex* mutex = Server->createMutex();
	IMutex *onetime_token_mutex = Server->createMutex();
	static bool connected;
	size_t n_connections = 0;
	static int64 last_lan_connection;
	static std::set<int> update_settings;
	static std::map<int, ICondition*> running_client_facets;
	static SServerSettings server_settings;
	ICondition* wakeup_cond = Server->createCondition();
	int auth_err = 0;
	std::queue<std::pair<unsigned int, std::string> > onetime_tokens;
	static bool do_exit;
	static std::map<int,std::string> status_msgs;

	std::string settings_fn;
	int facet_id;
};

class InternetClientThread : public IThread
{
public:
	InternetClientThread(InternetClient* internet_client, IPipe *cs, const SServerSettings &server_settings, CTCPStack* tcpstack);
	~InternetClientThread();
	void operator()(void);

	char *getReply(CTCPStack *tcpstack, IPipe *pipe, size_t &replysize, unsigned int timeoutms);

	void runServiceWrapper(IPipe *pipe, ICustomClient *client);

private:
	std::string generateRandomBinaryAuthKey(void);
	void printInfo( IPipe * pipe );
	IPipe *cs;
	CTCPStack* tcpstack;
	SServerSettings server_settings;
	InternetClient* internet_client;
};
