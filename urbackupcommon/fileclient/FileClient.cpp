/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "../../Interface/Server.h"

#include "FileClient.h"

#include "../../fileservplugin/chunk_settings.h"

#include "../../common/data.h"
#include "../../stringtools.h"

#include "../../md5.h"
#include "../SparseFile.h"

#include <iostream>
#include <memory.h>
#include <algorithm>
#include <assert.h>
#include <limits.h>
#include <cstring>
#include <memory>

#ifndef _WIN32
#include <errno.h>
#endif

namespace
{
	const std::string str_tmpdir="C:\\Windows\\Temp";
#ifndef _DEBUG
	const unsigned int DISCOVERY_TIMEOUT=30000; //30sec
#else
	const unsigned int DISCOVERY_TIMEOUT=1000; //1sec
#endif

	const size_t maxQueuedFiles = 3000;
	const size_t queuedFilesLow = 100;

	std::string ipToString(sockaddr_in sa)
	{
		char str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(sa.sin_addr), str, INET_ADDRSTRLEN);
		return str;
	}
}

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

FileClient::FileClient(bool enable_find_servers, std::string identity, int protocol_version, bool internet_connection,
	FileClient::ReconnectionCallback *reconnection_callback, FileClient::NoFreeSpaceCallback *nofreespace_callback)
	: tcpsock(NULL), starttime(0), connect_starttime(0), socket_open(false), connected(false),
	serveraddr(), local_ip(),
	max_version(), server_addr(), connection_id(), 
	protocol_version(protocol_version), internet_connection(internet_connection),
	transferred_bytes(0), reconnection_callback(reconnection_callback),
	nofreespace_callback(nofreespace_callback), reconnection_timeout(300000), retryBindToNewInterfaces(true),
	identity(identity), received_data_bytes(0), queue_callback(NULL), dl_off(0),
	last_transferred_bytes(0), last_progress_log(0), progress_log_callback(NULL), needs_flush(false),
	real_transferred_bytes(0), is_downloading(false), sparse_extends_f(NULL), sparse_bytes(0)
{
	memset(buffer, 0, BUFFERSIZE_UDP);

	if(enable_find_servers)
	{
		bindToNewInterfaces();
	}

	socket_open=false;
	stack.setAddChecksum(internet_connection);

	mutex = Server->createMutex();
}

void FileClient::bindToNewInterfaces()
{
	std::string s_broadcast_source_port = Server->getServerParameter("broadcast_source_port");
	unsigned short broadcast_source_port = UDP_SOURCE_PORT;
	if(!s_broadcast_source_port.empty())
	{
		broadcast_source_port = static_cast<unsigned short>(atoi(s_broadcast_source_port.c_str()));
	}

	#ifndef _WIN32
	std::string bcast_interfaces=Server->getServerParameter("broadcast_interfaces", "");

	std::vector<std::string> bcast_filter;
	if(!bcast_interfaces.empty())
	{
		Tokenize(bcast_interfaces, bcast_filter, ";,");
	}

	ifaddrs *start_ifap;
	int rc=getifaddrs(&start_ifap);
	if(rc==0)
	{
		ifaddrs* ifap=start_ifap;
		for(;ifap!=NULL;ifap=ifap->ifa_next)
		{
			bool found_name = bcast_filter.empty() || std::find(bcast_filter.begin(), bcast_filter.end(), ifap->ifa_name)!=bcast_filter.end();

			if(found_name &&
				!(ifap->ifa_flags & IFF_LOOPBACK) 
				&& !(ifap->ifa_flags & IFF_POINTOPOINT) 
				&&  (ifap->ifa_flags & IFF_BROADCAST)
				&&  ifap->ifa_addr->sa_family == AF_INET )
			{			
				sockaddr_in source_addr;
				memset(&source_addr, 0, sizeof(source_addr));
				source_addr.sin_addr=((struct sockaddr_in *)ifap->ifa_addr)->sin_addr;
				source_addr.sin_family = AF_INET;
				source_addr.sin_port = htons(broadcast_source_port);

				if(std::find(broadcast_iface_addrs.begin(), broadcast_iface_addrs.end(), source_addr.sin_addr.s_addr)!=broadcast_iface_addrs.end())
					continue;

				int type = SOCK_DGRAM;
#if defined(SOCK_CLOEXEC)
				type |= SOCK_CLOEXEC;
#endif
				SOCKET udpsock=socket(AF_INET, type,0);
				if(udpsock==-1)
				{
					Server->Log(std::string("Error creating socket for interface ")+std::string(ifap->ifa_name), LL_ERROR);
					continue;
				}

				BOOL val=TRUE;
				int rc = setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(BOOL));
				if(rc<0)
				{
					Server->Log(std::string("Setting SO_REUSEADDR failed for interface ")+std::string(ifap->ifa_name), LL_ERROR);
				}

				Server->Log(std::string("Binding to interface ")+std::string(ifap->ifa_name)+" for broadcasting...", LL_DEBUG);

				rc = bind(udpsock, (struct sockaddr *)&source_addr, sizeof(source_addr));
				if(rc<0)
				{
					Server->Log(std::string("Binding UDP socket failed for interface ")+std::string(ifap->ifa_name), LL_ERROR);
				}
				
				rc = setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, (char*)&val, sizeof(BOOL) );
				if(rc<0)
				{
					Server->Log(std::string("Enabling SO_BROADCAST for UDP socket failed for interface ")+std::string(ifap->ifa_name), LL_ERROR);
					closesocket(udpsock);
					continue;
				}

				#if defined(__FreeBSD__)
				int optval=1;
				if(setsockopt(udpsock, IPPROTO_IP, IP_ONESBCAST, &optval, sizeof(int))==-1)
				{
					Server->Log(std::string("Error setting IP_ONESBCAST for interface " )+std::string(ifap->ifa_name), LL_ERROR);
				}
				#endif

				broadcast_iface_addrs.push_back(source_addr.sin_addr.s_addr);
				broadcast_addrs.push_back(*((struct sockaddr_in *)ifap->ifa_broadaddr));
				udpsocks.push_back(udpsock);

				Server->Log("Broadcasting on interface IP "+ ipToString(source_addr));
			}
		}
		freeifaddrs(start_ifap);
	}
	else
	{
		retryBindToNewInterfaces=false;

		Server->Log("Getting interface ips failed. errno="+convert(errno)+
			". Server may not listen properly on all network devices when discovering clients.", LL_ERROR);

		int type = SOCK_DGRAM;
#if defined(SOCK_CLOEXEC)
		type |= SOCK_CLOEXEC;
#endif
		SOCKET udpsock=socket(AF_INET, type,0);
		if(udpsock==-1)
		{
			Server->Log("Error creating socket", LL_ERROR);
		}
		else
		{
			BOOL val=TRUE;
			int rc = setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(BOOL));
			if(rc<0)
			{
				Server->Log("Setting SO_REUSEADDR failed", LL_ERROR);
			}

			sockaddr_in source_addr;
			memset(&source_addr, 0, sizeof(source_addr));
			source_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			source_addr.sin_family = AF_INET;
			source_addr.sin_port = htons(broadcast_source_port);

			Server->Log("Binding to no interface for broadcasting. Entering IP on restore CD won't work.", LL_WARNING);

			rc = setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, (char*)&val, sizeof(BOOL) );
			if(rc<0)
			{
				Server->Log("Enabling SO_BROADCAST for UDP socket failed", LL_ERROR);
				closesocket(udpsock);
			}
			else
			{
				udpsocks.push_back(udpsock);
				source_addr.sin_addr.s_addr = INADDR_BROADCAST;
				broadcast_addrs.push_back(source_addr);
				broadcast_iface_addrs.push_back(source_addr.sin_addr.s_addr);

				Server->Log("Broadcasting on interface IP "+ ipToString(source_addr));
			}
		}
	}
#else
	char hostname[MAX_PATH];
    struct    hostent* h;

    _i32 rc=gethostname(hostname, MAX_PATH);
    if(rc==SOCKET_ERROR)
	{
		Server->Log("Error getting Hostname", LL_ERROR);
		retryBindToNewInterfaces=false;
        return;
	}
	std::vector<_u32> addresses;

    if(NULL != (h = gethostbyname(hostname)))
    {
		if(h->h_addrtype!=AF_INET)
		{
			Server->Log("Hostname hostent is not AF_INET (ipv4)", LL_ERROR);
		}
		else
		{
			for(_u32 x = 0; (h->h_addr_list[x]); x++)
			{
				sockaddr_in source_addr;
				memset(&source_addr, 0, sizeof(source_addr));
				source_addr.sin_family = AF_INET;
				source_addr.sin_addr.s_addr = *((_u32*)h->h_addr_list[x]);
				source_addr.sin_port = htons(broadcast_source_port);

				if(std::find(broadcast_iface_addrs.begin(), broadcast_iface_addrs.end(), source_addr.sin_addr.s_addr)!=broadcast_iface_addrs.end())
					continue;

				SOCKET udpsock=socket(AF_INET,SOCK_DGRAM,0);	

				int optval=1;
				int rc=setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
				if(rc==SOCKET_ERROR)
				{
					Server->Log("Failed setting SO_REUSEADDR in FileClient", LL_ERROR);
				}

				rc = bind(udpsock, (struct sockaddr *)&source_addr, sizeof(source_addr));
				if(rc<0)
				{
					Server->Log("Binding UDP socket failed", LL_ERROR);
				}

				setSockP(udpsock);

				BOOL val=TRUE;
				rc=setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, (char*)&val, sizeof(BOOL) );      
				if(rc<0)
				{
					Server->Log("Failed setting SO_BROADCAST in FileClient", LL_ERROR);
				}

				udpsocks.push_back(udpsock);
				broadcast_iface_addrs.push_back(source_addr.sin_addr.s_addr);

				Server->Log("Broadcasting on interface IP "+ ipToString(source_addr));
			}
		}				
    }
#endif
}

FileClient::~FileClient(void)
{
	if(socket_open && tcpsock!=NULL)
	{
		Server->destroy(tcpsock);
	}
	for(size_t i=0;i<udpsocks.size();++i)
	{
		closesocket(udpsocks[i]);
	}

	Server->destroy(mutex);

	if (sparse_extends_f != NULL)
	{
		std::string tmpfn = sparse_extends_f->getFilename();
		Server->destroy(sparse_extends_f);
		Server->deleteFile(tmpfn);
	}
}

std::vector<sockaddr_in> FileClient::getServers(void)
{
        return servers;
}

std::vector<std::string> FileClient::getServerNames(void)
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
			if(retryBindToNewInterfaces)
			{
				bindToNewInterfaces();
			}

			max_version=0;
			for(size_t i=0;i<udpsocks.size();++i)
			{
				sockaddr_in addr_udp;
				addr_udp.sin_family=AF_INET;
				addr_udp.sin_port=htons(UDP_PORT);
#ifdef __FreeBSD__
				addr_udp.sin_addr.s_addr=broadcast_addrs[i].sin_addr.s_addr;
#else
				addr_udp.sin_addr.s_addr=INADDR_BROADCAST;
#endif
				memset(addr_udp.sin_zero,0, sizeof(addr_udp.sin_zero));

				char ch=ID_PING;
				int rc=sendto(udpsocks[i], &ch, 1, 0, (sockaddr*)&addr_udp, sizeof(sockaddr_in));
				if(rc==-1)
				{
					Server->Log("Sending broadcast failed!", LL_ERROR);
				}
			}

			if(!addr_hints.empty())
			{
				for(size_t i=0;i<udpsocks.size();++i)
				{
					int broadcast=0;
#ifdef _WIN32
#define SETSOCK_CAST (char*)
#else
#define SETSOCK_CAST
#endif

					if(setsockopt(udpsocks[i], SOL_SOCKET, SO_BROADCAST, SETSOCK_CAST &broadcast, sizeof(int))==-1)
					{
						Server->Log("Error setting socket to not broadcast", LL_ERROR);
					}

					#if defined(__FreeBSD__)
					int optval=0;
					if(setsockopt(udpsocks[i], IPPROTO_IP, IP_ONESBCAST, &optval, sizeof(int))==-1)
					{
						Server->Log(std::string("Error setting IP_ONESBCAST" ), LL_ERROR);
					}
					#endif

					for(size_t j=0;j<addr_hints.size();++j)
					{
						char ch=ID_PING;
						sockaddr_in addr_udp;
						addr_udp.sin_family=AF_INET;
						addr_udp.sin_port=htons(UDP_PORT);
						addr_udp.sin_addr.s_addr=addr_hints[j].s_addr;
						memset(addr_udp.sin_zero,0, sizeof(addr_udp.sin_zero));

						sendto(udpsocks[i], &ch, 1, 0, (sockaddr*)&addr_udp, sizeof(sockaddr_in) );
					}

					broadcast=1;
					if(setsockopt(udpsocks[i], SOL_SOCKET, SO_BROADCAST, SETSOCK_CAST &broadcast, sizeof(int))==-1)
					{
						Server->Log("Error setting socket to broadcast", LL_ERROR);
					}

#undef SETSOCK_CAST

					#if defined(__FreeBSD__)
					optval=1;
					if(setsockopt(udpsocks[i], IPPROTO_IP, IP_ONESBCAST, &optval, sizeof(int))==-1)
					{
						Server->Log(std::string("Error setting IP_ONESBCAST" ), LL_ERROR);
					}
					#endif
				}
			}

			starttime=Server->getTimeMS();

			servers.clear();
			servernames.clear();
			wvservers.clear(); 


			return ERR_CONTINUE;
        }
        else
        {
#ifdef _WIN32
			fd_set fdset;
			FD_ZERO(&fdset);

			SOCKET max_socket;

			if(!udpsocks.empty())
			{
				max_socket=udpsocks[0];
			}

			for(size_t i=0;i<udpsocks.size();++i)
			{
				FD_SET(udpsocks[i], &fdset);
				max_socket=(std::max)(max_socket, udpsocks[i]);
			}

			timeval lon;
			lon.tv_sec=0;
			lon.tv_usec=1000*1000;
			_i32 rc = select((int)max_socket+1, &fdset, 0, 0, &lon);
#else
			std::vector<pollfd> conn;
			conn.resize(udpsocks.size());
			for(size_t i=0;i<udpsocks.size();++i)
			{
				conn[i].fd=udpsocks[i];
				conn[i].events=POLLIN;
				conn[i].revents=0;
			}
			int rc = poll(&conn[0], conn.size(), 1000);
#endif
        	if(rc>0)
	        {

				for(size_t i=0;i<udpsocks.size();++i)
				{
#ifdef _WIN32
					if(FD_ISSET(udpsocks[i], &fdset))
#else
					if(conn[i].revents!=0)
#endif
					{
						socklen_t addrsize=sizeof(sockaddr_in);
						sockaddr_in sender;
						_i32 err = recvfrom(udpsocks[i], buffer, BUFFERSIZE_UDP, 0, (sockaddr*)&sender, &addrsize);
						if(err==SOCKET_ERROR)
						{
							continue;
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

								servernames.push_back((sn));
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
				}
			}


            if(Server->getTimeMS()-starttime>DISCOVERY_TIMEOUT)
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
	dl_off=0;
	queued.clear();
	if(queue_callback!=NULL)
	{
		queue_callback->resetQueueFull();
	}

	if(tcpsock!=NULL)
	{
		IScopedLock lock(mutex);
		transferred_bytes+=tcpsock->getTransferedBytes();
		real_transferred_bytes+=tcpsock->getRealTransferredBytes();
		Server->destroy(tcpsock);
		tcpsock=NULL;
	}
	connect_starttime=Server->getTimeMS();

	while(Server->getTimeMS()-connect_starttime<reconnection_timeout)
	{
		IPipe* new_tcpsock;
		if(reconnection_callback==NULL)
		{
			new_tcpsock=Server->ConnectStream(inet_ntoa(server_addr.sin_addr), TCP_PORT, 10000);
		}
		else
		{
			new_tcpsock=reconnection_callback->new_fileclient_connection();
		}
		if(new_tcpsock!=NULL)
		{
			for(size_t i=0;i<throttlers.size();++i)
			{
				new_tcpsock->addThrottler(throttlers[i]);
			}
			Server->Log("Reconnected successfully,", LL_DEBUG);
			IScopedLock lock(mutex);
			tcpsock = new_tcpsock;
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

 _u32 FileClient::GetFile(std::string remotefn, IFsFile *backing_file, bool hashed, bool metadata_only, size_t folder_items, bool is_script, size_t file_id)
{
	if(tcpsock==NULL)
		return ERR_ERROR;

	resetSparseExtentsFile();

	int tries=5000;

	if(!hashed && protocol_version>1)
	{
		//Disable hashed transfer (protocol_version>1)
		protocol_version=1;
	}

	if(queued.empty())
	{
		CWData data;
		data.addUChar( metadata_only? ID_GET_FILE_METADATA_ONLY : (
				file_id!=0 ? ID_GET_FILE_WITH_METADATA :
				(protocol_version>1?ID_GET_FILE_RESUME_HASH:ID_GET_FILE_RESUME) ) );
		data.addString( remotefn );
		data.addString( identity );

		if(metadata_only)
		{
			data.addChar(0);
			data.addVarInt(folder_items);
			data.addVarInt(file_id);
		}
		else if(file_id!=0)
		{
			data.addChar(0);
			data.addChar(hashed?1:0);
			data.addVarInt(file_id);
			data.addChar(1);
		}

		if(stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() )!=data.getDataSize())
		{
			Server->Log("Timeout during file request (1)", LL_ERROR);
			return ERR_TIMEOUT;
		}

		needs_flush=true;
	}
	else
	{
		assert(queued.front().fn == remotefn);
		assert(!queued.front().finish_script);
		queued.pop_front();
	}

	_u64 filesize=0;
	_u64 received=0;
	_u64 next_checkpoint=c_checkpoint_dist;
	_u64 last_checkpoint=0;
	_u64 num_sparse_extends = 0;
	bool firstpacket=true;
	last_progress_log=0;

    starttime=Server->getTimeMS();


	enum EReceiveState
	{
		EReceiveState_Data=0,
		EReceiveState_Hash=1,
		EReceiveState_SparseExtends=2
	};

	EReceiveState state = EReceiveState_Data;
	char hash_buf[16];
	_u32 hash_r;
	MD5 hash_func;
	IFile* file = backing_file;
	std::auto_ptr<SparseFile> sparse_file;

	char* buf = dl_buf;

	while(true)
	{        
		if(!is_script)
		{
			fillQueue();
		}
		else
		{
			if(needs_flush)
			{
				needs_flush=false;
				Flush();
			}
		}

		size_t rc;
		if(tcpsock->isReadable() || dl_off==0 ||
			(firstpacket && dl_buf[0]==ID_FILESIZE && dl_off<1+sizeof(_u64) ) )
		{
			rc = tcpsock->Read(&dl_buf[dl_off], BUFFERSIZE-dl_off, 120000)+dl_off;
		}
		else
		{
			rc = dl_off;
		}
		dl_off=0;

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
				data.addUChar( metadata_only?ID_GET_FILE_METADATA_ONLY : 
					( file_id!=0 ? ID_GET_FILE_WITH_METADATA :
					(protocol_version>1?ID_GET_FILE_RESUME_HASH:ID_GET_FILE_RESUME) ) );
				data.addString( remotefn );
				data.addString( identity );

				if(metadata_only)
				{
					data.addChar(0);
					data.addVarInt(folder_items);
					data.addVarInt(file_id);
				}
				else if(file_id!=0)
				{
					data.addChar(0);
					data.addChar(hashed?1:0);
					data.addVarInt(file_id);
					data.addChar(1);
				}

				if( protocol_version>1 )
				{
					received=last_checkpoint;
				}

				if( received>0 )
					data.addInt64( received ); 

				if(file!=NULL)
				{
					file->Seek(received);
				}

				rc=stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );
				if(rc==0)
				{
					Server->Log("FileClient: Error sending request", LL_INFO);
				}
				starttime=Server->getTimeMS();

				if(protocol_version>0)
					firstpacket=true;

				hash_func.init();
				state=EReceiveState_Data;
				needs_flush=true;
			}
		}
        else
        {
			starttime=Server->getTimeMS();

			_u32 off=0;
			uchar PID=buf[0];
                        
            if( firstpacket==true)
            {
				firstpacket=false;
				if(PID==ID_COULDNT_OPEN)
				{
					if(rc>1)
					{
						memmove(dl_buf, dl_buf+1, rc-1);
						dl_off = rc-1;
					}
					return ERR_FILE_DOESNT_EXIST;
				}
				else if(PID==ID_BASE_DIR_LOST)
				{
					if(rc>1)
					{
						memmove(dl_buf, dl_buf+1, rc-1);
						dl_off = rc-1;
					}
					return ERR_BASE_DIR_LOST;
				}
				else if(PID==ID_FILESIZE || PID== ID_FILESIZE_AND_EXTENTS)
				{
					size_t req_size;
					if (PID == ID_FILESIZE)
					{
						req_size = 1 + sizeof(_u64);
					}
					else
					{
						req_size = 1 + 2 * sizeof(_u64);
					}

					if(rc >= req_size)
					{
						{
							IScopedLock lock(mutex);
							is_downloading=true;
						}

						memcpy(&filesize, buf+1, sizeof(_u64) );
						filesize=little_endian(filesize);

						if (PID == ID_FILESIZE_AND_EXTENTS)
						{
							memcpy(&num_sparse_extends, buf + 1 + sizeof(_u64), sizeof(num_sparse_extends));
							num_sparse_extends = little_endian(num_sparse_extends);
						}
						else
						{
							num_sparse_extends = 0;
						}

						off = static_cast<_u32>(req_size);

						if( (filesize==0 && num_sparse_extends==0) || metadata_only)
						{
							if(rc>off)
							{
								memmove(dl_buf, dl_buf+off, rc-off);
								dl_off = rc-off;
							}
							return ERR_SUCCESS;
						}
						else if(filesize<0)
						{
							filesize = LLONG_MAX;
						}
						else if(filesize==received && num_sparse_extends==0)
						{
							if(rc>off)
							{
								memmove(dl_buf, dl_buf+off, rc-off);
								dl_off = rc-off;
							}
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

						if (num_sparse_extends > 0)
						{
							state = EReceiveState_SparseExtends;
							hash_func.init();

							resetSparseExtentsFile();

							sparse_extends_f = temporaryFileRetry();

							if (!writeFileRetry(sparse_extends_f, reinterpret_cast<char*>(&num_sparse_extends), sizeof(num_sparse_extends)))
							{
								Server->Log("Error while writing to temporary file -1", LL_ERROR);
								Reconnect();
								return ERR_ERROR;
							}
							hash_r = static_cast<_u32>(2*num_sparse_extends*sizeof(_u64) + 16);
						}
					}
					else
					{
						dl_off=rc;
						firstpacket=true;
						continue;
					}
				}
				else
				{
					if(rc>1)
					{
						memmove(dl_buf, dl_buf+1, rc-1);
						dl_off = rc-1;
					}
					return ERR_ERROR;
				}
            }

			if (state == EReceiveState_SparseExtends && (_u32)rc > off)
			{
				if (hash_r > 16)
				{
					_u32 tc_hash = (std::min)((_u32)rc - off, hash_r - 16);

					hash_func.update(reinterpret_cast<unsigned char*>(&buf[off]), tc_hash);
				}

				_u32 tc = (std::min)((_u32)rc - off, hash_r);

				if (!writeFileRetry(sparse_extends_f, &buf[off], tc))
				{
					Server->Log("Error while writing to temporary file -2", LL_ERROR);
					Reconnect();
					return ERR_ERROR;
				}

				off += tc;
				hash_r -= tc;

				if (hash_r == 0)
				{
					hash_func.finalize();

					sparse_extends_f->Seek(sparse_extends_f->Size() - 16);
					std::string received_hash = sparse_extends_f->Read(16);

					if (memcmp(hash_func.raw_digest_int(), received_hash.data(), 16) != 0)
					{
						Server->Log("Error while downloading file: sparse extends hash wrong", LL_ERROR);
						Reconnect();
						return ERR_HASH;
					}

					sparse_extends_f->Seek(0);

					sparse_file.reset(new SparseFile(backing_file, sparse_extends_f, false, 1, false));
					file = sparse_file.get();

					if (sparse_file->hasError())
					{
						Server->Log("Error while creating sparse file view", LL_ERROR);
						Reconnect();
						return ERR_ERROR;
					}

					{
						IScopedLock lock(mutex);
						sparse_bytes += sparse_file->getSparseSize();
					}

					hash_func.init();

					if (filesize == 0 || filesize == received)
					{
						if (rc>off)
						{
							memmove(dl_buf, dl_buf + off, rc - off);
							dl_off = rc - off;
						}
						return ERR_SUCCESS;
					}
					else
					{
						state = EReceiveState_Data;
					}
				}
			}

			if( state==EReceiveState_Hash && (_u32) rc > off )
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
						Reconnect();
						return ERR_HASH;
					}
					hash_func.init();
					state=EReceiveState_Data;
				}

				if(received >= filesize && state==0)
				{
					assert(received==filesize);
					if(off < rc)
					{
						memmove(dl_buf, dl_buf+off, rc-off);
						dl_off = rc-off;
					}
					return ERR_SUCCESS;
				}
			}

            if( state==EReceiveState_Data && (_u32) rc > off )
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

						if(file==NULL)
						{
							return ERR_ERROR;
						}

						_u32 cw=file->Write(&buf[written], tw);
						hash_func.update((unsigned char*)&buf[written], cw);
						written+=cw;
						write_remaining-=cw;
						received+=cw;
						if(write_remaining==0)
							break;
						if(written<rc)
						{
							if(nofreespace_callback!=NULL
								&& !nofreespace_callback->handle_not_enough_space(file->getFilename()) )
							{
								Server->Log("Error while writing to file. No free space -2", LL_ERROR);
								Reconnect();
								return ERR_ERROR;
							}

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

							if(received<filesize)
							{
								if(hash_r>16)
								{
									hash_r=16;
									c=true;
									write_remaining=next_checkpoint-received;
									written+=16;
								}
							}
							else if(hash_r>=16)
							{
								written+=16;
							}
						}

						hash_off+=hash_r;

						if(hash_r<16)
						{
							hash_r=16-hash_r;
							state=EReceiveState_Hash;
						}
						else
						{
							hash_func.finalize();
							if(memcmp(hash_func.raw_digest_int(), hash_buf, 16)!=0)
							{
								Server->Log("Error while downloading file: hash wrong -2", LL_ERROR);
								Reconnect();
								return ERR_HASH;
							}
							hash_func.init();
						}
					}
				}

				{
					IScopedLock lock(mutex);
					received_data_bytes+=written-off;
				}

				if( received >= filesize && state==0)
				{
					assert(received==filesize);
					if(written < rc)
					{
						memmove(dl_buf, dl_buf+written, rc-written);
						dl_off = rc-written;
					}
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
				data.addUChar( metadata_only?ID_GET_FILE_METADATA_ONLY : 
					( file_id!=0 ? ID_GET_FILE_WITH_METADATA :
					(protocol_version>1?ID_GET_FILE_RESUME_HASH:ID_GET_FILE_RESUME) ) );
				data.addString( remotefn );
				data.addString( identity );

				if(metadata_only)
				{
					data.addChar(0);
					data.addVarInt(folder_items);
					data.addVarInt(file_id);
				}
				else if(file_id!=0)
				{
					data.addChar(0);
					data.addChar(hashed?1:0);
					data.addVarInt(file_id);
					data.addChar(1);
				}

				if( protocol_version>1 )
				{
					received=last_checkpoint;
				}

				if( received>0 )
					data.addInt64( received ); 

				if(file!=NULL)
				{
					file->Seek(received);
				}

				if(stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() )!=data.getDataSize())
				{
					Server->Log("Timeout during file request (2)", LL_ERROR);
					return ERR_TIMEOUT;
				}
				starttime=Server->getTimeMS();

				if(protocol_version>0)
					firstpacket=true;

				hash_func.init();
				state=EReceiveState_Data;
				needs_flush=true;
			}
		}

		logProgress(remotefn, filesize, received);
	}
}
        
_i64 FileClient::getTransferredBytes(void)
{
	IScopedLock lock(mutex);
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
	DEFEC(ERRORCODES);
	}
#undef DEFEC
	return "";
}

void FileClient::setReconnectionTimeout(unsigned int t)
{
	reconnection_timeout=t;
}

_i64 FileClient::getReceivedDataBytes( bool with_sparse )
{
	IScopedLock lock(mutex);

	if (with_sparse)
	{
		return received_data_bytes + sparse_bytes;
	}
	else
	{
		return received_data_bytes;
	}
}

void FileClient::resetReceivedDataBytes(bool with_sparse)
{
	IScopedLock lock(mutex);
	received_data_bytes=0;

	if (with_sparse)
	{
		sparse_bytes = 0;
	}
}

void FileClient::setQueueCallback( QueueCallback* cb )
{
	queue_callback = cb;
}

void FileClient::fillQueue()
{
	if(queue_callback==NULL)
	{
		if(needs_flush)
		{
			needs_flush=false;
			Flush();
		}

		return;
	}

	if(queued.size()>queuedFilesLow)
	{
		if (needs_flush)
		{
			needs_flush = false;
			Flush();
		}

		return;
	}

	bool needs_send_flush=false;

	std::vector<SQueueItem> queued_files;
	int64 queue_starttime = Server->getTimeMS();

	while(queued.size()<maxQueuedFiles
		&& Server->getTimeMS()-queue_starttime<10000)
	{
		if(!tcpsock->isWritable())
		{
			if (needs_flush)
			{
				needs_flush = false;
				Flush();
			}

			return;
		}

		MetadataQueue metadata_queue = MetadataQueue_Data;
		size_t folder_items = 0;
		bool finish_script=false;
		int64 file_id;
		std::string queue_fn = queue_callback->getQueuedFileFull(metadata_queue, folder_items, finish_script, file_id);

		if(queue_fn.empty())
		{
			if(needs_flush)
			{
				needs_flush=false;
				needs_send_flush=false;
				Flush();
			}

			return;
		}

		CWData data;
		if(metadata_queue==MetadataQueue_Data)
		{
			if(file_id==0)
			{
				data.addUChar( protocol_version>1?ID_GET_FILE_RESUME_HASH:ID_GET_FILE_RESUME );
			}
			else
			{
				data.addUChar( ID_GET_FILE_WITH_METADATA );
			}
		}
		else if(metadata_queue == MetadataQueue_MetadataAndHash)
		{
			data.addUChar( ID_FILE_HASH_AND_METADATA );
		}
		else if(metadata_queue == MetadataQueue_Metadata)
		{
			data.addUChar(ID_GET_FILE_METADATA_ONLY);
		}
		data.addString( queue_fn );
		data.addString( identity );

		if(metadata_queue == MetadataQueue_Metadata)
		{
			data.addChar(0);
			data.addVarInt(folder_items);
			data.addVarInt(file_id);
		}
		else if(file_id!=0)
		{
			data.addChar(0);
			data.addChar(protocol_version>1);
			data.addVarInt(file_id);
			data.addChar(1);
		}

		needs_send_flush=true;
		if(stack.Send( tcpsock, data.getDataPtr(), data.getDataSize(), c_default_timeout, false)!=data.getDataSize())
		{
			Server->Log("Queueing file failed", LL_DEBUG);
			queue_callback->unqueueFileFull(queue_fn, finish_script);

			if (needs_flush)
			{
				needs_flush = false;
				Flush();
			}

			return;
		}

		queued.push_back(SQueueItem(queue_fn, finish_script));
		queued_files.push_back(SQueueItem(queue_fn, finish_script));
		needs_flush=true;
	}

	if (needs_flush)
	{
		needs_flush = false;
		Flush();
	}
	else if(needs_send_flush)
	{
		if(!tcpsock->Flush(c_default_timeout))
		{
			Server->Log("Flushing failed after queueing files", LL_DEBUG);
			for(size_t i=0;i<queued_files.size();++i)
			{
				queue_callback->unqueueFileFull(queued_files[i].fn, queued_files[i].finish_script);
			}
		}
	}
}

void FileClient::logProgress(const std::string& remotefn, _u64 filesize, _u64 received)
{
	int64 ct = Server->getTimeMS();
	if(filesize>0 && (last_progress_log==0 ||
		ct-last_progress_log>60000) )
	{
		int64 new_transferred=getTransferredBytes();
		if( last_transferred_bytes!=0 &&
			last_progress_log!=0 )
		{
			int64 tranferred = new_transferred - last_transferred_bytes;
			int64 speed_bps = tranferred*1000 / (ct-last_progress_log);

			if(tranferred>0 && progress_log_callback)
			{
				progress_log_callback->log_progress(remotefn,
					filesize, received, speed_bps);
			}
		}

		last_transferred_bytes = new_transferred;
		last_progress_log = ct;
	}	
}

IFile * FileClient::temporaryFileRetry()
{
	while (true)
	{
		IFile* ret = Server->openTemporaryFile();

		if (ret == NULL)
		{
			Server->Log("Error opening temporary file in FileClient. Retrying...", LL_WARNING);
			Server->wait(10000);
		}
		else
		{
			return ret;
		}
	}
}

bool FileClient::writeFileRetry(IFile * f, const char * buf, _u32 bsize)
{
	_u32 w = 0;
	while (w < bsize)
	{
		bool has_error = false;
		w += f->Write(buf + w, bsize - w, &has_error);

		if (has_error)
		{
			return false;
		}

		if (w < bsize)
		{
			Server->Log("Error writing to file " + f->getFilename() + ". Retrying...", LL_WARNING);
			Server->wait(10000);
		}
	}
	return true;
}

void FileClient::setProgressLogCallback( ProgressLogCallback* cb )
{
	progress_log_callback = cb;
}

_u32 FileClient::GetFileHashAndMetadata( std::string remotefn, std::string& hash, std::string& permissions, int64& filesize, int64& created, int64& modified )
{
	if(queued.empty())
	{
		CWData data;
		data.addUChar( ID_GET_FILE_HASH_AND_METADATA );
		data.addString( remotefn );
		data.addString( identity );

		if(stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() )!=data.getDataSize())
		{
			Server->Log("Timeout during file hash request (1)", LL_ERROR);
			return ERR_TIMEOUT;
		}
	}
	else
	{
		assert(queued.front().fn == remotefn);
		assert(!queued.front().finish_script);
		queued.pop_front();
	}


	bool firstpacket=true;
	int tries=5000;
	unsigned short metadata_size;
	std::string metadata;
	size_t metadata_pos=0;

	while(true)
	{        
		fillQueue();

		size_t rc;
		if(tcpsock->isReadable() || dl_off==0 || 
			(firstpacket && dl_buf[0]==ID_GET_FILE_HASH_AND_METADATA && dl_off<1+sizeof(unsigned short) ) )
		{
			rc = tcpsock->Read(&dl_buf[dl_off], BUFFERSIZE-dl_off, 120000)+dl_off;
		}
		else
		{
			rc = dl_off;
		}
		dl_off=0;

		if( rc==0 )
		{
			Server->Log("Server timeout (2) in FileClient while getting hash and metadata", LL_DEBUG);
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
				data.addUChar( ID_GET_FILE_HASH_AND_METADATA );
				data.addString( remotefn );
				data.addString( identity );

				rc=stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );
				if(rc==0)
				{
					Server->Log("FileClient: Error sending request for hash and metadata", LL_INFO);
				}
				starttime=Server->getTimeMS();

				firstpacket=true;
				needs_flush=true;
			}
		}
		else
		{
			starttime=Server->getTimeMS();

			_u32 off=0;
			uchar PID=dl_buf[0];

			if( firstpacket==true)
			{
				firstpacket=false;
				if(PID==ID_COULDNT_OPEN)
				{
					if(rc>1)
					{
						memmove(dl_buf, dl_buf+1, rc-1);
						dl_off = rc-1;
					}
					return ERR_FILE_DOESNT_EXIST;
				}
				else if(PID==ID_BASE_DIR_LOST)
				{
					if(rc>1)
					{
						memmove(dl_buf, dl_buf+1, rc-1);
						dl_off = rc-1;
					}
					return ERR_BASE_DIR_LOST;
				}
				else if(PID==ID_FILE_HASH_AND_METADATA)
				{
					if(rc >= 1+sizeof(unsigned short))
					{
						memcpy(&metadata_size, dl_buf+1, sizeof(metadata_size) );
						off=1+sizeof(metadata_size);

						metadata.resize(metadata_size);

						if( metadata_size==0 )
						{
							if(rc>off)
							{
								memmove(dl_buf, dl_buf+off, rc-off);
								dl_off = rc-off;
							}
							return ERR_ERROR;
						}
					}
					else
					{
						dl_off=rc;
						firstpacket=true;
						continue;
					}
				}
				else
				{
					if(rc>1)
					{
						memmove(dl_buf, dl_buf+1, rc-1);
						dl_off = rc-1;
					}
					return ERR_ERROR;
				}

				if(rc>off)
				{
					size_t toread=rc-off;
					if(toread>metadata_size)
					{
						toread=metadata_size;
					}

					memcpy(&metadata[metadata_pos], &dl_buf[off], toread);
					metadata_pos+=toread;
					
					off+=static_cast<_u32>(toread);

					if(rc>off)
					{
						memmove(dl_buf, dl_buf+off, rc-off);
						dl_off = rc-off;
					}

					if(metadata_pos==metadata.size())
					{
						CRData data(&metadata);

						if(!data.getStr(&metadata))
						{
							return ERR_ERROR;
						}

						if(!data.getStr(&permissions))
						{
							return ERR_ERROR;
						}

						if(!data.getInt64(&filesize))
						{
							return ERR_ERROR;
						}

						if(!data.getInt64(&modified))
						{
							return ERR_ERROR;
						}

						if(!data.getInt64(&created))
						{
							return ERR_ERROR;
						}

						return ERR_SUCCESS;
					}
				}
			}
		}

		if( Server->getTimeMS()-starttime > SERVER_TIMEOUT )
		{
			Server->Log("Server timeout in FileClient while downloading hash and metadata. Trying to reconnect...", LL_INFO);
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
				data.addUChar( ID_GET_FILE_HASH_AND_METADATA );
				data.addString( remotefn );
				data.addString( identity );

				rc=stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );
				if(rc==0)
				{
					Server->Log("FileClient: Error sending request for hash and metadata", LL_INFO);
				}
				starttime=Server->getTimeMS();

				firstpacket=true;
				needs_flush=true;
			}
		}
	}
}

_u32 FileClient::InformMetadataStreamEnd( const std::string& server_token )
{
	assert(queued.empty());

	CWData data;
	data.addUChar( ID_INFORM_METADATA_STREAM_END );
	data.addString( identity );
	data.addString(server_token);

	int tries=5000;

	if(stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() )!=data.getDataSize())
	{
		Server->Log("Timeout during sending metadata stream end (1)", LL_ERROR);
		return ERR_TIMEOUT;
	}

	while(true)
	{
		size_t rc = tcpsock->Read(dl_buf, 1, 120000);

		if(rc==0)
		{
			Server->Log("Server timeout (2) in FileClient sending metadata stream end", LL_DEBUG);
			bool b=Reconnect();
			--tries;
			if(!b || tries<=0 )
			{
				Server->Log("FileClient: ERR_TIMEOUT (metadata stream)", LL_INFO);
				return ERR_TIMEOUT;
			}
			else
			{
				CWData data;
				data.addUChar( ID_INFORM_METADATA_STREAM_END );
				data.addString( identity );
				data.addString(server_token);

				rc=stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );
				if(rc==0)
				{
					Server->Log("FileClient: Error sending metadata stream end", LL_INFO);
				}
				starttime=Server->getTimeMS();
			}
		}
		else
		{
			if(*dl_buf==ID_PONG)
			{
				return ERR_SUCCESS;
			}
			else
			{
				return ERR_ERROR;
			}
		}
	}
}

_u32 FileClient::FinishScript(std::string remotefn)
{
	if(queued.empty())
	{
		CWData data;
		data.addUChar( ID_SCRIPT_FINISH );
		data.addString( identity );
		data.addString(remotefn);

		int tries=5000;

		if(stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() )!=data.getDataSize())
		{
			Server->Log("Timeout during sending finish script (1)", LL_ERROR);
			return ERR_TIMEOUT;
		}
	}
	else
	{
		assert(queued.front().fn == remotefn);
		assert(queued.front().finish_script);
		queued.pop_front();
	}

	int tries=20;

	while(true)
	{
		size_t rc = tcpsock->Read(dl_buf, 1, 120000);

		if(rc==0)
		{
			Server->Log("Server timeout (2) in FileClient sending finish script", LL_DEBUG);
			bool b=Reconnect();
			--tries;
			if(!b || tries<=0 )
			{
				Server->Log("FileClient: ERR_TIMEOUT (finish script)", LL_INFO);
				return ERR_TIMEOUT;
			}
			else
			{
				CWData data;
				data.addUChar( ID_SCRIPT_FINISH );
				data.addString( identity );
				data.addString(remotefn);

				rc=stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() );
				if(rc==0)
				{
					Server->Log("FileClient: Error sending metadata stream end", LL_INFO);
				}
				starttime=Server->getTimeMS();
			}
		}
		else
		{
			if(*dl_buf==ID_PONG)
			{
				return ERR_SUCCESS;
			}
			else
			{
				return ERR_ERROR;
			}
		}
	}
}

_u32 FileClient::Flush()
{
	if(tcpsock==NULL)
		return ERR_ERROR;

	Server->Log("Flushing FileClient...", LL_DEBUG);

	CWData data;
	data.addUChar(ID_FLUSH_SOCKET);

	if(stack.Send( tcpsock, data.getDataPtr(), data.getDataSize() )!=data.getDataSize())
	{
		Server->Log("Timeout during flush request", LL_ERROR);
		return ERR_TIMEOUT;
	}

	return ERR_SUCCESS;
}

_i64 FileClient::getRealTransferredBytes()
{
	if(tcpsock!=NULL)
	{
		return real_transferred_bytes+=tcpsock->getRealTransferredBytes();
	}
	return real_transferred_bytes;
}

void FileClient::Shutdown()
{
	IScopedLock lock(mutex);
	if(tcpsock!=NULL)
	{
		tcpsock->shutdown();
	}
}

bool FileClient::isDownloading()
{
	IScopedLock lock(mutex);
	return is_downloading;
}

IFile * FileClient::releaseSparseExtendsFile()
{
	IFile* ret = sparse_extends_f;
	sparse_extends_f = NULL;
	return ret;
}

void FileClient::resetSparseExtentsFile()
{
	if (sparse_extends_f != NULL)
	{
		std::string tmpfn = sparse_extends_f->getFilename();
		Server->destroy(sparse_extends_f);
		Server->deleteFile(tmpfn);
		sparse_extends_f = NULL;
	}
}

