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

#include "FileServFactory.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "FileServ.h"

int start_server_int(unsigned short tcpport, unsigned short udpport, const std::string &pSname, const bool *pDostop);

class ExecThread : public IThread
{
public:
	ExecThread(unsigned short pTcpport, unsigned short pUdpport, const std::wstring &pName, bool *pDostop)
	{
		tcpport=pTcpport;
		udpport=pUdpport;
		name=pName;
		dostop=pDostop;
	}

	void operator()(void)
	{
		int r=start_server_int(tcpport, udpport, Server->ConvertToUTF8(name), dostop);
		if(r!=2)
		{
			Server->Log("FileServ exit with error code: "+nconvert(r), LL_ERROR);
		}
		delete this;
	}

private:
	unsigned short tcpport,udpport;
	std::wstring name;
	bool *dostop;
};

IFileServ * FileServFactory::createFileServ(unsigned short tcpport, unsigned short udpport, const std::wstring &name)
{
	bool *dostop=new bool;
	*dostop=false;
	ExecThread *et=new ExecThread(tcpport, udpport, name, dostop);
	THREADPOOL_TICKET t=Server->getThreadPool()->execute(et);
	FileServ *fs=new FileServ(dostop, name, t);
	return fs;
}

void FileServFactory::destroyFileServ(IFileServ *filesrv)
{
	delete ((FileServ*)filesrv);
}
