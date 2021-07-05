#include "resource.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <set>

struct ERESOURCE_INT {
	ERESOURCE_INT()
		: owner(std::thread::id()),
		excl_count(0) {}

	std::mutex mutex;
	std::condition_variable cond;
	std::thread::id owner;
	std::set<std::thread::id> shared_threads;
	int excl_count;
};

BOOLEAN ExAcquireResourceSharedLite(PERESOURCE Resource, BOOLEAN Wait)
{
	std::unique_lock<std::mutex> lock(Resource->res->mutex);

	if (Resource->res->owner!= std::thread::id() &&
		Resource->res->owner == std::this_thread::get_id())
	{
		++Resource->res->excl_count;
		return TRUE;
	}

	if (Resource->res->owner == std::thread::id())
	{
		Resource->res->shared_threads.insert(std::this_thread::get_id());
		return TRUE;
	}

	if (!Wait)
	{
		return FALSE;
	}

	while (Resource->res->owner != std::thread::id())
	{
		Resource->res->cond.wait(lock);
	}

	Resource->res->shared_threads.insert(std::this_thread::get_id());
	return TRUE;
}

BOOLEAN ExAcquireResourceExclusiveLite(PERESOURCE Resource, BOOLEAN Wait)
{
	std::unique_lock<std::mutex> lock(Resource->res->mutex);

	if (Resource->res->owner!=std::thread::id() &&
		Resource->res->owner == std::this_thread::get_id())
	{
		++Resource->res->excl_count;
		return TRUE;
	}

	if (Resource->res->shared_threads.empty() &&
		  Resource->res->owner == std::thread::id())
	{
		Resource->res->owner = std::this_thread::get_id();
		Resource->res->excl_count = 1;
		return TRUE;
	}

	if (!Wait)
	{
		return FALSE;
	}

	while (!Resource->res->shared_threads.empty() ||
		Resource->res->owner != std::thread::id())
	{
		Resource->res->cond.wait(lock);
	}

	Resource->res->owner = std::this_thread::get_id();
	Resource->res->excl_count = 1;
	
	return TRUE;
}

BOOLEAN ExReleaseResourceLite(PERESOURCE Resource)
{
	std::unique_lock<std::mutex> lock(Resource->res->mutex);

	if (Resource->res->owner !=std::thread::id() &&
		Resource->res->owner == std::this_thread::get_id())
	{
		if (Resource->res->excl_count > 1)
		{
			--Resource->res->excl_count;
		}
		else
		{
			Resource->res->owner = std::thread::id();
			Resource->res->excl_count = 0;
			Resource->res->cond.notify_all();
		}
	}
	else
	{
		Resource->res->shared_threads.erase(std::this_thread::get_id());
		if (Resource->res->shared_threads.empty())
		{
			Resource->res->cond.notify_all();
		}
	}

	return TRUE;
}

void ExConvertExclusiveToSharedLite(PERESOURCE Res)
{
	std::unique_lock<std::mutex> lock(Res->res->mutex);

	Res->res->owner = std::thread::id();
	Res->res->excl_count = 0;
	Res->res->shared_threads.insert(std::this_thread::get_id());
}

BOOLEAN ExDeleteResourceLite(PERESOURCE Resource)
{
	delete Resource->res;
	return TRUE;
}

BOOLEAN ExInitializeResourceLite(PERESOURCE Resource)
{
	Resource->res = new ERESOURCE_INT;
	return TRUE;
}

BOOLEAN ExIsResourceAcquiredExclusiveLite(PERESOURCE Resource)
{
	std::unique_lock<std::mutex> lock(Resource->res->mutex);
	return Resource->res->owner != std::thread::id() &&
		Resource->res->excl_count > 0 &&
		Resource->res->owner == std::this_thread::get_id();
}

void ExReleaseResource(PERESOURCE Resource)
{
	ExReleaseResourceLite(Resource);
}

BOOLEAN ExIsResourceAcquiredSharedLite(PERESOURCE Resource)
{
	std::unique_lock<std::mutex> lock(Resource->res->mutex);

	if (Resource->res->owner != std::thread::id())
	{
		return Resource->res->owner == std::this_thread::get_id() ? TRUE : FALSE;
	}	

	return Resource->res->shared_threads.find(std::this_thread::get_id()) != Resource->res->shared_threads.end();
}

BOOLEAN ExIsResourceAcquiredExclusive(PERESOURCE Resource)
{
	return ExIsResourceAcquiredExclusiveLite(Resource);
}