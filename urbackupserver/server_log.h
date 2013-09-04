#include "../Interface/Server.h"
#include "../Interface/Mutex.h"


struct SLogEntry
{
	std::wstring data;
	int loglevel;
	unsigned int time;
};

class ServerLogger
{
public:
	static void Log(int clientid, const std::string &pStr, int LogLevel=LL_INFO);
	static void Log(int clientid, const std::wstring &pStr, int LogLevel=LL_INFO);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static std::wstring getLogdata(int clientid, int &errors, int &warnings, int &infos);
	static std::wstring getWarningLevelTextLogdata(int clientid);

	static void reset(int clientid);

private:
	static std::map<int, std::vector<SLogEntry> > logdata;
	static IMutex *mutex;
};