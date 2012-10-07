/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "socket_header.h"
#include "DownloadThread.h"
#include "../Interface/Server.h"
#include <fstream>
#include <memory.h>
#include "../stringtools.h"
#include <stdlib.h>

using namespace std;

in_addr getIP(std::string ip);

#define BUFFERSIZE 32768
#ifndef _WIN32
#define _unlink unlink
#endif

in_addr getIP(string ip)
{	
    const char* host=ip.c_str();
    in_addr dest;
    unsigned int addr = inet_addr(host);
    if (addr != INADDR_NONE) {
        dest.s_addr = addr;
		return dest;
    }
    else {
        hostent* hp = gethostbyname(host);
        if (hp != 0) {
            memcpy(&(dest), hp->h_addr, hp->h_length);
        }
        else {
			memset(&dest,0,sizeof(in_addr) );
            return dest;
        }
    }
    return dest;
}

bool DownloadfileThreaded(std::string url,std::string filename, IPipe *pipe, std::string proxy, unsigned short proxyport)
{
        {
                CWData wd;
				wd.addUChar(DL2_INFO);
                wd.addUChar(DL2_INFO_PREPARING);
				pipe->Write(wd.getDataPtr(), wd.getDataSize());
        }

        int Cs;
        string ret;
        sockaddr_in addr;
        fstream out;
        out.open(filename.c_str(),ios::out|ios::binary);
        if(out.is_open()==false)
        {
                CWData wd;
				wd.addUChar(DL2_ERROR);
                wd.addInt(DL2_ERR_COULDNOTOPENFILE);
				pipe->Write(wd.getDataPtr(), wd.getDataSize());
	        return false;
        }

        string host;
        string site;
        int pos=0;
        //--------
        for(unsigned int i=0;i<url.length();i++)
        {
                if(url[i]=='/'){
                        pos++;
                        if(pos<3)continue;
                }
                if(pos==2)host+=url[i];
                if(pos>2) site+=url[i];
        }

#ifdef _WIN32
        WSADATA wsa;
	int rc = WSAStartup(MAKEWORD(2, 0), &wsa);
	if(rc == SOCKET_ERROR)
	{
                CWData wd;
				wd.addUChar(DL2_ERROR);
                wd.addInt(DL2_ERR_WSASTARTUP_FAILED);
				pipe->Write(wd.getDataPtr(), wd.getDataSize());
				return false;
	}
#endif

        Cs=(int)socket(AF_INET,SOCK_STREAM,0);

        {
                CWData wd;
				wd.addUChar(DL2_INFO);
                wd.addUChar(DL2_INFO_RESOLVING);
				pipe->Write(wd.getDataPtr(), wd.getDataSize());
        }

        addr.sin_family=AF_INET;
        if(proxy=="")
	{
		addr.sin_port=htons(80);
		addr.sin_addr=getIP(host);
	}
	else
	{
		addr.sin_port=htons(proxyport);
		addr.sin_addr=getIP(proxy);
	}

#ifdef _WIN32
        u_long nonBlocking=1;
        ioctlsocket(Cs,FIONBIO,&nonBlocking);
#else
	int flags=fcntl(Cs,F_GETFL,0);
	fcntl(Cs, F_SETFL, flags|O_NONBLOCK);
#endif

        int error;
        connect(Cs,(sockaddr*)&addr,sizeof(sockaddr));
		       

        fd_set conn;
	FD_ZERO(&conn);
	FD_SET(Cs, &conn);

        timeval timeout;

		timeout.tv_sec=60;
		timeout.tv_usec=0;

	error=select(Cs+1,NULL,&conn,NULL,&timeout);

	if(error<1)
	{
		CWData wd;
		wd.addUChar(DL2_ERROR);
        wd.addInt(DL2_ERR_COULDNOTCONNECT);
		pipe->Write(wd.getDataPtr(), wd.getDataSize());
		return false;
	}

        string tosend;

        {
                CWData wd;
				wd.addUChar(DL2_INFO);
                wd.addUChar(DL2_INFO_REQUESTING);
				pipe->Write(wd.getDataPtr(), wd.getDataSize());
        }

	tosend="GET "+site+" HTTP/1.1\r\n";
	tosend=tosend+  "Host: "+host+"\r\n"+
                        "User-Agent: Mozilla/5.0 (X11; U; Linux i686; de-AT; rv:1.4.1) Gecko/20031030\r\n"+
                        "Accept: text/xml,application/xml,application/xhtml+xml,text/html;q=0.9,text/plain;q=0.8,video/x-mng,image/png,image/jpeg,image/gif;q=0.2,*/*;q=0.1\r\n"+
                        "Accept-Language: de-at,de;q=0.8,en-us;q=0.5,en;q=0.3\r\n"+
                        "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"+
                        "Connection: close\r\n";
        tosend+="\r\n";

        send(Cs,tosend.c_str(),(int)tosend.length(),MSG_NOSIGNAL);

        int bytes=BUFFERSIZE;
        int totalbytes=0;

	char *Buffer=new char[BUFFERSIZE+1];

        {
                CWData wd;
				wd.addUChar(DL2_INFO);
                wd.addUChar(DL2_INFO_DOWNLOADING);
				pipe->Write(wd.getDataPtr(), wd.getDataSize());
        }

        bool writing=false;
        std::string tmpbuf;
        int lastinfo=0;
        bool chunked=false;
        int chunksize=-1;
        bool exit=false;

        while(bytes>0 && exit==false)
        {
                timeval timeout3;
		timeout3.tv_sec=timeout.tv_sec*5;
		timeout3.tv_usec=0;
		error=select(Cs+1,&conn,NULL,NULL,&timeout3);
                if(error>0)
                {
                        bytes = recv(Cs, Buffer, BUFFERSIZE,  MSG_NOSIGNAL);

                        if( writing==true )
                        {
                                if( chunked==false )
                                {
                                        out.write(Buffer,bytes);
                                        totalbytes+=bytes;
                                }
                                else
                                {
                                        int off=0;
                                        if( chunksize!=-1 )
                                        {
                                                if( chunksize-bytes>0 )
                                                {
                                                        out.write(Buffer, bytes);
                                                        totalbytes+=bytes;
                                                        chunksize-=bytes;
                                                }
                                                else
                                                {
                                                        out.write(Buffer, chunksize);
                                                        totalbytes+=chunksize;
                                                        off=chunksize;
                                                        tmpbuf.clear();
                                                        chunksize=-1;
                                                 }
                                        }
                                        if( chunksize==-1 )
                                        {
                                                size_t osize=tmpbuf.size();
                                                tmpbuf.resize( tmpbuf.size()+(bytes-off) );
                                                memcpy(&tmpbuf[osize], &Buffer[off], bytes-off);

                                                std::string csize2=getbetween("\r\n","\r\n", tmpbuf);

                                                if( csize2!="" )
                                                {
                                                        chunksize=(int)hexToULong((char*)csize2.c_str() );
														chunksize=(std::max)(-1, (std::min)(1048576,chunksize) );
                                                        while( chunksize<=(int)(tmpbuf.size()-4-csize2.size()) && chunksize>0)
                                                        {
                                                                out.write(&tmpbuf[4+csize2.size()], chunksize);
                                                                out.flush();
                                                                totalbytes+=chunksize;
                                                                tmpbuf.erase(0,4+csize2.size()+chunksize);

                                                                csize2=getbetween("\r\n","\r\n", tmpbuf);

                                                                if( csize2!="" )
                                                                {
                                                                        chunksize=hexToULong((char*)csize2.c_str() );
																		chunksize=(std::max)(-1, (std::min)(1048576,chunksize) );
                                                                }
                                                                else
                                                                        chunksize=-1;

                                                        }
                                                        
                                                        if( chunksize==0 )
                                                        {
                                                                exit=true;
                                                        }
                                                        else if( chunksize>0 )
                                                        {
                                                                out.write(&tmpbuf[4+csize2.size()], tmpbuf.size()-4-csize2.size());
                                                                out.flush();
                                                                chunksize-=tmpbuf.size()-4-csize2.size();
                                                                totalbytes+=(int)(tmpbuf.size()-4-csize2.size());
                                                                tmpbuf.clear();
                                                        }
                                                }
                                        }
                                }
                        }
                        else
                        {
                                size_t osize=tmpbuf.size();
                                tmpbuf.resize( tmpbuf.size()+bytes);
								if(bytes>0)
								{
									memcpy(&tmpbuf[osize], Buffer, bytes);
								}

                                if( Buffer[bytes]==0 )
                                {
                                        int xsds=2;
                                }

                                size_t offadd=4;
                                size_t off=tmpbuf.find("\r\n\r\n");
                                if( off==std::string::npos )
                                {
                                        off=tmpbuf.find("\n\n");
                                        offadd=2;
                                }

                                if( off!=std::string::npos && off+offadd<tmpbuf.size())
                                {
                                        std::string length=getbetween("Content-Length: ","\n", tmpbuf);

                                        std::string error_code=getbetween("HTTP/1.1 "," ",tmpbuf);

                                        if( error_code=="" )
                                                error_code=getbetween("HTTP/1.0 "," ",tmpbuf);

                                        if( trim(getbetween("Transfer-Encoding:", "\r\n", tmpbuf))=="chunked" )
                                        {
                                                chunked=true;
                                        }

                                        if( error_code!="200" )
                                        {
                                                CWData wd;
												wd.addUChar(DL2_ERROR);
                                                wd.addUChar(DL2_ERR_404);
												pipe->Write(wd.getDataPtr(), wd.getDataSize());

                                                out.close();

                                                delete[] Buffer;

                                                closesocket(Cs);

                                                _unlink(filename.c_str() );
                                                
                                                return false;
                                                
                                        }

                                        if( length!="" )
                                        {
                                                int dlength=atoi( length.c_str() );

                                                CWData wd;
												wd.addUChar(DL2_CONTENT_LENGTH);
                                                wd.addInt(dlength);
												pipe->Write(wd.getDataPtr(), wd.getDataSize());
                                        }

                                        if( chunked==true )
                                        {
                                                std::string csize1=getbetween("\r\n\r\n","\r\n", tmpbuf);
                                                if( csize1!="" )
                                                {
                                                        chunksize=hexToULong((char*)csize1.c_str() );
                                                        chunksize=(std::max)(-1, (std::min)(1048576,chunksize) );
                                                }
                                                offadd+=csize1.size()+2;
                                        }

                                        if( chunked==false )
                                        {
                                                out.write(&tmpbuf[off+offadd], tmpbuf.size()-off-offadd );
                                                totalbytes+=(int)(tmpbuf.size()-off-offadd);
                                                writing=true;
                                        }
                                        else if( chunksize!=-1 )
                                        {
                                                while( (int)(tmpbuf.size()-off-offadd)>=chunksize && chunksize>0 )
                                                {
                                                        out.write(&tmpbuf[off+offadd], chunksize);
                                                        out.flush();
                                                        totalbytes+=chunksize;
                                                        tmpbuf.erase(0, off+offadd+chunksize);
                                                        std::string ncsize=getbetween("\r\n", "\r\n", tmpbuf);
                                                        if( ncsize!="" )
                                                        {
                                                                chunksize=hexToULong((char*)ncsize.c_str() );
                                                                chunksize=(std::max)(-1, (std::min)(1048576,chunksize) );
                                                                off=4+ncsize.size();
                                                                offadd=0;
                                                        }
                                                        else
                                                        {
                                                                chunksize=-1;
                                                        }
                                                }

                                                if(chunksize>0 )
                                                {
                                                        out.write(&tmpbuf[off+offadd], tmpbuf.size()-off-offadd );
                                                        out.flush();
                                                        chunksize-=tmpbuf.size()-off-offadd;
                                                        totalbytes+=(int)(tmpbuf.size()-off-offadd);
							tmpbuf.clear();                                                
                                                }
                                                else if( chunksize==0 )
                                                {
                                                        exit=true;
                                                }

                                                writing=true;
                                        }
                                }
                        }
                        {
                                CWData wd;
								wd.addUChar(DL2_BYTES);
                                wd.addInt(totalbytes);
								pipe->Write(wd.getDataPtr(), wd.getDataSize());
                        }
                }
                else
                {
                        CWData wd;
						wd.addUChar(DL2_ERROR);
                        wd.addUChar(DL2_ERR_TIMEOUT);
						pipe->Write(wd.getDataPtr(), wd.getDataSize());

                        bytes=0;
                }
        }

        out.close();

        delete[] Buffer;

        closesocket(Cs);

        if(totalbytes!=0)
        {
                return true;
        }
        else
        {
                CWData wd;
				wd.addUChar(DL2_ERROR);
                wd.addInt(DL2_ERR_NORESPONSE);
				pipe->Write(wd.getDataPtr(), wd.getDataSize());
                return false;
        }
}

CFileDownload::CFileDownload()
{
        dlthread=NULL;
        pipe=NULL;

        content_length=0;
        lbytes_received=0;
        received=0;
}

CFileDownload::~CFileDownload()
{
		if(dlthread!=NULL)
		{
			Server->getThreadPool()->waitFor(dlthread_ticket);
			delete dlthread;
		}
		if(pipe!=NULL)
			Server->destroy(pipe);
}

void CFileDownload::setProxy(  std::string pProxy, unsigned short pProxyport )
{
        proxy=pProxy;
        proxyport=pProxyport;
}

void CFileDownload::download( std::string pUrl, std::string pFilename )
{
        url=pUrl;
        filename=pFilename;

        if( dlthread!=NULL )
		{
				Server->getThreadPool()->waitFor(dlthread_ticket);
                delete dlthread;
		}
        if( pipe!=NULL )
		{
			Server->destroy(pipe);
		}

        dlthread=new DownloadThread();
        pipe=Server->createMemoryPipe();

        dlthread->init( url, filename, pipe, proxy, proxyport);

		dlthread_ticket=Server->getThreadPool()->execute(dlthread);
}

uchar CFileDownload::download(int wait)
{
        uchar id=0;
		std::string data;
		pipe->Read(&data, wait);
		if(!data.empty())
		{
			CRData rd(data.c_str(), data.size());
			rd.getUChar(&id);
			switch(id)
			{
					case DL2_ERROR:
					{
							uchar eid;
							rd.getUChar(&eid);

							if( eid==DL2_ERR_TIMEOUT )
							{
									return FD_ERR_TIMEOUT;
							}
							else if( eid==DL2_ERR_404 )
							{
									return FD_ERR_FILE_DOESNT_EXIST;
							}
							else
							{
									return FD_ERR_ERROR;
							}
					}
					case DL2_INFO:
					{
							uchar iid;
							rd.getUChar(&iid);

							if( iid==DL2_INFO_DOWNLOADING )
							{
									return FD_ERR_CONNECTED;
							}

					}break;
					case DL2_DONE:
					{
							uchar b;
							rd.getUChar(&b);

							if( b==0 )
									return FD_ERR_ERROR;
							else
									return FD_ERR_SUCCESS;
					}
					case DL2_CONTENT_LENGTH:
					{
							rd.getInt(&content_length);

							return FD_ERR_CONTENT_LENGTH;
					}
					case DL2_BYTES:
					{
							rd.getInt(&received);

							return FD_ERR_QUEUE_ITEMS;
					}break;
			}
		}

        return FD_ERR_CONTINUE;
}

int CFileDownload::getContentLength(void)
{
        return content_length;
}

int CFileDownload::getDownloadedBytes(void)
{
        return received;
}

int CFileDownload::getNewDownloadedBytes(void)
{
        int ndb=received-lbytes_received;
        lbytes_received=received;
        return ndb;
}

std::string CFileDownload::getErrorString(uchar err)
{
	std::string errmsg="NOT_DEFINED";
#define ERR2STR(x) if(err==FD_ERR_ ## x) errmsg=#x

	ERR2STR(CONTINUE);
	ERR2STR(SUCCESS);
	ERR2STR(TIMEOUT);
	ERR2STR(FILE_DOESNT_EXIST);
	ERR2STR(SOCKET_ERROR);
	ERR2STR(CONNECTED);
	ERR2STR(ERROR);
	ERR2STR(CONTENT_LENGTH);
	ERR2STR(QUEUE_ITEMS);

	return errmsg;
}