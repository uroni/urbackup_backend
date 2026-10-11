#include <vector>
#include <string>
#include <queue>

#include "../Interface/Thread.h"
#include "../Interface/Types.h"

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

//Internet connection to one server of the server list (ServerList entry with internet enabled).
//The static functions manage the connections to all servers
class InternetClient : public IThread
{
public:
	static void init_mutex(void);
	static void destroy_mutex(void);
	//A server connected via LAN. Pauses the internet connection to the same server
	static void hasLANConnection(const std::string& server_ident);
	//The internet server of the client facet server_id authenticated with identity server_ident
	static void setInternetServerIdentity(int server_id, const std::string& server_ident);
	//True if any internet connection is established
	static bool isConnected(void);
	static int64 timeSinceLastLanConnection();

	static THREADPOOL_TICKET start(bool use_pool=false);
	static void stop(THREADPOOL_TICKET tt=ILLEGAL_THREADPOOL_TICKET);

	//The server list or settings.cfg changed
	static void updateSettings(void);

	//Status of the internet connection of the default facet or of the facet server_id
	static std::string getStatusMsg();
	static std::string getStatusMsg(int server_id);

	static IPipe* connect(const SServerConnectionSettings& selected_settings, CTCPStack& tcpstack);

	explicit InternetClient(int server_id);

	void operator()(void);

	int getServerId() const { return server_id; }
	//The facet was removed or its internet connection disabled
	bool isRetired();

	void setHasConnection(bool b);
	void newConnection(void);
	void rmConnection(void);
	void setHasAuthErr(void);
	void resetAuthErr(void);
	void addOnetimeToken(const std::string &token);
	std::pair<unsigned int, std::string> getOnetimeToken(void);
	void clearOnetimeTokens();
	void setStatusMsg(const std::string& msg);

private:
	void doUpdateSettings(void);
	bool tryToConnect(IScopedLock *lock);
	//Starts connections for new server list entries and stops removed ones (mutex locked)
	static void reconcileInstances();
	static InternetClient* findInstance(int server_id);

	int server_id;
	std::string server_ident;
	bool wait_for_local;
	bool connected;
	size_t n_connections;
	int64 last_lan_connection;
	bool update_settings;
	SServerSettings server_settings;
	int auth_err;
	std::queue<std::pair<unsigned int, std::string> > onetime_tokens;
	std::string status_msg;
	bool retired;
	THREADPOOL_TICKET ticket;

	static IMutex *mutex;
	static IMutex *onetime_token_mutex;
	static ICondition *wakeup_cond;
	static bool do_exit;
	static bool use_pool;
	static int64 last_any_lan_connection;
	static std::vector<InternetClient*> instances;
};

class InternetClientThread : public IThread
{
public:
	InternetClientThread(InternetClient* parent, IPipe *cs, const SServerSettings &server_settings, CTCPStack* tcpstack);
	~InternetClientThread();
	void operator()(void);

	char *getReply(CTCPStack *tcpstack, IPipe *pipe, size_t &replysize, unsigned int timeoutms);

	void runServiceWrapper(IPipe *pipe, ICustomClient *client);

private:
	std::string generateRandomBinaryAuthKey(void);
	void printInfo( IPipe * pipe );
	void checkHttp(IPipe* pipe, const std::string& hostname);
	InternetClient* parent;
	IPipe *cs;
	CTCPStack* tcpstack;
	SServerSettings server_settings;
};
