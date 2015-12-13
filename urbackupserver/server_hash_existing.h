#pragma once

#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "IncrFileBackup.h"
#include <deque>
#include "server_log.h"


class ServerHashExisting : public IThread
{
public:
	ServerHashExisting(int clientid, logid_t logid, IncrFileBackup* incr_backup);
	~ServerHashExisting();

	void queueStop(bool front);

	void queueFile(const std::string& fullpath, const std::string& hashpath);

	void operator()();

private:

	struct SHashItem
	{
		SHashItem()
			: do_stop(false)
		{}

		std::string fullpath;
		std::string hashpath;
		bool do_stop;
	};

	IMutex* mutex;
	ICondition* cond;
	std::deque<SHashItem> queue;
	bool has_error;
	int clientid;
	IncrFileBackup* incr_backup;
	logid_t logid;
};