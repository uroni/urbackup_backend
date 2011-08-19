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

#include "server_ping.h"
#include "server_get.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

const unsigned int ping_intervall=10000;
extern std::string server_token;

ServerPingThread::ServerPingThread(BackupServerGet *pServer_get) : server_get(pServer_get)
{
	stop=false;
}

void ServerPingThread::operator()(void)
{
	while(stop==false)
	{
		Server->Log("Sending ping running...", LL_DEBUG);
		server_get->sendClientMessage("PING RUNNING -"+nconvert(server_get->getPCDone())+"-#token="+server_token, "OK", L"Error sending 'running' ping to client", 30000, false);
		Server->Log("Done sending ping running.", LL_DEBUG);
		Server->wait(ping_intervall);
	}
	Server->wait(1000);
	delete this;
}

void ServerPingThread::setStop(bool b)
{
	stop=b;
}

#endif //CLIENT_ONLY