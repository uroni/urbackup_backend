#include "IFileServFactory.h"

class FileServFactory : public IFileServFactory
{
public:
	static bool backgroundBackupsEnabled();
	IFileServ * createFileServ(unsigned short tcpport, unsigned short udpport, const std::wstring &name=L"", bool use_fqdn_default=false, bool enable_background_priority=true);
	void destroyFileServ(IFileServ *filesrv);

private:
	static bool backupground_backups_enabled;
};