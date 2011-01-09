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
	sa_full_image=4
};

struct SStatus
{
	SStatus(void){ online=false; has_status=false; done=false; statusaction=sa_none; r_online=false; clientid=0; pcdone=-1; prepare_hashqueuesize=0; hashqueuesize=0; starttime=0; action_done=false; }
	SStatus(bool pOnline) { online=pOnline; has_status=false; done=false; clientid=0; action_done=false;}
	std::string client;
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
};

class ServerStatus
{
public:
	static void setServerStatus(const SStatus &pStatus, bool setactive=true);
	static void setOnline(const std::string &clientname, bool bonline);
	static void setDone(const std::string &clientname, bool bdone);
	static void setROnline(const std::string &clientname, bool bonline);

	static void init_mutex(void);

	static std::vector<SStatus> getStatus(void);
	static SStatus getStatus(const std::string &clientname);

	static bool isActive(void);
	static void updateActive(void);

private:
	static std::map<std::string, SStatus> status;
	static IMutex *mutex;
	static unsigned int last_status_update;
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