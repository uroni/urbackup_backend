#pragma once

#include "../Interface/Action.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../urbackupcommon/json.h"
#include "../stringtools.h"
#include "../urbackupcommon/settings.h"
#include <stdlib.h>

#include "../Interface/Action.h"

namespace Actions
{
	ACTION(status);
	ACTION(login);
	ACTION(get_clientnames);
	ACTION(get_backupimages);
	ACTION(start_download);
	ACTION(download_progress);
	ACTION(has_network_device);
	ACTION(ping_server);
	ACTION(has_internet_connection);
	ACTION(configure_server);
	ACTION(get_disks);
	ACTION(get_is_disk_mbr);
	ACTION(write_mbr);
	ACTION(get_partition);
	ACTION(restart);
}

namespace restore
{ 
	static void writeJsonResponse(THREAD_ID tid, JSON::Object& ret)
	{
		Server->setContentType(tid, "application/json");
		Server->Write(tid, ret.stringify(false));
	}
}
