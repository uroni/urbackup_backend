/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
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

#ifndef CLIENT_ONLY

#include "server_status.h"
#include "../Interface/Server.h"
#include "action_header.h"
#include <time.h>

IMutex *ServerStatus::mutex=NULL;
std::map<std::wstring, SStatus> ServerStatus::status;
unsigned int ServerStatus::last_status_update;

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

void ServerStatus::setServerStatus(const SStatus &pStatus, bool setactive)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[pStatus.client];
	s->hashqueuesize=pStatus.hashqueuesize;
	s->prepare_hashqueuesize=pStatus.prepare_hashqueuesize;
	s->starttime=pStatus.starttime;
	s->pcdone=pStatus.pcdone;
	s->has_status=true;
	s->statusaction=pStatus.statusaction;
	s->clientid=pStatus.clientid;
	if(setactive)
	{
		last_status_update=Server->getTimeMS();
	}
}

void ServerStatus::updateActive(void)
{
	IScopedLock lock(mutex);
	last_status_update=Server->getTimeMS();
}

void ServerStatus::setOnline(const std::wstring &clientname, bool bonline)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	if(bonline)
	{
		*s=SStatus();
	}
	s->online=bonline;
	s->client=clientname;
	s->done=false;
	s->has_status=true;
	s->r_online=bonline;
	if(bonline)
	{
		last_status_update=Server->getTimeMS();
	}
}

void ServerStatus::setROnline(const std::wstring &clientname, bool bonline)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->r_online=bonline;
	if(bonline)
	{
		last_status_update=Server->getTimeMS();
	}
}

void ServerStatus::setDone(const std::wstring &clientname, bool bdone)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->done=bdone;
}

void ServerStatus::setIP(const std::wstring &clientname, unsigned int ip)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->ip_addr=ip;
}

void ServerStatus::setWrongIdent(const std::wstring &clientname, bool b)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->wrong_ident=b;
}

void ServerStatus::setTooManyClients(const std::wstring &clientname, bool b)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->too_many_clients=b;
}

void ServerStatus::setCommPipe(const std::wstring &clientname, IPipe *p)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->comm_pipe=p;
}

void ServerStatus::stopBackup(const std::wstring &clientname, bool b)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->stop_backup=b;
}

bool ServerStatus::isBackupStopped(const std::wstring &clientname)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	return s->stop_backup;
}

std::vector<SStatus> ServerStatus::getStatus(void)
{
	IScopedLock lock(mutex);
	std::vector<SStatus> ret;
	for(std::map<std::wstring, SStatus>::iterator it=status.begin();it!=status.end();++it)
	{
		ret.push_back(it->second);
	}
	return ret;
}

SStatus ServerStatus::getStatus(const std::wstring &clientname)
{
	IScopedLock lock(mutex);
	std::map<std::wstring, SStatus>::iterator iter=status.find(clientname);
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

void ServerStatus::setClientVersionString(const std::wstring &clientname, const std::string& client_version_string)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->client_version_string=client_version_string;
}

void ServerStatus::setOSVersionString(const std::wstring &clientname, const std::string& os_version_string)
{
	IScopedLock lock(mutex);
	SStatus *s=&status[clientname];
	s->os_version_string=os_version_string;
}

ACTION_IMPL(server_status)
{
#ifndef _DEBUG
	Server->Write(tid, "Forbidden");
#else
	ITemplate *tmpl=Server->createTemplate("urbackup/status.htm");

	std::vector<SStatus> status=ServerStatus::getStatus();

	tmpl->getTable(L"CLIENTS");
	
	for(size_t i=0;i<status.size();++i)
	{
		if(status[i].done==false)
		{
			ITable *tab=tmpl->getTable(L"CLIENTS."+convert(i));
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
				std::wstring action;
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
			}
		}
	}

	Server->Write(tid, tmpl->getData());
	Server->destroy(tmpl);
#endif
}

#endif //CLIENT_ONLY
