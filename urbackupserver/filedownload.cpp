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

#include <string>

#include "../urbackupcommon/fileclient/FileClient.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "server_prepare_hash.h"

#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../Interface/Server.h"

#include "filedownload.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"

#include <stdlib.h>
#include "../urbackupcommon/chunk_hasher.h"

extern std::string server_identity;

bool FileDownload::copy_file_fd(IFile *fsrc, IFile *fdst)
{
	fsrc->Seek(0);
	fdst->Seek(0);
	char buf[4096];
	size_t rc;
	while( (rc=(_u32)fsrc->Read(buf, 4096))>0)
	{
		fdst->Write(buf, (_u32)rc);
	}
	return true;
}

void FileDownload::filedownload(std::string remotefn, std::string dest, int method, int predicted_filesize, SQueueStatus queueStatus)
{
	IFsFile *dstfile;
	if(queueStatus==SQueueStatus_Queue || queueStatus==SQueueStatus_NoQueue)
	{
		Server->Log("Opening destination file...");

		if(method==0)
		{
			dstfile=Server->openFile(dest, MODE_WRITE);
		}
		else
		{
			dstfile=Server->openFile(dest, MODE_RW_CREATE);
		}

		if(dstfile==NULL)
		{
			Server->Log("Cannot open file");
			return;
		}
	}	

	_u32 rc=ERR_SUCCESS;
	if(method==0)
	{	
		if(queueStatus==SQueueStatus_Queue)
		{
			FileDownloadQueueItemFull qi = {
				remotefn,
				predicted_filesize,
				false
			};

			dlqueueFull.push_back(qi);
		}
		else
		{
			Server->Log("Downloading file...");

			for(size_t i=0;i<dlqueueFull.size();++i)
			{
				if(dlqueueFull[i].remotefn==remotefn)
				{
					dlqueueFull.erase(dlqueueFull.begin()+i);
					break;
				}
			}

			rc=fc->GetFile(remotefn, dstfile, true, false, 0, false, 0);

			Server->destroy(dstfile);
		}
	}
	else if(method==1)
	{
		IFile *hashfile;
		IFsFile *hashfile_output;

		if(queueStatus==SQueueStatus_Queue || queueStatus==SQueueStatus_NoQueue)
		{
			hashfile=Server->openTemporaryFile();
			hashfile_output=Server->openTemporaryFile();

			Server->Log("Building hashes...");
			build_chunk_hashs(dstfile, hashfile, NULL, NULL, false, NULL, NULL);

			Server->Log("Downloading file...");

			if(queueStatus==SQueueStatus_Queue)
			{
				FileDownloadQueueItemChunked qi =
				{
					remotefn,
					dstfile,
					NULL,
					hashfile,
					hashfile_output,
					predicted_filesize,
					false
				};

				dlqueueChunked.push_back(qi);
			}
		}
		else
		{
			for(size_t i=0;i<dlqueueChunked.size();++i)
			{
				if(dlqueueChunked[i].remotefn==remotefn)
				{
					hashfile = dlqueueChunked[i].chunkhashes;
					hashfile_output = dlqueueChunked[i].hashoutput;

					dlqueueChunked.erase(dlqueueChunked.begin()+i);
					break;
				}
			}
		}

		int64 remote_filesize=-1;

		if(predicted_filesize>=0)
		{
			remote_filesize = predicted_filesize;
		}

		if(queueStatus==SQueueStatus_NoQueue || queueStatus==SQueueStatus_IsQueued)
		{

			rc=fc_chunked->GetFileChunked(remotefn, dstfile, hashfile, hashfile_output, remote_filesize, 0, false, NULL);

			cleanup_tmpfile(hashfile);
			cleanup_tmpfile(hashfile_output);

			_i64 fsize=dstfile->Size();
			Server->destroy(dstfile);
			if(rc==ERR_SUCCESS && fsize>remote_filesize)
			{
				os_file_truncate(dest, remote_filesize);
			}
		}

		
	}
	else if(method==2)
	{
		IFile *hashfile;
		IFsFile *hashfile_output;
		IFile *patchfile;

		if(queueStatus==SQueueStatus_Queue || queueStatus==SQueueStatus_NoQueue)
		{
			hashfile=Server->openTemporaryFile();
			hashfile_output=Server->openTemporaryFile();
			patchfile=Server->openTemporaryFile();

			Server->Log("Building hashes...");
			build_chunk_hashs(dstfile, hashfile, NULL, NULL, false);

			if(queueStatus==SQueueStatus_Queue)
			{
				FileDownloadQueueItemChunked qi =
				{
					remotefn,
					dstfile,
					patchfile,
					hashfile,
					hashfile_output,
					predicted_filesize,
					false
				};

				dlqueueChunked.push_back(qi);
			}

			/*int64 dstf_size = dstfile->Size();
			if(dstf_size>0)
			{
				dstfile->Remove();
				os_file_truncate(widen(dest), (dstf_size/(512*1024)) * 512*1024 + 1);
				dstfile=Server->openFile(dest, MODE_RW);
			}*/		
		}
		else
		{
			for(size_t i=0;i<dlqueueChunked.size();++i)
			{
				if(dlqueueChunked[i].remotefn==remotefn)
				{
					dstfile = dlqueueChunked[i].orig_file;
					patchfile = dlqueueChunked[i].patchfile;
					hashfile = dlqueueChunked[i].chunkhashes;
					hashfile_output = dlqueueChunked[i].hashoutput;

					dlqueueChunked.erase(dlqueueChunked.begin()+i);
					break;
				}
			}
		}

		
		int64 remote_filesize=-1;

		if(predicted_filesize>0)
		{
			remote_filesize = predicted_filesize;
		}

		if(queueStatus==SQueueStatus_IsQueued || queueStatus==SQueueStatus_NoQueue)
		{
			Server->Log("Downloading file...");
			rc=fc_chunked->GetFilePatch(remotefn, dstfile, patchfile, hashfile, hashfile_output, remote_filesize, 0, false, NULL);

			IFile *tmpfile=Server->openTemporaryFile();
			Server->Log("Copying to temporary...");
			copy_file_fd(dstfile, tmpfile);

			tmpfile->Seek(0);
			m_chunkpatchfile=tmpfile;
			ChunkPatcher patcher;
			patcher.setCallback(this);
			chunk_patch_pos=0;
			Server->Log("Patching temporary...");
			patcher.ApplyPatch(dstfile, patchfile, NULL);

			Server->Log("Copying back...");
			copy_file_fd(tmpfile, dstfile);


			cleanup_tmpfile(hashfile);
			cleanup_tmpfile(hashfile_output);
			cleanup_tmpfile(tmpfile);

			_i64 fsize=dstfile->Size();
			Server->destroy(dstfile);
			if(remote_filesize==patcher.getFilesize() && fsize>=patcher.getFilesize())
			{
				if(rc==ERR_SUCCESS && fsize>patcher.getFilesize())
				{
					os_file_truncate(dest, patcher.getFilesize());
				}
				cleanup_tmpfile(patchfile);
			}
			else
			{
				Server->Log("Filesize is wrong...", LL_ERROR);
				Server->Log("Patchfile: "+patchfile->getFilename(), LL_ERROR);
			}
		}		
	}
	else
	{
		Server->Log("File download method unknown", LL_ERROR);
		return;
	}

	if(queueStatus==SQueueStatus_NoQueue || queueStatus==SQueueStatus_IsQueued)
	{
		if(rc!=ERR_SUCCESS)
		{
			Server->Log("File download failed ec="+FileClient::getErrorString(rc));
		}
		else
		{
			Server->Log("File download succeeded");
		}
	}
	
}

void FileDownload::filedownload( std::string csvfile )
{
	std::string data = getFile(csvfile);

	int lines = linecount(data);

	for(int line=0;line<lines+1;++line)
	{
		std::string ldata = getline(line, data);

		std::vector<std::string> cols;
		Tokenize(ldata, cols, ",");

		if(cols.size()!=4) continue;

		filedownload(cols[0], cols[1], atoi(cols[2].c_str()), atoi(cols[3].c_str()), SQueueStatus_Queue);
	}

	for(int line=0;line<lines+1;++line)
	{
		std::string ldata = getline(line, data);

		std::vector<std::string> cols;
		Tokenize(ldata, cols, ",");

		if(cols.size()!=4) continue;

		filedownload(cols[0], cols[1], atoi(cols[2].c_str()), atoi(cols[3].c_str()), SQueueStatus_IsQueued);
	}
}

IPipe * FileDownload::new_fileclient_connection(void)
{
	for(size_t i=0;i<30;++i)
	{
		IPipe *cp=Server->ConnectStream(m_servername, m_tcpport, 10000);
		if(cp==NULL)
		{
			Server->Log("Cannot connect to server");
			Server->wait(2000);
		}
		else
		{
			Server->Log("Reconnected");
			return cp;
		}
	}
	return NULL;
}

void FileDownload::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed, bool* is_sparse)
{
	if(changed)
	{
		if(!m_chunkpatchfile->Seek(chunk_patch_pos) || m_chunkpatchfile->Write(buf, (_u32)bsize)!=bsize)
		{
			Server->Log("Writing to file failed", LL_ERROR);
			exit(3);
		}
	}
	chunk_patch_pos+=bsize;
}

void FileDownload::next_sparse_extent_bytes(const char * buf, size_t bsize)
{
}

void FileDownload::cleanup_tmpfile(IFile *tmpfile)
{
	std::string fn=tmpfile->getFilename();
	Server->destroy(tmpfile);
	Server->deleteFile(fn);
}

bool FileDownload::getQueuedFileChunked( std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFsFile*& hashoutput, _i64& predicted_filesize, int64& file_id, bool& is_script)
{
	for(size_t i=0;i<dlqueueChunked.size();++i)
	{
		if(dlqueueChunked[i].queued==false)
		{
			remotefn = dlqueueChunked[i].remotefn;
			orig_file = dlqueueChunked[i].orig_file;
			patchfile = dlqueueChunked[i].patchfile;
			chunkhashes = dlqueueChunked[i].chunkhashes;
			hashoutput = dlqueueChunked[i].hashoutput;
			predicted_filesize = dlqueueChunked[i].predicted_filesize;
			dlqueueChunked[i].queued=true;
			file_id=0;
			is_script = false;
			return true;
		}
	}

	return false;
}

void FileDownload::unqueueFileChunked( const std::string& remotefn )
{
	for(size_t i=0;i<dlqueueChunked.size();++i)
	{
		if(dlqueueChunked[i].queued && dlqueueChunked[i].remotefn==remotefn)
		{
			dlqueueChunked[i].queued=false;
		}
	}
}

void FileDownload::resetQueueChunked()
{
	for(size_t i=0;i<dlqueueChunked.size();++i)
	{
		dlqueueChunked[i].queued=false;
	}
}

FileDownload::FileDownload( std::string servername, unsigned int tcpport )
	: fc_chunked(), m_servername(servername), m_tcpport(tcpport)
{
	IPipe *cp=NULL;
	for(size_t i=0;i<30 && cp==NULL;++i)
	{
		Server->Log("Connecting to server...");
		cp=Server->ConnectStream(servername, tcpport, 10000);
		if(cp==NULL)
		{
			Server->Log("Cannot connect to server");
			Server->wait(2000);
		}
	}

	if(cp==NULL) return;

	fc_chunked.reset(new FileClientChunked(cp, true, &tcpstack, this, NULL, server_identity, NULL));
	fc_chunked->setQueueCallback(this);
	fc_chunked->setDestroyPipe(true);

	fc.reset(new FileClient(false, server_identity, 2));

	cp=NULL;
	for(size_t i=0;i<30 && cp==NULL;++i)
	{
		Server->Log("Connecting to server...");
		cp=Server->ConnectStream(servername, tcpport, 10000);
		if(cp==NULL)
		{
			Server->Log("Cannot connect to server");
			Server->wait(2000);
		}
	}

	if(cp==NULL) return;

	fc->Connect(cp);
	fc->setQueueCallback(this);
}

std::string FileDownload::getQueuedFileFull( FileClient::MetadataQueue& metadata, size_t& folder_items, bool& finish_script, int64& file_id)
{
	for(size_t i=0;i<dlqueueFull.size();++i)
	{
		if(!dlqueueFull[i].queued)
		{
			metadata=FileClient::MetadataQueue_Data;
			folder_items=0;
			file_id=0;
			finish_script=false;
			return dlqueueFull[i].remotefn;
		}
	}
	return std::string();
}

void FileDownload::unqueueFileFull( const std::string& fn, bool finish_script)
{
	for(size_t i=0;i<dlqueueFull.size();++i)
	{
		if(dlqueueFull[i].queued && dlqueueFull[i].remotefn==fn)
		{
			dlqueueFull[i].queued=false;
		}
	}
}

void FileDownload::resetQueueFull()
{
	for(size_t i=0;i<dlqueueFull.size();++i)
	{
		dlqueueFull[i].queued=false;
	}
}
