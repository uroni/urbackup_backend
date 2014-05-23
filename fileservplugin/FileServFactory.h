#include "IFileServFactory.h"

class FileServFactory : public IFileServFactory
{
public:
	IFileServ * createFileServ(unsigned short tcpport, unsigned short udpport, const std::wstring &name=L"", bool use_fqdn_default=false);
	void destroyFileServ(IFileServ *filesrv);
};