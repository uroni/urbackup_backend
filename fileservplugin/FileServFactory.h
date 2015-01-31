#include "IFileServFactory.h"

class FileServFactory : public IFileServFactory
{
public:
	IFileServ * createFileServ(unsigned short tcpport, unsigned short udpport, const std::wstring &name=L"", bool use_fqdn_default=false);
	void destroyFileServ(IFileServ *filesrv);

	IFileServ* createFileServNoBind(const std::wstring &name=L"", bool use_fqdn_default=false);

	void setPermissionCallback(IPermissionCallback* new_permission_callback);

	static IPermissionCallback* getPermissionCallback();

private:
	static IPermissionCallback* permission_callback;
};