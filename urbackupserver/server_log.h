#include "../Interface/Server.h"
#include "../Interface/Mutex.h"


struct SLogEntry
{
	std::string data;
	int loglevel;
	unsigned int time;
};

struct SCircularData
{
	std::vector<SCircularLogEntry> data;
	size_t idx;
	size_t id;
};

class ServerLogger
{
public:
	static void Log(int clientid, const std::string &pStr, int LogLevel=LL_INFO);
	static void Log(int clientid, const std::wstring &pStr, int LogLevel=LL_INFO);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static std::wstring getLogdata(int clientid, int &errors, int &warnings, int &infos);
	static std::string getWarningLevelTextLogdata(int clientid);

	static void reset(int clientid);

	static std::vector<SCircularLogEntry> getCircularLogdata(int clientid, size_t minid);

private:

	static void logCircular(int clientid, const std::string &pStr, int LogLevel);
	static void logMemory(int clientid, const std::string &pStr, int LogLevel);

	static std::map<int, std::vector<SLogEntry> > logdata;
	static std::map<int, SCircularData> circular_logdata;
	static IMutex *mutex;
};