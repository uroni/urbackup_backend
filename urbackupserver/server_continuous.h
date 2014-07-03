#pragma once

#include "../Interface/Thread.h"
#include <string>
#include <vector>
#include "../Interface/Mutex.h"
#include "../Server.h"
#include "../Interface/Condition.h"


class BackupServerContinuous : public IThread
{
public:
	BackupServerContinuous()
	{
		mutex = Server->createMutex();
		cond = Server->createCondition();
	}

	~BackupServerContinuous()
	{
		Server->destroy(mutex);
	}

	void operator()()
	{

	}

	void addChanges(const std::string& change_data)
	{
		IScopedLock lock(mutex);

		changes.push_back(change_data);

		cond->notify_all();
	}

	struct SSequence
	{
		int64 id;
		int64 next;
	};

private:

	bool compactChanges()
	{
		for(size_t i=0;i<changes.size();++i)
		{

		}
	}

	std::vector<std::string> changes;
	std::vector<SSequence> sequences;

	IMutex* mutex;
	ICondition* cond;
};