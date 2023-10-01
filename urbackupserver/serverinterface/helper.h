#pragma once

#include "../../Interface/Database.h"
#include "../../Interface/Server.h"
#include "../../Interface/SessionMgr.h"
#include "../../Interface/Template.h"
#include "../../Interface/Mutex.h"
#include "../../Interface/SharedMutex.h"
#include "../../Interface/Condition.h"
#include "../../urbackupcommon/os_functions.h"
#include <map>
#include <set>

const int SESSION_ID_ADMIN = 0;
const int SESSION_ID_INVALID = -1;
const int SESSION_ID_TOKEN_AUTH = -2;

class Helper
{
public:
	Helper(THREAD_ID pTID, str_map *pPOST, str_map *pPARAMS);
	void update(THREAD_ID pTID, str_map *pPOST, str_map *pPARAMS);
	~Helper(void);
	SUser *getSession(void);
	std::string generateSession(std::string username);
	void OverwriteLanguage(std::string pLanguage);
	ITemplate *createTemplate(std::string name);
	IDatabase *getDatabase(void);
	std::string getRights(const std::string &domain);

	std::string getTimeFormatString(void);

	std::string getLanguage(void);

	void Write(std::string str, bool content_type_json=true);
	void WriteTemplate(ITemplate *tmpl);

	void releaseAll(void);

	std::vector<int> getRightIDs(std::string rights);
	bool hasRights(int clientid, std::string rights, std::vector<int> right_ids);

	bool checkPassword(const std::string &username, const std::string &password, int *user_id, bool plainpw);
	bool ldapLogin(const std::string &username, const std::string &password,
		std::string* ret_errmsg=NULL, std::string* rights=NULL, bool dry_login=false);

	std::vector<int> clientRights(const std::string& right_name, bool& all_client_rights);

	std::string getStrippedServerIdentity(void);

	void sleep(unsigned int ms);

	bool ldapEnabled();

	std::string remoteAddr();

	class IpLogin
	{
		const std::string remote_addr;
	public:
		IpLogin(const std::string& remote_addr)
			: remote_addr(remote_addr)
		{
			Helper::startLogin(remote_addr);
		}
		~IpLogin()
		{
			Helper::stopLogin(remote_addr);
		}
	};

	static void startLogin(const std::string& remote_addr);

	static void stopLogin(const std::string& remote_addr);

	static bool rateLimited(const std::string& remote_addr);

	static void addToRateLimit(const std::string& remote_addr);

	static void rateLimitTimeout();

	static void init_mutex();
private:

	static bool failedLoginRateLimit();

	std::string getIdentData();

	std::string getRightsInt(const std::string &domain);
	std::map<std::string, std::string> parseRightsString(const std::string& rights);


	SUser* session;
	std::vector<ITemplate*> templates;
	std::string language;

	bool invalid_session;

	str_map *POST;
	str_map *PARAMS;

	std::map<std::string, std::string> ldap_rights;

	THREAD_ID tid;

	bool prioritized;
	SPrioInfo prio_info;

	static ISharedMutex* rate_limit_mutex;
	static IMutex* login_wait_mutex;
	static ICondition* login_wait_cond;
	typedef std::map<int64, std::map<std::string, int64> > rate_limit_map;
	static rate_limit_map rates_per_bucket;
	static std::set<std::string> logging_in;
	static std::map<std::string, int64> banned_ips;
};

struct SStartupStatus
{
	SStartupStatus(void)
		: upgrading_database(false),
		  creating_filesindex(false),
		  pc_done(-1.0),
		curr_db_version(0),
		target_db_version(0),
		processed_file_entries(0),
		mutex(NULL) {}

	bool upgrading_database;
	int curr_db_version;
	int target_db_version;

	bool creating_filesindex;
	size_t processed_file_entries;
	
	double pc_done;

	IMutex *mutex;
};