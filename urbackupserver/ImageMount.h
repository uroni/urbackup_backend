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

	static bool mount_image(int backupid, int partition, ScopedMountedImage& mounted_image, int64 timeoutms, bool& has_timeout, std::string& errmsg);
	static std::string get_mount_path(int backupid, int partition, bool do_mount, ScopedMountedImage& mounted_image, int64 timeoutms, bool& has_timeout, std::string& errmsg);

	static bool unmount_images(int backupid);

	static void incrImageMounted(int backupid, int partition);
	static void decrImageMounted(int backupid, int partition);

	static bool lockImage(int backupid, int64 timeoutms);
	static void unlockImage(int backupid);

	static void mount_image_thread(int backupid, int partition, std::string& errmsg);

private:

	struct SMountId
	{
		SMountId(int backupid, int partition)
			: backupid(backupid), partition(partition) {}

		int backupid;
		int partition;

		bool operator<(const SMountId& other) const {
			return std::make_pair(backupid, partition)
				< std::make_pair(other.backupid, other.partition);
		}
	};

	static bool mount_image_int(int backupid, int partition, ScopedMountedImage& mounted_image, int64 timeoutms, bool& has_timeout, std::string& errmsg);

	static std::map<SMountId, THREADPOOL_TICKET> mount_processes;
	static IMutex* mount_processes_mutex;

	static std::map<SMountId, size_t> mounted_images;
	static std::set<int> locked_images;
	static IMutex* mounted_images_mutex;
};

class ScopedMountedImage
{
public:
	ScopedMountedImage()
		: backupid(0), partition(0)
	{}

	~ScopedMountedImage()
	{
		if (backupid != 0)
		{
			ImageMount::decrImageMounted(backupid, partition);
		}
	}

	void reset(int bid = 0, int part=0)
	{
		if (bid == backupid
			&& part==partition)
		{
			return;
		}
		if (backupid != 0)
		{
			ImageMount::decrImageMounted(backupid, partition);
		}
		backupid = bid;
		partition = part;
		if (backupid != 0)
		{
			ImageMount::incrImageMounted(backupid, partition);
		}
	}

private:
	int backupid;
	int partition;
};