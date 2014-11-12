#include <string>

#include "fileclient/FileClient.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "server_prepare_hash.h"

#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../Interface/Server.h"

#include "filedownload.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"

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

void FileDownload::filedownload(std::string remotefn, std::string servername, std::string dest, unsigned int tcpport, int method)
{
	m_servername=servername;
	m_tcpport=tcpport;

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

	Server->Log("Opening destination file...");
	IFile *dstfile;
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

	_u32 rc;
	if(method==0)
	{
		FileClient fc(false, server_identity, 2);
		fc.Connect(cp);
		
		Server->Log("Downloading file...");
		rc=fc.GetFile(remotefn, dstfile, true);

		Server->destroy(dstfile);
	}
	else if(method==1)
	{
		CTCPStack tcpstack;
		FileClientChunked fc(cp, true, &tcpstack, this, NULL, server_identity, NULL);
		fc.setDestroyPipe(true);

		IFile *hashfile=Server->openTemporaryFile();
		IFile *hashfile_output=Server->openTemporaryFile();

		Server->Log("Building hashes...");
		BackupServerPrepareHash::build_chunk_hashs(dstfile, hashfile, NULL, false, NULL, false);

		Server->Log("Downloading file...");
		int64 remote_filesize=-1;
		rc=fc.GetFileChunked(remotefn, dstfile, hashfile, hashfile_output, remote_filesize);

		cleanup_tmpfile(hashfile);
		cleanup_tmpfile(hashfile_output);

		_i64 fsize=dstfile->Size();
		Server->destroy(dstfile);
		if(rc==ERR_SUCCESS && fsize>remote_filesize)
		{
			os_file_truncate(widen(dest), remote_filesize);
		}
	}
	else if(method==2)
	{
		CTCPStack tcpstack;
		FileClientChunked fc(cp, true, &tcpstack, this, NULL, server_identity, NULL);
		fc.setDestroyPipe(true);

		IFile *hashfile=Server->openTemporaryFile();
		IFile *hashfile_output=Server->openTemporaryFile();
		IFile *patchfile=Server->openTemporaryFile();

		Server->Log("Building hashes...");
		BackupServerPrepareHash::build_chunk_hashs(dstfile, hashfile, NULL, false, NULL, false);

		/*int64 dstf_size = dstfile->Size();
		if(dstf_size>0)
		{
			dstfile->Remove();
			os_file_truncate(widen(dest), (dstf_size/(512*1024)) * 512*1024 + 1);
			dstfile=Server->openFile(dest, MODE_RW);
		}*/		

		Server->Log("Downloading file...");
		int64 remote_filesize=-1;
		rc=fc.GetFilePatch(remotefn, dstfile, patchfile, hashfile, hashfile_output, remote_filesize);

		IFile *tmpfile=Server->openTemporaryFile();
		Server->Log("Copying to temporary...");
		copy_file_fd(dstfile, tmpfile);

		tmpfile->Seek(0);
		m_chunkpatchfile=tmpfile;
		ChunkPatcher patcher;
		patcher.setCallback(this);
		chunk_patch_pos=0;
		Server->Log("Patching temporary...");
		patcher.ApplyPatch(dstfile, patchfile);

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
				os_file_truncate(widen(dest), patcher.getFilesize());
			}
			cleanup_tmpfile(patchfile);
		}
		else
		{
			Server->Log("Filesize is wrong...", LL_ERROR);
			Server->Log("Patchfile: "+patchfile->getFilename(), LL_ERROR);
		}
	}
	else
	{
		Server->Log("File download method unknown", LL_ERROR);
		return;
	}

	if(rc!=ERR_SUCCESS)
	{
		Server->Log("File download failed ec="+FileClient::getErrorString(rc));
	}
	else
	{
		Server->Log("File download succeeded");
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

void FileDownload::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed)
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

void FileDownload::cleanup_tmpfile(IFile *tmpfile)
{
	std::string fn=tmpfile->getFilename();
	Server->destroy(tmpfile);
	Server->deleteFile(fn);
}