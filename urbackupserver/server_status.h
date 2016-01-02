#ifndef SERVERSTATUS_H
#define SERVERSTATUS_H

#include <map>
#include <vector>

#include "../Interface/Mutex.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

enum SStatusAction
{
	sa_none=0,
	sa_incr_file=1,
	sa_full_file=2,
	sa_incr_image=3,
	sa_full_image=4,
	sa_resume_incr_file=5,
	sa_resume_full_file=6,
	sa_cdp_sync=7,
	sa_restore_file=8,
	sa_restore_image=9,
	sa_update=10
};

enum SStatusError
{
	se_none,
	se_ident_error,
	se_authentication_error,
	se_too_many_clients
};

enum ERestore
{
	ERestore_disabled,
	ERestore_client_confirms,
	ERestore_server_confirms
};

class IPipe;

struct SProcess
{
	SProcess(size_t id, SStatusAction action, std::string details)
		: id(id), action(action), prepare_hashqueuesize(0),
		 hashqueuesize(0), starttime(0), pcdone(-1), eta_ms(0),
		 eta_set_time(0), stop(false), details(details)
	{

	}

	size_t id;
	SStatusAction action;
	unsigned int prepare_hashqueuesize;
	unsigned int hashqueuesize;
	int64 starttime;
	int pcdone;
	int64 eta_ms;
	int64 eta_set_time;
	bool stop;
	std::string details;

	bool operator==(const SProcess& other) const
	{
		return id==other.id;
	}
};

struct SStatus
{
	SStatus(void){ online=false; has_status=false;r_online=false; clientid=0; 
		comm_pipe=NULL; status_error=se_none; running_jobs=0; restore=ERestore_disabled; }

	std::string client;
	int clientid;
	bool has_status;
	bool online;
	bool r_online;
	unsigned int ip_addr;
	SStatusError status_error;
	IPipe *comm_pipe;
	std::string client_version_string;
	std::string os_version_string;
	std::vector<SProcess> processes;
	int running_jobs;
	ERestore restore;
};

class ServerStatus
{
public:
	static void setOnline(const std::string &clientname, bool bonline);
	static void setROnline(const std::string &clientname, bool bonline);
	static bool removeStatus(const std::string &clientname);

	static void setIP(const std::string &clientname, unsigned int ip);
	static void setStatusError(const std::string &clientname, SStatusError se);
	static void setCommPipe(const std::string &clientname, IPipe *p);
	static void stopProcess(const std::string &clientname, size_t id, bool b);
	static bool isProcessStopped(const std::string &clientname, size_t id);
	static void setClientVersionString(const std::string &clientname, const std::string& client_version_string);
	static void setOSVersionString(const std::string &clientname, const std::string& os_version_string);
	static bool sendToCommPipe(const std::string &clientname, const std::string& msg);
	static void setClientId(const std::string &clientname, int clientid);
	static void setRestore(const std::string &clientname, ERestore restore);
	static bool canRestore(const std::string &clientname, bool& server_confirms);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static std::vector<SStatus> getStatus(void);
	static SStatus getStatus(const std::string &clientname);

	static bool isActive(void);
	static void updateActive(void);

	static void incrementServerNospcStalled(int add);
	static void setServerNospcFatal(bool b);

	static int getServerNospcStalled(void);
	static bool getServerNospcFatal(void);

	static size_t startProcess(const std::string &clientname, SStatusAction action, const std::string& details);
	static bool stopProcess(const std::string &clientname, size_t id);

	static void setProcessQueuesize(const std::string &clientname, size_t id,
		unsigned int prepare_hashqueuesize,
		unsigned int hashqueuesize);

	static void setProcessStarttime(const std::string &clientname, size_t id,
		int64 starttime);

	static void setProcessEta(const std::string &clientname, size_t id,
		int64 eta_ms, int64 eta_set_time);

	static void setProcessEta(const std::string &clientname, size_t id,
		int64 eta_ms);

	static void setProcessEtaSetTime(const std::string &clientname, size_t id,
		int64 eta_set_time);

	static void setProcessPcDone(const std::string &clientname, size_t id,
		int pcdone);

	static void addRunningJob(const std::string &clientname);

	static void subRunningJob(const std::string &clientname);

	static int numRunningJobs(const std::string &clientname);

	static SProcess getProcess(const std::string &clientname, size_t id);

private:
	static SProcess* getProcessInt(const std::string &clientname, size_t id);

	static std::map<std::string, SStatus> status;
	static IMutex *mutex;
	static int64 last_status_update;
	static size_t curr_process_id;

	static int server_nospc_stalled;
	static bool server_nospc_fatal;
};

class ScopedProcess
{
public:
	ScopedProcess(std::string clientname, SStatusAction action, const std::string& details)
		: clientname(clientname)
	{
		status_id = ServerStatus::startProcess(clientname, action, details);
	}
	
	~ScopedProcess()
	{
		ServerStatus::stopProcess(clientname, status_id);
	}
private:
	std::string clientname;
	size_t status_id;
};

class ActiveThread : public IThread
{
public:
	ActiveThread(void) : do_exit(false) {}

	void operator()(void)
	{
		while(!do_exit)
		{
			ServerStatus::updateActive();
			Server->wait(1000);
		}		
	}

	void Exit(void) { do_exit=true; }

private:
	bool do_exit;
};

class ScopedActiveThread
{
public:
	ScopedActiveThread(void)
	{
		at=new ActiveThread;
		at_ticket=Server->getThreadPool()->execute(at);
	}

	~ScopedActiveThread(void)
	{
		at->Exit();
		Server->getThreadPool()->waitFor(at_ticket);
		delete at;
	}

	ActiveThread* get()
	{
		return at;
	}

private:
	ActiveThread *at;
	THREADPOOL_TICKET at_ticket;
};

#endif //SERVERSTATUS_H
