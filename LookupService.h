#include "socket_header.h"
#include <string>

bool LookupBlocking(std::string pServer, in_addr *dest);
bool LookupHostname(const std::string& pIp, std::string& hostname);