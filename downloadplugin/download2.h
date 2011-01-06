#include <vector>
#include <deque>
#include "data.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Pipe.h"
#include "../Interface/ThreadPool.h"

#include "IFileDownload.h"

class DownloadThread;

const uchar DL2_ERROR=1;
const uchar DL2_DONE=2;
const uchar DL2_INFO=3;
const uchar DL2_CONTENT_LENGTH=4;
const uchar DL2_BYTES=5;


const uchar DL2_ERR_COULDNOTOPENFILE=0;
const uchar DL2_ERR_WSASTARTUP_FAILED=1;
const uchar DL2_ERR_COULDNOTCONNECT=2;
const uchar DL2_ERR_NORESPONSE=3;
const uchar DL2_ERR_TIMEOUT=3;
const uchar DL2_ERR_404=4;

const uchar DL2_INFO_PREPARING=0;
const uchar DL2_INFO_RESOLVING=1;
const uchar DL2_INFO_REQUESTING=2;
const uchar DL2_INFO_DOWNLOADING=3;


bool DownloadfileThreaded(std::string url,std::string filename, IPipe *pipe, std::string proxy, unsigned short proxyport);


class CFileDownload : public IFileDownload
{
public:
        CFileDownload();
		~CFileDownload();

        void setProxy( std::string pProxy, unsigned short pProxyport );
        void download( std::string pUrl, std::string pFilename );

        uchar download(int wait=-1);

        int getContentLength(void);

        int getDownloadedBytes(void);
        int getNewDownloadedBytes(void);

private:

        std::string proxy;
        unsigned short proxyport;

        std::string url, filename;

        int content_length;

        DownloadThread *dlthread;
		THREADPOOL_TICKET dlthread_ticket;
        IPipe *pipe;

        int received;
        int lbytes_received;		
};


