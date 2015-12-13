#include "IFileServFactory.h"

class FileServFactory : public IFileServFactory
{
public:
	static bool backgroundBackupsEnabled();
	IFileServ * createFileServ(unsigned short tcpport, unsigned short udpport, const std::string &name="", bool use_fqdn_default=false, bool enable_background_priority=true);
	void destroyFileServ(IFileServ *filesrv);

	IFileServ* createFileServNoBind(const std::string &name="", bool use_fqdn_default=false);

	void setPermissionCallback(IPermissionCallback* new_permission_callback);

	static IPermissionCallback* getPermissionCallback();

	std::string getDefaultServerName(bool use_fqdn);

private:
	static IPermissionCallback* permission_callback;
	static bool backupground_backups_enabled;
};