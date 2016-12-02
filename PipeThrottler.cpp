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
	bool percent_max,
	IPipeThrottlerUpdater* updater)
	: throttle_bps(bps), percent_max(percent_max), curr_bytes(0),
	lastresettime(0), updater(updater),
	throttle_state(ThrottleState_Probe),
	lastprobetime(0), probe_bps(0),
	throttle_percent(bps), last_probe_result(0),
	probe_interval(10 * 60 * 1000)
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
		size_t new_throttle_bps = updater->getThrottleLimit(percent_max);

		if (percent_max)
		{
			throttle_percent = new_throttle_bps;
			if (throttle_percent == 0)
			{
				throttle_bps = 0;
			}
		}
		else
		{
			throttle_bps = new_throttle_bps;
		}

		lastupdatetime = ctime;
		if(throttle_bps==0) return true;
	}

	if (percent_max &&
		throttle_state == ThrottleState_Throttle
		&& ctime - lastprobetime > static_cast<int64>(probe_interval))
	{
		throttle_state = ThrottleState_Probe;
		probe_bps = 0;
		Server->Log("PROBE Starting probing for max speed");
	}

	if(ctime-lastresettime>1000)
	{
		if (percent_max && throttle_state == ThrottleState_Probe)
		{
			int64 passed_time = ctime - lastresettime;
			float bps = (curr_bytes * 1000.f) / passed_time;
			if (bps > 10 * 1024)
			{
				if (probe_bps == 0)
				{
					probe_bps = bps;
				}
				else
				{
					float new_probe_bps = 0.8f*probe_bps + 0.2f*bps;
					float pdiff = new_probe_bps / probe_bps;
					if (pdiff > 0.99f && pdiff < 1.01f)
					{
						throttle_bps = static_cast<size_t>((static_cast<float>(throttle_percent) / 100)*new_probe_bps + 0.5f);
						Server->Log("PROBE Probing finished at current speed " + PrettyPrintSpeed(static_cast<size_t>(bps + 0.5f))
							+ " last avg " + PrettyPrintSpeed(static_cast<size_t>(probe_bps + 0.5f))
							+ " curr avg " + PrettyPrintSpeed(static_cast<size_t>(new_probe_bps + 0.5f))
							+ " pdiff " + convert(pdiff)
							+ " throttling "+convert(throttle_percent)+"% to "+PrettyPrintSpeed(throttle_bps), LL_DEBUG);
						lastprobetime = ctime;
						throttle_state = ThrottleState_Throttle;
						

						if (last_probe_result != 0)
						{
							pdiff = last_probe_result / new_probe_bps;
							Server->Log("PROBE Curr probe result " + PrettyPrintSpeed(static_cast<size_t>(new_probe_bps + 0.5f))
								+ " last probe result " + PrettyPrintSpeed(static_cast<size_t>(last_probe_result + 0.5f))
								+ " pdiff " + convert(pdiff), LL_DEBUG);
							if (pdiff > 0.95f && pdiff < 1.05f
								&& probe_interval < 60*60*1000 )
							{
								probe_interval += 10 * 60 * 1000;
								Server->Log("PROBE New probe interval: " + PrettyPrintTime(probe_interval), LL_DEBUG);
							}
						}
						last_probe_result = new_probe_bps;
					}
					else
					{
						Server->Log("PROBE Probing at current speed " + PrettyPrintSpeed(static_cast<size_t>(bps + 0.5f))
							+ " last avg " + PrettyPrintSpeed(static_cast<size_t>(probe_bps + 0.5f))
							+ " curr avg " + PrettyPrintSpeed(static_cast<size_t>(new_probe_bps + 0.5f))
							+ " pdiff " + convert(pdiff), LL_DEBUG);
					}
					probe_bps = new_probe_bps;
				}
			}
			else
			{
				Server->Log("PROBE Discarding current speed of " + PrettyPrintSpeed(static_cast<size_t>(bps + 0.5f)) +
					" during probing for max speed because it is too low", LL_DEBUG);
			}
		}

		lastresettime=ctime;
		curr_bytes=0;
	}

	curr_bytes += new_bytes;

	if (percent_max && throttle_state == ThrottleState_Probe)
	{
		return true;
	}

	int64 passed_time=ctime-lastresettime;

	size_t maxRateTime=(size_t)(((_i64)curr_bytes*1000)/throttle_bps);
	if(passed_time>0)
	{
		size_t bps=(size_t)(((_i64)curr_bytes*1000)/passed_time);

		if (throttle_state == ThrottleState_Throttle
			&& curr_bytes > static_cast<size_t>(1.1f*last_probe_result+0.5f))
		{
			Server->Log("PROBE Current speed per second at " + PrettyPrintSpeed(curr_bytes) +
				" 10% higher than max speed during probe at " + PrettyPrintSpeed(static_cast<size_t>(last_probe_result + 0.5f)) +
				". Reprobing for max speed.", LL_DEBUG);
			throttle_state = ThrottleState_Probe;
			probe_bps = 0;
			return true;
		}

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
		if (throttle_state == ThrottleState_Throttle
			&& curr_bytes > static_cast<size_t>(1.1f*last_probe_result + 0.5f))
		{
			Server->Log("PROBE 2 Current speed per second at " + PrettyPrintSpeed(curr_bytes) +
				" significantly higher than max speed during probe at " + PrettyPrintSpeed(static_cast<size_t>(last_probe_result + 0.5f)) +
				". Reprobing for max speed.", LL_DEBUG);
			throttle_state = ThrottleState_Probe;
			probe_bps = 0;
			return true;
		}

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

void PipeThrottler::changeThrottleLimit(size_t bps, bool p_percent_max)
{
	IScopedLock lock(mutex);

	percent_max = p_percent_max;

	if (percent_max)
	{
		throttle_percent = bps;
	}
	else
	{
		throttle_bps = bps;
	}
}

void PipeThrottler::changeThrottleUpdater(IPipeThrottlerUpdater* new_updater)
{
	IScopedLock lock(mutex);

	updater.reset(new_updater);
}
