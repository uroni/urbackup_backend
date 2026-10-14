#pragma once

#include "../Interface/Action.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../urbackupcommon/json.h"
#include "../stringtools.h"
#include "../urbackupcommon/settings.h"
#include <stdlib.h>
#include <atomic>

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
	ACTION(get_keyboard_layouts);
	ACTION(set_keyboard_layout);
	ACTION(get_connection_settings);
	ACTION(get_spill_disks);
	ACTION(test_spill_disks);
	ACTION(setup_spill_disks);
	ACTION(cleanup_spill_disks);
	ACTION(resize_disk);
	ACTION(restore_finished);
	ACTION(resize_part);
	ACTION(capabilities);
	ACTION(get_tmpfn);
	ACTION(get_timezone_data);
	ACTION(get_timezone_areas);
	ACTION(get_timezone_cities);
	ACTION(set_timezone);
}

namespace restore
{ 
	static void writeJsonResponse(THREAD_ID tid, JSON::Object& ret)
	{
		Server->setContentType(tid, "application/json");
		Server->Write(tid, ret.stringify(false));
	}

	std::string resize_ntfs(int64 new_size, const std::string& disk_fn, std::atomic<int>& pc_complete);
}
