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

#include "ThrottleUpdater.h"
#include "server_settings.h"
#include "../Interface/Database.h"
#include "database.h"
#include "../Interface/Server.h"

ThrottleUpdater::ThrottleUpdater(int clientid, ThrottleScope throttle_scope)
	: clientid(clientid), throttle_scope(throttle_scope)
{

}

int64 ThrottleUpdater::getUpdateIntervalMs()
{
	return 10*60*1000; //10 min
}

size_t ThrottleUpdater::getThrottleLimit(bool& percent_max)
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerSettings server_settings(db, clientid);

	int ret;
	switch(throttle_scope)
	{
	case ThrottleScope_GlobalInternet:
		ret = server_settings.getGlobalInternetSpeed();
		break;
	case ThrottleScope_GlobalLocal:
		ret = server_settings.getGlobalLocalSpeed();
		break;
	case ThrottleScope_Internet:
		ret = server_settings.getInternetSpeed();
		break;
	case ThrottleScope_Local:
		ret = server_settings.getLocalSpeed();
		break;
	default:
		percent_max = false;
		return std::string::npos;
	}

	if (ret < -1)
	{
		percent_max = true;
		return ret*-1 - 1;
	}
	else
	{
		percent_max = false;
		return ret<0 ? 0 : ret;
	}
}

