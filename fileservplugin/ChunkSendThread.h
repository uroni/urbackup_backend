#pragma once

#include "FileServ.h"
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../Interface/File.h"
#include "../md5.h"
#include <memory>

class ScopedPipeFileUser;
class CClientThread;
struct SChunk;

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
	IFileServ::CbtHashFileInfo cbt_hash_file_info;
	std::vector<IFsFile::SFileExtent> file_extents;
	bool has_more_extents;

	char *chunk_buf;

	bool has_error;

	MD5 md5_hash;
};