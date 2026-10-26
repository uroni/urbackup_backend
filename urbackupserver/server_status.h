#ifndef SERVERSTATUS_H
#define SERVERSTATUS_H

#include <map>
#include <vector>
#include <deque>

#include "../Interface/Mutex.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

#include "server_log.h"

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
	sa_update=10,
	sa_check_integrity=11,
	sa_backup_database=12,
	sa_recalculate_statistics=13,
	sa_nightly_cleanup=14,
	sa_emergency_cleanup=15,
	sa_storage_migration=16,
	sa_startup_recovery=17
};

enum SStatusError
{
	se_none,
	se_ident_error,
	se_authentication_error,
	se_too_many_clients,
	se_uid_changed,
	se_authenticating,
	se_settings,
	se_startup,
	se_unknown
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
		 eta_set_time(0), stop(false), details(details),
		speed_bpms(0), can_stop(false), total_bytes(-1),
		done_bytes(0), detail_pc(-1), paused(false), start_id(0),
		paused_since(0), keeping_awake(-1)
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
	int detail_pc;
	double speed_bpms;
	std::deque<double> past_speed_bpms;
	logid_t logid;
	bool can_stop;
	int64 total_bytes;
	int64 done_bytes;
	bool paused;
	int64 start_id;
	//Time (unix seconds) the client reported paused, 0 if not paused
	int64 paused_since;
	//Client keeps the computer awake: 1 yes, 0 no, -1 unknown (not reported by the client)
	int keeping_awake;

	bool operator==(const SProcess& other) const
	{
		return id==other.id;
	}
};

struct SStatus
{
	SStatus(void){ online=false; has_status=false;r_online=false; clientid=0; 
		comm_pipe=NULL; status_error=se_none; running_jobs=0; restore=ERestore_disabled;
		lastseen = 0; client_paused_since = 0; client_pause_until = 0; client_keeping_awake = -1; client_prevent_sleep = -1;
	}

	std::string client;
	int clientid;
	bool has_status;
	bool online;
	bool r_online;
	std::string ip_addr_binary;
	SStatusError status_error;
	IPipe *comm_pipe;
	std::string client_version_string;
	std::string os_version_string;
	std::string client_subname;
	std::vector<SProcess> processes;
	int running_jobs;
	ERestore restore;
	int64 lastseen;
	//Reported by the client on its channel, independent of any running backup.
	//Not stored in the database: refreshed whenever the channel is (re)established.
	//Unix seconds, 0 if not paused/unknown
	int64 client_paused_since;
	//Unix seconds the client's pause times out, 0 if never/unknown
	int64 client_pause_until;
	//1 yes, 0 no, -1 unknown
	int client_keeping_awake;
	int client_prevent_sleep;
};

class ServerStatus
{
public:
	static void setOnline(const std::string &clientname, bool bonline);
	static void setROnline(const std::string &clientname, bool bonline);
	static bool removeStatus(const std::string &clientname);

	static void setIP(const std::string &clientname, unsigned int ip);
	static void setIPv6(const std::string &clientname, const char* ip);
	static void setStatusError(const std::string &clientname, SStatusError se);
	static void setCommPipe(const std::string &clientname, IPipe *p);
	static void stopProcess(const std::string &clientname, size_t id, bool b);
	static bool isProcessStopped(const std::string &clientname, size_t id);
	static void setClientVersionString(const std::string &clientname, const std::string& client_version_string);
	static void setOSVersionString(const std::string &clientname, const std::string& os_version_string);
	static bool sendToCommPipe(const std::string &clientname, const std::string& msg);
	static void setClientId(const std::string &clientname, int clientid, const std::string& client_subname);
	static void setRestore(const std::string &clientname, ERestore restore);
	static bool canRestore(const std::string &clientname, bool& server_confirms);
	static bool isRestoreDisabled(const std::string &clientname, std::string& client_subname);
	static void updateLastseen(const std::string &clientname);
	static void setClientState(const std::string &clientname, bool paused, int64 paused_for,
		int keeping_awake, int prevent_sleep, int64 resumes_in);
	static int64 getClientPausedSince(const std::string &clientname);
	static void resetClientState(const std::string &clientname);
	static int64 getLastseen(const std::string &clientname);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static std::vector<SStatus> getStatus(void);
	static SStatus getStatus(const std::string &clientname);

	static bool isActive(void);
	static void updateActive(void);

	static void resetServerNospcStalled();
	static void incrementServerNospcStalled(int add);
	static void setServerNospcFatal(bool b);

	static int getServerNospcStalled(void);
	static bool getServerNospcFatal(void);

	static size_t startProcess(const std::string &clientname, SStatusAction action,
		const std::string& details, logid_t logid, bool can_stop, int clientid=0);

	static bool stopProcess(const std::string &clientname, size_t id);

	static bool changeProcess(const std::string &clientname, size_t id, SStatusAction action);

	static void setProcessQueuesize(const std::string &clientname, size_t id,
		unsigned int prepare_hashqueuesize,
		unsigned int hashqueuesize);

	static void setProcessStarttime(const std::string &clientname, size_t id,
		int64 starttime);

	static void setProcessEta(const std::string &clientname, size_t id,
		int64 eta_ms, int64 eta_set_time);

	static void setProcessSpeed(const std::string &clientname, size_t id,
		double speed_bpms);

	static void setProcessEta(const std::string &clientname, size_t id,
		int64 eta_ms);

	static void setProcessEtaSetTime(const std::string &clientname, size_t id,
		int64 eta_set_time);

	static void setProcessPcDone(const std::string &clientname, size_t id,
		int pcdone);

	static void setProcessStartId(const std::string &clientname, size_t id,
		int64 start_id);

	static void setProcessTotalBytes(const std::string &clientname, size_t id,
		int64 total_bytes);

	static void setProcessDoneBytes(const std::string &clientname, size_t id,
		int64 done_bytes);

	static void setProcessDoneBytes(const std::string &clientname, size_t id,
		int64 done_bytes, int64 total_bytes);

	static void setProcessDetails(const std::string &clientname, size_t id,
		std::string details, int detail_pc);

	static void setProcessKeepingAwake(const std::string &clientname, size_t id,
		int keeping_awake);

	static void setProcessPaused(const std::string &clientname, size_t id,
		bool b);

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
	ScopedProcess(std::string clientname, SStatusAction action, const std::string& details, logid_t logid, bool can_stop, int clientid=0)
		: clientname(clientname)
	{
		status_id = ServerStatus::startProcess(clientname, action, details, logid, can_stop, clientid);
	}
	
	~ScopedProcess()
	{
		ServerStatus::stopProcess(clientname, status_id);
	}

	size_t getStatusId()
	{
		return status_id;
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
		at_ticket=Server->getThreadPool()->execute(at, "backup active");
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
