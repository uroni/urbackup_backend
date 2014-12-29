#include "IFileServ.h"
#include "../Interface/Mutex.h"
#include "../Interface/ThreadPool.h"
#include <vector>

class FileServ : public IFileServ
{
public:
	FileServ(bool *pDostop, const std::wstring &servername, THREADPOOL_TICKET serverticket, bool use_fqdn);
	~FileServ(void);
	void shareDir(const std::wstring &name, const std::wstring &path);
	void removeDir(const std::wstring &name);
	void stopServer(void);
	std::wstring getServerName(void);
	std::wstring getShareDir(const std::wstring &name);
	void addIdentity(const std::string &pIdentity);
	bool removeIdentity(const std::string &pIdentity);
	void setPause(bool b);
	bool getPause(void);
	bool getExitInformation(const std::wstring& cmd, std::string& stderr_data, int& exit_code);

	virtual void runClient(IPipe *cp);

	static bool isPause(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static bool checkIdentity(const std::string &pIdentity);

private:
	bool *dostop;
	THREADPOOL_TICKET serverticket;
	std::wstring servername;

	static std::vector<std::string> identities;
	static bool pause;
	
	static IMutex *mutex;
};
