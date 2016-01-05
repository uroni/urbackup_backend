#pragma once
#include "PipeFileBase.h"
#include "../Interface/Pipe.h"
#include <string>
#include <deque>
#include "../Interface/Condition.h"
#include "IFileServ.h"

#ifdef _WIN32
#include <Windows.h>
const _u32 ID_METADATA_OS = 1<<0;
#else
const _u32 ID_METADATA_OS = 1<<2;
#endif
#include "IFileServ.h"

const _u32 ID_METADATA_NOP = 0;
const _u32 ID_METADATA_V1 = ID_METADATA_OS | 1<<3;

class FileMetadataPipe : public PipeFileBase
{
public:
	FileMetadataPipe(IPipe* pipe, const std::string& cmd);

	virtual bool getExitCode( int& exit_code );

	void forceExitWait();

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
	std::string stream_name;
#else
	enum BackupState
	{
        BackupState_StatInit,
		BackupState_Stat,
		BackupState_EAttrInit,
		BackupState_EAttr,
		BackupState_EAttr_Vals_Key,
		BackupState_EAttr_Vals_Val
	};

	BackupState backup_state;
	size_t eattr_idx;
	std::vector<std::string> eattr_keys;
	size_t eattr_key_off;
	std::string eattr_val;
	size_t eattr_val_off;
#endif

	enum MetadataState
	{
		MetadataState_Wait,
		MetadataState_FnSize,
		MetadataState_Fn,
		MetadataState_FnChecksum,
		MetadataState_Common,
		MetadataState_Os,
		MetadataState_OsChecksum,
		MetadataState_File,
		MetadataState_FileChecksum
	};

	size_t fn_off;
	std::string public_fn;
	std::deque<std::string> last_public_fns;
	std::string local_fn;
	int64 folder_items;
	IFileServ::IMetadataCallback* callback;
	std::string server_token;
	IFile* metadata_file;
	int64 metadata_file_off;
	int64 metadata_file_size;
	int64 metadata_id;

	MetadataState metadata_state;

	std::string stderr_buf;
	std::auto_ptr<IPipe> errpipe;
	IPipe* pipe;

	size_t metadata_buffer_size;
	size_t metadata_buffer_off;
	std::vector<char> metadata_buffer;

	std::auto_ptr<IFileServ::ITokenCallback> token_callback;

	unsigned int curr_checksum;
};
