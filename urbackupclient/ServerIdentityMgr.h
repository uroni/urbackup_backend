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

	static void setFileServ(IFileServ *pFilesrv); 

private:
	static std::vector<std::string> identities;

	static IMutex *mutex;

	static IFileServ *filesrv;
};