#include <vector>

#include "../Interface/Thread.h"

class IMutex;
class IPipe;
class ISettingsReader;
class CTCPStack;
class ICustomClient;

struct SServerSettings
{
	std::string name;
	unsigned short port;
	std::string clientname;
	std::string authkey;
};

class InternetClient
{
public:
	static void init_mutex(void);
	static void hasLANConnection(void);
	static bool isConnected(void);
	static void setHasConnection(bool b);

	static void newConnection(void);
	static void rmConnection(void);

	void operator()(void);

	void doUpdateSettings(void);
	bool tryToConnect(void);

	static void updateSettings(void);

private:
	static IMutex *mutex;
	static bool connected;
	static size_t n_connections;
	static unsigned int last_lan_connection;
	static bool update_settings;
	static SServerSettings server_settings;
};

class InternetClientThread : public IThread
{
public:
	InternetClientThread(IPipe *cs, const SServerSettings &server_settings);
	void operator()(void);

	char *getReply(CTCPStack *tcpstack, IPipe *pipe, size_t &replysize, unsigned int timeoutms);

	void runServiceWrapper(IPipe *pipe, ICustomClient *client);

private:
	IPipe *cs;
	SServerSettings server_settings;
};