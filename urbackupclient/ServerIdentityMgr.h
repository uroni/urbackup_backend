#include "../Interface/Mutex.h"
#include "../fileservplugin/IFileServFactory.h"

class ServerIdentityMgr
{
public:
	static void addServerIdentity(const std::string &pIdentity);
	static bool checkServerIdentity(const std::string &pIdentity);
	static void loadServerIdentities(void);
	static size_t numServerIdentities(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static void setFileServ(IFileServ *pFilesrv);

	static bool isNewIdentity(const std::string &pIdentity);
	static bool hasOnlineServer(void);

private:
	static void writeServerIdentities(void);

	static std::vector<std::string> identities;
	static std::vector<unsigned int> online_identities;
	static std::vector<std::string> new_identities;

	static IMutex *mutex;

	static IFileServ *filesrv;
};
