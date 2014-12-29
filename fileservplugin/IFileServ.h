#ifndef IFILESERV_H
#define IFILESERV_H

#include <string>

#include "../Interface/Object.h"

class IPipe;

class IFileServ : public IObject
{
public:
	virtual void shareDir(const std::wstring &name, const std::wstring &path)=0;
	virtual void removeDir(const std::wstring &name)=0;
	virtual std::wstring getServerName(void)=0;
	virtual void stopServer(void)=0;
	virtual std::wstring getShareDir(const std::wstring &name)=0;
	virtual void addIdentity(const std::string &pIdentity)=0;
	virtual bool removeIdentity(const std::string &pIdentity)=0;
	virtual void setPause(bool b)=0;
	virtual bool getPause(void)=0;
	virtual void runClient(IPipe *cp)=0;
	virtual bool getExitInformation(const std::wstring& cmd, std::string& stderr_data, int& exit_code) = 0;
};

#endif //IFILESERV_H