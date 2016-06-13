#include <map>
#include <vector>

#include "Interface/SessionMgr.h"
#include "Interface/Mutex.h"
#include "Interface/Condition.h"
#include "Interface/Thread.h"

class CSessionMgr : public ISessionMgr, public IThread
{
public:
	CSessionMgr(void);
	~CSessionMgr();
	virtual std::string GenerateSessionIDWithUser(const std::string &pUsername, const std::string &pIdentData, bool update_user=false);

	virtual SUser *getUser(const std::string &pSID, const std::string &pIdentData, bool update=true);
	virtual void releaseUser(SUser *user);
	virtual void lockUser(SUser *user);

	virtual bool RemoveSession(const std::string &pSID);
	
	void startTimeoutSessionThread();	
	void operator()(void);
private:
	
	unsigned int TimeoutSessions();

	int SESSIONID_LEN;
	int SESSION_TIMEOUT_S;

	std::vector<char> Pool;
	std::map<std::string, SUser*> mSessions;

	IMutex* sess_mutex;
	
	ICondition *wait_cond;
	IMutex *wait_mutex;
	
	IMutex *stop_mutex;
	ICondition *stop_cond;
	bool run;
};
