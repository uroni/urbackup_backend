#ifndef LOG_REDIR_H
#define LOG_REDIR_H

#include <iostream>
#include "stringtools.h"

#ifdef NO_SERVER
#define LL_DEBUG -1
#define LL_INFO 0
#define LL_WARNING 1
#define LL_ERROR 2

void LOG(std::string msg)
{
	std::cout << "INFO: " << msg << std::endl;
}
void LOG(std::wstring msg)
{
	std::cout << "INFO: " << wnarrow(msg) << std::endl;
}
void PrintLoglevel(int loglevel)
{
	if(loglevel==LL_ERROR)
		std::cout << "ERROR: ";
	if(loglevel==LL_WARNING)
		std::cout << "WARNING: ";
	if(loglevel==LL_INFO)
		std::cout << "INFO: ";
	if(loglevel==LL_DEBUG)
		std::cout << "DEBUG: ";
}
void LOG(std::string msg, int loglevel)
{
	PrintLoglevel(loglevel);
	std::cout << msg << std::endl;
}
void LOG(std::wstring msg, int loglevel)
{
	PrintLoglevel(loglevel);
	std::cout << wnarrow(msg) << std::endl;
}

#else
#include "../Interface/Server.h"
void LOG(std::string msg)
{
	Server->Log(msg);
}
void LOG(std::wstring msg)
{
	Server->Log(msg);
}
void LOG(std::string msg, int loglevel)
{
	Server->Log(msg, loglevel);
}
void LOG(std::wstring msg, int loglevel)
{
	Server->Log(msg, loglevel);
}
#endif

#endif //LOG_REDIR_H