#include "fileclient/FileClientChunked.h"
#include "ChunkPatcher.h"
#include <string>

class IFile;

class FileDownload : public FileClientChunked::ReconnectionCallback, public IChunkPatcherCallback
{
public:
	void filedownload(std::string remotefn, std::string servername, std::string dest, unsigned int tcpport, int method);

	virtual IPipe * new_fileclient_connection(void);
	virtual void next_chunk_patcher_bytes(const char *buf, size_t bsize);
private:

	bool copy_file_fd(IFile *fsrc, IFile *fdst);
	void cleanup_tmpfile(IFile *tmpfile);

	std::string m_servername;
	unsigned int m_tcpport;
	IFile *m_chunkpatchfile;
};