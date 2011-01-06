#include "IDownloadFactory.h"

class DownloadFactory : public IDownloadFactory
{
public:
	virtual IFileDownload* createFileDownload(void);
	virtual void destroyFileDownload(IFileDownload* dl);
};