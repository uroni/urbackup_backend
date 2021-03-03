#include "resource.h"
#include <mutex>
#include <condition_variable>
#include <atomic>

struct ERESOURCE_INT {
	ERESOURCE_INT()
		: shared_lock(0), owner(std::thread::id()),
		excl_count(0) {}

	std::mutex mutex;
	std::condition_variable cond;
	int shared_lock;
	std::thread::id owner;
	int excl_count;
};

BOOLEAN ExAcquireResourceSharedLite(PERESOURCE Resource, BOOLEAN Wait)
{
	std::unique_lock<std::mutex> lock(Resource->res->mutex);

	if (Resource->res->owner == std::this_thread::get_id())
	{
		++Resource->res->excl_count;
		return TRUE;
	}

	if (Resource->res->owner == std::thread::id())
	{
		++Resource->res->shared_lock;
		return TRUE;
	}

	if (!Wait)
	{
		return FALSE;
	}

	while (Resource->res->owner == std::thread::id())
	{
		Resource->res->cond.wait(lock);
	}

	++Resource->res->shared_lock;
	return TRUE;
}

BOOLEAN ExAcquireResourceExclusiveLite(PERESOURCE Resource, BOOLEAN Wait)
{
	std::unique_lock<std::mutex> lock(Resource->res->mutex);

	if (Resource->res->owner == std::this_thread::get_id())
	{
		++Resource->res->excl_count;
		return TRUE;
	}

	if (!Resource->res->shared_lock &&
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

	while (Resource->res->shared_lock &&
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

	if (Resource->res->owner == std::this_thread::get_id())
	{
		if (Resource->res->excl_count > 1)
		{
			--Resource->res->excl_count;
		}
		else
		{
			Resource->res->owner = std::thread::id();
			Resource->res->excl_count = 0;
			Resource->res->cond.notify_one();
		}
	}
	else
	{
		--Resource->res->shared_lock;
		if (Resource->res->shared_lock == 0)
		{
			Resource->res->cond.notify_one();
		}
	}

	return TRUE;
}

void ExConvertExclusiveToSharedLite(PERESOURCE Res)
{
	std::unique_lock<std::mutex> lock(Res->res->mutex);

	Res->res->owner = std::thread::id();
	Res->res->excl_count = 0;
	++Res->res->shared_lock;
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

