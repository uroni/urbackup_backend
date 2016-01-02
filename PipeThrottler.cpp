/*************************************************************************
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

#include "PipeThrottler.h"
#include "Server.h"
#include "Interface/Mutex.h"
#include "stringtools.h"

#define DLOG(x) //x

PipeThrottler::PipeThrottler(size_t bps,
	IPipeThrottlerUpdater* updater)
	: throttle_bps(bps), curr_bytes(0),
	  lastresettime(0), updater(updater)
{
	mutex=Server->createMutex();
	lastupdatetime=Server->getTimeMS();
	if(updater!=NULL)
	{
		update_time_interval = updater->getUpdateIntervalMs();
	}
	else
	{
		update_time_interval = -1;
	}
}

PipeThrottler::~PipeThrottler(void)
{
	Server->destroy(mutex);
}

bool PipeThrottler::addBytes(size_t new_bytes, bool wait)
{
	IScopedLock lock(mutex);

	if(throttle_bps==0) return true;

	int64 ctime=Server->getTimeMS();

	if(updater.get() && update_time_interval>=0 &&
		ctime-lastupdatetime>update_time_interval)
	{
		throttle_bps = updater->getThrottleLimit();
		lastupdatetime = ctime;
		if(throttle_bps==0) return true;
	}

	if(ctime-lastresettime>1000)
	{
		lastresettime=ctime;
		curr_bytes=0;
	}

	curr_bytes+=new_bytes;

	int64 passed_time=ctime-lastresettime;

	size_t maxRateTime=(size_t)(((_i64)curr_bytes*1000)/throttle_bps);
	if(passed_time>0)
	{
		size_t bps=(size_t)(((_i64)curr_bytes*1000)/passed_time);
		if(bps>throttle_bps)
		{		
			unsigned int sleepTime=(unsigned int)(maxRateTime-passed_time);

			if(sleepTime>0)
			{
				if(wait)
				{
					DLOG(Server->Log("Throttler: Sleeping for " + convert(sleepTime)+ "ms", LL_DEBUG));
					Server->wait(sleepTime);

					if(Server->getTimeMS()-lastresettime>1000)
					{
						curr_bytes=0;
						lastresettime=Server->getTimeMS();
					}
				}

				return false;
			}
		}
	}
	else if(curr_bytes>=throttle_bps)
	{
		if(wait)
		{
			DLOG(Server->Log("Throttler: Sleeping for " + convert(maxRateTime)+ "ms", LL_DEBUG));
			Server->wait(static_cast<unsigned int>(maxRateTime));
		}
		curr_bytes=0;
		lastresettime=Server->getTimeMS();
		return false;
	}

	return true;
}

void PipeThrottler::changeThrottleLimit(size_t bps)
{
	IScopedLock lock(mutex);

	throttle_bps=bps;
}

void PipeThrottler::changeThrottleUpdater(IPipeThrottlerUpdater* new_updater)
{
	IScopedLock lock(mutex);

	updater.reset(new_updater);
}
