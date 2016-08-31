#pragma once
#include "PipeFileBase.h"
#include "../Interface/Pipe.h"
#include <string>
#include <memory>
#include <deque>
#include "../Interface/Condition.h"
#include "IFileServ.h"
#include "../urbackupcommon/sha2/sha2.h"

#ifdef _WIN32
#include <Windows.h>
const _u32 ID_METADATA_OS = 1<<0;
#else
const _u32 ID_METADATA_OS = 1<<2;
#endif
#include "IFileServ.h"

const _u32 ID_METADATA_NOP = 0;
const _u32 ID_METADATA_V1 = ID_METADATA_OS | 1<<3;
const _u32 ID_RAW_FILE = 1 << 4;

const char METADATA_PIPE_SEND_FILE = 0;
const char METADATA_PIPE_SEND_RAW = 1;
const char METADATA_PIPE_EXIT = 2;
const char METADATA_PIPE_SEND_RAW_FILEDATA = 3;

class FileMetadataPipe : public PipeFileBase
{
public:
	FileMetadataPipe(IPipe* pipe, const std::string& cmd);
	~FileMetadataPipe();

	virtual bool getExitCode( int& exit_code );

	void forceExitWait();

protected:
	virtual bool readStdoutIntoBuffer( char* buf, size_t buf_avail, size_t& read_bytes );

	virtual void finishStdout();

	virtual bool readStderrIntoBuffer( char* buf, size_t buf_avail, size_t& read_bytes );

	virtual void cleanupOnForceShutdown();

private:

	bool transmitCurrMetadata(char* buf, size_t buf_avail, size_t& read_bytes);

	bool openFileHandle();

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
		MetadataState_FileChecksum,
		MetadataState_Raw,
		MetadataState_RawFileFnSize,
		MetadataState_RawFileFn,
		MetadataState_RawFileFnChecksum,
		MetadataState_RawFileDataSize,
		MetadataState_RawFileData,
		MetadataState_RawFileDataChecksum
	};

	size_t fn_off;
	std::string public_fn;
	std::deque<std::string> last_public_fns;
	std::string local_fn;
	int64 folder_items;
	IFileServ::IMetadataCallback* callback;
	std::string server_token;
	std::auto_ptr<IFile> metadata_file;
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

	std::string raw_metadata;

	unsigned int curr_checksum;

	IFile* transmit_file;
	IPipe* transmit_wait_pipe;
	sha512_ctx transmit_file_ctx;

	std::auto_ptr<IFileServ::ITokenCallback> token_callback;
};

#ifndef _WIN32
#include <sys/stat.h>
#include "../common/data.h"

void serialize_stat_buf(const struct stat64& buf, const std::string& symlink_target, CWData& data);
#endif