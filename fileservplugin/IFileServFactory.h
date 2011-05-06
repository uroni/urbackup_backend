#ifndef IFILESERVFACTORY_H
#define IFILESERVFACTORY_H

#include "IFileServ.h"

#include "../Interface/Plugin.h"

#include <string>

class IFileServFactory : public IPlugin
{
public:
	virtual IFileServ * createFileServ(unsigned short tcpport, unsigned short udpport, const std::wstring &name=L"")=0;
	virtual void destroyFileServ(IFileServ *filesrv)=0;
};

#endif //IFILESERVFACTORY_H