#pragma once

#include "../fileservplugin/IPipeFileExt.h"
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../common/data.h"
#include "clientdao.h"

namespace
{
	const char ID_SET_CURR_DIRS = 0;
	const char ID_FINISH_CURR_DIR = 1;
	const char ID_HASH_FILE = 2;
	const char ID_CBT_DATA = 3;
	const char ID_INIT_HASH = 4;
}

class ClientHash;

class ParallelHash : public IPipeFileExt
{
public:
	ParallelHash(IFile* phash_queue, int sha_version);

	virtual bool getExitCode(int & exit_code);
	virtual void forceExit();
	virtual bool readStdoutIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes);
	virtual void finishStdout();
	virtual bool readStderrIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes);

	void operator()();

private:
	bool hashFile(CRData& data, ClientDAO& clientdao);
	void addToStdoutBuf(const char* ptr, size_t size);
	void addModifyFileBuffer(ClientDAO& clientdao, const std::string& path, int tgroup, const std::vector<SFileAndHash>& files, int64 target_generation);
	void commitModifyFileBuffer(ClientDAO& clientdao);
	size_t calcBufferSize(const std::string &path, const std::vector<SFileAndHash> &data);

	std::vector<char> stdout_buf;
	size_t stdout_buf_pos;
	size_t stdout_buf_size;
	volatile bool do_quit;
	int64 phash_queue_pos;
	IFile* phash_queue;
	std::auto_ptr<IMutex> mutex;
	std::string curr_dir;
	int curr_tgroup;
	std::string curr_snapshot_dir;
	std::vector<SFileAndHash> curr_files;
	std::auto_ptr<ClientHash> client_hash;
	int sha_version;

	struct SBufferItem
	{
		SBufferItem(std::string path, int tgroup, std::vector<SFileAndHash> files)
			: path(path), tgroup(tgroup), files(files)
		{}

		std::string path;
		int tgroup;
		std::vector<SFileAndHash> files;
		int64 target_generation;
	};

	std::vector< SBufferItem > modify_file_buffer;
	size_t modify_file_buffer_size;
	int64 last_file_buffer_commit_time;
};
