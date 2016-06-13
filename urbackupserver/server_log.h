#pragma once

#include "../Interface/Server.h"
#include "../Interface/Mutex.h"


struct SLogEntry
{
	std::string data;
	int loglevel;
	int64 time;
};

typedef std::pair<int64, int> logid_t;

struct SCircularLogEntryWithId
{
	SCircularLogEntryWithId(void)
		: loglevel(LL_DEBUG), id(std::string::npos), time(0),
		 logid()
	{
	}

	std::string utf8_msg;
	int loglevel;
	size_t id;
	int64 time;
	logid_t logid;
};

struct SCircularData
{
	std::vector<SCircularLogEntryWithId> data;
	size_t idx;
	size_t id;
};

const int LOG_CATEGORY_CLEANUP = -4;

class ServerLogger
{
public:
	static void Log(logid_t logid, const std::string &pStr, int LogLevel=LL_INFO);
	static void Log(int64 times, logid_t logid, const std::string &pStr, int LogLevel=LL_INFO);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static std::string getLogdata(logid_t logid, int &errors, int &warnings, int &infos);
	static std::string getWarningLevelTextLogdata(logid_t logid);

	static void reset(logid_t id);

	static void reset(int clientid);

	static std::vector<SCircularLogEntry> getCircularLogdata(int clientid, size_t minid, logid_t logid);

	static logid_t getLogId(int clientid);
	
	static bool hasClient(logid_t id, int clientid);

private:

	static void logCircular(int clientid, logid_t logid, const std::string &pStr, int LogLevel);
	static void logMemory(int64 times, logid_t logid, const std::string &pStr, int LogLevel);

	static std::vector<SCircularLogEntry> stripLogIdFilter(const std::vector<SCircularLogEntryWithId>& data, logid_t logid);

	static std::map<logid_t, std::vector<SLogEntry> > logdata;
	static std::map<int, SCircularData> circular_logdata;
	static std::map<logid_t, int> logid_client;
	static IMutex *mutex;
	static logid_t logid_gen;
};