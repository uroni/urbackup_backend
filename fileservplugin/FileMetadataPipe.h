#pragma once
#include "PipeFileBase.h"
#include "../Interface/Pipe.h"
#include <string>
#include "../Interface/Condition.h"

#ifdef _WIN32
#include <Windows.h>
const _u32 ID_METADATA_OS = 1<<0;
#else
const _u32 ID_METADATA_OS = 1<<2;
#endif

const _u32 ID_METADATA_NOP = 0;
const _u32 ID_METADATA_V1 = ID_METADATA_OS | 1<<3;


class FileMetadataPipe : public PipeFileBase
{
public:
	FileMetadataPipe(IPipe* pipe, const std::wstring& cmd);

	virtual bool getExitCode( int& exit_code );

protected:
	virtual bool readStdoutIntoBuffer( char* buf, size_t buf_avail, size_t& read_bytes );

	virtual bool readStderrIntoBuffer( char* buf, size_t buf_avail, size_t& read_bytes );


private:

	bool transmitCurrMetadata(char* buf, size_t buf_avail, size_t& read_bytes);

#ifdef _WIN32
	HANDLE hFile;
	int backup_read_state;
	void* backup_read_context;
	LARGE_INTEGER curr_stream_size;
	int64 curr_pos;
#endif

	enum MetadataState
	{
		MetadataState_Wait,
		MetadataState_FnSize,
		MetadataState_Fn,
		MetadataState_Os
	};

	size_t fn_off;
	std::string public_fn;
	std::string local_fn;

	MetadataState metadata_state;

	std::string stderr_buf;
	std::auto_ptr<IPipe> errpipe;
	IPipe* pipe;

	size_t metadata_buffer_size;
	size_t metadata_buffer_off;
	std::vector<char> metadata_buffer;
};