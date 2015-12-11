#pragma once

#include "../Interface/Server.h"
#include "../Interface/Mutex.h"


struct SLogEntry
{
	std::string data;
	int loglevel;
	int64 time;
};

struct SCircularData
{
	std::vector<SCircularLogEntry> data;
	size_t idx;
	size_t id;
};

typedef std::pair<int64, int> logid_t;

class ServerLogger
{
public:
	static void Log(logid_t logid, const std::string &pStr, int LogLevel=LL_INFO);
	static void Log(int64 times, logid_t logid, const std::string &pStr, int LogLevel=LL_INFO);
	static void Log(logid_t logid, const std::wstring &pStr, int LogLevel=LL_INFO);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static std::wstring getLogdata(logid_t logid, int &errors, int &warnings, int &infos);
	static std::string getWarningLevelTextLogdata(logid_t logid);

	static void reset(logid_t id);

	static void reset(int clientid);

	static std::vector<SCircularLogEntry> getCircularLogdata(int clientid, size_t minid);

	static logid_t getLogId(int clientid);
	
	static bool hasClient(logid_t id, int clientid);

private:

	static void logCircular(int clientid, const std::string &pStr, int LogLevel);
	static void logMemory(int64 times, logid_t logid, const std::string &pStr, int LogLevel);

	static std::map<logid_t, std::vector<SLogEntry> > logdata;
	static std::map<int, SCircularData> circular_logdata;
	static std::map<logid_t, int> logid_client;
	static IMutex *mutex;
	static logid_t logid_gen;
};