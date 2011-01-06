#include "IFileDownload.h"
#include "../Interface/Plugin.h"

class IDownloadFactory : public IPlugin
{
public:
	virtual IFileDownload *createFileDownload(void)=0;
	virtual void destroyFileDownload(IFileDownload* dl)=0;
};