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

#include "FileServFactory.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "FileServ.h"
#include "CUDPThread.h"

int start_server_int(unsigned short tcpport, unsigned short udpport, const std::string &pSname, const bool *pDostop, bool use_fqdn);

bool FileServFactory::backupground_backups_enabled = true;


class ExecThread : public IThread
{
public:
	ExecThread(unsigned short pTcpport, unsigned short pUdpport, const std::string &pName, bool *pDostop, bool pUse_fqdn)
	{
		tcpport=pTcpport;
		udpport=pUdpport;
		name=pName;
		dostop=pDostop;
		use_fqdn=pUse_fqdn;
	}

	void operator()(void)
	{
		int r=start_server_int(tcpport, udpport, name, dostop, use_fqdn);
		if(r!=2)
		{
			Server->Log("FileServ exit with error code: "+convert(r), LL_ERROR);
		}
		delete this;
	}

private:
	unsigned short tcpport,udpport;
	std::string name;
	bool *dostop;
	bool use_fqdn;
};


IPermissionCallback* FileServFactory::permission_callback = NULL;

IFileServ * FileServFactory::createFileServ(unsigned short tcpport, unsigned short udpport, const std::string &name, bool use_fqdn_default, bool enable_background_priority)
{
	bool *dostop=new bool;
	*dostop=false;
	backupground_backups_enabled = enable_background_priority;
	ExecThread *et=new ExecThread(tcpport, udpport, name, dostop, use_fqdn_default);
	THREADPOOL_TICKET t=Server->getThreadPool()->execute(et);
	FileServ *fs=new FileServ(dostop, name, t, use_fqdn_default);
	return fs;
}

void FileServFactory::destroyFileServ(IFileServ *filesrv)
{
	delete ((FileServ*)filesrv);
}

void FileServFactory::setPermissionCallback( IPermissionCallback* new_permission_callback )
{
	permission_callback=new_permission_callback;
}

bool FileServFactory::backgroundBackupsEnabled()
{
	return backupground_backups_enabled;
}

IPermissionCallback* FileServFactory::getPermissionCallback()
{
	return permission_callback;
}

IFileServ* FileServFactory::createFileServNoBind(const std::string &name, bool use_fqdn_default)
{
	bool *dostop=new bool;
	*dostop=false;
	FileServ *fs=new FileServ(dostop, name, ILLEGAL_THREADPOOL_TICKET, use_fqdn_default);
	return fs;
}

std::string FileServFactory::getDefaultServerName( bool use_fqdn )
{
	return getSystemServerName(use_fqdn);
}
