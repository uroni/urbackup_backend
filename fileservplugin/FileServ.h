#pragma once

#include "IFileServ.h"
#include "../Interface/Mutex.h"
#include "../Interface/ThreadPool.h"
#include <vector>

class IPipeFile;

class FileServ : public IFileServ
{
public:
	FileServ(bool *pDostop, const std::string &servername, THREADPOOL_TICKET serverticket, bool use_fqdn);
	~FileServ(void);
	void shareDir(const std::string &name, const std::string &path, const std::string& identity, bool allow_exec);
	bool removeDir(const std::string &name, const std::string& identity);
	void stopServer(void);
	std::string getServerName(void);
	std::string getShareDir(const std::string &name, const std::string& identity);
	void addIdentity(const std::string &pIdentity, bool only_tunneled);
	bool removeIdentity(const std::string &pIdentity);
	void setPause(bool b);
	bool getPause(void);
	bool getExitInformation(const std::string& cmd, std::string& stderr_data, int& exit_code);
	void addScriptOutputFilenameMapping(const std::string& script_output_fn, const std::string& script_fn, bool tar_file);

	virtual void registerMetadataCallback(const std::string &name, const std::string& identity, IMetadataCallback* callback);

	virtual void removeMetadataCallback(const std::string &name, const std::string& identity);

	virtual void runClient(IPipe *cp, std::vector<char>* extra_buffer);

	static bool isPause(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static bool checkIdentity(const std::string &pIdentity, bool tunneled);

	static std::string mapScriptOutputNameToScript(const std::string& script_fn, bool& tar_file, IPipeFile*& pipe_file);

	virtual void registerTokenCallbackFactory( IFileServ::ITokenCallbackFactory* callback_factory );

	static IFileServ::ITokenCallback* newTokenCallback();

	static size_t incrShareActive(std::string sharename);

	static void decrShareActive(std::string sharename, size_t gen);

	bool hasActiveTransfers(const std::string& sharename, const std::string& server_token);

	bool hasActiveTransfersGen(const std::string& sharename, const std::string& server_token, size_t gen);

	bool registerFnRedirect(const std::string& source_fn, const std::string& target_fn);

	static std::string getRedirectedFn(const std::string& source_fn);

	static void callErrorCallback(std::string sharename, const std::string& filepath, int64 pos, const std::string& msg);

	static bool hasReadError(const std::string& filepath);

	virtual void registerReadErrorCallback(IReadErrorCallback* cb);

	void clearReadErrors();

	static void clearReadErrorFile(const std::string& filepath);

	void setCbtHashFile(const std::string& sharename, const std::string& identity, CbtHashFileInfo hash_file_info);

	static CbtHashFileInfo getCbtHashFile(const std::string& sharename, const std::string& identity);

	virtual void registerScriptPipeFile(const std::string& script_fn, IPipeFileExt* pipe_file);

	virtual void deregisterScriptPipeFile(const std::string& script_fn);

	virtual IFileMetadataPipe* getFileMetadataPipe();

	size_t incrActiveGeneration();

private:
	bool *dostop;
	THREADPOOL_TICKET serverticket;
	std::string servername;

	struct SIdentity
	{
		SIdentity()
			: tunneled(false)
		{

		}

		SIdentity(std::string identity, bool tunneled)
			: identity(identity), tunneled(tunneled)
		{}

		bool operator==(const SIdentity& other)
		{
			return identity == other.identity;
		}

		std::string identity;
		bool tunneled;
	};
	static std::vector<SIdentity > identities;
	static bool pause;

	struct SScriptMapping
	{
		SScriptMapping()
			: tar_file(false), pipe_file(NULL)
		{

		}

		SScriptMapping(std::string script_fn,
			bool tar_file, IPipeFile* pipe_file)
			: script_fn(script_fn), tar_file(tar_file), pipe_file(pipe_file)
		{}

		std::string script_fn;
		bool tar_file;
		IPipeFile* pipe_file;
	};

	static std::map<std::string, SScriptMapping> script_mappings;

	static std::map<std::string, std::string> fn_redirects;
	
	static IMutex *mutex;

	static ITokenCallbackFactory* token_callback_factory;

	static std::map<std::pair<std::string, size_t>, size_t> active_shares;

	static size_t active_generation;

	static IReadErrorCallback* read_error_callback;

	static std::vector<std::string> read_error_files;

	static std::map<std::pair<std::string, std::string>, CbtHashFileInfo> cbt_hash_files;
};


class ScopedShareActive
{
	size_t gen;
public:
	ScopedShareActive()
	{

	}

	ScopedShareActive(const std::string& sharename)
		: sharename(sharename)
	{
		if (!sharename.empty())
		{
			gen = FileServ::incrShareActive(sharename);
		}
	}

	~ScopedShareActive()
	{
		if (!sharename.empty())
		{
			FileServ::decrShareActive(sharename, gen);
		}
	}

	void reset(const std::string& new_sharename)
	{
		if (!sharename.empty())
		{
			FileServ::decrShareActive(sharename, gen);
		}
		sharename = new_sharename;
		if (!sharename.empty())
		{
			gen = FileServ::incrShareActive(sharename);
		}
	}

	size_t release()
	{
		sharename.clear();
		return gen;
	}

private:
	std::string sharename;
};