#pragma once
/*
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "../Interface/Types.h"
#include <string>
#include <vector>
#include <mutex>
#include "../urbackupcommon/BlameStats.h"

/**
* What the file hashing/linking threads of one file backup are doing right now.
* Every worker thread owns one slot; FileBackup reads all slots to show
* e.g. "Hashing foo.vhdx - 391 GB of 480 GB (81%)" in the activities.
*/
class PostTransferProgress
{
public:
	struct SItem
	{
		SItem()
			: done(0), size(0), start_ms(0) {}

		std::string phase;
		std::string name;
		int64 done;
		int64 size;
		int64 start_ms;
	};

	enum EWorker
	{
		EWorker_Hash = 0,
		EWorker_Link = 1
	};

	PostTransferProgress()
	{
		for (size_t i = 0; i < 2; ++i)
		{
			busy_us[i] = 0;
			n_workers[i] = 0;
		}
	}

	//Call from the thread that creates the worker, before the worker runs
	size_t addSlot(EWorker worker)
	{
		std::lock_guard<std::mutex> lock(mutex);
		slots.push_back(SItem());
		slot_worker.push_back(worker);
		slot_busy_start.push_back(0);
		++n_workers[worker];
		return slots.size() - 1;
	}

	void start(size_t slot, const char* phase, const std::string& name, int64 size, int64 now_ms)
	{
		std::lock_guard<std::mutex> lock(mutex);
		SItem& item = slots[slot];
		if (slot_busy_start[slot] == 0)
		{
			slot_busy_start[slot] = BlameCounter::nowUs();
		}
		item.phase = phase;
		item.name = name;
		item.done = 0;
		item.size = size;
		item.start_ms = now_ms;
	}

	void setPhase(size_t slot, const char* phase)
	{
		std::lock_guard<std::mutex> lock(mutex);
		slots[slot].phase = phase;
	}

	void setDone(size_t slot, int64 done)
	{
		std::lock_guard<std::mutex> lock(mutex);
		slots[slot].done = done;
	}

	void finish(size_t slot)
	{
		std::lock_guard<std::mutex> lock(mutex);
		SItem& item = slots[slot];
		if (slot_busy_start[slot] != 0)
		{
			busy_us[slot_worker[slot]] += BlameCounter::nowUs() - slot_busy_start[slot];
			slot_busy_start[slot] = 0;
		}
		item.phase.clear();
		item.name.clear();
		item.done = 0;
		item.size = 0;
	}

	//All slots that are currently working
	std::vector<SItem> getActive()
	{
		std::vector<SItem> ret;
		std::lock_guard<std::mutex> lock(mutex);
		for (size_t i = 0; i < slots.size(); ++i)
		{
			if (!slots[i].phase.empty())
			{
				ret.push_back(slots[i]);
			}
		}
		return ret;
	}

	//Total time (us) the hash resp. link workers spent working on files (not waiting for work), including the file in progress
	void getBusyTimes(int64& hash_busy, int64& link_busy, size_t& n_hash, size_t& n_link)
	{
		int64 busy[2];
		int64 now = BlameCounter::nowUs();
		std::lock_guard<std::mutex> lock(mutex);
		for (size_t i = 0; i < 2; ++i)
		{
			busy[i] = busy_us[i];
		}
		for (size_t i = 0; i < slots.size(); ++i)
		{
			if (slot_busy_start[i] != 0)
			{
				busy[slot_worker[i]] += now - slot_busy_start[i];
			}
		}
		hash_busy = busy[EWorker_Hash];
		link_busy = busy[EWorker_Link];
		n_hash = n_workers[EWorker_Hash];
		n_link = n_workers[EWorker_Link];
	}

	//Time the hash workers spent reading the received files (the rest of their busy time is hashing)
	BlameCounter hash_read_wait;

private:
	std::mutex mutex;
	std::vector<SItem> slots;
	std::vector<EWorker> slot_worker;
	std::vector<int64> slot_busy_start;
	int64 busy_us[2];
	size_t n_workers[2];
};

/**
* Rate limited writer to one slot, used for byte counters in tight loops
*/
class PostTransferProgressWriter
{
public:
	PostTransferProgressWriter(PostTransferProgress* progress, size_t slot, int64 interval_ms=250)
		: progress(progress), slot(slot), interval_ms(interval_ms), last_ms(0) {}

	void setDone(int64 done, int64 now_ms)
	{
		if (progress != NULL
			&& now_ms - last_ms >= interval_ms)
		{
			last_ms = now_ms;
			progress->setDone(slot, done);
		}
	}

private:
	PostTransferProgress* progress;
	size_t slot;
	int64 interval_ms;
	int64 last_ms;
};
