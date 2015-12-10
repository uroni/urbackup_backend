#include "../Interface/Mutex.h"
#include "../fileservplugin/IFileServFactory.h"

struct SSessionIdentity
{
	std::string ident;
	std::string endpoint;

	bool operator==(const SSessionIdentity& other) const
	{
		return ident==other.ident;
	}
};

struct SPublicKeys
{
	SPublicKeys()
	{
	}

	SPublicKeys(std::string dsa_key,
		std::string ecdsa409k1_key)
		: dsa_key(dsa_key),
		  ecdsa409k1_key(ecdsa409k1_key)
	{

	}

	bool empty() const
	{
		return dsa_key.empty() && ecdsa409k1_key.empty();
	}

	std::string dsa_key;
	std::string ecdsa409k1_key;
};

class ServerIdentityMgr
{
public:
	static void addServerIdentity(const std::string &pIdentity, const SPublicKeys& pPublicKey);
	static bool checkServerSessionIdentity(const std::string &pIdentity, const std::string& endpoint);
	static bool checkServerIdentity(const std::string &pIdentity);
	static bool hasPublicKey(const std::string &pIdentity);
	static SPublicKeys getPublicKeys(const std::string &pIdentity);
	static bool setPublicKeys(const std::string &pIdentity, const SPublicKeys &pPublicKeys);
	static void loadServerIdentities(void);
	static size_t numServerIdentities(void);
	static void addSessionIdentity(const std::string &pIdentity, const std::string& endpoint);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static void setFileServ(IFileServ *pFilesrv);

	static bool isNewIdentity(const std::string &pIdentity);
	static bool hasOnlineServer(void);

private:
	static void writeServerIdentities(void);
	static void writeSessionIdentities();

	static std::vector<std::string> identities;
	static std::vector<SPublicKeys> publickeys;
	static std::vector<int64> online_identities;
	static std::vector<std::string> new_identities;
	static std::vector<SSessionIdentity> session_identities;
	static std::vector<int64> online_session_identities;

	static IMutex *mutex;

	static IFileServ *filesrv;
};
