class CClientThread;
class IFile;
struct SChunk;

#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../md5.h"
#include <memory>

class ScopedPipeFileUser;

class ChunkSendThread : public IThread
{
public:
	ChunkSendThread(CClientThread *parent);
	~ChunkSendThread(void);

	void operator()(void);

	bool sendChunk(SChunk *chunk);

private:

	bool sendError(_u32 errorcode1, _u32 errorcode2);

	CClientThread *parent;
	IFile *file;
	std::string s_filename;
	std::auto_ptr<ScopedPipeFileUser> pipe_file_user;
	_i64 curr_hash_size;
	_i64 curr_file_size;

	char *chunk_buf;

	bool has_error;

	MD5 md5_hash;
};