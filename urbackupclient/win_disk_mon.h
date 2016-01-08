#pragma once
#include <string>
#include <vector>

struct SFailedDisk
{
	std::string name;
	std::string status;
	std::string status_info;
};

std::vector<SFailedDisk> get_failed_disks();