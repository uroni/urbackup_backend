class CClientThread;
class IFile;
struct SChunk;

#include "../Interface/Thread.h"
#include "../md5.h"

class ChunkSendThread : public IThread
{
public:
	ChunkSendThread(CClientThread *parent, IFile *file);

	void operator()(void);

	bool sendChunk(SChunk *chunk);

private:

	CClientThread *parent;
	IFile *file;

	char *chunk_buf;

	MD5 md5_hash;
};