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

#include "vld.h"
#include <stdlib.h>
#include "SessionMgr.h"
#include "Server.h"
#include "stringtools.h"
#include <assert.h>

CSessionMgr::CSessionMgr(void)
{
	sess_mutex=Server->createMutex();
	sess_cond = Server->createCondition();
	
	wait_cond=Server->createCondition();
	wait_mutex=Server->createMutex();
	
	stop_mutex=Server->createMutex();
	stop_cond=Server->createCondition();
	
	SESSIONID_LEN=30;//Server->Settings->getValue("SESSIONID_LEN",15);
	SESSION_TIMEOUT_S=1800;//Server->Settings->getValue("SESSION_TIMEOUT_S",600);

	for(unsigned char i=48;i<58;++i)
		Pool.push_back(i);
	for(unsigned char i=65;i<91;++i)
		Pool.push_back(i);
	for(unsigned char i=97;i<122;++i)
		Pool.push_back(i);
		
	run=false;	
}

CSessionMgr::~CSessionMgr()
{
	{
	  IScopedLock lock( sess_mutex );
	  Server->Log("removing sessions...");
	  if(!mSessions.empty() )
	  {
		  std::vector<std::string> sesids;
		  for(std::map<std::string, SUser*>::iterator i=mSessions.begin();i!=mSessions.end();++i)
		  {
			  sesids.push_back(i->first);
		  }

		  lock.relock(NULL);

		  for(size_t i=0;i<sesids.size();++i)
		  {
			  RemoveSession(sesids[i]);
		  }
	  }
	  Server->Log("done.");
	}
  if(run)
  {
    IScopedLock slock(stop_mutex);
    Server->Log("waiting for sessionmgr...");
    run=false;
    wait_cond->notify_all();
    stop_cond->wait(&slock);
    Server->Log("done.");
  }
  Server->destroy(sess_mutex);
  Server->destroy(sess_cond);
  Server->destroy(wait_mutex);
  Server->destroy(stop_mutex);
  
  Server->destroy(wait_cond);
  Server->destroy(stop_cond);
}

void CSessionMgr::startTimeoutSessionThread()
{
    run=true;
    Server->createThread(this, "session timeouts");
}

std::string CSessionMgr::GenerateSessionIDWithUser(const std::string &pUsername, const std::string &pIdentData, bool update_user)
{
	std::string ret;
	ret.resize(SESSIONID_LEN);
	std::vector<unsigned int> rnd_n=Server->getSecureRandomNumbers(SESSIONID_LEN);
	for(int i=0;i<SESSIONID_LEN;++i)
		ret[i]+=Pool[rnd_n[i]%Pool.size()];

	IScopedLock lock( sess_mutex );
	if(update_user)
	{
		std::vector<std::string> to_remove;
		for(std::map<std::string, SUser*>::iterator i=mSessions.begin();i!=mSessions.end();++i)
		{
			if( i->second->username==pUsername )
			{
				to_remove.push_back(i->first);
			}
		}
		
		lock.relock(NULL);

		for(size_t i=0;i<to_remove.size();++i)
		{
			RemoveSession(to_remove[i]);
		}

		lock.relock(sess_mutex);
	}


	SUser *user=new SUser;
	user->username=pUsername;
	user->session=ret;
	user->ident_data=pIdentData;
	user->id=-1;
	user->lastused=Server->getTimeMS();
	user->waitlock = 0;
	user->refcount = 0;
	mSessions.insert(std::pair<std::string, SUser*>(ret, user) );

	return ret;
}

SUser *CSessionMgr::getUser(const std::string &pSID, const std::string &pIdentData, bool update)
{
	IScopedLock lock( sess_mutex );
	std::map<std::string, SUser*>::iterator i=mSessions.find(pSID);
	if( i!=mSessions.end() )
	{
		if( i->second->ident_data!=pIdentData )
			return NULL;

		SUser* user = i->second;
		++user->refcount;

		while (user->waitlock > 0)
		{
			sess_cond->wait(&lock);
		}

		++user->waitlock;

		if( update )
			user->lastused=Server->getTimeMS();
		return user;
	}
	else
		return NULL;
}

void CSessionMgr::releaseUser(SUser *user)
{
	if( user!=NULL )
	{
		IScopedLock lock(sess_mutex);
		assert(user->refcount > 0);
		--user->refcount;
		assert(user->waitlock > 0);
		--user->waitlock;
		if (user->refcount > 0)
		{
			sess_cond->notify_all();
		}
	}
}

void CSessionMgr::unlockUser(SUser * user)
{
	if (user != NULL)
	{
		IScopedLock lock(sess_mutex);
		assert(user->waitlock > 0);
		--user->waitlock;
		if (user->refcount > 1)
		{
			sess_cond->notify_all();
		}
	}
}

void CSessionMgr::lockUser(SUser *user)
{
	if( user!=NULL )
	{
		IScopedLock lock(sess_mutex);
		while (user->waitlock > 0)
		{
			sess_cond->wait(&lock);
		}

		++user->waitlock;
	}
}

bool CSessionMgr::RemoveSession(const std::string &pSID)
{
	IScopedLock lock( sess_mutex );
	std::map<std::string, SUser*>::iterator i=mSessions.find(pSID);
	if( i!=mSessions.end() )
	{
		SUser* user=i->second;

		if (user->refcount>0)
		{
			return false;
		}

		assert(user->waitlock == 0);

		mSessions.erase(i);

		lock.relock(NULL);

		for(std::map<std::string, IObject* >::iterator iter=user->mCustom.begin();
			iter!=user->mCustom.end();++iter)
		{
			iter->second->Remove();
		}

		delete user;
		
		return true;
	}
	else
		return false;
}

unsigned int CSessionMgr::TimeoutSessions(void)
{
	if(Server!=NULL)
		Server->Log("Looking for old Sessions... "+convert(mSessions.size())+" sessions", LL_INFO);
	unsigned int ret=0;
	IScopedLock lock( sess_mutex );
	int64 ttime=Server->getTimeMS();
	std::vector<std::string> to_timeout;
	for(std::map<std::string, SUser*>::iterator i=mSessions.begin();i!=mSessions.end();++i)
	{
		int64 diff=ttime-i->second->lastused;
		if( diff > (unsigned int)(SESSION_TIMEOUT_S)*1000 )
		{
			Server->Log("Session timeout: Session "+i->first, LL_INFO);
			to_timeout.push_back(i->first);
		}
		else
		{
		    ret=static_cast<unsigned int>((std::max)(diff, static_cast<int64>(ret)));
		}
	}

	lock.relock(NULL);

	for(size_t i=0;i<to_timeout.size();++i)
	{
		RemoveSession(to_timeout[i]);
	}

	return (unsigned int)((SESSION_TIMEOUT_S)*1000)-ret+1000;
}

void CSessionMgr::operator()(void)
{
    {
		IScopedLock lock( wait_mutex );
		while(run)
		{	
			unsigned int wtime=TimeoutSessions();
			wait_cond->wait(&lock, wtime);
		}
    }
    IScopedLock slock(stop_mutex);
    stop_cond->notify_one();
}
