#ifndef IFILEDOWNLOAD_H
#define IFILEDOWNLOAD_H


#include "../Interface/Types.h"

const uchar FD_ERR_CONTINUE=0;
const uchar FD_ERR_SUCCESS=1;
const uchar FD_ERR_TIMEOUT=2;
const uchar FD_ERR_FILE_DOESNT_EXIST=3;
const uchar FD_ERR_SOCKET_ERROR=4;
const uchar FD_ERR_CONNECTED=5;
const uchar FD_ERR_ERROR=6;
const uchar FD_ERR_CONTENT_LENGTH=7;
const uchar FD_ERR_QUEUE_ITEMS=8;

class IFileDownload
{
public:
	virtual void setProxy( std::string pProxy, unsigned short pProxyport )=0;
    virtual void download( std::string pUrl, std::string pFilename )=0;

    virtual uchar download(int wait=-1)=0;

    virtual int getContentLength(void)=0;

    virtual int getDownloadedBytes(void)=0;
    virtual int getNewDownloadedBytes(void)=0;
};

#endif //IFILEDOWNLOAD_H