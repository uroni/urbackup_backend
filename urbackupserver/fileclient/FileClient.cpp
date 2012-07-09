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

#ifndef CLIENT_ONLY

#include "../../Interface/Server.h"

#include "FileClient.h"

#include "../../urbackupcommon/fileclient/data.h"
#include "../../stringtools.h"

#include "../../md5.h"

#include <iostream>
#include <memory.h>

const std::string str_tmpdir="C:\\Windows\\Temp";
extern std::string server_identity;
const _u64 c_checkpoint_dist=512*1024;

void Log(std::string str)
{
	Server->Log(str);
}

int curr_fnum=0;

bool setSockP(SOCKET sock)
{
#ifdef _WIN32
		DWORD dwBytesReturned = 0;
		BOOL bNewBehavior = FALSE;
		int status;

		// disable  new behavior using
		// IOCTL: SIO_UDP_CONNRESET
		#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
		status = WSAIoctl(sock, SIO_UDP_CONNRESET,
						&bNewBehavior, sizeof(bNewBehavior),
					NULL, 0, &dwBytesReturned,
					NULL, NULL);
		if (SOCKET_ERROR == status)
		{
			return false;
		}
#endif
        return true;
}

_i32 selectnull(SOCKET socket)
{
        fd_set fdset;

        FD_ZERO(&fdset);

        FD_SET(socket, &fdset);

        timeval lon;
        lon.tv_usec=0;
        lon.tv_sec=0;
        return select((int)socket+1, &fdset, 0, 0, &lon);
}

_i32 selectmin(SOCKET socket)
{
        fd_set fdset;

        FD_ZERO(&fdset);

        FD_SET(socket, &fdset);

        timeval lon;
        lon.tv_sec=0;
        lon.tv_usec=10000;
        return select((int)socket+1, &fdset, 0, 0, &lon);
}

_i32 selectc(SOCKET socket, int usec)
{
        fd_set fdset;

        FD_ZERO(&fdset);

        FD_SET(socket, &fdset);

        timeval lon;
        lon.tv_sec=0;
        lon.tv_usec=usec;
        return select((int)socket+1, &fdset, 0, 0, &lon);
}
       

FileClient::FileClient(int protocol_version, bool internet_connection, FileClient::ReconnectionCallback *reconnection_callback)
	: protocol_version(protocol_version), internet_connection(internet_connection), transferred_bytes(0), reconnection_callback(reconnection_callback)
{
        udpsock=socket(AF_INET,SOCK_DGRAM,0);

        setSockP(udpsock);

        BOOL val=TRUE;
        setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, (char*)&val, sizeof(BOOL) );      

        socket_open=false;

		stack.setAddChecksum(internet_connection);
}

FileClient::~FileClient(void)
{
	if(socket_open && tcpsock!=NULL)
	{
		Server->destroy(tcpsock);
	}
	closesocket(udpsock);
}

std::vector<sockaddr_in> FileClient::getServers(void)
{
        return servers;
}

std::vector<std::wstring> FileClient::getServerNames(void)
{
        return servernames;
}

std::vector<sockaddr_in> FileClient::getWrongVersionServers(void)
{
        return wvservers;
}

_u32 FileClient::getLocalIP(void)
{
        return local_ip;
}

_u32 FileClient::GetServers(bool start, const std::vector<in_addr> &addr_hints)
{
        if(start==true)
        {
				max_version=0;
#ifdef _WIN32                
                //get local ip address
                char hostname[MAX_PATH];
                struct    hostent* h;
                _u32     address;

                _i32 rc=gethostname(hostname, MAX_PATH);
                if(rc==SOCKET_ERROR)
                        return 0;

                std::vector<_u32> addresses;

                if(NULL != (h = gethostbyname(hostname)))
                {
                for(_u32 x = 0; (h->h_addr_list[x]); x++)
                {
               
                        ((uchar*)(&address))[0] = h->h_addr_list[x][0];
                        ((uchar*)(&address))[1] = h->h_addr_list[x][1];
                        ((uchar*)(&address))[2] = h->h_addr_list[x][2];
                        ((uchar*)(&address))[3] = h->h_addr_list[x][3];
                        ((uchar*)(&address))[3]=255;
                        addresses.push_back(address);
                        local_ip=address;
                }
                }

                sockaddr_in addr_udp;

                ((uchar*)(&address))[0]=255;
                ((uchar*)(&address))[1]=255;
                ((uchar*)(&address))[2]=255;
                ((uchar*)(&address))[3]=255;

                addr_udp.sin_family=AF_INET;
                addr_udp.sin_port=htons(UDP_PORT);
                addr_udp.sin_addr.s_addr=address;

                char ch=ID_PING;
                sendto(udpsock, &ch, 1, 0, (sockaddr*)&addr_udp, sizeof(sockaddr_in) );

                for(size_t i=0;i<addresses.size();++i)
                {
                        addr_udp.sin_addr.s_addr=addresses[i];
                        sendto(udpsock, &ch, 1, 0, (sockaddr*)&addr_udp, sizeof(sockaddr_in) );
                }

				for(size_t i=0;i<addr_hints.size();++i)
				{
					addr_udp.sin_addr.s_addr=addr_hints[i].s_addr;
					sendto(udpsock, &ch, 1, 0, (sockaddr*)&addr_udp, sizeof(sockaddr_in) );
				}
#else
		int broadcast=1;
		if(setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(int))==-1)
		{
			Server->Log("Error setting socket to broadcast", LL_ERROR);
		}
		
		sockaddr_in addr_udp;
		addr_udp.sin_family=AF_INET;
		addr_udp.sin_port=htons(UDP_PORT);
		addr_udp.sin_addr.s_addr=inet_addr("255.255.255.255");
		memset(addr_udp.sin_zero,0, sizeof(addr_udp.sin_zero));
		
		char ch=ID_PING;
		int rc=sendto(udpsock, &ch, 1, 0, (sockaddr*)&addr_udp, sizeof(sockaddr_in));
		if(rc==-1)
		{
			Server->Log("Sending broadcast failed!", LL_ERROR);
		}

		broadcast=0;
		if(setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(int))==-1)
		{
			Server->Log("Error setting socket to not broadcast", LL_ERROR);
		}

		for(size_t i=0;i<addr_hints.size();++i)
		{
			addr_udp.sin_addr.s_addr=addr_hints[i].s_addr;
			sendto(udpsock, &ch, 1, 0, (sockaddr*)&addr_udp, sizeof(sockaddr_in) );
		}
#endif

                starttime=Server->getTimeMS();

                servers.clear();
				servernames.clear();
                wvservers.clear(); 


                return ERR_CONTINUE;
        }
        else
        {
            _i32 rc = selectmin( udpsock );

        	if(rc>0)
	        {
		        socklen_t addrsize=sizeof(sockaddr_in);
		        sockaddr_in sender;
		        _i32 err = recvfrom(udpsock, buffer, BUFFERSIZE_UDP, 0, (sockaddr*)&sender, &addrsize);
		        if(err==SOCKET_ERROR)
		        {
			        return ERR_ERROR;
        		}
                        if(err>2&&buffer[0]==ID_PONG)
                        {
                                int version=(unsigned char)buffer[1];
                                if(version==VERSION)
                                {
                                        servers.push_back(sender);

                                        std::string sn;
                                        sn.resize(err-2);
                                        memcpy((char*)sn.c_str(), &buffer[2], err-2);

										servernames.push_back(Server->ConvertToUnicode(sn));
                                }
                                else
                                {
                                        wvservers.push_back(sender);
                                }
                                
                                if( version>max_version )
                                {
                                        max_version=version;
                                }
                        }
                }

                if(Server->getTimeMS()-starttime>1000)
                {
                        return ERR_TIMEOUT;
                }
                else
                        return ERR_CONTINUE;

        }

        
}

int FileClient::getMaxVersion(void)
{
        return max_version;        
}

_u32 FileClient::Connect(sockaddr_in *addr)
{
	if( socket_open==true )
    {
            Server->destroy(tcpsock);
    }

	tcpsock=Server->ConnectStream(inet_ntoa(addr->sin_addr), TCP_PORT, 10000);

	if(tcpsock!=NULL)
	{
		socket_open=true;

		for(size_t i=0;i<throttlers.size();++i)
		{
			tcpsock->addThrottler(throttlers[i]);
		}
	}

	server_addr=*addr;

    if(tcpsock==NULL)
		return ERR_ERROR;
	else
		return ERR_CONNECTED;
}    

void FileClient::addThrottler(IPipeThrottler *throttler)
{
	throttlers.push_back(throttler);
	if(tcpsock!=NULL)
	{
		tcpsock->addThrottler(throttler);
	}
}

_u32 FileClient::Connect(IPipe *cp)
{
	if( socket_open==true )
    {
            Server->destroy(tcpsock);
    }

	tcpsock=cp;

	if(tcpsock!=NULL)
	{
		socket_open=true;
	}

	if(tcpsock==NULL)
		return ERR_ERROR;
	else
		return ERR_CONNECTED;
}

_u32 FileClient::GetGameList(void)
{

		CWData data;
		data.addUChar(ID_GET_GAMELIST);
		data.addString( server_identity );

		stack.reset();
		stack.Send(tcpsock, data.getDataPtr(), data.getDataSize() );

		starttime=Server->getTimeMS();
		num_games_res=false;
		num_games_get=0;

		while(true)
		{
			size_t rc=tcpsock->Read(buffer, BUFFERSIZE_UDP, 10000);

			if(rc==0)
				return ERR_ERROR;

			starttime=Server->getTimeMS();

            stack.AddData(buffer, rc);
                                
			char *packet;
			size_t packetsize;
			while( (packet=stack.getPacket(&packetsize) )!=NULL )
			{
					if( packetsize>1 && packet[0]==ID_GAMELIST && num_games_res==false)
					{
							CRData data(&packet[1], packetsize);

							if( !data.getUInt(&num_games) )
							{
									delete [] packet;
									return ERR_ERROR;
							}

							res_name=true;
							num_games_res=true;
							num_games_get=0;

							if( num_games==0 )
							{
									delete [] packet;
									return ERR_SUCCESS;
							}
					}
					else if( num_games_res==true )
					{
							if( res_name==true )
							{
									games.push_back(&packet[0]);
									res_name=false;
							}
							else
							{
									writestring(packet, (unsigned int)packetsize, str_tmpdir+conv_filename(mServerName+"-"+games[ games.size()-1]) );
									res_name=true;
									num_games_get++;
									if( num_games_get==num_games )
									{
											delete [] packet;
											return ERR_SUCCESS;
									}
							}
					}
					delete []packet;
			}
               
			if( Server->getTimeMS()-starttime>10000)
            {
                    return ERR_TIMEOUT;
            }
        }
}

bool FileClient::ListDownloaded(void)
{
        if( num_games_get==num_games )
        {
                return true;
        }
        else
                return false;
}

std::vector<std::string> FileClient::getGameList(void)
{
        return games;
}

void FileClient::setServerName(std::string pName)
{
        mServerName=pName;
}

std::string FileClient::getServerName(void)
{
        return mServerName;
}

bool FileClient::isConnected(void)
{
        return connected;
}

bool FileClient::Reconnect(void)
{
	transferred_bytes+=tcpsock->getTransferedBytes();
	Server->destroy(tcpsock);
	connect_starttime=Server->getTimeMS();

	while(Server->getTimeMS()-connect_starttime<300000)
	{
		if(reconnection_callback==NULL)
		{
			tcpsock=Server->ConnectStream(inet_ntoa(server_addr.sin_addr), TCP_PORT, 10000);
		}
		else
		{
			tcpsock=reconnection_callback->new_fileclient_connection();
		}
		if(tcpsock!=NULL)
		{
			for(size_t i=0;i<throttlers.size();++i)
			{
				tcpsock->addThrottler(throttlers[i]);
			}
			Server->Log("Reconnected successfully,", LL_DEBUG);
			socket_open=true;
			return true;
		}
		else
		{
			Server->wait(1000);
		}
	}
	Server->Log("Reconnecting failed.", LL_DEBUG);
	socket_open=false;
	return false;
}

 _u32 FileClient::GetFile(std::string remotefn, IFile *file)
{
	if(tcpsock==NULL)
		return ERR_ERROR;

	int tries=50;


    CWData data;
    data.addUChar( protocol_version>1?ID_GET_FILE_RESUME_HASH:ID_GET_FILE );
    data.addString( remotefn );
	data.addString( server_identity );

    stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );

	_u64 filesize=0;
	_u64 received=0;
	_u64 next_checkpoint=c_checkpoint_dist;
	_u64 last_checkpoint=0;
	bool firstpacket=true;

	if(file==NULL)
		return ERR_ERROR;

    starttime=Server->getTimeMS();

	char buf[BUFFERSIZE];
	int state=0;
	char hash_buf[16];
	_u32 hash_r;
	MD5 hash_func;


	while(true)
	{        
		size_t rc=tcpsock->Read(buf, BUFFERSIZE, 120000);

        if( rc==0 )
        {
			Server->Log("Server timeout (2) in FileClient", LL_DEBUG);
			bool b=Reconnect();
			--tries;
			if(!b || tries<=0 )
			{
				Server->Log("FileClient: ERR_TIMEOUT", LL_INFO);
				return ERR_TIMEOUT;
			}
			else
			{
				CWData data;
				data.addUChar( protocol_version>1?ID_GET_FILE_RESUME_HASH:(protocol_version>0?ID_GET_FILE_RESUME:ID_GET_FILE) );
				data.addString( remotefn );
				data.addString( server_identity );

				if( protocol_version>1 )
				{
					received=last_checkpoint;
				}

				if( firstpacket==false )
					data.addInt64( received ); 

				file->Seek(received);

				rc=stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );
				if(rc==0)
				{
					Server->Log("FileClient: Error sending request", LL_INFO);
				}
				starttime=Server->getTimeMS();

				if(protocol_version>0)
					firstpacket=true;

				hash_func.init();
				state=0;
			}
		}
        else
        {
			starttime=Server->getTimeMS();

			_u32 off=0;
			uchar PID=buf[0];
                        
            if( firstpacket==true)
            {
                    if(PID==ID_COULDNT_OPEN)
                    {
                        return ERR_FILE_DOESNT_EXIST;
                    }
					else if(PID==ID_BASE_DIR_LOST)
					{
						return ERR_BASE_DIR_LOST;
					}
                    else if(PID==ID_FILESIZE && rc >= 1+sizeof(_u64))
                    {
                            memcpy(&filesize, buf+1, sizeof(_u64) );
                            off=1+sizeof(_u64);

                            if( filesize==0 )
                            {
                                    return ERR_SUCCESS;
                            }

							if(protocol_version>1)
							{
								if(filesize<next_checkpoint)
									next_checkpoint=filesize;
							}
							else
							{
								next_checkpoint=filesize;
							}
                    }
                    firstpacket=false;
            }

			if( state==1 && (_u32) rc > off )
			{
				_u32 tc=(std::min)((_u32)rc-off, hash_r);
				memcpy(&hash_buf[16-hash_r], &buf[off],  tc);
				off+=tc;
				hash_r-=tc;

				if(hash_r==0)
				{
					hash_func.finalize();
					if(memcmp(hash_func.raw_digest_int(), hash_buf, 16)!=0)
					{
						Server->Log("Error while downloading file: hash wrong -1", LL_ERROR);
						return ERR_HASH;
					}
					hash_func.init();
					state=0;
				}

				if(received >= filesize && state==0)
				{
					return ERR_SUCCESS;
				}
			}

            if( state==0 && (_u32) rc > off )
            {
				_u32 written=off;
				_u64 write_remaining=next_checkpoint-received;
				_u32 hash_off=0;
				bool c=true;
				while(c)
				{
					c=false;
					while(written<rc)
					{
						_u32 tw=(_u32)rc-written;
						if((_u64)tw>write_remaining)
							tw=(_u32)write_remaining;

						_u32 cw=file->Write(&buf[written], tw);
						hash_func.update((unsigned char*)&buf[written], cw);
						written+=cw;
						write_remaining-=cw;
						received+=cw;
						if(write_remaining==0)
							break;
						if(written<rc)
						{
							Server->Log("Failed to write to file... waiting...", LL_WARNING);
							Server->wait(10000);
							starttime=Server->getTimeMS();
						}
					}

					if(write_remaining==0 && protocol_version>1) 
					{
						if(next_checkpoint<filesize)
						{
							last_checkpoint=next_checkpoint;
						}
						next_checkpoint+=c_checkpoint_dist;
						if(next_checkpoint>filesize)
							next_checkpoint=filesize;

						hash_r=(_u32)rc-written;
						if(hash_r>0)
						{
							memcpy(hash_buf, &buf[written], (std::min)(hash_r, (_u32)16));

							if(hash_r>16)
							{
								hash_r=16;
								c=true;
								write_remaining=next_checkpoint-received;
								written+=16;
							}
						}
						else
						{
							int asfsf=3;
						}

						hash_off+=hash_r;

						if(hash_r<16)
						{
							hash_r=16-hash_r;
							state=1;
						}
						else
						{
							hash_func.finalize();
							if(memcmp(hash_func.raw_digest_int(), hash_buf, 16)!=0)
							{
								Server->Log("Error while downloading file: hash wrong -2", LL_ERROR);
								return ERR_HASH;
							}
							hash_func.init();
						}
					}
				}

				if( received >= filesize && state==0)
                {
					return ERR_SUCCESS;
				}
            }
		}
            
	    if( Server->getTimeMS()-starttime > SERVER_TIMEOUT )
		{
				Server->Log("Server timeout in FileClient. Trying to reconnect...", LL_INFO);
				bool b=Reconnect();
				--tries;
				if(!b || tries<=0 )
				{
					Server->Log("FileClient: ERR_TIMEOUT", LL_INFO);
					return ERR_TIMEOUT;
				}
				else
				{
					CWData data;
					data.addUChar( protocol_version>0?ID_GET_FILE_RESUME:ID_GET_FILE );
					data.addString( remotefn );
					data.addString( server_identity );

					if( protocol_version>1 )
					{
						received=last_checkpoint;
					}

					if( firstpacket==false )
						data.addInt64( received ); 

					file->Seek(received);

					stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );
					starttime=Server->getTimeMS();

					if(protocol_version>0)
						firstpacket=true;

					hash_func.init();
					state=0;
				}
		}
	}
}
        
_i64 FileClient::getTransferredBytes(void)
{
	if(tcpsock!=NULL)
	{
		transferred_bytes+=tcpsock->getTransferedBytes();
		tcpsock->resetTransferedBytes();
	}
	return transferred_bytes;
}

std::string FileClient::getErrorString(_u32 ec)
{
#define DEFEC(x) case ERR_##x : return #x;
	switch(ec)
	{
	DEFEC(CONTINUE);
	DEFEC(SUCCESS);
	DEFEC(TIMEOUT);
	DEFEC(FILE_DOESNT_EXIST);
	DEFEC(SOCKET_ERROR);
	DEFEC(CONNECTED);
	DEFEC(ERROR);
	DEFEC(BASE_DIR_LOST);
	DEFEC(HASH);
	DEFEC(INT_ERROR);
	DEFEC(CONN_LOST);
	}
#undef DEFEC
	return "";
}
        
#endif //CLIENT_ONLY

