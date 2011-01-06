//---------------------------------------------------------------------------

#ifndef DownloadThreadH
#define DownloadThreadH
#include "download2.h"
#include "../Interface/Thread.h"


class DownloadThread : public IThread
{            
private:
        std::string url,filename,proxy;
        unsigned short proxyport;
        IPipe *pipe;
protected:
        void operator()(void);
public:
        DownloadThread();

        void init(std::string pUrl,std::string pFilename, IPipe *pPipe, std::string pProxy, unsigned short pProxyport);
};
//---------------------------------------------------------------------------
#endif
