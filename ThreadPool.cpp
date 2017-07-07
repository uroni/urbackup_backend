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

#include "Interface/Thread.h"

#include "ThreadPool.h"
#include "Server.h"
#include "stringtools.h"
#include <math.h>
#include <assert.h>

#if defined(_WIN32) && defined(_DEBUG)
#include <Windows.h>
#include <assert.h>
#elif defined(__linux__)
void assert_process_priority();
#endif

namespace
{
	void checkThreadPriority()
	{
#if defined(_WIN32) && defined(_DEBUG)
		int thread_prio = GetThreadPriority(GetCurrentThread());
		assert(thread_prio== THREAD_PRIORITY_ERROR_RETURN 
			|| thread_prio == THREAD_PRIORITY_NORMAL);
#elif defined(__linux__)
		assert_process_priority();
#endif
	}
}

CPoolThread::CPoolThread(CThreadPool *pMgr)
{
	mgr=pMgr;
	dexit=false;
}

void CPoolThread::operator()(void)
{
	checkThreadPriority();

	THREAD_ID tid = Server->getThreadID();
	THREADPOOL_TICKET ticket;
	bool stop=false;
	std::string name;
	IThread *tr=mgr->getRunnable(&ticket, false, stop, name);
	if(tr!=NULL)
	{
		if (!name.empty())
		{
			Server->setCurrentThreadName(name);
		}
		else
		{
			Server->setCurrentThreadName("unnamed");
		}
		(*tr)();
		checkThreadPriority();
		Server->clearDatabases(tid);
	}

	if(!stop)
	{
		while(!dexit)
		{
			stop=false;
			std::string name;
			IThread *tr=mgr->getRunnable(&ticket, true, stop, name);
			if(tr!=NULL)
			{
				if (!name.empty())
				{
					Server->setCurrentThreadName(name);
				}
				else
				{
					Server->setCurrentThreadName("unnamed");
				}
				(*tr)();
				checkThreadPriority();
				Server->clearDatabases(tid);
			}
			else if(stop)
			{
				break;
			}
		}
	}
	Server->destroyDatabases(tid);
	mgr->Remove(this, !stop);
	delete this;
}

void CPoolThread::shutdown(void)
{
	dexit=true;
}

IThread * CThreadPool::getRunnable(THREADPOOL_TICKET *todel, bool del, bool& stop, std::string& name)
{
	IScopedLock lock(mutex);

	if( del==true )
	{
		--nRunning;
		std::map<THREADPOOL_TICKET, ICondition *>::iterator it=running.find(*todel);
		if( it!=running.end() )
		{
			if( it->second!=NULL )
				it->second->notify_all();

			running.erase(it);
		}
	}

	IThread *ret=NULL;
	while(ret==NULL && !dexit)
	{
		if( toexecute.size()==0)
		{
			if(nThreads>=nRunning
				&& nThreads-nRunning>max_waiting_threads)
			{
				--nThreads;
				stop=true;
				return NULL;
			}
			Server->setCurrentThreadName(idle_name);
			cond->wait(&lock);
		}
		else
		{
			ret=toexecute[0].runnable;
			*todel=toexecute[0].ticket;
			name = toexecute[0].name;
			toexecute.erase( toexecute.begin() );
		}
	}
	return ret;
}

void CThreadPool::Remove(CPoolThread *pt, bool decr)
{
	IScopedLock lock(mutex);
	assert(!decr || nThreads > 0);
	for(size_t i=0;i<threads.size();++i)
	{
		if( threads[i]==pt )
		{
			threads.erase( threads.begin()+i);
			if (decr)
			{
				--nThreads;
			}
			return;
		}
	}
	assert(false);
}

CThreadPool::CThreadPool(size_t max_threads, 
	size_t max_waiting_threads, std::string idle_name)
	: max_threads(max_threads), max_waiting_threads(max_waiting_threads), idle_name(idle_name),
	nRunning(0), nThreads(0), currticket(0), dexit(false), mutex(Server->createMutex()),
	cond(Server->createCondition())
{

}

CThreadPool::~CThreadPool()
{	
	delete mutex;
	delete cond;
}

void CThreadPool::Shutdown(void)
{
	bool do_leak_check=(Server->getServerParameter("leak_check")=="true");
	IScopedLock lock(mutex);
	for(size_t i=0;i<threads.size();++i)
	{
		threads[i]->shutdown();
	}
	dexit=true;

	unsigned int max=0;
	while(threads.size()>0 )
	{
		lock.relock(NULL);
		cond->notify_all();
		Server->wait(100);
		lock.relock(mutex);

		//wait for max 300 msec
		if( (!do_leak_check && max>=3) || (do_leak_check && max>=30) )
		{
			Server->Log("Maximum wait time for thread pool exceeded. Shutting down the hard way", LL_ERROR);
			break;
		}
		++max;
	}
}

bool CThreadPool::isRunningInt(THREADPOOL_TICKET ticket)
{
	std::map<THREADPOOL_TICKET, ICondition*>::iterator it=running.find(ticket);
	if( it!=running.end() )
		return true;
	else
		return false;
}

bool CThreadPool::isRunning(THREADPOOL_TICKET ticket)
{
	IScopedLock lock(mutex);
	return isRunningInt(ticket);
}

bool CThreadPool::waitFor(std::vector<THREADPOOL_TICKET> tickets, int timems)
{
	int64 starttime;
	if(timems>=0)
	{
		starttime = Server->getTimeMS();
	}

	IScopedLock lock(mutex);
	ICondition *cond=Server->createCondition();

	for( size_t i=0;i<tickets.size();++i)
	{
		std::map<THREADPOOL_TICKET, ICondition*>::iterator it=running.find(tickets[i]);
		if( it!=running.end() )
		{
			it->second=cond;
		}
	}

	bool ret=false;

	while(true)
	{
		bool r=false;
		for(size_t i=0;i<tickets.size();++i)
		{
			if( isRunningInt(tickets[i]) )
			{
				r=true;
				break;
			}
		}

		if( !r )
		{
			ret = true;
			break;
		}

		int left = timems;
		if (timems >= 0)
		{
			int64 ctime = Server->getTimeMS();
			if (ctime - starttime>=timems)
			{
				break;
			}
			else
			{
				left = timems - static_cast<int>(ctime - starttime);
			}
		}

		cond->wait(&lock, left);
	}

	for( size_t i=0;i<tickets.size();++i)
	{
		std::map<THREADPOOL_TICKET, ICondition*>::iterator it=running.find(tickets[i]);
		if( it!=running.end() )
		{
			if(it->second==cond)
			{
				it->second=NULL;
			}
		}
	}
	
	Server->destroy(cond);

	return ret;
}

THREADPOOL_TICKET CThreadPool::execute(IThread *runnable, const std::string& name)
{
	IScopedLock lock(mutex);
	size_t retries = 0;
	while(nThreads>=nRunning
		&& nThreads-nRunning==0
		&& nThreads<max_threads )
	{
		CPoolThread *nt=new CPoolThread(this);
		if (!Server->createThread(nt))
		{
			delete nt;
			lock.relock(NULL);
			unsigned int waittime = (std::min)(static_cast<unsigned int>(1000.*pow(2., static_cast<double>(++retries))), (unsigned int)30 * 60 * 1000); //30min
			if (retries>20)
			{
				waittime = (unsigned int)30 * 60 * 1000;
			}
			Server->Log("Retrying creating thread for thread pool in "+PrettyPrintTime(waittime)+"...", LL_WARNING);
			Server->wait(waittime);
			lock.relock(mutex);
		}
		else
		{
			++nThreads;
			threads.push_back(nt);
		}
	}

	++currticket;
	while (running.find(currticket) != running.end()
		|| currticket==ILLEGAL_THREADPOOL_TICKET)
	{
		++currticket;
	}

	toexecute.push_back(SNewTask(runnable, currticket, name));
	running.insert(std::pair<THREADPOOL_TICKET, ICondition*>(currticket, (ICondition*)NULL) );
	++nRunning;
	cond->notify_one();
	return currticket;
}

void CThreadPool::executeWait(IThread *runnable, const std::string& name)
{
	THREADPOOL_TICKET ticket=execute(runnable, name);
	waitFor(ticket);
}

bool CThreadPool::waitFor(THREADPOOL_TICKET ticket, int timems)
{
	std::vector<THREADPOOL_TICKET> t;
	t.push_back(ticket);
	return waitFor(t, timems);
}

