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

#include "../vld.h"
#include "../Interface/Server.h"
#include "CClientThread.h"
#include "CTCPFileServ.h"
#include "map_buffer.h"
#include "log.h"
#include "packet_ids.h"
#include "../stringtools.h"
#include "CriticalSection.h"
#include "FileServ.h"


#define CLIENT_TIMEOUT	120
#define CHECK_BASE_PATH

CriticalSection cs;

int curr_tid=0;

#ifdef _WIN32
bool isDirectory(const std::wstring &path)
{
        DWORD attrib = GetFileAttributesW(path.c_str());

        if ( attrib == 0xFFFFFFFF || !(attrib & FILE_ATTRIBUTE_DIRECTORY) )
        {
                return false;
        }
        else
        {
                return true;
        }
}
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
bool isDirectory(const std::wstring &path)
{
        struct stat64 f_info;
		int rc=stat64(Server->ConvertToUTF8(path).c_str(), &f_info);
		if(rc!=0)
		{
			return false;
		}

        if ( S_ISDIR(f_info.st_mode) )
        {
                return true;
        }
        else
        {
                return false;
        }
}
#endif

CClientThread::CClientThread(SOCKET pSocket, CTCPFileServ* pParent)
{
	mSocket=pSocket;

	DisableNagle();

	stopped=false;
	killable=false;

	parent=pParent;

#ifdef _WIN32
	bufmgr=new CBufMgr(NBUFFERS,READSIZE);
#else
	bufmgr=NULL;
#endif

	hFile=0;

#ifdef _WIN32
	int window_size;
	int window_size_len=sizeof(window_size);
	getsockopt(mSocket, SOL_SOCKET, SO_SNDBUF,(char *) &window_size, &window_size_len );
	Log("Info: Window size=%i", window_size);
#endif

	close_the_socket=true;
	errcount=0;
}

CClientThread::~CClientThread()
{
	delete bufmgr;
}

void CClientThread::EnableNagle(void)
{
#ifdef DISABLE_NAGLE
#ifdef _WIN32
	BOOL opt=FALSE;
	int err=setsockopt(mSocket,IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(BOOL) );
	if( err==SOCKET_ERROR )
		Log("Error: Setting TCP_NODELAY failed");
#else
	static bool once=true;
	if( once==true )
	{
		once=false;
		int opt=1;
		int err=setsockopt(mSocket, IPPROTO_TCP, TCP_CORK, (char*)&opt, sizeof(int) );
		if( err==SOCKET_ERROR )
		{
			Log("Error: Setting TCP_CORK failed. errno: %i", errno);
		}
	}
#endif
#endif
}

void CClientThread::DisableNagle(void)
{
#ifdef DISABLE_NAGLE
#ifdef _WIN32
	BOOL opt=TRUE;
	int err=setsockopt(mSocket,IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(BOOL) );
	if( err==SOCKET_ERROR )
		Log("Error: Setting TCP_NODELAY failed");
#endif
#endif
}

void CClientThread::operator()(void)
{
#ifdef _WIN32
#ifdef BACKGROUND_PRIORITY
#ifdef THREAD_MODE_BACKGROUND_BEGIN
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
#endif
#endif

#ifdef HIGH_PRIORITY
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

	while( RecvMessage()==true && stopped==false )
	{
	}
	ReleaseMemory();

	if( close_the_socket==true )
	{
		closesocket(mSocket);
	}

	if( hFile!=0 )
	{
		CloseHandle( hFile );
		hFile=0;
	}

	killable=true;
}

bool CClientThread::RecvMessage(void)
{
	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(mSocket,&fdset);

	timeval lon;
	lon.tv_usec=0;
	lon.tv_sec=60;

	_i32 rc = select((int)mSocket+1, &fdset, 0, 0, &lon);
	if( rc==0 )
	{
		Log("1 min Timeout deleting Buffers (%i KB) and waiting 1h more...", (NBUFFERS*READSIZE)/1024 );
		delete bufmgr;
		lon.tv_sec=3600;
		FD_ZERO(&fdset);
		FD_SET(mSocket,&fdset);
		rc = select((int)mSocket+1, &fdset, 0, 0, &lon);
		Log("Reallocating Buffers...");
#ifdef _WIN32
		bufmgr=new CBufMgr(NBUFFERS,READSIZE);
#endif
	}
	
	if( rc<1)
	{
		Log("Select Error/Timeout in RecvMessage");

		return false;
	}
	else
	{
		rc=recv(mSocket, buffer, BUFFERSIZE, MSG_NOSIGNAL);

		if(rc<1)
		{
			Log("Recv Error in RecvMessage");
			return false;
		}
		else
		{
			Log("Received data...");
			stack.AddData(buffer, rc);				
		}

		size_t packetsize;
		char* packet;
		while( (packet=stack.getPacket(&packetsize)) != NULL )
		{
			Log("Received a Packet.");
			CRData data(packet, packetsize);

			bool b=ProcessPacket( &data );
			delete[] packet;

			if( b==false )
				return false;
		}
	}
	return true;
}

bool CClientThread::ProcessPacket(CRData *data)
{
	uchar id;
	if( data->getUChar(&id)==true )
	{
		switch(id)
		{
		case ID_GET_GAMELIST:
			{	
#ifdef CHECK_IDENT
				std::string ident;
				data->getStr(&ident);
				if(!FileServ::checkIdentity(ident))
				{
					Log("Identity check failed -1");
					return false;
				}
#endif

				hFile=0;
				std::vector<std::string> games=get_maps();


				Log("Sending game list");

				EnableNagle();

				CWData data;
				data.addUChar( ID_GAMELIST );
				data.addUInt( (unsigned int)games.size() );

				stack.Send(mSocket, data );

				for(size_t i=0;i<games.size();++i)
				{
					std::string version;
					std::wstring udir;
					version=getFile(wnarrow(map_file(widen(games[i])+L"\\version.uri",true,&udir)));

					if( udir!=L"" )
						games[i]+="|"+wnarrow(udir);
					
					stack.Send(mSocket, (char*)games[i].c_str(), games[i].size() );
					
					stack.Send(mSocket, (char*)version.c_str(), version.size() );
				}
				
				Log("done.");

				DisableNagle();
			}break;
		case ID_GET_FILE:
			{
				std::string s_filename;
				if(data->getStr(&s_filename)==false)
					break;

#ifdef CHECK_IDENT
				std::string ident;
				data->getStr(&ident);
				if(!FileServ::checkIdentity(ident))
				{
					Log("Identity check failed -2");
					return false;
				}
#endif

				std::wstring filename=Server->ConvertToUnicode(s_filename);

				_i64 start_offset=0;
				bool offset_set=data->getInt64(&start_offset);

				Log("Sending file %s",wnarrow(filename).c_str());

				filename=map_file(filename);
				
				Log("Mapped name: %s", wnarrow(filename).c_str() );
				
#ifndef LINUX
				hFile=CreateFileW(filename.c_str(), FILE_READ_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

				if(hFile == INVALID_HANDLE_VALUE)
				{
					hFile=NULL;
#ifdef CHECK_BASE_PATH
					std::wstring basePath=map_file(getuntil(L"/",filename)+L"/");
					if(!isDirectory(basePath))
					{
						char ch=ID_BASE_DIR_LOST;
						int rc=send(mSocket, &ch, 1, MSG_NOSIGNAL);
						if(rc==SOCKET_ERROR)
						{
							Log("Error: Socket Error - DBG: Send BASE_DIR_LOST");
							return false;
						}
						Log("Info: Base dir lost");
						break;
					}
#endif
					
					char ch=ID_COULDNT_OPEN;
					int rc=send(mSocket, &ch, 1, MSG_NOSIGNAL);
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: Send COULDNT OPEN");
						return false;
					}
					Log("Info: Couldn't open file");
					break;
				}

				currfilepart=0;
				sendfilepart=0;

				LARGE_INTEGER filesize;
				GetFileSizeEx(hFile, &filesize);


				if( offset_set==false )
				{
					CWData data;
					data.addUChar(ID_FILESIZE);
					data.addUInt64(filesize.QuadPart);

					int rc=send(mSocket, data.getDataPtr(), data.getDataSize(), MSG_NOSIGNAL );	
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: SendSize");
						CloseHandle(hFile);
						hFile=NULL;
						return false;
					}
				}

				if(filesize.QuadPart==0)
				{
					CloseHandle(hFile);
					hFile=NULL;
					break;
				}

				for(_i64 i=start_offset;i<filesize.QuadPart && stopped==false;i+=READSIZE)
				{
					bool last;
					if(i+READSIZE<filesize.QuadPart)
						last=false;
					else
					{
						last=true;
						Log("Reading last file part");
					}

					while(bufmgr->nfreeBufffer()==0 && stopped==false)
					{
						int rc;
						SleepEx(0,true);
						rc=SendData();
						if(rc==-1)
						{
							Log("Error: Send failed in file loop -1");
							CloseThread(hFile);
						}
						else if(rc==0)
							SleepEx(1,true);
					}

					if( stopped==false )
						ReadFilePart(hFile, i, last);

					if(FileServ::isPause() )
					{
						unsigned int starttime=GetTickCount();
						while(GetTickCount()-starttime<5000)
						{
							SleepEx(500,true);
						}
					}
				}

				while(bufmgr->nfreeBufffer()!=NBUFFERS && stopped==false)
				{
					SleepEx(0,true);
					int rc;
					rc=SendData();
					
					if( rc==2 && bufmgr->nfreeBufffer()!=NBUFFERS )
					{
						Log("Error: File end and not all Buffers are free!-1");
					}

					if(rc==-1)
					{
						Log("Error: Send failed in off file loop -3");
						CloseThread(hFile);
					}
					else if(rc==0)
						SleepEx(1,true);
				}

				if( stopped==false )
				{
					Log("Closed file.");
					CloseHandle(hFile);
					hFile=NULL;
				}
#else //LINUX
				hFile=open64(wnarrow(filename).c_str(), O_RDONLY|O_LARGEFILE);
				
				if(hFile == INVALID_HANDLE_VALUE)
				{
#ifdef CHECK_BASE_PATH
					std::wstring basePath=map_file(getuntil(L"/",filename)+L"/");
					if(!isDirectory(basePath))
					{
						char ch=ID_BASE_DIR_LOST;
						int rc=send(mSocket, &ch, 1, MSG_NOSIGNAL);
						if(rc==SOCKET_ERROR)
						{
							Log("Error: Socket Error - DBG: Send BASE_DIR_LOST");
							return false;
						}
						Log("Info: Base dir lost");
						break;
					}
#endif
					hFile=0;
					char ch=ID_COULDNT_OPEN;
					int rc=send(mSocket, &ch, 1, MSG_NOSIGNAL);
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: Send COULDNT OPEN");
						return false;
					}
					Log("Info: Couldn't open file");
					break;
				}
				
				currfilepart=0;
				sendfilepart=0;
				
				struct stat64 stat_buf;
				fstat64(hFile, &stat_buf);
				
				off64_t filesize=stat_buf.st_size;
				
				if( offset_set==false )
				{
					CWData data;
					data.addUChar(ID_FILESIZE);
					data.addUInt64(filesize);

					int rc=send(mSocket, data.getDataPtr(), data.getDataSize(), MSG_NOSIGNAL );	
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: SendSize");
						CloseHandle(hFile);
						hFile=0;
						return false;
					}
				}
				
				if(filesize==0)
				{
					CloseHandle(hFile);
					hFile=0;
					break;
				}
				
				off64_t foffset=start_offset;
				
				while( foffset < filesize )
				{
					sendfile64(mSocket, hFile, &foffset, 32768);
					if(FileServ::isPause() )
					{
						Sleep(500);
					}
				}
				
				CloseHandle(hFile);
				hFile=0;
				
#endif

			}break;
		}
	}
	if( stopped==true )
		return false;
	else
		return true;
}

#ifndef LINUX
void ProcessReadData( SLPData *ldata )
{
	for(DWORD i=0;i<ldata->bsize;i+=SENDSIZE)
	{
		SSendData *sdata=new SSendData;

		if( i+SENDSIZE >=ldata->bsize )
		{
			sdata->bsize=ldata->bsize-i;
			sdata->delbufptr=ldata->buffer;
			sdata->delbuf=true;
			sdata->last=ldata->last;
		}
		else
		{
			sdata->bsize=SENDSIZE;
			sdata->delbufptr=NULL;
			sdata->delbuf=false;
			sdata->last=false;
		}

		sdata->buffer=&ldata->buffer[i];

		ldata->t_send->push_back(sdata);
	}	
}

void CALLBACK FileIOCompletionRoutine(DWORD dwErrorCode,DWORD dwNumberOfBytesTransfered,LPOVERLAPPED lpOverlapped)
{
	SLPData *ldata=(SLPData*)lpOverlapped->hEvent;

	if( dwNumberOfBytesTransfered > 0)
	{
		ldata->bsize=dwNumberOfBytesTransfered;

		if( *ldata->sendfilepart!=ldata->filepart )
		{
			Log("Warning: Packets out of order.... shifting");
			ldata->t_unsend->push_back(ldata);
			delete lpOverlapped;
			return;
		}

		ProcessReadData(ldata);

		++(*ldata->sendfilepart);

		if( ldata->t_unsend->size()>0 )
		{
			bool refresh=true;
			while( refresh==true )
			{
				refresh=false;
				for(size_t i=0;i<ldata->t_unsend->size();++i)
				{
					SLPData *pdata=(*ldata->t_unsend)[i];
					if( *pdata->sendfilepart==pdata->filepart )
					{
						Log("Using shifted packet...");
						ProcessReadData(pdata);
						++(*pdata->sendfilepart);
						refresh=true;
						ldata->t_unsend->erase( ldata->t_unsend->begin()+i );
						delete pdata;
						break;
					}
				}
			}
		}

		delete ldata;
	}
	else
	{
		Log( "Warning: Chunk size=0");
		delete ldata;
	}
	delete lpOverlapped;
}
#endif

#ifndef LINUX
void CClientThread::ReadFilePart(HANDLE hFile, const _i64 &offset,const bool &last)
{
	LPOVERLAPPED overlap=new OVERLAPPED;
	//memset(overlap, 0, sizeof(OVERLAPPED) );
	
	overlap->Offset=(DWORD)offset;
	overlap->OffsetHigh=(DWORD)(offset>>32);

	SLPData *ldata=new SLPData;
	ldata->buffer=bufmgr->getBuffer();

	if( ldata->buffer==NULL ) 
	{
		Log("Error: No Free Buffer");
		Log("Info: Free Buffers=%i", bufmgr->nfreeBufffer() );
		return;
	}

	ldata->t_send=&t_send;
	ldata->t_unsend=&t_unsend;
	ldata->last=last;
	ldata->filepart=currfilepart;
	ldata->sendfilepart=&sendfilepart;
	overlap->hEvent=ldata;

	BOOL b=ReadFileEx(hFile, ldata->buffer, READSIZE, overlap, FileIOCompletionRoutine);

	++currfilepart;

	if( /*GetLastError() != ERROR_SUCCESS ||*/ b==false)
	{
		Log("Error: Can't start reading from File");
		return;
	}
}
#else
void CClientThread::ReadFilePart(HANDLE hFile, const _i64 &offset,const bool &last)
{

}
#endif

int CClientThread::SendData(void)
{
	if( t_send.size()==0 )
		return 0;

	SSendData* ldata=t_send[0];

	fd_set fdset;
				
	FD_ZERO(&fdset);
	FD_SET(mSocket, &fdset);

	timeval lon;
	lon.tv_sec=CLIENT_TIMEOUT;
	lon.tv_usec=0;

	_i32 ret;
	ret=select((int)mSocket+1, 0, &fdset,0, &lon);
	if(ret < 1)
	{
		Log("Client Timeout occured.");
		
		t_send.erase( t_send.begin() );
		delete ldata;
		return -1;
	}
	else
	{
		if( ldata->bsize>0 )
		{
			_i32 rc=send(mSocket, ldata->buffer, ldata->bsize, MSG_NOSIGNAL );		
			if( rc==SOCKET_ERROR )
			{
				int err;
#ifdef _WIN32
				err=WSAGetLastError();
#else
				err=errno;
#endif
				Log("SOCKET_ERROR in SendData(). BSize: %i WSAGetLastError: %i Packet: %i", ldata->bsize, ldata->buffer );
				
				if( ldata->delbuf==true )
				{
					bufmgr->releaseBuffer(ldata->delbufptr);
					ldata->delbuf=false;
				}
				t_send.erase( t_send.begin() );
				delete ldata;
				return -1;
			}
		}

		if( ldata->delbuf==true )
		{
			bufmgr->releaseBuffer( ldata->delbufptr );
			ldata->delbuf=false;
		}
		
		if( ldata->last==true )
		{
			Log("Info: File End");

			if( t_send.size() > 1 )
			{
				Log("Error: Senddata exceeds 1");
			}

			for(size_t i=0;i<t_send.size();++i)
			{
				if( t_send[i]->delbuf==true )
				{
					bufmgr->releaseBuffer( t_send[i]->buffer );
				}
				delete t_send[i];
			}
			t_send.clear();
			return 2;		
		}		

		t_send.erase( t_send.begin() );
		delete ldata;
		return 1;
	}
}

void CClientThread::ReleaseMemory(void)
{
#ifdef _WIN32
	Log("Deleting Memory...");
	while( bufmgr->nfreeBufffer()!=NBUFFERS )
	{
		while( SleepEx(1000,true)!=0 );

		for(size_t i=0;i<t_send.size();++i)
		{
			if( t_send[i]->delbuf==true )
				bufmgr->releaseBuffer( t_send[i]->delbufptr );
			delete t_send[i];
		}


		t_send.clear();
	}
	Log("done.");
#endif
}

void CClientThread::CloseThread(HANDLE hFile)
{
	StopThread();
}

bool CClientThread::isStopped(void)
{
	return stopped;
}

void CClientThread::StopThread(void)
{
	stopped=true;
	Log("Client thread stopped");
}

bool CClientThread::isKillable(void)
{
	return killable;
}

