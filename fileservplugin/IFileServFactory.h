#ifndef IFILESERVFACTORY_H
#define IFILESERVFACTORY_H

#include "IFileServ.h"

#include "../Interface/Plugin.h"

#include <string>

#include "IPermissionCallback.h"

class IFileServFactory : public IPlugin
{
public:
	virtual IFileServ * createFileServ(unsigned short tcpport, unsigned short udpport, const std::string &name="", bool use_fqdn_default=false, bool enable_background_priority=true)=0;
	virtual IFileServ * createFileServNoBind(const std::string &name="", bool use_fqdn_default=false)=0;
	virtual void destroyFileServ(IFileServ *filesrv)=0;
	virtual void setPermissionCallback(IPermissionCallback* new_permission_callback)=0;
	virtual std::string getDefaultServerName(bool use_fqdn)=0;
};

#endif //IFILESERVFACTORY_H