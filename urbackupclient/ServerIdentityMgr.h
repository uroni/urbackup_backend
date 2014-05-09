#include "../Interface/Mutex.h"
#include "../fileservplugin/IFileServFactory.h"

class ServerIdentityMgr
{
public:
	static void addServerIdentity(const std::string &pIdentity, const std::string& pPublicKey);
	static bool checkServerSessionIdentity(const std::string &pIdentity);
	static bool checkServerIdentity(const std::string &pIdentity);
	static bool hasPublicKey(const std::string &pIdentity);
	static std::string getPublicKey(const std::string &pIdentity);
	static bool setPublicKey(const std::string &pIdentity, const std::string &pPublicKey);
	static void loadServerIdentities(void);
	static size_t numServerIdentities(void);
	static void addSessionIdentity(const std::string &pIdentity);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static void setFileServ(IFileServ *pFilesrv);

	static bool isNewIdentity(const std::string &pIdentity);
	static bool hasOnlineServer(void);

private:
	static void writeServerIdentities(void);
	static void writeSessionIdentities();

	static std::vector<std::string> identities;
	static std::vector<std::string> publickeys;
	static std::vector<int64> online_identities;
	static std::vector<std::string> new_identities;
	static std::vector<std::string> session_identities;
	static std::vector<int64> online_session_identities;

	static IMutex *mutex;

	static IFileServ *filesrv;
};
