#include "../Interface/Service.h"

class CHTTPService : public IService
{
public:
	CHTTPService(std::string pRoot, std::string pProxy_server, int pProxy_port, int pShare_proxy_connections);
	virtual ICustomClient* createClient();
	virtual void destroyClient( ICustomClient * pClient);

	std::string getRoot(void);
	std::string getProxyServer(void);
	int getProxyPort(void);
	int getShareProxyConnections(void);

private:
	std::string root;
	std::string proxy_server;
	int proxy_port;
	int share_proxy_connections;
};
