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
#include "../Interface/Pipe.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/File.h"
#include "CClientThread.h"
#include "CTCPFileServ.h"
#include "map_buffer.h"
#include "log.h"
#include "packet_ids.h"
#include "../stringtools.h"
#include "CriticalSection.h"
#include "FileServ.h"
#include "ChunkSendThread.h"

#include <algorithm>
#include <memory.h>

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
	DisableNagle();

	stopped=false;
	killable=false;
	has_socket=true;
	int_socket=pSocket;

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
	getsockopt(pSocket, SOL_SOCKET, SO_SNDBUF,(char *) &window_size, &window_size_len );
	Log("Info: Window size="+nconvert(window_size));
#endif

	close_the_socket=true;
	errcount=0;
	clientpipe=Server->PipeFromSocket(pSocket);
	mutex=NULL;
	cond=NULL;
	state=CS_NONE;
	update_file=NULL;
	chunk_send_thread_ticket=ILLEGAL_THREADPOOL_TICKET;
}

CClientThread::CClientThread(IPipe *pClientpipe, CTCPFileServ* pParent)
{
	stopped=false;
	killable=false;
	has_socket=false;

	parent=pParent;

	bufmgr=NULL;

	hFile=0;

	close_the_socket=false;
	errcount=0;
	clientpipe=pClientpipe;
	state=CS_NONE;
	mutex=NULL;
	cond=NULL;
	update_file=NULL;
	chunk_send_thread_ticket=ILLEGAL_THREADPOOL_TICKET;

	stack.setAddChecksum(true);
}

CClientThread::~CClientThread()
{
	delete bufmgr;
	if(mutex!=NULL)
	{
		Server->destroy(mutex);
		Server->destroy(cond);
	}
}

void CClientThread::EnableNagle(void)
{
	if(has_socket)
	{
#ifdef DISABLE_NAGLE
#ifdef _WIN32
	BOOL opt=FALSE;
	int err=setsockopt(int_socket,IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(BOOL) );
	if( err==SOCKET_ERROR )
		Log("Error: Setting TCP_NODELAY=FALSE failed", LL_WARNING);
#else
	static bool once=true;
	if( once==true )
	{
		once=false;
		int opt=1;
		int err=setsockopt(int_socket, IPPROTO_TCP, TCP_CORK, (char*)&opt, sizeof(int) );
		if( err==SOCKET_ERROR )
		{
			Log("Error: Setting TCP_CORK failed. errno: "+nconvert(errno), LL_WARNING);
		}
	}
#endif
#endif
	}
}

void CClientThread::DisableNagle(void)
{
	if(has_socket)
	{
#ifdef DISABLE_NAGLE
#ifdef _WIN32
	BOOL opt=TRUE;
	int err=setsockopt(int_socket,IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(BOOL) );
	if( err==SOCKET_ERROR )
		Log("Error: Setting TCP_NODELAY=TRUE failed", LL_WARNING);
#endif
#endif
	}
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

	if( hFile!=0 )
	{
		CloseHandle( hFile );
		hFile=0;
	}

	if(close_the_socket)
	{
		Server->destroy(clientpipe);
	}

	if(chunk_send_thread_ticket!=ILLEGAL_THREADPOOL_TICKET)
	{
		{
			IScopedLock lock(mutex);
			state=CS_NONE;
			while(!next_chunks.empty())
				next_chunks.pop();
			if(update_file!=NULL)
			{
				Server->destroy(update_file);
				update_file=NULL;
			}
		}
		cond->notify_all();
		Server->getThreadPool()->waitFor(chunk_send_thread_ticket);
	}

	killable=true;
}

bool CClientThread::RecvMessage(void)
{
	_i32 rc;
	timeval lon;
	lon.tv_usec=0;
	lon.tv_sec=60;
	rc=clientpipe->isReadable(lon.tv_sec*1000)?1:0;
	if(clientpipe->hasError())
		rc=-1;
	if( rc==0 )
	{
		if(state==CS_NONE)
		{
			Log("1 min Timeout deleting Buffers ("+nconvert((NBUFFERS*READSIZE)/1024 )+" KB) and waiting 1h more...", LL_DEBUG);
			delete bufmgr;
			bufmgr=NULL;
			lon.tv_sec=3600;
			int n=0;
			while(stopped==false && rc==0 && n<60)
			{
				rc=clientpipe->isReadable(lon.tv_sec*1000);
				if(clientpipe->hasError())
					rc=-1;
				++n;
			}
		}
	}
	
	if( rc<1)
	{
		Log("Select Error/Timeout in RecvMessage", LL_DEBUG);

		return false;
	}
	else
	{
		rc=(_i32)clientpipe->Read(buffer, BUFFERSIZE);


		if(rc<1)
		{
			Log("Recv Error in RecvMessage", LL_DEBUG);
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
			Log("Received a Packet.", LL_DEBUG);
			CRData data(packet, packetsize);

			bool b=ProcessPacket( &data );
			delete[] packet;

			if( b==false )
				return false;
		}
	}
	return true;
}

int CClientThread::SendInt(const char *buf, size_t bsize)
{
	return (int)(clientpipe->Write(buf, bsize)?bsize:SOCKET_ERROR);
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
					Log("Identity check failed -1", LL_DEBUG);
					return false;
				}
#endif

				hFile=0;
				std::vector<std::wstring> games=get_maps();


				Log("Sending game list", LL_DEBUG);

				EnableNagle();

				CWData data;
				data.addUChar( ID_GAMELIST );
				data.addUInt( (unsigned int)games.size() );

				stack.Send(clientpipe, data);

				for(size_t i=0;i<games.size();++i)
				{
					std::string version;
					std::wstring udir;
					version=getFile(wnarrow(map_file(games[i]+L"\\version.uri",true,&udir)));

					if( udir!=L"" )
						games[i]+=L"|"+udir;

					std::string game=Server->ConvertToUTF8(games[i]);
					

					stack.Send(clientpipe, (char*)game.c_str(), game.size() );					
					stack.Send(clientpipe, (char*)version.c_str(), version.size() );
				}
				
				Log("done.", LL_DEBUG);

				DisableNagle();
			}break;
		case ID_GET_FILE_RESUME:
		case ID_GET_FILE:
		case ID_GET_FILE_RESUME_HASH:
			{
				std::string s_filename;
				if(data->getStr(&s_filename)==false)
					break;

#ifdef CHECK_IDENT
				std::string ident;
				data->getStr(&ident);
				if(!FileServ::checkIdentity(ident))
				{
					Log("Identity check failed -2", LL_DEBUG);
					return false;
				}
#endif

				std::wstring o_filename=Server->ConvertToUnicode(s_filename);

				_i64 start_offset=0;
				bool offset_set=data->getInt64(&start_offset);

				Log("Sending file "+Server->ConvertToUTF8(o_filename), LL_DEBUG);

				std::wstring filename=map_file(o_filename);

				Log("Mapped name: "+Server->ConvertToUTF8(filename), LL_DEBUG);

				if(filename.empty())
				{
					char ch=ID_BASE_DIR_LOST;
					int rc=SendInt(&ch, 1);

					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: Send BASE_DIR_LOST -1", LL_DEBUG);
						return false;
					}
					Log("Info: Base dir lost -1", LL_DEBUG);
					break;
				}

				cmd_id=id;

				if( id==ID_GET_FILE_RESUME_HASH )
				{
					hash_func.init();
				}

#ifdef _WIN32
				if(filename.size()<2 || (filename[0]!='\\' && filename[1]!='\\' ) )
				{
					filename=L"\\\\?\\"+filename;			
				}

				if(bufmgr==NULL)
				{
					bufmgr=new CBufMgr(NBUFFERS,READSIZE);
				}
#endif
				
#ifndef LINUX
#ifndef BACKUP_SEM
				hFile=CreateFileW(filename.c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else
				hFile=CreateFileW(filename.c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#endif

				if(hFile == INVALID_HANDLE_VALUE)
				{
					hFile=NULL;
#ifdef CHECK_BASE_PATH
					std::wstring basePath=map_file(getuntil(L"/",o_filename)+L"/");
					if(!isDirectory(basePath))
					{
						char ch=ID_BASE_DIR_LOST;
						int rc=SendInt(&ch, 1);
						if(rc==SOCKET_ERROR)
						{
							Log("Error: Socket Error - DBG: Send BASE_DIR_LOST", LL_DEBUG);
							return false;
						}
						Log("Info: Base dir lost", LL_DEBUG);
						break;
					}
#endif
					
					char ch=ID_COULDNT_OPEN;
					int rc=SendInt(&ch, 1);
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: Send COULDNT OPEN", LL_DEBUG);
						return false;
					}
					Log("Info: Couldn't open file", LL_DEBUG);
					break;
				}

				currfilepart=0;
				sendfilepart=0;
				sent_bytes=0;

				LARGE_INTEGER filesize;
				GetFileSizeEx(hFile, &filesize);

				curr_filesize=filesize.QuadPart;

				next_checkpoint=start_offset+c_checkpoint_dist;
				if(next_checkpoint>curr_filesize)
					next_checkpoint=curr_filesize;


				if( offset_set==false || id==ID_GET_FILE_RESUME || id==ID_GET_FILE_RESUME_HASH )
				{
					CWData data;
					data.addUChar(ID_FILESIZE);
					data.addUInt64(filesize.QuadPart);

					int rc=SendInt(data.getDataPtr(), data.getDataSize());
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: SendSize", LL_DEBUG);
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
						Log("Reading last file part", LL_DEBUG);
					}

					while(bufmgr->nfreeBufffer()==0 && stopped==false)
					{
						int rc;
						SleepEx(0,true);
						rc=SendData();
						if(rc==-1)
						{
							Log("Error: Send failed in file loop -1", LL_DEBUG);
							CloseThread(hFile);
						}
						else if(rc==0)
							SleepEx(1,true);
					}

					if( stopped==false )
						ReadFilePart(hFile, i, last);

					if(FileServ::isPause() )
					{
						DWORD starttime=GetTickCount();
						while(GetTickCount()-starttime<5000)
						{
							SleepEx(500,true);

							int rc=SendData();
							if(rc==-1)
							{
								Log("Error: Send failed in file pause loop -2", LL_DEBUG);
								CloseThread(hFile);
							}
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
						Log("Error: File end and not all Buffers are free!-1", LL_WARNING);
					}

					if(rc==-1)
					{
						Log("Error: Send failed in off file loop -3", LL_DEBUG);
						CloseHandle(hFile);
						hFile=NULL;
						break;
					}
					else if(rc==0)
						SleepEx(1,true);
				}

				if( stopped==false )
				{
					Log("Closed file.", LL_DEBUG);
					CloseHandle(hFile);
					hFile=NULL;
				}
#else //LINUX
				hFile=open64(Server->ConvertToUTF8(filename).c_str(), O_RDONLY|O_LARGEFILE);
				
				if(hFile == INVALID_HANDLE_VALUE)
				{
#ifdef CHECK_BASE_PATH
					std::wstring basePath=map_file(getuntil(L"/",o_filename)+L"/");
					if(!isDirectory(basePath))
					{
						char ch=ID_BASE_DIR_LOST;
						int rc=SendInt(&ch, 1);
						if(rc==SOCKET_ERROR)
						{
							Log("Error: Socket Error - DBG: Send BASE_DIR_LOST", LL_DEBUG);
							return false;
						}
						Log("Info: Base dir lost", LL_DEBUG);
						break;
					}
#endif
					hFile=0;
					char ch=ID_COULDNT_OPEN;
					int rc=SendInt(&ch, 1);
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: Send COULDNT OPEN", LL_DEBUG);
						return false;
					}
					Log("Info: Couldn't open file", LL_DEBUG);
					break;
				}
				
				currfilepart=0;
				sendfilepart=0;
				
				struct stat64 stat_buf;
				fstat64(hFile, &stat_buf);
				
				off64_t filesize=stat_buf.st_size;
				
				if( offset_set==false || id==ID_GET_FILE_RESUME || id==ID_GET_FILE_RESUME_HASH )
				{
					CWData data;
					data.addUChar(ID_FILESIZE);
					data.addUInt64(filesize);

					int rc=SendInt(data.getDataPtr(), data.getDataSize() );	
					if(rc==SOCKET_ERROR)
					{
						Log("Error: Socket Error - DBG: SendSize", LL_DEBUG);
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

				unsigned int s_bsize=8192;

				if(id==ID_GET_FILE || id==ID_GET_FILE_RESUME )
					s_bsize=32768;

				char buf[s_bsize];

				bool has_error=false;
				
				while( foffset < filesize )
				{
					size_t count=(std::max)((size_t)s_bsize, (size_t)(filesize-foffset));
					if( clientpipe==NULL && ( id==ID_GET_FILE || id==ID_GET_FILE_RESUME ) )
					{
						sendfile64(int_socket, hFile, &foffset, count);
					}
					else
					{
						ssize_t rc=read(hFile, buf, count);
						if(rc>0)
						{
							rc=SendInt(buf, rc);
							if(rc==SOCKET_ERROR)
							{
								Log("Error: Sending data failed");
								CloseHandle(hFile);
								return false;
							}
							else if(id==ID_GET_FILE_RESUME_HASH)
							{
								hash_func.update((unsigned char*)buf, rc);
							}
						}
						else
						{
							Log("Error: Reading from file failed", LL_DEBUG);
							CloseHandle(hFile);
							return false;
						}
					}
					if(FileServ::isPause() )
					{
						Sleep(500);
					}
				}

				if(id==ID_GET_FILE_RESUME_HASH)
				{
					hash_func.finalize();
					SendInt((char*)hash_func.raw_digest_int(), 16);
				}
				
				CloseHandle(hFile);
				hFile=0;
#endif

			}break;
		case ID_GET_FILE_BLOCKDIFF:
			{
				bool b=GetFileBlockdiff(data);
				if(!b)
					return false;
			}break;
		case ID_BLOCK_REQUEST:
			{
				if(state==CS_BLOCKHASH)
				{
					Handle_ID_BLOCK_REQUEST(data);
				}
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
			Log("Packets out of order.... shifting", LL_DEBUG);
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
						Log("Using shifted packet...", LL_DEBUG);
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
		Log( "Info: Chunk size=0", LL_DEBUG);
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
		Log("Error: No Free Buffer", LL_DEBUG);
		Log("Info: Free Buffers="+nconvert(bufmgr->nfreeBufffer()), LL_DEBUG );
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
		Log("Error: Can't start reading from File", LL_DEBUG);
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

	_i32 ret;
	ret=clientpipe->isWritable(CLIENT_TIMEOUT*1000)?1:0;

	if(ret < 1)
	{
		Log("Client Timeout occured.", LL_DEBUG);
		
		t_send.erase( t_send.begin() );
		delete ldata;
		return -1;
	}
	else
	{
		if( ldata->bsize>0 )
		{
			unsigned int sent=0;
			while(sent<ldata->bsize)
			{
				_i32 ts;
				if(cmd_id==ID_GET_FILE_RESUME_HASH)
					ts=(std::min)((unsigned int)(next_checkpoint-sent_bytes), ldata->bsize-sent);
				else
					ts=ldata->bsize;

				_i32 rc=SendInt(&ldata->buffer[sent], ts);
				if( rc==SOCKET_ERROR )
				{
					int err;
	#ifdef _WIN32
					err=WSAGetLastError();
	#else
					err=errno;
	#endif
					Log("SOCKET_ERROR in SendData(). BSize: "+nconvert(ldata->bsize)+" WSAGetLastError: "+nconvert(err), LL_DEBUG);
				
					if( ldata->delbuf==true )
					{
						bufmgr->releaseBuffer(ldata->delbufptr);
						ldata->delbuf=false;
					}
					t_send.erase( t_send.begin() );
					delete ldata;
					return -1;
				}
				else if(cmd_id==ID_GET_FILE_RESUME_HASH)
				{
					hash_func.update((unsigned char*)&ldata->buffer[sent], ts);
				}
				sent+=ts;
				sent_bytes+=ts;
				if(cmd_id==ID_GET_FILE_RESUME_HASH)
				{
					if(next_checkpoint-sent_bytes==0)
					{
						hash_func.finalize();
						SendInt((char*)hash_func.raw_digest_int(), 16);
						next_checkpoint+=c_checkpoint_dist;
						if(next_checkpoint>curr_filesize)
							next_checkpoint=curr_filesize;
						hash_func.init();
					}
				}
			}
		}
		else
		{
			Log("ldata is null", LL_DEBUG);
		}

		if( ldata->delbuf==true )
		{
			bufmgr->releaseBuffer( ldata->delbufptr );
			ldata->delbuf=false;
		}
		
		if( ldata->last==true )
		{
			Log("Info: File End", LL_DEBUG);

			if( t_send.size() > 1 )
			{
				Log("Error: Senddata exceeds 1", LL_DEBUG);
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
	Log("Deleting Memory...", LL_DEBUG);
	if(bufmgr!=NULL)
	{
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
	}
	Log("done.", LL_DEBUG);
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
	Log("Client thread stopped", LL_DEBUG);
}

bool CClientThread::isKillable(void)
{
	return killable;
}

bool CClientThread::GetFileBlockdiff(CRData *data)
{
	std::string s_filename;
	if(data->getStr(&s_filename)==false)
		return false;

#ifdef CHECK_IDENT
	std::string ident;
	data->getStr(&ident);
	if(!FileServ::checkIdentity(ident))
	{
		Log("Identity check failed -2", LL_DEBUG);
		return false;
	}
#endif

	std::wstring o_filename=Server->ConvertToUnicode(s_filename);

	_i64 start_offset=0;
	data->getInt64(&start_offset);

	curr_hash_size=0;
	data->getInt64(&curr_hash_size);

	Log("Sending file "+Server->ConvertToUTF8(o_filename), LL_DEBUG);

	std::wstring filename=map_file(o_filename);

	Log("Mapped name: "+Server->ConvertToUTF8(filename), LL_DEBUG);

	if(filename.empty())
	{
		char ch=ID_BASE_DIR_LOST;
		int rc=SendInt(&ch, 1);

		if(rc==SOCKET_ERROR)
		{
			Log("Error: Socket Error - DBG: Send BASE_DIR_LOST -1", LL_DEBUG);
			return false;
		}
		Log("Info: Base dir lost -1", LL_DEBUG);
		return true;
	}

	hash_func.init();

#ifdef _WIN32
	if(filename.size()<2 || (filename[0]!='\\' && filename[1]!='\\' ) )
		filename=L"\\\\?\\"+filename;			
#endif
				
#ifdef _WIN32
#ifndef BACKUP_SEM
	hFile=CreateFileW(filename.c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else
	hFile=CreateFileW(filename.c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#endif
#else //_WIN32
	hFile=open64(Server->ConvertToUTF8(filename).c_str(), O_RDONLY|O_LARGEFILE);
#endif //_WIN32

	if(hFile == INVALID_HANDLE_VALUE)
	{
		hFile=(HANDLE)NULL;
#ifdef CHECK_BASE_PATH
		std::wstring basePath=map_file(getuntil(L"/",o_filename)+L"/");
		if(!isDirectory(basePath))
		{
			char ch=ID_BASE_DIR_LOST;
			int rc=SendInt(&ch, 1);
			if(rc==SOCKET_ERROR)
			{
				Log("Error: Socket Error - DBG: Send BASE_DIR_LOST", LL_DEBUG);
				return false;
			}
			Log("Info: Base dir lost", LL_DEBUG);
			return true;
		}
#endif
					
		char ch=ID_COULDNT_OPEN;
		int rc=SendInt(&ch, 1);
		if(rc==SOCKET_ERROR)
		{
			Log("Error: Socket Error - DBG: Send COULDNT OPEN", LL_DEBUG);
			return false;
		}
		Log("Info: Couldn't open file", LL_DEBUG);
		return true;
	}

	currfilepart=0;
	sendfilepart=0;
	sent_bytes=0;

#ifdef _WIN32
	LARGE_INTEGER filesize;
	GetFileSizeEx(hFile, &filesize);

	curr_filesize=filesize.QuadPart;
#else
	struct stat64 stat_buf;
	fstat64(hFile, &stat_buf);
	
	curr_filesize=stat_buf.st_size;
#endif

	next_checkpoint=start_offset+c_checkpoint_dist;
	if(next_checkpoint>curr_filesize)
		next_checkpoint=curr_filesize;


	CWData sdata;
	sdata.addUChar(ID_FILESIZE);
	sdata.addUInt64(curr_filesize);
	SendInt(sdata.getDataPtr(), sdata.getDataSize());

	if(mutex==NULL)
	{
		mutex=Server->createMutex();
		cond=Server->createCondition();
	}

	state=CS_BLOCKHASH;

	if(chunk_send_thread_ticket==ILLEGAL_THREADPOOL_TICKET)
	{
		IFile * tf=Server->openFileFromHandle((void*)hFile);
		if(tf==NULL)
		{
			Log("Could not open file from handle", LL_ERROR);
			return false;
		}
		chunk_send_thread_ticket=Server->getThreadPool()->execute(new ChunkSendThread(this, tf, curr_hash_size) );
	}
	else
	{
		IScopedLock lock(mutex);
		update_file=Server->openFileFromHandle((void*)hFile);
		if(update_file==NULL)
		{
			Log("Could not open update file from handle", LL_ERROR);
		}
		cond->notify_all();
	}
	hFile=(HANDLE)NULL;

	return true;
}

bool CClientThread::Handle_ID_BLOCK_REQUEST(CRData *data)
{
	SChunk chunk;
	bool b=data->getInt64(&chunk.startpos);
	if(!b)
		return false;
	b=data->getChar(&chunk.transfer_all);
	if(!b)
		return false;

	if(data->getLeft()==big_hash_size+small_hash_size*(c_checkpoint_dist/c_small_hash_dist))
	{
		memcpy(chunk.big_hash, data->getCurrentPtr(), big_hash_size);
		data->incrementPtr(big_hash_size);
		memcpy(chunk.small_hash, data->getCurrentPtr(), small_hash_size*(c_checkpoint_dist/c_small_hash_dist));
	}
	else if(chunk.transfer_all==0)
	{
		return false;
	}

	IScopedLock lock(mutex);
	next_chunks.push(chunk);
	cond->notify_all();

	return true;
}

bool CClientThread::getNextChunk(SChunk *chunk, IFile **new_file, _i64 *new_hash_size)
{
	IScopedLock lock(mutex);
	while(next_chunks.empty() && state==CS_BLOCKHASH && update_file==NULL)
	{
		cond->wait(&lock);
	}

	if(update_file!=NULL)
	{
		*new_file=update_file;
		*new_hash_size=curr_hash_size;
		update_file=NULL;
		return true;
	}

	if(!next_chunks.empty())
	{
		*chunk=next_chunks.front();
		next_chunks.pop();
		return true;
	}
	else
	{
		return false;
	}
}
