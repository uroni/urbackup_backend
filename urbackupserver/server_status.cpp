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

#ifndef CLIENT_ONLY

#include "server_status.h"
#include "../Interface/Server.h"
#include "../Interface/Pipe.h"
#include "action_header.h"
#include <time.h>
#include <algorithm>

IMutex *ServerStatus::mutex=NULL;
std::map<std::string, SStatus> ServerStatus::status;
int64 ServerStatus::last_status_update;
size_t ServerStatus::curr_process_id = 0;


int ServerStatus::server_nospc_stalled=0;
bool ServerStatus::server_nospc_fatal=false;

const unsigned int inactive_time_const=30*60*1000;

void ServerStatus::init_mutex(void)
{
	mutex=Server->createMutex();
	last_status_update=Server->getTimeMS();
}

void ServerStatus::destroy_mutex(void)
{
	Server->destroy(mutex);
}

void ServerStatus::updateActive(void)
{
	IScopedLock lock(mutex);
	last_status_update=Server->getTimeMS();
}

void ServerStatus::setOnline(const std::string &clientname, bool bonline)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	if(bonline)
	{
		*s=SStatus();
	}
	s->online=bonline;
	s->client=clientname;
	s->has_status=true;
	s->r_online=bonline;
	if(bonline)
	{
		last_status_update=Server->getTimeMS();
	}
}

void ServerStatus::setROnline(const std::string &clientname, bool bonline)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->r_online=bonline;
	if(bonline)
	{
		last_status_update=Server->getTimeMS();
	}
}

void ServerStatus::setIP(const std::string &clientname, unsigned int ip)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->ip_addr=ip;
}

void ServerStatus::setStatusError(const std::string &clientname, SStatusError se)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->status_error=se;
}

void ServerStatus::setCommPipe(const std::string &clientname, IPipe *p)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->comm_pipe=p;
}

void ServerStatus::stopProcess(const std::string &clientname, size_t id, bool b)
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);
	if(proc!=NULL)
	{
		proc->stop=true;
	}
}

bool ServerStatus::isProcessStopped(const std::string &clientname, size_t id)
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);
	if(proc!=NULL)
	{
		return proc->stop;
	}
	return false;
}

std::vector<SStatus> ServerStatus::getStatus(void)
{
	IScopedLock lock(mutex);
	std::vector<SStatus> ret;
	for(std::map<std::string, SStatus>::iterator it=status.begin();it!=status.end();++it)
	{
		ret.push_back(it->second);
	}
	return ret;
}

SStatus ServerStatus::getStatus(const std::string &clientname)
{
	IScopedLock lock(mutex);
	std::map<std::string, SStatus>::iterator iter=status.find(clientname);
	if(iter!=status.end())
		return iter->second;
	else
		return SStatus();
}

bool ServerStatus::isActive(void)
{
	IScopedLock lock(mutex);
	if( Server->getTimeMS()-last_status_update>inactive_time_const)
	{
		return false;
	}
	else
	{
		return true;
	}
}

void ServerStatus::incrementServerNospcStalled(int add)
{
	IScopedLock lock(mutex);
	server_nospc_stalled+=add;
}

void ServerStatus::setServerNospcFatal(bool b)
{
	IScopedLock lock(mutex);
	server_nospc_fatal=b;
}

int ServerStatus::getServerNospcStalled(void)
{
	IScopedLock lock(mutex);
	return server_nospc_stalled;
}

bool ServerStatus::getServerNospcFatal(void)
{
	IScopedLock lock(mutex);
	return server_nospc_fatal;
}

void ServerStatus::setClientVersionString(const std::string &clientname, const std::string& client_version_string)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->client_version_string=client_version_string;
}

void ServerStatus::setOSVersionString(const std::string &clientname, const std::string& os_version_string)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->os_version_string=os_version_string;
}

bool ServerStatus::sendToCommPipe( const std::string &clientname, const std::string& msg )
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	if(s->comm_pipe==NULL)
		return false;

	s->comm_pipe->Write(msg);

	return true;
}

size_t ServerStatus::startProcess( const std::string &clientname, SStatusAction action, const std::string& details)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];

	SProcess new_proc(curr_process_id++, action, details);
	s->processes.push_back(new_proc);

	return new_proc.id;
}

bool ServerStatus::stopProcess( const std::string &clientname, size_t id )
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];

	std::vector<SProcess>::iterator it = std::find(s->processes.begin(), s->processes.end(), SProcess(id, sa_none, std::string()));

	if(it!=s->processes.end())
	{
		s->processes.erase(it);
		return true;
	}
	else
	{
		return false;
	}
}

bool ServerStatus::changeProcess(const std::string & clientname, size_t id, SStatusAction action)
{
	IScopedLock lock(mutex);
	SStatus *s = &status[clientname];

	std::vector<SProcess>::iterator it = std::find(s->processes.begin(), s->processes.end(), SProcess(id, sa_none, std::string()));

	if (it != s->processes.end())
	{
		it->action = action;
		return true;
	}
	else
	{
		return false;
	}
}

SProcess* ServerStatus::getProcessInt( const std::string &clientname, size_t id )
{
	SStatus *s=&status[clientname];

	std::vector<SProcess>::iterator it = std::find(s->processes.begin(), s->processes.end(), SProcess(id, sa_none, std::string()));

	if(it!=s->processes.end())
	{
		return &(*it);
	}
	else
	{
		return NULL;
	}
}

void ServerStatus::setProcessQueuesize( const std::string &clientname, size_t id, unsigned int prepare_hashqueuesize, unsigned int hashqueuesize )
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);

	if(proc!=NULL)
	{
		proc->prepare_hashqueuesize = prepare_hashqueuesize;
		proc->hashqueuesize = hashqueuesize;
	}
}

void ServerStatus::setProcessStarttime( const std::string &clientname, size_t id, int64 starttime )
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);

	if(proc!=NULL)
	{
		proc->starttime = starttime;
	}
}

void ServerStatus::setProcessEta( const std::string &clientname, size_t id, int64 eta_ms, int64 eta_set_time )
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);

	if(proc!=NULL)
	{
		proc->eta_ms = eta_ms;
		proc->eta_set_time = eta_set_time;
	}
}

void ServerStatus::setProcessEta( const std::string &clientname, size_t id, int64 eta_ms )
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);

	if(proc!=NULL)
	{
		proc->eta_ms = eta_ms;
	}
}

bool ServerStatus::removeStatus( const std::string &clientname )
{
	IScopedLock lock(mutex);

	std::map<std::string, SStatus>::iterator it=status.find(clientname);

	if(it!=status.end())
	{
		status.erase(it);
		return true;
	}
	else
	{
		return false;
	}
}

void ServerStatus::setProcessPcDone( const std::string &clientname, size_t id, int pcdone )
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);

	if(proc!=NULL)
	{
		proc->pcdone = pcdone;
	}
}

SProcess ServerStatus::getProcess( const std::string &clientname, size_t id )
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);
	if(proc!=NULL)
	{
		return *proc;
	}
	else
	{
		return SProcess(0, sa_none, std::string());
	}
}

void ServerStatus::setProcessEtaSetTime( const std::string &clientname, size_t id, int64 eta_set_time )
{
	IScopedLock lock(mutex);
	SProcess* proc = getProcessInt(clientname, id);

	if(proc!=NULL)
	{
		proc->eta_set_time = eta_set_time;
	}
}

void ServerStatus::setClientId( const std::string &clientname, int clientid)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->clientid = clientid;
}

void ServerStatus::addRunningJob( const std::string &clientname )
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->running_jobs+=1;
}

void ServerStatus::subRunningJob( const std::string &clientname )
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->running_jobs-=1;
}

int ServerStatus::numRunningJobs( const std::string &clientname )
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	return s->running_jobs;
}

void ServerStatus::setRestore( const std::string &clientname, ERestore restore )
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->restore = restore;
}

bool ServerStatus::canRestore( const std::string &clientname, bool& server_confirms)
{
	IScopedLock lock(mutex);
	std::map<std::string, SStatus>::iterator it=status.find(clientname);
	if(it==status.end())
	{
		return false;
	}
	SStatus* s=&it->second;
	server_confirms = s->restore==ERestore_server_confirms;
	return s->online && s->r_online && s->restore!=ERestore_disabled;
}

ACTION_IMPL(server_status)
{
#ifndef _DEBUG
	Server->Write(tid, "Forbidden");
#else
	ITemplate *tmpl=Server->createTemplate("urbackup/status.htm");

	std::vector<SStatus> status=ServerStatus::getStatus();

	tmpl->getTable("CLIENTS");
	
	for(size_t i=0;i<status.size();++i)
	{
		/*ITable *tab=tmpl->getTable(L"CLIENTS."+convert(i));
		tab->addString(L"NAME", status[i].client );
		if(status[i].has_status)
		{
			tab->addString(L"PCDONE", convert(status[i].pcdone) );
			tab->addString(L"HASHQUEUESIZE", convert(status[i].hashqueuesize) );
			tab->addString(L"HASHQUEUE_PREPARE", convert(status[i].prepare_hashqueuesize) );
			tab->addString(L"ONLINE", convert(status[i].online) );

			time_t tt=(time_t)(time(0)-((Server->getTimeMS()-status[i].starttime)/1000));
#ifdef _WIN32
			struct tm ttm;
			localtime_s(&ttm, &tt);
			char buf[1000];
			strftime(buf, 1000, "%d.%m.%Y %H:%M",  &ttm);
#else
			struct tm *ttm=localtime(&tt);
			char buf[1000];
			strftime(buf, 1000, "%d.%m.%Y %H:%M", ttm);
#endif

			tab->addString(L"STARTTIME", widen((std::string)buf) );
			tab->addString(L"DONE", convert(status[i].done) );
			std::string action;
			switch(status[i].statusaction)
			{
			case sa_none:
				action=L"none"; break;
			case sa_incr_file:
				action=L"incr_file"; break;
			case sa_full_file:
				action=L"full_file"; break;
			case sa_incr_image:
				action=L"incr_image"; break;
			case sa_full_image:
				action=L"full_image"; break;
			case sa_resume_full_file:
				action=L"resume_full_file"; break;
			case sa_resume_incr_file:
				action=L"resume_incr_file"; break;
			}
			tab->addString(L"ACTION", action);
		}
		else
		{
			tab->addString(L"PCDONE", L"&nbsp;");
			tab->addString(L"HASHQUEUESIZE", L"&nbsp;");
			tab->addString(L"HASHQUEUE_PREPARE", L"&nbsp;");
			tab->addString(L"ONLINE", convert(status[i].online));
			tab->addString(L"STARTTIME", L"&nbsp;");
			tab->addString(L"DONE", L"&nbsp;");
			tab->addString(L"ACTION", L"&nbsp;");
		}*/
	}

	Server->Write(tid, tmpl->getData());
	Server->destroy(tmpl);
#endif
}

#endif //CLIENT_ONLY
