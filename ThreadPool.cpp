/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "Interface/Thread.h"

#include "ThreadPool.h"
#include "Server.h"

CPoolThread::CPoolThread(CThreadPool *pMgr)
{
	mgr=pMgr;
	dexit=false;
}

void CPoolThread::operator()(void)
{
	THREAD_ID tid = Server->getThreadID();
	THREADPOOL_TICKET ticket;
	IThread *tr=mgr->getRunnable(&ticket, false);
	if(tr!=NULL)
	  (*tr)();
	while(dexit==false)
	{
		IThread *tr=mgr->getRunnable(&ticket, true);
		if(tr!=NULL)
		{
		  (*tr)();
		  Server->clearDatabases(tid);
		}
	}
	mgr->Remove(this);
	delete this;
}

void CPoolThread::shutdown(void)
{
	dexit=true;
}

IThread * CThreadPool::getRunnable(THREADPOOL_TICKET *todel, bool del)
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
	while(ret==NULL && dexit==false)
	{
		if( toexecute.size()==0)
			cond->wait(&lock);
		else
		{
			ret=toexecute[0].first;
			*todel=toexecute[0].second;
			toexecute.erase( toexecute.begin() );
		}
	}
	return ret;
}

void CThreadPool::Remove(CPoolThread *pt)
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<threads.size();++i)
	{
		if( threads[i]==pt )
		{
			threads.erase( threads.begin()+i);
			return;
		}
	}
}

CThreadPool::CThreadPool()
{
	nRunning=0;
	nThreads=0;
	currticket=0;
	dexit=false;
	
	mutex=Server->createMutex();
	cond=Server->createCondition();
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

void CThreadPool::waitFor(std::vector<THREADPOOL_TICKET> tickets)
{
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

	while(true)
	{
		bool r=false;
		for(size_t i=0;i<tickets.size();++i)
		{
			if( isRunningInt(tickets[i])==true )
			{
				r=true;
				break;
			}
		}

		if( r==false )
			break;

		cond->wait(&lock);
	}
	
	Server->destroy(cond);
}

THREADPOOL_TICKET CThreadPool::execute(IThread *runnable)
{
	IScopedLock lock(mutex);
	if( nThreads-nRunning==0 )
	{
		CPoolThread *nt=new CPoolThread(this);
		Server->createThread(nt);
		++nThreads;
		threads.push_back(nt);
	}

	toexecute.push_back(std::pair<IThread*, THREADPOOL_TICKET>(runnable, ++currticket) );
	running.insert(std::pair<THREADPOOL_TICKET, ICondition*>(currticket, (ICondition*)NULL) );
	++nRunning;
	cond->notify_one();
	return currticket;
}

void CThreadPool::executeWait(IThread *runnable)
{
	THREADPOOL_TICKET ticket=execute(runnable);
	waitFor(ticket);
}

void CThreadPool::waitFor(THREADPOOL_TICKET ticket)
{
	std::vector<THREADPOOL_TICKET> t;
	t.push_back(ticket);
	waitFor(t);
}

