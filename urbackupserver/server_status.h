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
	sa_resume_full_file=6
};

enum SStatusError
{
	se_none,
	se_ident_error,
	se_authentication_error,
	se_too_many_clients
};

class IPipe;

struct SStatus
{
	SStatus(void){ online=false; has_status=false; done=false; statusaction=sa_none; r_online=false; clientid=0; pcdone=-1;
		prepare_hashqueuesize=0; hashqueuesize=0; starttime=0; action_done=false;
		comm_pipe=NULL; stop_backup=false; status_error=se_none;}

	std::wstring client;
	int clientid;
	unsigned int starttime;
	int pcdone;
	unsigned int prepare_hashqueuesize;
	unsigned int hashqueuesize;
	bool has_status;
	bool online;
	bool done;
	bool r_online;
	bool action_done;
	SStatusAction statusaction;
	unsigned int ip_addr;
	SStatusError status_error;
	IPipe *comm_pipe;
	bool stop_backup;
	std::string client_version_string;
	std::string os_version_string;
};

class ServerStatus
{
public:
	static void setServerStatus(const SStatus &pStatus, bool setactive=true);
	static void setOnline(const std::wstring &clientname, bool bonline);
	static void setDone(const std::wstring &clientname, bool bdone);
	static void setROnline(const std::wstring &clientname, bool bonline);
	static void setIP(const std::wstring &clientname, unsigned int ip);
	static void setStatusError(const std::wstring &clientname, SStatusError se);
	static void setCommPipe(const std::wstring &clientname, IPipe *p);
	static void stopBackup(const std::wstring &clientname, bool b);
	static bool isBackupStopped(const std::wstring &clientname);
	static void setClientVersionString(const std::wstring &clientname, const std::string& client_version_string);
	static void setOSVersionString(const std::wstring &clientname, const std::string& os_version_string);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static std::vector<SStatus> getStatus(void);
	static SStatus getStatus(const std::wstring &clientname);

	static bool isActive(void);
	static void updateActive(void);

	static void incrementServerNospcStalled(int add);
	static void setServerNospcFatal(bool b);

	static int getServerNospcStalled(void);
	static bool getServerNospcFatal(void);

private:
	static std::map<std::wstring, SStatus> status;
	static IMutex *mutex;
	static unsigned int last_status_update;

	static int server_nospc_stalled;
	static bool server_nospc_fatal;
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

private:
	ActiveThread *at;
	THREADPOOL_TICKET at_ticket;
};

#endif //SERVERSTATUS_H
