#pragma once

#include "../Interface/Mutex.h"
#include "PipeFile.h"
#include <memory>
#include <set>
#include <sys/stat.h>
#include "../stringtools.h"

#if defined(_WIN32) || defined(__APPLE__) || defined(__FreeBSD__)
#define stat64 stat
#endif

class PipeFileTar : public IPipeFile
{
public:
	struct STarFile
	{
		STarFile()
			: available(false), pos(0),
			size(0), is_dir(false), is_special(false)
		{}

		bool available;
		int64 pos;
		int64 size;
		std::string fn;
		std::string symlink_target;
		bool is_dir;
		bool is_special;
		bool is_symlink;

		struct stat64 buf;
	};

	struct PipeFileStore
	{
		PipeFileStore(IPipeFile* pipe_file)
			: refcount(1), pipe_file(pipe_file), mutex(Server->createMutex())
		{}

		void decr()
		{
			IScopedLock lock(mutex.get());
			--refcount;
		}

		void inc()
		{
			IScopedLock lock(mutex.get());
			++refcount;
		}

		void add_path(const std::string& path)
		{
			IScopedLock lock(mutex.get());
			std::set<std::string>::iterator it = paths.lower_bound(path);
			if (it == paths.end())
			{
				paths.insert(path);
				return;
			}

			if ((*it) != path)
			{
				if (next(path, 0, *it + "/"))
				{
					paths.erase(it);
				}
				paths.insert(path);
			}
		}

		bool has_path(const std::string& path)
		{
			IScopedLock lock(mutex.get());
			std::set<std::string>::iterator it = paths.lower_bound(path);
			return it!=paths.end() && (*it == path
							|| next(*it, 0, path + "/") );
		}

		size_t refcount;
		IPipeFile* pipe_file;
		std::auto_ptr<IMutex> mutex;
		std::set<std::string> paths;
	};

	PipeFileTar(const std::string& pCmd, int backupnum, int64 fn_random, std::string output_fn, const std::string& server_token, const std::string& identity);
	PipeFileTar(PipeFileStore* pipe_file, const STarFile& tar_file, int64 file_offset, int backupnum, int64 fn_random, std::string output_fn, const std::string& server_token, const std::string& identity);
	~PipeFileTar();

	virtual std::string Read(_u32 tr, bool *has_error = NULL);

	virtual std::string Read(int64 spos, _u32 tr, bool *has_error = NULL);

	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error = NULL);

	virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error = NULL);

	virtual bool Seek(_i64 spos);

	bool switchNext(std::string& fn, bool& is_dir, bool& is_symlink, bool& is_special, std::string& symlink_target, int64& size, std::string & stderr_ret, bool *has_error = NULL);

	virtual _i64 Size();

	// Inherited via IPipeFile
	virtual _u32 Write(const std::string & tw, bool * has_error = NULL);
	virtual _u32 Write(int64 spos, const std::string & tw, bool * has_error = NULL);
	virtual _u32 Write(const char * buffer, _u32 bsiz, bool * has_error = NULL);
	virtual _u32 Write(int64 spos, const char * buffer, _u32 bsiz, bool * has_error = NULL);
	virtual _i64 RealSize();
	virtual bool PunchHole(_i64 spos, _i64 size);
	virtual bool Sync();
	virtual std::string getFilename(void);
	virtual int64 getLastRead();
	virtual bool getHasError();
	virtual std::string getStdErr();
	virtual bool getExitCode(int & exit_code);
	virtual void forceExitWait();
	virtual void addUser();
	virtual void removeUser();
	virtual bool hasUser();
	virtual int64 getPos();
	virtual bool waitForStderr(int64 timeoutms);

private:

	void hashReadData(int64 spos, const char* buffer, _u32 bsize);

	std::string buildCurrMetadata();

	bool readHeader(bool* has_error, std::string & stderr_ret);

	void addErrMsg(const std::string& msg, std::string& stderr_ret);

	int64 file_offset;	

	STarFile tar_file;

	sha_def_ctx sha_ctx;
	int64 hash_pos;

	std::auto_ptr<IMutex> mutex;

	PipeFileStore* pipe_file;

	int backupnum;
	int64 fn_random;

	bool free_pipe_file;
	bool has_next;

	std::string output_fn;

	std::string server_token;
	std::string identity;
};


#if defined(_WIN32) || defined(__APPLE__) || defined(__FreeBSD__)
#undef stat64
#endif
