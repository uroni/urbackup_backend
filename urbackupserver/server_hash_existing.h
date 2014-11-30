#pragma once

#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "IncrFileBackup.h"
#include <deque>


class ServerHashExisting : public IThread
{
public:
	ServerHashExisting(int clientid, IncrFileBackup* incr_backup);
	~ServerHashExisting();

	void queueStop(bool front);

	void queueFile(const std::wstring& fullpath, const std::wstring& hashpath);

	void operator()();

private:

	struct SHashItem
	{
		SHashItem()
			: do_stop(false)
		{}

		std::wstring fullpath;
		std::wstring hashpath;
		bool do_stop;
	};

	IMutex* mutex;
	ICondition* cond;
	std::deque<SHashItem> queue;
	bool has_error;
	int clientid;
	IncrFileBackup* incr_backup;
};