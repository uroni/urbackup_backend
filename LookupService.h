#pragma once
#include "socket_header.h"
#include <string>
#include <vector>

struct SLookupBlockingResult
{
	bool is_ipv6;
	union
	{
		unsigned int addr_v4;
		char addr_v6[16];
	};
	unsigned int zone;
};

std::vector<SLookupBlockingResult> LookupBlocking(std::string pServer);
bool LookupHostname(const std::string& pIp, std::string& hostname);
