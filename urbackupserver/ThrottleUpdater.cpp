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

size_t ThrottleUpdater::getThrottleLimit()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerSettings server_settings(db, clientid);

	switch(throttle_scope)
	{
	case ThrottleScope_GlobalInternet:
		return server_settings.getGlobalInternetSpeed();
	case ThrottleScope_GlobalLocal:
		return server_settings.getGlobalLocalSpeed();
	case ThrottleScope_Internet:
		return server_settings.getInternetSpeed();
	case ThrottleScope_Local:
		return server_settings.getLocalSpeed();
	default:
		return std::string::npos;
	}
}

