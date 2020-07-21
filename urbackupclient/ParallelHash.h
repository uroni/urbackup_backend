#pragma once

#include "../fileservplugin/IPipeFileExt.h"
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../common/data.h"
#include "clientdao.h"
#include "client.h"
#include <memory>
#include <deque>
#include <set>

namespace
{
	const char ID_SET_CURR_DIRS = 0;
	const char ID_FINISH_CURR_DIR = 1;
	const char ID_HASH_FILE = 2;
	const char ID_CBT_DATA = 3;
	const char ID_INIT_HASH = 4;
	const char ID_PHASH_FINISH = 5;
}

class ClientHash;

class ParallelHash : public IPipeFileExt, public IThread
{
public:
	ParallelHash(SQueueRef* phash_queue, int sha_version, size_t extra_n_threads);

	virtual bool getExitCode(int & exit_code);
	virtual void forceExit();
	virtual bool readStdoutIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes);
	virtual void finishStdout();
	virtual bool readStderrIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes);

	void operator()();

	void addQueuedStdoutMsgs();

private:
	struct SCurrDir
	{
		int tgroup;
		std::string dir;
		std::string snapshot_dir;
		std::vector<SFileAndHash> files;
		int64 target_generation;
		int64 dir_target_nfiles;
		bool finish;
	};

	bool hashFile(int64 working_file_id, CRData& data, ClientDAO& clientdao);
	bool finishDir(ParallelHash::SCurrDir* dir, ClientDAO& clientdao, const int64& target_generation, int64& id);
	bool addToStdoutBuf(const char* ptr, size_t size);
	void addModifyFileBuffer(ClientDAO& clientdao, const std::string& path, int tgroup, const std::vector<SFileAndHash>& files, int64 target_generation);
	void commitModifyFileBuffer(ClientDAO& clientdao);
	size_t calcBufferSize(const std::string &path, const std::vector<SFileAndHash> &data);
	void runExtraThread();

	std::map<int64, CWData> stdout_msg_buf;
	std::set<int64> working_file_ids;

	std::vector<char> stdout_buf;
	size_t stdout_buf_pos;
	size_t stdout_buf_size;
	volatile bool do_quit;
	volatile bool eof;
	int64 phash_queue_pos;
	SQueueRef* phash_queue;
	std::auto_ptr<IMutex> mutex;	
	
	std::map<int64, SCurrDir> curr_dirs;

	std::deque<std::string> postponed_finish;
	
	std::auto_ptr<ClientHash> client_hash;
	int sha_version;
	THREADPOOL_TICKET ticket;
	std::vector<THREADPOOL_TICKET> extra_tickets;

	struct SBufferItem
	{
		SBufferItem(std::string path, int tgroup, std::vector<SFileAndHash> files, int64 target_generation)
			: path(path), tgroup(tgroup), files(files), target_generation(target_generation)
		{}

		std::string path;
		int tgroup;
		std::vector<SFileAndHash> files;
		int64 target_generation;
	};

	std::auto_ptr<IMutex> modify_file_buffer_mutex;
	std::vector< SBufferItem > modify_file_buffer;
	size_t modify_file_buffer_size;
	int64 last_file_buffer_commit_time;
	size_t extra_n_threads;
	bool extra_thread;

	std::deque<std::pair<int64, std::string> > extra_queue;
	std::auto_ptr<IMutex> extra_mutex;
	std::auto_ptr<ICondition> extra_cond;
};
