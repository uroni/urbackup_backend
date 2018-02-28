#pragma once

#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/ThreadPool.h"
#include <string>
#include <map>
#include <set>

class ScopedMountedImage;

class ImageMount : public IThread
{
public:
	void operator()();

	static bool mount_image(int backupid, ScopedMountedImage& mounted_image, int64 timeoutms, bool& has_timeout, std::string& errmsg);
	static std::string get_mount_path(int backupid, bool do_mount, ScopedMountedImage& mounted_image, int64 timeoutms, bool& has_timeout, std::string& errmsg);

	static void incrImageMounted(int backupid);
	static void decrImageMounted(int backupid);

	static bool lockImage(int backupid, int64 timeoutms);
	static void unlockImage(int backupid);

	static void mount_image_thread(int backupid, std::string& errmsg);

private:
	static bool mount_image_int(int backupid, ScopedMountedImage& mounted_image, int64 timeoutms, bool& has_timeout, std::string& errmsg);

	static std::map<int, THREADPOOL_TICKET> mount_processes;
	static IMutex* mount_processes_mutex;

	static std::map<int, size_t> mounted_images;
	static std::set<int> locked_images;
	static IMutex* mounted_images_mutex;
};

class ScopedMountedImage
{
public:
	ScopedMountedImage()
		: backupid(0)
	{}

	~ScopedMountedImage()
	{
		if (backupid != 0)
		{
			ImageMount::decrImageMounted(backupid);
		}
	}

	void reset(int bid = 0)
	{
		if (bid == backupid)
		{
			return;
		}
		if (backupid != 0)
		{
			ImageMount::decrImageMounted(backupid);
		}
		backupid = bid;
		if (backupid != 0)
		{
			ImageMount::incrImageMounted(backupid);
		}
	}

private:
	int backupid;
};