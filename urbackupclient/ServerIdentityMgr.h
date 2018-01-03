#include "../Interface/Mutex.h"
#include "../fileservplugin/IFileServFactory.h"

struct SSessionIdentity
{
	explicit SSessionIdentity(std::string ident)
		: ident(ident), onlinetime(0)
	{

	}

	SSessionIdentity(std::string ident, std::string endpoint, int64 onlinetime)
		: ident(ident), endpoint(endpoint), onlinetime(onlinetime)
	{

	}

	std::string ident;
	std::string endpoint;
	int64 onlinetime;

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

struct SIdentity
{
	explicit SIdentity(std::string ident)
		: ident(ident), onlinetime(0)
	{

	}

	SIdentity(std::string ident, SPublicKeys publickeys)
		: ident(ident), onlinetime(0), publickeys(publickeys)
	{

	}

	std::string ident;
	int64 onlinetime;
	SPublicKeys publickeys;

	bool operator==(const SIdentity& other) const
	{
		return ident==other.ident;
	}
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
	static bool write_file_admin_atomic(const std::string& data, const std::string& fn);

	static std::vector<SIdentity> identities;
	static std::vector<std::string> new_identities;
	static std::vector<SSessionIdentity> session_identities;

	static IMutex *mutex;

	static IFileServ *filesrv;
};
