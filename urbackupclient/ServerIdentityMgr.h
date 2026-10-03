#include "../Interface/Mutex.h"
#include "../fileservplugin/IFileServFactory.h"
#include <map>

struct SSessionIdentity
{
	explicit SSessionIdentity(std::string ident)
		: ident(ident), onlinetime(0)
	{

	}

	SSessionIdentity(std::string ident, std::string endpoint, int64 onlinetime, std::string secret_key, std::string server_ident)
		: ident(ident), endpoint(endpoint), onlinetime(onlinetime), secret_key(secret_key), server_ident(server_ident)
	{

	}

	std::string ident;
	std::string endpoint;
	int64 onlinetime;
	std::string secret_key;
	std::string server_ident;

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
	std::string fingerprint;
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
	static bool checkServerSessionIdentity(const std::string &pIdentity, const std::string& endpoint, std::string& secret_key);
	static bool checkServerIdentity(const std::string &pIdentity);
	static bool hasPublicKey(const std::string &pIdentity, bool count_fingerprint);
	static SPublicKeys getPublicKeys(const std::string &pIdentity);
	static bool setPublicKeys(const std::string &pIdentity, const SPublicKeys &pPublicKeys);
	static void loadServerIdentities(void);
	static size_t numServerIdentities(void);
	static void addSessionIdentity(const std::string &pIdentity, const std::string& endpoint, std::string secret_key,
		const std::string& server_ident);

	//Returns the permanent identity of the server using pIdentity (a session or server identity)
	//or an empty string if unknown
	static std::string getServerIdentity(const std::string &pIdentity);
	static void setServerTokenIdentity(const std::string& server_token, const std::string& server_ident);
	static std::string getServerTokenIdentity(const std::string& server_token);

	//Settings file holding the settings of one server (e.g. urbackup/data/settings_srv_<ident>.cfg)
	static std::string getServerSettingsFn(const std::string& settings_fn, const std::string& server_ident);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static void setFileServ(IFileServ *pFilesrv);

	static bool isNewIdentity(const std::string &pIdentity);
	static bool hasOnlineServer(void);

	static std::string calcFingerprint(const SPublicKeys& pPublicKeys);

private:
	static void writeServerIdentities(void);
	static void writeSessionIdentities();
	static bool write_file_admin_atomic(const std::string& data, const std::string& fn);

	static bool checkFingerprint(const SPublicKeys& pPublicKeys, const std::string& fingerprint);

	static std::vector<SIdentity> identities;
	static std::vector<std::string> new_identities;
	static std::vector<SSessionIdentity> session_identities;
	static std::map<std::string, std::string> server_token_identities;

	static IMutex *mutex;

	static IFileServ *filesrv;
};
