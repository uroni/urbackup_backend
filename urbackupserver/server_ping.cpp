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

#ifndef CLIENT_ONLY

#include "server_ping.h"
#include "ClientMain.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

const int64 ping_intervall=10000;
extern std::string server_token;

ServerPingThread::ServerPingThread(ClientMain *client_main, const std::wstring& clientname, size_t status_id, bool with_eta)
	: client_main(client_main), clientname(clientname), status_id(status_id), with_eta(with_eta)
{
	stop=false;
	is_timeout=false;
}

void ServerPingThread::operator()(void)
{
	int64 last_ping_ok=Server->getTimeMS();
	while(stop==false)
	{
		//Server->Log("Sending ping running...", LL_DEBUG);
		std::string pcdone;
		SProcess proc = ServerStatus::getProcess(clientname, status_id);

		if(proc.action==sa_none)
		{
			break;
		}

		int i_pcdone = proc.pcdone;
		if(i_pcdone>=0)
		{
			pcdone=nconvert(i_pcdone);
		}

		int64 add_time = Server->getTimeMS() - proc.eta_set_time;
		int64 etams = proc.eta_ms - add_time;

		if(!with_eta)
		{
			if(client_main->sendClientMessage("PING RUNNING -"+pcdone+"-#token="+server_token, "OK", L"Error sending 'running' ping to client", 30000, false, LL_DEBUG))
			{
				last_ping_ok=Server->getTimeMS();
			}
		}
		else
		{
			if(etams>0 && etams<60*1000)
			{
				etams=61*1000;
			}
			if(client_main->sendClientMessage("2PING RUNNING pc_done="+pcdone+(etams>0?"&eta_ms="+nconvert(etams):"")
							+"&status_id="+nconvert(status_id)+"#token="+server_token, "OK", L"Error sending 'running' (2) ping to client", 30000, false, LL_DEBUG))
			{
				last_ping_ok=Server->getTimeMS();
			}
		}
		
		//Server->Log("Done sending ping running.", LL_DEBUG);

		if(Server->getTimeMS()-last_ping_ok>ping_intervall*6)
		{
			is_timeout=true;
		}
		else
		{
			is_timeout=false;
		}

		Server->wait(ping_intervall);
	}
	Server->wait(1000);
	delete this;
}

void ServerPingThread::setStop(bool b)
{
	stop=b;
}

bool ServerPingThread::isTimeout(void)
{
	return is_timeout;
}

#endif //CLIENT_ONLY