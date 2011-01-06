#include "IFileServ.h"
#include "../Interface/Mutex.h"
#include <vector>

class FileServ : public IFileServ
{
public:
	FileServ(bool *pDostop);
	void shareDir(const std::string &name, const std::wstring &path);
	void removeDir(const std::string &name);
	void stopServer(void);
	std::wstring getShareDir(const std::string &name);
	void addIdentity(const std::string &pIdentity);
	void setPause(bool b);
	bool getPause(void);

	static bool isPause(void);

	static void init_mutex(void);

	static bool checkIdentity(const std::string &pIdentity);

private:
	bool *dostop;

	static std::vector<std::string> identities;
	static bool pause;
	
	static IMutex *mutex;
};