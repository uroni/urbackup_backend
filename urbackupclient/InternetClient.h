#include <vector>
#include <string>
#include <queue>

#include "../Interface/Thread.h"

class IMutex;
class IPipe;
class ISettingsReader;
class CTCPStack;
class ICustomClient;
class IScopedLock;
class ICondition;

struct SServerSettings
{
	std::string name;
	unsigned short port;
	std::string clientname;
	std::string authkey;
	bool internet_compress;
	bool internet_encrypt;
};

class InternetClient : public IThread
{
public:
	static void init_mutex(void);
	
	static void hasLANConnection(void);
	static bool isConnected(void);
	static void setHasConnection(bool b);

	static void newConnection(void);
	static void rmConnection(void);

	static void start(void);

	void operator()(void);

	void doUpdateSettings(void);
	bool tryToConnect(IScopedLock *lock);

	static void setHasAuthErr(void);
	static void resetAuthErr(void);

	static void updateSettings(void);

	static void addOnetimeToken(const std::string &token);

private:
	static IMutex *mutex;
	static IMutex *onetime_token_mutex;
	static bool connected;
	static size_t n_connections;
	static unsigned int last_lan_connection;
	static bool update_settings;
	static SServerSettings server_settings;
	static ICondition *wakeup_cond;
	static int auth_err;
	static std::queue<std::pair<unsigned int, std::string> > onetime_tokens;
};

class InternetClientThread : public IThread
{
public:
	InternetClientThread(IPipe *cs, const SServerSettings &server_settings);
	void operator()(void);

	char *getReply(CTCPStack *tcpstack, IPipe *pipe, size_t &replysize, unsigned int timeoutms);

	void runServiceWrapper(IPipe *pipe, ICustomClient *client);

private:
	std::string generateRandomBinaryAuthKey(void);
	IPipe *cs;
	SServerSettings server_settings;
};