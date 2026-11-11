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

#include <atomic>
#include <chrono>

/**
* Total time (microseconds) a thread spent inside one kind of blocking call,
* e.g. waiting for data from the network or writing to disk.
* Written by one worker thread, read by the status loop. Used to tell
* who is waiting on whom ("waiting for server storage", "waiting for network").
*/
class BlameCounter
{
public:
	BlameCounter()
		: us(0) {}

	void add(long long d)
	{
		us.fetch_add(d, std::memory_order_relaxed);
	}

	long long get() const
	{
		return us.load(std::memory_order_relaxed);
	}

	static long long nowUs()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

private:
	BlameCounter(const BlameCounter&);
	BlameCounter& operator=(const BlameCounter&);

	std::atomic<long long> us;
};

class ScopedBlameTimer
{
public:
	ScopedBlameTimer(BlameCounter& counter)
		: counter(counter), start(BlameCounter::nowUs()) {}

	~ScopedBlameTimer()
	{
		counter.add(BlameCounter::nowUs() - start);
	}

private:
	BlameCounter& counter;
	long long start;
};
