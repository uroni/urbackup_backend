#ifndef IFILESERV_H
#define IFILESERV_H

#include <string>

#include "../Interface/Object.h"

class IFileServ : public IObject
{
public:
	virtual void shareDir(const std::string &name, const std::wstring &path)=0;
	virtual void removeDir(const std::string &name)=0;
	virtual void stopServer(void)=0;
	virtual std::wstring getShareDir(const std::string &name)=0;
	virtual void addIdentity(const std::string &pIdentity)=0;
	virtual void setPause(bool b)=0;
	virtual bool getPause(void)=0;
};

#endif //IFILESERV_H