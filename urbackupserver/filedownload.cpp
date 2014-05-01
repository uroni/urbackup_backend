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
		dstfile=Server->openFile(dest, MODE_RW);
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
		rc=fc.GetFileChunked(remotefn, dstfile, hashfile, hashfile_output, -1);

		cleanup_tmpfile(hashfile);
		cleanup_tmpfile(hashfile_output);

		_i64 fsize=dstfile->Size();
		Server->destroy(dstfile);
		if(rc==ERR_SUCCESS && fsize>fc.getSize())
		{
			os_file_truncate(widen(dest), fc.getSize());
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

		Server->Log("Downloading file...");
		rc=fc.GetFilePatch(remotefn, dstfile, patchfile, hashfile, hashfile_output, -1);

		IFile *tmpfile=Server->openTemporaryFile();
		Server->Log("Copying to temporary...");
		copy_file_fd(dstfile, tmpfile);

		tmpfile->Seek(0);
		m_chunkpatchfile=tmpfile;
		ChunkPatcher patcher;
		patcher.setCallback(this);
		Server->Log("Patching temporary...");
		patcher.ApplyPatch(dstfile, patchfile);

		Server->Log("Copying back...");
		copy_file_fd(tmpfile, dstfile);


		cleanup_tmpfile(hashfile);
		cleanup_tmpfile(hashfile_output);
		cleanup_tmpfile(patchfile);
		cleanup_tmpfile(tmpfile);

		_i64 fsize=dstfile->Size();
		Server->destroy(dstfile);
		if(rc==ERR_SUCCESS && fsize>patcher.getFilesize())
		{
			os_file_truncate(widen(dest), patcher.getFilesize());
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
	m_chunkpatchfile->Write(buf, (_u32)bsize);
}

void FileDownload::cleanup_tmpfile(IFile *tmpfile)
{
	std::string fn=tmpfile->getFilename();
	Server->deleteFile(fn);
	Server->destroy(tmpfile);
}