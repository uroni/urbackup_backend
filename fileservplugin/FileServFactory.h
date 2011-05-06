#include "IFileServFactory.h"

class FileServFactory : public IFileServFactory
{
public:
	IFileServ * createFileServ(unsigned short tcpport, unsigned short udpport, const std::wstring &name=L"");
	void destroyFileServ(IFileServ *filesrv);
};