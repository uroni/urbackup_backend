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
#include "../urbackupcommon/sha2/sha2.h"

#include <algorithm>
#include <limits.h>
#include <memory.h>
#include "FileServFactory.h"
#include "../urbackupcommon/os_functions.h"
#include "PipeSessions.h"

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <assert.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/uio.h>
#define open64 open
#define off64_t off_t
#define lseek64 lseek
#define O_LARGEFILE 0
#define stat64 stat
#define fstat64 fstat

#if defined(__FreeBSD__)
#define sendfile64(a, b, c, d, e) sendfile(a, b, c, d, NULL, e, 0)
#else
#define sendfile64(a, b, c, d, e) sendfile(a, b, c, e, NULL, 0)
#endif

#endif

#ifndef SEEK_DATA
#define SEEK_DATA 3
#endif
#ifndef SEEK_HOLE
#define SEEK_HOLE 4
#endif

#define CLIENT_TIMEOUT	120
#define CHECK_BASE_PATH
#define SEND_TIMEOUT 300000


CClientThread::CClientThread(SOCKET pSocket, CTCPFileServ* pParent)
	: extra_buffer(NULL)
{
	int_socket=pSocket;

	stopped=false;
	killable=false;
	has_socket=true;

	parent=pParent;

#ifdef _WIN32
	bufmgr=new fileserv::CBufMgr(NBUFFERS, READSIZE);
#else
	bufmgr=NULL;
#endif

	hFile=INVALID_HANDLE_VALUE;

#ifdef _WIN32
	int window_size;
	int window_size_len=sizeof(window_size);
	getsockopt(pSocket, SOL_SOCKET, SO_SNDBUF,(char *) &window_size, &window_size_len );
	Log("Info: Window size="+convert(window_size));
#endif

	close_the_socket=true;
	errcount=0;
	clientpipe=Server->PipeFromSocket(pSocket);
	mutex=NULL;
	cond=NULL;
	state=CS_NONE;
	chunk_send_thread_ticket=ILLEGAL_THREADPOOL_TICKET;
}

CClientThread::CClientThread(IPipe *pClientpipe, CTCPFileServ* pParent, std::vector<char>* extra_buffer)
	: extra_buffer(extra_buffer)
{
	stopped=false;
	killable=false;
	has_socket=false;

	parent=pParent;

	bufmgr=NULL;

	hFile=INVALID_HANDLE_VALUE;

	close_the_socket=false;
	errcount=0;
	clientpipe=pClientpipe;
	state=CS_NONE;
	mutex=NULL;
	cond=NULL;
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

	if(close_the_socket)
	{
		Server->destroy(clientpipe);
	}
}

void CClientThread::operator()(void)
{
#ifdef BACKGROUND_PRIORITY
	ScopedBackgroundPrio background_prio(false);
	if(FileServFactory::backgroundBackupsEnabled())
	{
#ifndef _DEBUG
		background_prio.enable();
#endif
	}
#endif

	while( RecvMessage() && !stopped )
	{
	}
	ReleaseMemory();

	if( hFile!=INVALID_HANDLE_VALUE )
	{
		CloseHandle( hFile );
		hFile=INVALID_HANDLE_VALUE;
	}

	if(chunk_send_thread_ticket!=ILLEGAL_THREADPOOL_TICKET)
	{
		{
			IScopedLock lock(mutex);
			state=CS_NONE;
			while(!next_chunks.empty())
			{
				if(next_chunks.front().update_file!=NULL)
				{
					delete next_chunks.front().pipe_file_user;
					Server->destroy(next_chunks.front().update_file);
				}

				next_chunks.pop();
			}
			cond->notify_all();
		}
		Server->getThreadPool()->waitFor(chunk_send_thread_ticket);
	}

	killable=true;
}

bool CClientThread::RecvMessage()
{
	_i32 rc;
	timeval lon;
	lon.tv_usec=0;
	lon.tv_sec=60;
	if(extra_buffer==NULL || extra_buffer->empty())
	{
		rc=clientpipe->isReadable(lon.tv_sec*1000)?1:0;
		if(clientpipe->hasError())
			rc=-1;
		if( rc==0 )
		{
			Log("1 min Timeout deleting Buffers ("+convert((NBUFFERS*READSIZE)/1024 )+" KB) and waiting 1h more...", LL_DEBUG);
			delete bufmgr;
			bufmgr=NULL;
			lon.tv_sec=3600;
			int n=0;
			while(!stopped && rc==0 && n<60)
			{
				rc=clientpipe->isReadable(lon.tv_sec*1000)?1:0;
				if(clientpipe->hasError())
					rc=-1;
				++n;
			}
		}
	}
	else
	{
		rc=1;
	}
	
	
	if(rc<1)
	{
		Log("Select Error/Timeout in RecvMessage", LL_DEBUG);

		return false;
	}
	else
	{
		if(extra_buffer==NULL || extra_buffer->empty())
		{
			rc=(_i32)clientpipe->Read(buffer, BUFFERSIZE, lon.tv_sec*1000);
		}
		else
		{
			size_t tocopy = (std::min)((size_t)BUFFERSIZE, extra_buffer->size());
			memcpy(buffer, extra_buffer->data(), tocopy);
			extra_buffer->erase(extra_buffer->begin(), extra_buffer->begin()+tocopy);
			rc = static_cast<_i32>(tocopy);
		}

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

			if( !b )
				return false;
		}
	}
	return true;
}

int CClientThread::SendInt(const char *buf, size_t bsize, bool flush)
{
	if(bsize==0)
	{
		clientpipe->shutdown();
		return 0;
	}
	else
	{
        return (int)(clientpipe->Write(buf, bsize, SEND_TIMEOUT, flush)?bsize:SOCKET_ERROR);
	}
}

bool CClientThread::FlushInt()
{
	return clientpipe->Flush(CLIENT_TIMEOUT * 1000);
}

bool CClientThread::ProcessPacket(CRData *data)
{
	uchar id;
	if( data->getUChar(&id)==true )
	{
		switch(id)
		{
		case ID_GET_FILE:
		case ID_GET_FILE_RESUME:
		case ID_GET_FILE_RESUME_HASH:
		case ID_GET_FILE_METADATA_ONLY:
		case ID_GET_FILE_WITH_METADATA:
			{
				errorcode = 0;
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
				int64 metadata_id = 0;
				int64 folder_items=0;
				if(id==ID_GET_FILE_METADATA_ONLY)
				{
					char c_version;
					if(!data->getChar(&c_version))
					{
						break;
					}

					if(c_version!=0)
					{
						break;
					}

					if(!data->getVarInt(&folder_items))
					{
						break;
					}

					if(!data->getVarInt(&metadata_id))
					{
						break;
					}
				}

				with_hashes = id==ID_GET_FILE_RESUME_HASH;

				bool with_sparse = false;

				if(id==ID_GET_FILE_WITH_METADATA)
				{
					char c_version;
					if(!data->getChar(&c_version))
					{
						break;
					}

					if(c_version!=0)
					{
						break;
					}

					char c_with_hash;
					if(!data->getChar(&c_with_hash))
					{
						break;
					}

					with_hashes = c_with_hash!=0;

					if(!data->getVarInt(&metadata_id))
					{
						break;
					}

					char c_with_sparse_handling = 0;
					if (!data->getChar(&c_with_sparse_handling))
					{
						break;
					}

					with_sparse = c_with_sparse_handling != 0;
				}

				_i64 start_offset=0;
				bool offset_set=data->getInt64(&start_offset);
								
				bool is_script = false;
				if(next(s_filename, 0, "SCRIPT|"))
				{
					s_filename = s_filename.substr(7);
					is_script=true;
				}

				std::string o_filename=s_filename;

                if(id!=ID_GET_FILE_METADATA_ONLY)
                {
                    if(!is_script)
                    {
                        Log("Sending file (normal) "+o_filename+" metadata_id="+convert(metadata_id), LL_DEBUG);
                    }
                    else
                    {
                        Log("Sending script output (normal) "+o_filename + " metadata_id=" + convert(metadata_id), LL_DEBUG);
                    }
                }
                else
                {
                    Log("Sending meta data of "+o_filename +" metadata_id=" + convert(metadata_id) , LL_DEBUG);

					assert(metadata_id!=0);
                }
				

				bool allow_exec;
				std::string filename=map_file(o_filename, ident, allow_exec);

				if (is_script)
				{
					filename = FileServ::getRedirectedFn(filename);
				}

				Log("Mapped name: "+filename, LL_DEBUG);

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

				if( with_hashes )
				{
					hash_func.init();
				}

				bool has_file_extents = false;
				std::vector<SExtent> file_extents;

#ifdef _WIN32
				if(!is_script)
				{
					if(filename.size()>=2 && filename[0]=='\\' && filename[1]=='\\' )
					{
						if(filename.size()<3 || filename[2]!='?')
						{
							filename="\\\\?\\UNC"+filename.substr(1);
						}
					}
					else
					{
						filename = "\\\\?\\"+filename;
					}
				}				

				if(bufmgr==NULL && !is_script)
				{
					bufmgr=new fileserv::CBufMgr(NBUFFERS,READSIZE);
				}
#endif
				
				if(is_script)
				{
					ScopedPipeFileUser pipe_file_user;
					IFile* file;
					bool sent_metadata = false;
					if(next(s_filename, 0, "urbackup/FILE_METADATA|"))
					{
						file = PipeSessions::getFile(o_filename, pipe_file_user, std::string(), ident, NULL);
					}
					else if (next(s_filename, 0, "urbackup/TAR|"))
					{
						std::string server_token = getbetween("|", "|", s_filename);
						std::string tar_fn = getafter("urbackup/TAR|" + server_token + "|", s_filename);
						std::string map_res = map_file(tar_fn, ident, allow_exec);
						if (!map_res.empty() && allow_exec)
						{
							file = PipeSessions::getFile(tar_fn, pipe_file_user, server_token, ident, &sent_metadata);
						}
					}
					else if(allow_exec)
					{
						file = PipeSessions::getFile(filename, pipe_file_user, std::string(), ident, NULL);
					}					

					if(!file)
					{
						if (sent_metadata)
						{
							CWData data;
							data.addUChar(ID_FILESIZE);
							data.addUInt64(0);

							int rc = SendInt(data.getDataPtr(), data.getDataSize());
							if (rc == SOCKET_ERROR)
							{
								Log("Error: Socket Error - DBG: SendSize (script metadata)", LL_DEBUG);
								return false;
							}
							break;
						}

						char ch=ID_COULDNT_OPEN;
						int rc=SendInt(&ch, 1);
						if(rc==SOCKET_ERROR)
						{
							Log("Error: Socket Error - DBG: Send COULDNT OPEN", LL_DEBUG);
							return false;
						}
						Log("Info: Couldn't open script", LL_DEBUG);
						break;
					}
					else
					{
						bool b = sendFullFile(file, start_offset, with_hashes);
						if(!b)
						{
							Log("Sending script "+o_filename+" not finished yet", LL_INFO);
						}
						else
						{
							Log("Sent script "+o_filename, LL_INFO);
						}
						break;
					}
				}
				else if(metadata_id!=0 && next(s_filename, 0, "clientdl/"))
				{
					PipeSessions::transmitFileMetadata(filename,
						s_filename, ident, ident, folder_items, metadata_id);
				}
				else if(metadata_id!=0 && s_filename.find("|")!=std::string::npos)
				{
					PipeSessions::transmitFileMetadata(filename,
						getafter("|",s_filename), getuntil("|", s_filename), ident, folder_items, metadata_id);
				}

#ifndef LINUX
				DWORD extra_flags = 0;
				if (id == ID_GET_FILE_METADATA_ONLY)
				{
					extra_flags = FILE_FLAG_OPEN_REPARSE_POINT;
				}
#ifndef BACKUP_SEM
				hFile=CreateFileW(Server->ConvertToWchar(filename).c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else
				hFile=CreateFileW(Server->ConvertToWchar(filename).c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING,
					FILE_FLAG_OVERLAPPED|FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN|extra_flags, NULL);
#endif

				if(hFile == INVALID_HANDLE_VALUE)
				{
#ifdef CHECK_BASE_PATH
					std::string share_name = getuntil("/",o_filename);
					if(!share_name.empty())
					{
						bool allow_exec;
						std::string basePath=map_file(share_name+"/", ident, allow_exec);
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
				sent_bytes=start_offset;

				LARGE_INTEGER filesize;
				GetFileSizeEx(hFile, &filesize);

				curr_filesize=filesize.QuadPart;

				if (curr_filesize == 0)
				{
					with_sparse = false;
				}

				int64 n_sparse_extents;

				if (with_sparse)
				{
					int64 new_fsize = getFileExtents(curr_filesize, n_sparse_extents, file_extents, has_file_extents, start_offset);

					if (new_fsize >= 0)
					{
						curr_filesize = new_fsize;
					}
					else
					{
						Log("Getting extents of file \"" + filename + "\" failed", LL_WARNING);
						start_offset = sent_bytes;
					}
				}
				else
				{
					has_file_extents = false;
				}

				next_checkpoint= sent_bytes + c_checkpoint_dist;
				if(next_checkpoint>curr_filesize)
					next_checkpoint=curr_filesize;

				BY_HANDLE_FILE_INFORMATION fi;
				if(GetFileInformationByHandle(hFile, &fi)==TRUE &&
					fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					curr_filesize = 0;
				}

				CWData data;
				if (with_sparse && n_sparse_extents>0)
				{
					data.addUChar(ID_FILESIZE_AND_EXTENTS);
				}
				else
				{
					data.addUChar(ID_FILESIZE);
				}
				data.addUInt64(curr_filesize);

				if (with_sparse && n_sparse_extents>0)
				{
					data.addInt64(n_sparse_extents);
				}

				int rc=SendInt(data.getDataPtr(), data.getDataSize());
				if(rc==SOCKET_ERROR)
				{
					Log("Error: Socket Error - DBG: SendSize", LL_DEBUG);
					CloseHandle(hFile);
					hFile=INVALID_HANDLE_VALUE;
					return false;
				}

				if (with_sparse && n_sparse_extents>0)
				{
					if (!sendExtents(file_extents, filesize.QuadPart, n_sparse_extents))
					{
						Log("Error sending sparse extents", LL_DEBUG);
						CloseHandle(hFile);
						hFile = INVALID_HANDLE_VALUE;
						return false;
					}
				}
				else
				{
					has_file_extents = false;
				}

				if(curr_filesize==0 || id==ID_GET_FILE_METADATA_ONLY)
				{
					CloseHandle(hFile);
					hFile=INVALID_HANDLE_VALUE;
					break;
				}

				assert(!(GetFileAttributesW(Server->ConvertToWchar(filename).c_str()) & FILE_ATTRIBUTE_DIRECTORY));

				size_t extent_pos = 0;

				for(_i64 i=start_offset;i<filesize.QuadPart && !stopped;)
				{
					bool last = false;

					_u32 toread;
					if (has_file_extents)
					{
						toread = (std::min)(static_cast<_u32>(file_extents[extent_pos].offset + file_extents[extent_pos].size - i), (_u32)READSIZE);

						if (i + toread == file_extents[extent_pos].offset + file_extents[extent_pos].size
							&& extent_pos + 1 >= file_extents.size())
						{
							last = true;
						}
					}
					else
					{
						toread = READSIZE;
					}

					if(i+toread>=filesize.QuadPart)
					{
						last=true;
						Log("Reading last file part", LL_DEBUG);
					}

					while(bufmgr->nfreeBufffer()==0 && !stopped)
					{
						int rc;
						SleepEx(0,true);
						rc=SendData();
						if(rc==-1)
						{
							Log("Error: Send failed in file loop -1", LL_DEBUG);
							stopped=true;
						}
						else if(rc==0)
						{
							SleepEx(1,true);
						}
					}

					if(errorcode!=0)
					{
						Log("Error occurred while reading from file. Errorcode is "+convert(errorcode), LL_ERROR);
						stopped=true;						
					}

					if( !stopped && hFile!=INVALID_HANDLE_VALUE)
					{
						if(!ReadFilePart(hFile, i, last, toread))
						{
							Log("Reading from file failed. Last error is "+convert((unsigned int)GetLastError()), LL_ERROR);
							stopped=true;
						}
					}

					if(FileServ::isPause() )
					{
						DWORD starttime=GetTickCount();
						while(GetTickCount()-starttime<5000 && !stopped)
						{
							SleepEx(500,true);

							int rc=SendData();
							if(rc==-1)
							{
								Log("Error: Send failed in file pause loop -2", LL_DEBUG);
								stopped=true;

							}
						}
					}

					i += toread;

					if (has_file_extents
						&& i == file_extents[extent_pos].offset + file_extents[extent_pos].size)
					{
						++extent_pos;

						if (extent_pos < file_extents.size())
						{
							i = file_extents[extent_pos].offset;
						}
						else
						{
							break;
						}
					}
				}

				while(bufmgr->nfreeBufffer()!=NBUFFERS && !stopped)
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
						stopped=true;

					}
					else if(rc==0)
					{
						SleepEx(1,true);
					}
				}

				while(stopped && bufmgr->nfreeBufffer()!=NBUFFERS)
				{
					SleepEx(100, true);

					for(size_t i=0;i<t_send.size();++i)
					{
						if(t_send[i]->delbuf)
						{
							bufmgr->releaseBuffer(t_send[i]->delbufptr);
						}
						delete t_send[i];
					}
					t_send.clear();
				}

				if( !stopped )
				{
					Log("Closed file.", LL_DEBUG);
					if(hFile!=INVALID_HANDLE_VALUE)
					{
						CloseHandle(hFile);
						hFile=INVALID_HANDLE_VALUE;
					}
				}
#else //LINUX
                if(id==ID_GET_FILE_METADATA_ONLY || isDirectory(filename))
                {
                    CWData data;
                    data.addUChar(ID_FILESIZE);
                    data.addUInt64(little_endian(static_cast<uint64>(0)));

                    int rc=SendInt(data.getDataPtr(), data.getDataSize() );
                    if(rc==SOCKET_ERROR)
                    {
                        Log("Error: Socket Error - DBG: Send file size", LL_DEBUG);
                    }
                    break;
                }

				int flags = O_RDONLY | O_LARGEFILE;
#if defined(O_CLOEXEC)
				flags |= O_CLOEXEC;
#endif
				hFile=open64(filename.c_str(), flags);
				
				if(hFile == INVALID_HANDLE_VALUE)
				{
#ifdef CHECK_BASE_PATH
					std::string share_name = getuntil("/",o_filename);
					if(!share_name.empty())
					{
						bool allow_exec;
						std::string basePath=map_file(share_name+"/", ident, allow_exec);
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
				
				struct stat64 stat_buf;
				fstat64(hFile, &stat_buf);
				
				off64_t filesize=stat_buf.st_size;
				curr_filesize=filesize;

				int64 send_filesize = curr_filesize;

				int64 n_sparse_extents;
				if (with_sparse)
				{
					send_filesize = getFileExtents(curr_filesize, n_sparse_extents, file_extents, has_file_extents, start_offset);

					if (send_filesize<0)
					{
						Log("Error resetting file pointer", LL_DEBUG);
						CloseHandle(hFile);
						hFile = INVALID_HANDLE_VALUE;
						return false;
					}
				}
				
				CWData data;
				if (has_file_extents)
				{
					data.addUChar(ID_FILESIZE_AND_EXTENTS);
				}
				else
				{
					data.addUChar(ID_FILESIZE);
				}
				data.addUInt64(little_endian(static_cast<uint64>(send_filesize)));
				if (has_file_extents)
				{
					data.addInt64(n_sparse_extents);
				}

				int rc=SendInt(data.getDataPtr(), data.getDataSize() );	
				if(rc==SOCKET_ERROR)
				{
					Log("Error: Socket Error - DBG: SendSize", LL_DEBUG);
					CloseHandle(hFile);
					hFile=INVALID_HANDLE_VALUE;
					return false;
				}			

				if (has_file_extents)
				{
					if (!sendSparseExtents(file_extents))
					{
						Log("Error sending sparse extents", LL_DEBUG);
						CloseHandle(hFile);
						hFile = INVALID_HANDLE_VALUE;
						return false;
					}
				}
				
				if(filesize==0 || id==ID_GET_FILE_METADATA_ONLY)
				{
					CloseHandle(hFile);
					hFile=INVALID_HANDLE_VALUE;
					break;
				}
				
				off64_t foffset=start_offset;

				unsigned int s_bsize=8192;

				if( !with_hashes )
				{
					s_bsize=32768;
					next_checkpoint=curr_filesize;
				}
				else
				{
					next_checkpoint=start_offset+c_checkpoint_dist;
					if(next_checkpoint>curr_filesize)
					    next_checkpoint=curr_filesize;
				}

				if((clientpipe!=NULL || with_hashes) && foffset>0)
				{
					if(lseek64(hFile, foffset, SEEK_SET)!=foffset)
					{
						Log("Error: Seeking in file failed (5043)", LL_ERROR);
						CloseHandle(hFile);
						return false;
					}
				}				

				std::vector<char> buf;
				buf.resize(s_bsize);

				bool has_error=false;
				size_t extent_pos = 0;

				if (has_file_extents)
				{
					while (extent_pos < file_extents.size()
						&& foffset >= file_extents[extent_pos].offset+ file_extents[extent_pos].size)
					{
						++extent_pos;
					}
				}

				bool last_sent_hash = false;

				while (foffset < filesize)
				{
					if (has_file_extents)
					{
						bool is_finished = false;
						while (extent_pos < file_extents.size()
							&& foffset >= file_extents[extent_pos].offset)
						{
							foffset += file_extents[extent_pos].size;
							next_checkpoint += file_extents[extent_pos].size;

							if (next_checkpoint>curr_filesize)
								next_checkpoint = curr_filesize;

							if (clientpipe != NULL || with_hashes)
							{
								off64_t rc = lseek64(hFile, foffset, SEEK_SET);

								if (rc != foffset)
								{
									Log("Error seeking after sparse extent", LL_ERROR);
									CloseHandle(hFile);
									return false;
								}
							}

							if (foffset >= filesize
								&& last_sent_hash)
							{
								is_finished = true;
							}

							++extent_pos;
						}

						if (is_finished)
						{
							break;
						}
					}
				
					size_t count=(std::min)((size_t)s_bsize, (size_t)(next_checkpoint-foffset));

					if (has_file_extents)
					{
						if (extent_pos < file_extents.size())
						{
							count = static_cast<size_t>((std::min)(file_extents[extent_pos].offset - foffset, static_cast<int64>(count)));
						}
					}

					if( clientpipe==NULL && !with_hashes && count>0 )
					{
						#if defined(__APPLE__) || defined(__FreeBSD__)
						ssize_t rc=sendfile64(int_socket, hFile, foffset, count, reinterpret_cast<off_t*>(&count));
						if(rc==0)
						{
							foffset+=count;
							rc=count;
						}
						#else			
						ssize_t rc=sendfile64(int_socket, hFile, &foffset, count);
						#endif
						if(rc<0)
						{
							Log("Error: Reading and sending from file failed. Errno: "+convert(errno), LL_DEBUG);
							CloseHandle(hFile);
							return false;
						}
						else if(rc==0 && foffset<filesize && errno == 0) //other process made the file smaller
						{
							memset(buf.data(), 0, s_bsize);
							while(foffset<filesize)
							{
								rc=SendInt(buf.data(), (std::min)((size_t)s_bsize, (size_t)(next_checkpoint-foffset)));
								if(rc==SOCKET_ERROR)
								{
									Log("Error: Sending data failed");
									CloseHandle(hFile);
									return false;
								}
							}
						}
					}
					else
					{
						if (count > 0)
						{
							ssize_t rc = read(hFile, buf.data(), count);

							if (rc == 0 && rc < count && errno == 0)  //other process made the file smaller
							{
								memset(buf.data() + rc, 0, count - rc);
								rc = count;
							}
							else if (rc < 0)
							{
								Log("Error: Reading from file failed. Errno: " + convert(errno), LL_DEBUG);
								CloseHandle(hFile);
								return false;
							}

							rc = SendInt(buf.data(), rc);
							if (rc == SOCKET_ERROR)
							{
								Log("Error: Sending data failed");
								CloseHandle(hFile);
								return false;
							}
							else if (with_hashes)
							{
								hash_func.update((unsigned char*)buf.data(), rc);
							}

							foffset += rc;
							last_sent_hash = false;
						}
						
						if(with_hashes && foffset==next_checkpoint)
						{
							hash_func.finalize();
							SendInt((char*)hash_func.raw_digest_int(), 16);
							next_checkpoint+=c_checkpoint_dist;
							if(next_checkpoint>curr_filesize)
								next_checkpoint=curr_filesize;
							
							hash_func.init();
							last_sent_hash = true;
						}
					}
					if(FileServ::isPause() )
					{
						Sleep(500);
					}
				}
				
				CloseHandle(hFile);
				hFile=INVALID_HANDLE_VALUE;
#endif

			}break;
		case ID_GET_FILE_BLOCKDIFF:
			{
				bool b=GetFileBlockdiff(data, false);
				if(!b)
					return false;
			}break;
		case ID_GET_FILE_BLOCKDIFF_WITH_METADATA:
			{
				bool b=GetFileBlockdiff(data, true);
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
		case ID_GET_FILE_HASH_AND_METADATA:
			{
				if(!GetFileHashAndMetadata(data))
				{
					return false;
				}
			}break;

		case ID_INFORM_METADATA_STREAM_END:
			{
				if(!InformMetadataStreamEnd(data))
				{
					return false;
				}
			} break;
		case ID_SCRIPT_FINISH:
			{
				if(!FinishScript(data))
				{
					return false;
				}
			} break;
		case ID_FREE_SERVER_FILE:
			{
				if (chunk_send_thread_ticket != ILLEGAL_THREADPOOL_TICKET)
				{
					queueChunk(SChunk(ID_FREE_SERVER_FILE));
				}
			}break;
		case ID_FLUSH_SOCKET:
			{
				if (chunk_send_thread_ticket != ILLEGAL_THREADPOOL_TICKET)
				{
					queueChunk(SChunk(ID_FLUSH_SOCKET));
				}
				else
				{
					Server->Log("Received flush.", LL_DEBUG);
					if (!clientpipe->Flush(CLIENT_TIMEOUT * 1000))
					{
						Server->Log("Error flushing output socket", LL_INFO);
					}
				}
			} break;
		}
	}
	if( stopped )
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

	if(dwErrorCode!=ERROR_SUCCESS)
	{
		*ldata->errorcode = dwErrorCode;
	}

	if(*ldata->errorcode==0
		&& dwNumberOfBytesTransfered > 0)
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
bool CClientThread::ReadFilePart(HANDLE hFile, _i64 offset, bool last, _u32 toread)
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
		Log("Info: Free Buffers="+convert(bufmgr->nfreeBufffer()), LL_DEBUG );
		delete ldata;
		return true;
	}

	ldata->t_send=&t_send;
	ldata->t_unsend=&t_unsend;
	ldata->last=last;
	ldata->filepart=currfilepart;
	ldata->sendfilepart=&sendfilepart;
	ldata->errorcode=&errorcode;
	overlap->hEvent=ldata;

	BOOL b=ReadFileEx(hFile, ldata->buffer, toread, overlap, FileIOCompletionRoutine);

	++currfilepart;

	if( b==FALSE)
	{
		Log("Error: Can't start reading from File", LL_DEBUG);
		return false;
	}
	else
	{
		return true;
	}
}
#else
bool CClientThread::ReadFilePart(HANDLE hFile, _i64 offset, bool last, _u32 toread)
{
	return false;
}
#endif

int CClientThread::SendData()
{
	if( t_send.size()==0 )
		return 0;

	SSendData* ldata=t_send[0];

	_i32 ret;
	ret=clientpipe->isWritable(CLIENT_TIMEOUT*1000)?1:0;

	if(ret < 1)
	{
		Log("Client Timeout occured.", LL_DEBUG);
		
		if( ldata->delbuf )
		{
			bufmgr->releaseBuffer(ldata->delbufptr);
			ldata->delbuf=false;
		}
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
				if(with_hashes)
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
					Log("SOCKET_ERROR in SendData(). BSize: "+convert(ldata->bsize)+" WSAGetLastError: "+convert(err), LL_DEBUG);
				
					if( ldata->delbuf )
					{
						bufmgr->releaseBuffer(ldata->delbufptr);
						ldata->delbuf=false;
					}
					t_send.erase( t_send.begin() );
					delete ldata;
					return -1;
				}
				else if(with_hashes)
				{
					hash_func.update((unsigned char*)&ldata->buffer[sent], ts);
				}
				sent+=ts;
				sent_bytes+=ts;
				if(with_hashes)
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
	clientpipe->shutdown();
	Log("Client thread stopped", LL_DEBUG);
}

bool CClientThread::isKillable(void)
{
	return killable;
}

bool CClientThread::GetFileBlockdiff(CRData *data, bool with_metadata)
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

	int64 metadata_id=0;

	bool with_sparse = false;

	if(with_metadata)
	{
		char c_version;
		if(!data->getChar(&c_version))
		{
			return false;
		}

		if(c_version!=0)
		{
			return false;
		}

		if(!data->getVarInt(&metadata_id))
		{
			return false;
		}

		char c_with_sparse;
		if (!data->getChar(&c_with_sparse))
		{
			return false;
		}

		with_sparse = c_with_sparse == 1;
	}

	bool is_script=false;

	if(next(s_filename, 0, "SCRIPT|"))
	{
		is_script=true;
		s_filename = s_filename.substr(7);
	}

	std::string o_filename=s_filename;

	_i64 start_offset=0;
	data->getInt64(&start_offset);

	_i64 curr_hash_size=0;
	data->getInt64(&curr_hash_size);

	_i64 requested_filesize=-1;
	data->getInt64(&requested_filesize);

	Log("Sending file (chunked) "+o_filename, LL_DEBUG);

	bool allow_exec;
	std::string filename=map_file(o_filename, ident, allow_exec);

	if (is_script)
	{
		filename = FileServ::getRedirectedFn(filename);
	}

	Log("Mapped name: "+filename, LL_DEBUG);

	state=CS_BLOCKHASH;

	if(filename.empty())
	{
		queueChunk(SChunk(ID_BASE_DIR_LOST));
		Log("Info: Base dir lost -1", LL_DEBUG);
		return true;
	}

	hash_func.init();

#ifdef _WIN32
	if(!is_script)
	{
		if(filename.size()>=2 && filename[0]=='\\' && filename[1]=='\\' )
		{
			if(filename.size()<3 || filename[2]!='?')
			{
				filename="\\\\?\\UNC"+filename.substr(1);
			}
		}
		else
		{
			filename = "\\\\?\\"+filename;
		}
	}
#endif

	std::auto_ptr<ScopedPipeFileUser> pipe_file_user;
	IFile* srv_file = NULL;
	if(is_script)
	{
		pipe_file_user.reset(new ScopedPipeFileUser);

		if (next(s_filename, 0, "urbackup/TAR|"))
		{
			std::string server_token = getbetween("|", "|", s_filename);
			std::string tar_fn = getafter("urbackup/TAR|" + server_token + "|", s_filename);
			std::string map_res = map_file(tar_fn, ident, allow_exec);
			if (!map_res.empty() && allow_exec)
			{
				srv_file = PipeSessions::getFile(tar_fn, *pipe_file_user, server_token, ident, NULL);
			}
		}
		else if(allow_exec)
		{
			srv_file = PipeSessions::getFile(filename, *pipe_file_user, std::string(), ident, NULL);
		}

		if(srv_file==NULL)
		{
			queueChunk(SChunk(ID_COULDNT_OPEN));
			Log("Info: Couldn't open script", LL_DEBUG);
			return true;
		}
	}
	else
	{
		if(metadata_id!=0 && next(s_filename, 0, "clientdl/"))
		{
			PipeSessions::transmitFileMetadata(filename,
				s_filename, ident, ident, 0, metadata_id);
		}
		else if(metadata_id!=0 && s_filename.find("|"))
		{
			PipeSessions::transmitFileMetadata(filename,
				getafter("|",s_filename), getuntil("|", s_filename), ident, 0, metadata_id);
		}

#ifdef _WIN32
#ifndef BACKUP_SEM
		hFile=CreateFileW(Server->ConvertToWchar(filename).c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else
		hFile=CreateFileW(Server->ConvertToWchar(filename).c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#endif
#else //_WIN32
		int flags = O_RDONLY | O_LARGEFILE;
#if defined(O_CLOEXEC)
		flags |= O_CLOEXEC;
#endif
		hFile=open64(filename.c_str(), flags);
#endif //_WIN32

		if(hFile == INVALID_HANDLE_VALUE)
		{
	#ifdef CHECK_BASE_PATH
			std::string share_name = getuntil("/",o_filename);
			if(!share_name.empty())
			{
				bool allow_exec;
				std::string basePath=map_file(share_name+"/", ident, allow_exec);
				if(!isDirectory(basePath))
				{
					queueChunk(SChunk(ID_BASE_DIR_LOST));
					Log("Info: Base dir lost", LL_DEBUG);
					return true;
				}
			}			
	#endif
					
			queueChunk(SChunk(ID_COULDNT_OPEN));
			Log("Info: Couldn't open file", LL_DEBUG);
			return true;
		}
	}

	currfilepart=0;
	sendfilepart=0;
	sent_bytes=0;

	if(!is_script)
	{
#ifdef _WIN32
		LARGE_INTEGER filesize;
		GetFileSizeEx(hFile, &filesize);

		curr_filesize=filesize.QuadPart;
#else
		struct stat64 stat_buf;
		fstat64(hFile, &stat_buf);

		curr_filesize=stat_buf.st_size;
#endif
	}
	else
	{
        curr_filesize = srv_file->Size();
	}

	next_checkpoint=start_offset+c_checkpoint_dist;
	if(next_checkpoint>curr_filesize && curr_filesize>0)
		next_checkpoint=curr_filesize;

	if(!is_script)
	{
		srv_file = Server->openFileFromHandle((void*)hFile);

		if(srv_file==NULL)
		{
			CloseHandle(hFile);
			queueChunk(SChunk(ID_COULDNT_OPEN));
			Log("Info: Couldn't open file from handle", LL_ERROR);
			return true;
		}
	}

	SChunk chunk;
	chunk.update_file = srv_file;
	chunk.startpos = curr_filesize;
	chunk.hashsize = curr_hash_size;
	chunk.requested_filesize = requested_filesize;
	chunk.pipe_file_user = pipe_file_user.get();
	chunk.with_sparse = is_script ? false : with_sparse;
	pipe_file_user.release();

	hFile=INVALID_HANDLE_VALUE;

	queueChunk(chunk);

	return true;
}

bool CClientThread::Handle_ID_BLOCK_REQUEST(CRData *data)
{
	SChunk chunk;
	chunk.update_file = NULL;
	bool b=data->getInt64(&chunk.startpos);
	if(!b)
		return false;
	b=data->getChar(&chunk.transfer_all);
	if(!b)
		return false;

	if(data->getLeft()==big_hash_size+small_hash_size*(c_checkpoint_dist/c_small_hash_dist))
	{
		memcpy(chunk.big_hash, data->getCurrDataPtr(), big_hash_size);
		data->incrementPtr(big_hash_size);
		memcpy(chunk.small_hash, data->getCurrDataPtr(), small_hash_size*(c_checkpoint_dist/c_small_hash_dist));
	}
	else if(chunk.transfer_all==0)
	{
		return false;
	}

	queueChunk(chunk);

	return true;
}

bool CClientThread::getNextChunk(SChunk *chunk, bool has_error)
{
	IScopedLock lock(mutex);

	if(has_error)
	{
		clientpipe->shutdown();
		return false;
	}
	
	while(next_chunks.empty() && state==CS_BLOCKHASH)
	{
		cond->wait(&lock);
	}

	if(!next_chunks.empty() && state==CS_BLOCKHASH)
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

void CClientThread::queueChunk( SChunk chunk )
{
	if(chunk_send_thread_ticket==ILLEGAL_THREADPOOL_TICKET)
	{
		if(mutex==NULL)
		{
			mutex=Server->createMutex();
			cond=Server->createCondition();
		}

		next_chunks.push(chunk);

		chunk_send_thread_ticket=Server->getThreadPool()->execute(new ChunkSendThread(this), "filesrv: chunk send");
	}
	else
	{
		IScopedLock lock(mutex);

		next_chunks.push(chunk);
		cond->notify_all();
	}
}

bool CClientThread::GetFileHashAndMetadata( CRData* data )
{
	std::string s_filename;
	if(data->getStr(&s_filename)==false)
		return false;

#ifdef CHECK_IDENT
	std::string ident;
	data->getStr(&ident);
	if(!FileServ::checkIdentity(ident))
	{
		Log("Identity check failed -hash", LL_DEBUG);
		return false;
	}
#endif

	std::string o_filename=s_filename;

	Log("Calculating hash of file "+o_filename, LL_DEBUG);

	bool allow_exec;
	std::string filename=map_file(o_filename, ident, allow_exec);

	Log("Mapped name: "+filename, LL_DEBUG);

	if(filename.empty())
	{
		char ch=ID_BASE_DIR_LOST;
		int rc=SendInt(&ch, 1);
		if(rc==SOCKET_ERROR)
		{
			Log("Error: Socket Error - DBG: Send BASE_DIR_LOST -hash2", LL_DEBUG);
			return false;
		}
		Log("Info: Base dir lost -hash", LL_DEBUG);
		return true;
	}

#ifdef _WIN32
	if(filename.size()>=2 && filename[0]=='\\' && filename[1]=='\\' )
	{
		if(filename.size()<3 || filename[2]!='?')
		{
			filename="\\\\?\\UNC"+filename.substr(1);
		}
	}
	else
	{
		filename = "\\\\?\\"+filename;
	}		
#endif

#ifdef _WIN32
#ifndef BACKUP_SEM
	hFile=CreateFileW(Server->ConvertToWchar(filename).c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else
	hFile=CreateFileW(Server->ConvertToWchar(filename).c_str(), FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#endif
#else //_WIN32
	int flags = O_RDONLY | O_LARGEFILE;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
	hFile=open64(filename.c_str(), flags);
#endif //_WIN32

	if(hFile == INVALID_HANDLE_VALUE)
	{
#ifdef CHECK_BASE_PATH
		std::string share_name = getuntil("/",o_filename);
		bool allow_exec;
		std::string basePath=map_file(share_name + "/", ident, allow_exec);
		if(!isDirectory(basePath))
		{
			char ch=ID_BASE_DIR_LOST;
			int rc=SendInt(&ch, 1);
			if(rc==SOCKET_ERROR)
			{
				Log("Error: Socket Error - DBG: Send BASE_DIR_LOST -hash", LL_DEBUG);
				return false;
			}
			Log("Info: Base dir lost -hash", LL_DEBUG);
			return false;
		}
#endif

		char ch=ID_COULDNT_OPEN;
		int rc=SendInt(&ch, 1);
		if(rc==SOCKET_ERROR)
		{
			Log("Error: Socket Error - DBG: Send COULDNT OPEN -hash", LL_DEBUG);
			return false;
		}
		Log("Info: Couldn't open file -hash", LL_DEBUG);
		return false;
	}

	std::auto_ptr<IFile> tf(Server->openFileFromHandle((void*)hFile));
	if(tf.get()==NULL)
	{
		Log("Could not open file from handle -hash", LL_ERROR);
		return false;
	}

	unsigned int read;
	std::vector<char> buffer;
	buffer.resize(32768);
	
	sha512_ctx ctx;
	sha512_init(&ctx);

	while( (read=tf->Read(&buffer[0], static_cast<_u32>(buffer.size())))>0 )
	{
		sha512_update(&ctx, reinterpret_cast<const unsigned char*>(&buffer[0]), read);
	}

	std::string dig;
	dig.resize(64);
	sha512_final(&ctx, reinterpret_cast<unsigned char*>(&dig[0]));

	CWData send_data;
	send_data.addUChar(ID_FILE_HASH_AND_METADATA);
	send_data.addUShort(0);
	send_data.addString(dig);
	send_data.addString(FileServFactory::getPermissionCallback()->getPermissions(filename));
	send_data.addInt64(tf->Size());

	SFile file_metadata = getFileMetadata(filename);
	if(file_metadata.name.empty())
	{
		Log("Could not get metadata of file", LL_DEBUG);
		return false;
	}

	send_data.addInt64(file_metadata.last_modified);
	send_data.addInt64(file_metadata.created);

	unsigned short send_data_size=static_cast<unsigned short>(send_data.getDataSize());

	memcpy(send_data.getDataPtr()+1, &send_data_size, sizeof(send_data_size) );

	int rc=SendInt(send_data.getDataPtr(), send_data.getDataSize());
	if(rc==SOCKET_ERROR)
	{
		Log("Socket error while sending hash", LL_DEBUG);
		return false;
	}

	return true;
}

bool CClientThread::sendFullFile(IFile* file, _i64 start_offset, bool with_hashes)
{
	curr_filesize = file->Size();

	if (start_offset != 0)
	{
		Log("Sending pipe file from offset " + convert(start_offset), LL_DEBUG);
	}

	if (!file->Seek(start_offset))
	{
		Log("Error: Seeking in file failed (5044) to " + convert(start_offset) + " file size is " + convert(file->Size()), LL_ERROR);

		char ch = ID_BASE_DIR_LOST;
		int rc = SendInt(&ch, 1);

		if (rc == SOCKET_ERROR)
		{
			Log("Error: Socket Error - DBG: Send BASE_DIR_LOST -3", LL_DEBUG);
		}

		return false;
	}

	CWData data;
	data.addUChar(ID_FILESIZE);
	data.addUInt64(little_endian(static_cast<uint64>(curr_filesize)));

	int rc=SendInt(data.getDataPtr(), data.getDataSize(), true);	
	if(rc==SOCKET_ERROR)
	{
		return false;
	}

	if(curr_filesize==0)
	{
		return true;
	}

	unsigned int s_bsize=8192;

	if(!with_hashes)
	{
		s_bsize=32768;
		if(curr_filesize>=0)
		{
			next_checkpoint=curr_filesize;
		}
		else
		{
			next_checkpoint = LLONG_MAX;
		}		
	}
	else
	{
		next_checkpoint=start_offset+c_checkpoint_dist;
		if(next_checkpoint>curr_filesize && curr_filesize>0)
			next_checkpoint=curr_filesize;
	}

	std::vector<char> buf;
	buf.resize(s_bsize);

	bool has_error=false;

	int64 foffset = start_offset;
	bool is_eof=false;

	while( foffset < curr_filesize || (curr_filesize==-1 && !is_eof) )
	{
		size_t count=(std::min)((size_t)s_bsize, (size_t)(next_checkpoint-foffset));
			
		bool has_error = false;
		
		_u32 rc = file->Read(foffset, &buf[0], static_cast<_u32>(count), &has_error);

		if(rc==0 && curr_filesize==-1 && file->Size()!=-1)
		{
			is_eof=true;
		}
			
		if(rc>=0 && rc<count && curr_filesize!=-1 && !has_error)
		{
			memset(&buf[rc], 0, count-rc);
			rc=static_cast<_u32>(count);
		}
		else if(has_error)
		{
			Log("Error: Reading from file failed.", LL_DEBUG);
			return false;
		}

		bool curr_flush = rc<count;

		rc=SendInt(buf.data(), rc, curr_flush);
		if(rc==SOCKET_ERROR)
		{
			Log("Error: Sending data failed");
			return false;
		}
		else if(with_hashes)
		{
			hash_func.update((unsigned char*)buf.data(), rc);
		}

		foffset+=rc;

		if(with_hashes && foffset==next_checkpoint)
		{
			hash_func.finalize();
			SendInt((char*)hash_func.raw_digest_int(), 16);
			next_checkpoint+=c_checkpoint_dist;
			if(next_checkpoint>curr_filesize && curr_filesize>0)
				next_checkpoint=curr_filesize;

			hash_func.init();
		}

		if(FileServ::isPause() )
		{
			Sleep(500);
		}
	}

	if(curr_filesize==-1)
	{
		if(!clientpipe->Flush(CLIENT_TIMEOUT*1000))
		{
			Server->Log("Error flushing output socket (3)", LL_INFO);
		}

		//script needs one reconnect
		clientpipe->shutdown();
	}

	return curr_filesize!=-1;
}

bool CClientThread::InformMetadataStreamEnd( CRData * data )
{
#ifdef CHECK_IDENT
	std::string ident;
	data->getStr(&ident);
	if(!FileServ::checkIdentity(ident))
	{
		Log("Identity check failed -hash", LL_DEBUG);
		return false;
	}
#endif

	std::string token;
	data->getStr(&token);
	
	PipeSessions::metadataStreamEnd(token);

	char ch = ID_PONG;
	int rc = SendInt(&ch, 1);
	if(rc==SOCKET_ERROR)
	{
		Log("Error: Sending data failed (InformMetadataStreamEnd)");
		return false;
	}

	if(!clientpipe->Flush(CLIENT_TIMEOUT*1000))
	{
		Server->Log("Error flushing output socket (2)", LL_INFO);
	}

	return true;
}

bool CClientThread::FinishScript( CRData * data )
{
#ifdef CHECK_IDENT
	std::string ident;
	data->getStr(&ident);
	if(!FileServ::checkIdentity(ident))
	{
		Log("Identity check failed -FinishScript", LL_DEBUG);
		return false;
	}
#endif

	std::string s_filename;
	data->getStr(&s_filename);

	if(next(s_filename, 0, "SCRIPT|"))
	{
		s_filename = s_filename.substr(7);
	}
	else
	{
		return false;
	}

	Log("Finishing script "+s_filename, LL_DEBUG);

	bool allow_exec;
	std::string filename=map_file(s_filename, ident, allow_exec);
	filename = FileServ::getRedirectedFn(filename);

	Log("Mapped name: "+filename, LL_DEBUG);

	if(filename.empty())
	{
		char ch=ID_BASE_DIR_LOST;
		int rc=SendInt(&ch, 1);

		if(rc==SOCKET_ERROR)
		{
			Log("Error: Socket Error - DBG: Send BASE_DIR_LOST -4", LL_DEBUG);
			return false;
		}
		Log("Info: Base dir lost -2", LL_DEBUG);
		clientpipe->Flush(CLIENT_TIMEOUT*1000);
		return false;
	}

	ScopedPipeFileUser pipe_file_user;
	IFile* file;
	std::string f_name;
	bool sent_metadata = false;
	if(next(s_filename, 0, "urbackup/FILE_METADATA|"))
	{
		f_name = s_filename;
		file = PipeSessions::getFile(s_filename, pipe_file_user, std::string(), ident, NULL);
	}
	else if (next(s_filename, 0, "urbackup/TAR|"))
	{
		std::string server_token = getbetween("|", "|", s_filename);
		f_name = getafter("urbackup/TAR|" + server_token + "|", s_filename);
		std::string map_res = map_file(f_name, ident, allow_exec);
		if (!map_res.empty() && allow_exec)
		{
			file = PipeSessions::getFile(f_name, pipe_file_user, server_token, ident, &sent_metadata);
		}
	}
	else if(allow_exec)
	{
		f_name = filename;
		file = PipeSessions::getFile(filename, pipe_file_user, std::string(), ident, NULL);
	}			

	bool ret=false;

	if(!file && !sent_metadata)
	{
		char ch=ID_COULDNT_OPEN;
		int rc=SendInt(&ch, 1);
		if(rc==SOCKET_ERROR)
		{
			Log("Error: Socket Error - DBG: Send COULDNT OPEN", LL_DEBUG);
			return false;
		}
		Log("Info: Couldn't open script", LL_DEBUG);
	}
	else
	{
		pipe_file_user.reset(NULL);

		PipeSessions::removeFile(f_name);

		char ch = ID_PONG;
		int rc = SendInt(&ch, 1);
		if(rc==SOCKET_ERROR)
		{
			Log("Error: Sending data failed (FinishScript)");
			return false;
		}

		ret=true;
	}

	if(!clientpipe->Flush(CLIENT_TIMEOUT*1000))
	{
		Server->Log("Error flushing output socket (2)", LL_INFO);
		ret=false;
	}

	return ret;
}

int64 CClientThread::getFileExtents(int64 fsize, int64& n_sparse_extents, std::vector<SExtent>& file_extents, bool& has_file_extents, int64& start_offset)
{
	int64 real_fsize = 0;
	n_sparse_extents = 0;
#ifdef _WIN32
	FILE_ALLOCATED_RANGE_BUFFER query_range;
	query_range.FileOffset.QuadPart = 0;
	query_range.Length.QuadPart = fsize;

	int64 last_sparse_pos = 0;

	std::vector<FILE_ALLOCATED_RANGE_BUFFER> res_buffer;
	res_buffer.resize(1000);

	while (true)
	{
		DWORD output_bytes;
		BOOL b = DeviceIoControl(hFile, FSCTL_QUERY_ALLOCATED_RANGES,
			&query_range, sizeof(query_range), res_buffer.data(), static_cast<DWORD>(res_buffer.size()*sizeof(FILE_ALLOCATED_RANGE_BUFFER)),
			&output_bytes, NULL);

		bool more_data = (!b && GetLastError() == ERROR_MORE_DATA);

		if (more_data || b)
		{
			size_t res_size = output_bytes / sizeof(FILE_ALLOCATED_RANGE_BUFFER);
			size_t start_off = file_extents.size();
			file_extents.resize(start_off + res_size);
			for (size_t i = start_off; i < start_off + res_size; ++i)
			{
				file_extents[i] = SExtent(res_buffer[i - start_off].FileOffset.QuadPart, res_buffer[i - start_off].Length.QuadPart);

				if (file_extents[i].offset != last_sparse_pos)
				{
					if (last_sparse_pos <= start_offset)
					{
						start_offset += file_extents[i].offset - last_sparse_pos;
					}

					++n_sparse_extents;
				}

				last_sparse_pos = file_extents[i].offset + file_extents[i].size;

				if (i + 1 >= start_off + res_size)
				{
					query_range.FileOffset.QuadPart = res_buffer[i - start_off].FileOffset.QuadPart + res_buffer[i - start_off].Length.QuadPart;
					query_range.Length.QuadPart = fsize - query_range.FileOffset.QuadPart;
				}

				real_fsize += file_extents[i].size;
			}

			if (!more_data)
			{
				has_file_extents = true;

				if (last_sparse_pos != fsize)
				{
					if (last_sparse_pos <= start_offset)
					{
						start_offset += fsize - last_sparse_pos;
					}

					++n_sparse_extents;
				}

				return real_fsize;
			}
		}

		if (!b && !more_data)
		{
			has_file_extents = false;
			return -1;
		}
	}
#else
	off64_t last_sparse_pos = 0;
	has_file_extents = false;
	real_fsize = fsize;
	bool seek_suc = false;

	while (true)
	{
		int64 sparse_hole_start = lseek64(hFile, last_sparse_pos, SEEK_HOLE);

		if (sparse_hole_start == -1)
		{
			break;
		}

		seek_suc = true;

		if (sparse_hole_start == fsize)
		{
			break;
		}

		last_sparse_pos = lseek64(hFile, sparse_hole_start, SEEK_DATA);

		if (last_sparse_pos == -1)
		{
			if (sparse_hole_start <= start_offset)
			{
				start_offset += fsize - sparse_hole_start;
			}

			real_fsize -= fsize - sparse_hole_start;
			file_extents.push_back(SExtent(sparse_hole_start, fsize - sparse_hole_start));
			has_file_extents = true;
			break;
		}


		if (sparse_hole_start <= start_offset)
		{
			start_offset += last_sparse_pos - sparse_hole_start;
		}

		real_fsize -= last_sparse_pos - sparse_hole_start;
		file_extents.push_back(SExtent(sparse_hole_start, last_sparse_pos - sparse_hole_start));
		has_file_extents = true;
	}

	if (seek_suc)
	{
		off64_t rc = lseek64(hFile, 0, SEEK_SET);

		if (rc != 0)
		{
			return -1;
		}
	}

	n_sparse_extents = file_extents.size();

#endif
	return real_fsize;
}

bool CClientThread::sendExtents(const std::vector<SExtent>& file_extents, int64 fsize, int64 n_sparse_extents)
{
	std::vector<int64> send_buf;
	send_buf.resize(n_sparse_extents*2+2);

	int64 last_sparse_pos = 0;
	size_t curr_sparse_ext = 0;

	MD5 hash_func;

	for (size_t i = 0; i < file_extents.size(); ++i)
	{
		if (file_extents[i].offset != last_sparse_pos)
		{
			send_buf[curr_sparse_ext * 2 + 0] = last_sparse_pos;
			send_buf[curr_sparse_ext * 2 + 1] = file_extents[i].offset - last_sparse_pos;

			++curr_sparse_ext;
		}

		last_sparse_pos = file_extents[i].offset + file_extents[i].size;
	}

	if (last_sparse_pos != fsize)
	{
		send_buf[curr_sparse_ext * 2 + 0] = last_sparse_pos;
		send_buf[curr_sparse_ext * 2 + 1] = fsize - last_sparse_pos;

		++curr_sparse_ext;
	}

	assert(curr_sparse_ext == n_sparse_extents);

	hash_func.update(reinterpret_cast<unsigned char*>(send_buf.data()), static_cast<_u32>(n_sparse_extents * 2 * sizeof(int64)));

	hash_func.finalize();

	memcpy(&send_buf[n_sparse_extents * 2], hash_func.raw_digest_int(), 2 * sizeof(int64));

	int rc = SendInt(reinterpret_cast<char*>(send_buf.data()), send_buf.size()*sizeof(int64));

	return rc != SOCKET_ERROR;
}

bool CClientThread::sendSparseExtents(const std::vector<SExtent>& file_extents)
{
	std::vector<int64> send_buf;
	send_buf.resize(file_extents.size() * 2 + 2);

	int64 last_sparse_pos = 0;
	size_t curr_sparse_ext = 0;

	MD5 hash_func;

	for (size_t i = 0; i < file_extents.size(); ++i)
	{
		send_buf[i * 2 + 0] = little_endian(file_extents[i].offset);
		send_buf[i * 2 + 1] = little_endian(file_extents[i].size);
	}

	hash_func.update(reinterpret_cast<unsigned char*>(send_buf.data()), static_cast<_u32>(file_extents.size() * 2 * sizeof(int64)));

	hash_func.finalize();

	memcpy(&send_buf[file_extents.size() * 2], hash_func.raw_digest_int(), 2 * sizeof(int64));

	int rc = SendInt(reinterpret_cast<char*>(send_buf.data()), send_buf.size()*sizeof(int64));

	return rc != SOCKET_ERROR;
}


