/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

CSessionMgr::CSessionMgr(void)
{
	sess_mutex=Server->createMutex();
	
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
		  std::vector<std::wstring> sesids;
		  for(std::map<std::wstring, SUser*>::iterator i=mSessions.begin();i!=mSessions.end();++i)
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
  Server->destroy(wait_mutex);
  Server->destroy(stop_mutex);
  
  Server->destroy(wait_cond);
  Server->destroy(stop_cond);
}

void CSessionMgr::startTimeoutSessionThread()
{
    run=true;
    Server->createThread(this);
}

std::wstring CSessionMgr::GenerateSessionIDWithUser(const std::wstring &pUsername, const std::wstring &pIdentData, bool update_user)
{
	std::wstring ret;
	ret.resize(SESSIONID_LEN);
	std::vector<unsigned int> rnd_n=Server->getSecureRandomNumbers(SESSIONID_LEN);
	for(int i=0;i<SESSIONID_LEN;++i)
		ret[i]+=Pool[rnd_n[i]%Pool.size()];

	IScopedLock lock( sess_mutex );
	if(update_user)
	{
		std::vector<std::wstring> to_remove;
		for(std::map<std::wstring, SUser*>::iterator i=mSessions.begin();i!=mSessions.end();++i)
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
	user->mutex=Server->createMutex();
	user->lock=NULL;
	user->ident_data=pIdentData;
	user->id=-1;
	user->lastused=Server->getTimeMS();
	mSessions.insert(std::pair<std::wstring, SUser*>(ret, user) );

	return ret;
}

SUser *CSessionMgr::getUser(const std::wstring &pSID, const std::wstring &pIdentData, bool update)
{
	IScopedLock lock( sess_mutex );
	std::map<std::wstring, SUser*>::iterator i=mSessions.find(pSID);
	if( i!=mSessions.end() )
	{
		if( i->second->ident_data!=pIdentData )
			return NULL;

		lock.relock(NULL);

		ILock *lock=((IMutex*)i->second->mutex)->Lock2();
		i->second->lock=lock;
		if( update==true )
			i->second->lastused=Server->getTimeMS();
		return i->second;
	}
	else
		return NULL;
}

void CSessionMgr::releaseUser(SUser *user)
{
	if( user!=NULL )
	{
		((ILock*)user->lock)->Remove();
	}
}

void CSessionMgr::lockUser(SUser *user)
{
	if( user!=NULL )
	{
		ILock *lock=((IMutex*)user->mutex)->Lock2();
		user->lock=lock;
	}
}

bool CSessionMgr::RemoveSession(const std::wstring &pSID)
{
	IScopedLock lock( sess_mutex );
	std::map<std::wstring, SUser*>::iterator i=mSessions.find(pSID);
	if( i!=mSessions.end() )
	{
		SUser* user=i->second;
		mSessions.erase(i);

		lock.relock(NULL);

		IScopedLock *lock=new IScopedLock(((IMutex*)user->mutex));
		delete lock;

		Server->destroy((IMutex*)user->mutex);

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
		Server->Log("Looking for old Sessions... "+nconvert(mSessions.size())+" sessions", LL_INFO);
	unsigned int ret=0;
	IScopedLock lock( sess_mutex );
	int64 ttime=Server->getTimeMS();
	std::vector<std::wstring> to_timeout;
	for(std::map<std::wstring, SUser*>::iterator i=mSessions.begin();i!=mSessions.end();++i)
	{
		int64 diff=ttime-i->second->lastused;
		if( diff > (unsigned int)(SESSION_TIMEOUT_S)*1000 )
		{
			Server->Log(L"Session timeout: Session "+i->first, LL_INFO);
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
