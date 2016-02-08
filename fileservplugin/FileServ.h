#include "IFileServ.h"
#include "../Interface/Mutex.h"
#include "../Interface/ThreadPool.h"
#include <vector>

class FileServ : public IFileServ
{
public:
	FileServ(bool *pDostop, const std::string &servername, THREADPOOL_TICKET serverticket, bool use_fqdn);
	~FileServ(void);
	void shareDir(const std::string &name, const std::string &path, const std::string& identity, bool allow_exec);
	void removeDir(const std::string &name, const std::string& identity);
	void stopServer(void);
	std::string getServerName(void);
	std::string getShareDir(const std::string &name, const std::string& identity);
	void addIdentity(const std::string &pIdentity);
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

	static bool checkIdentity(const std::string &pIdentity);

	static std::string mapScriptOutputNameToScript(const std::string& script_fn, bool& tar_file);

	virtual void registerTokenCallbackFactory( IFileServ::ITokenCallbackFactory* callback_factory );

	static IFileServ::ITokenCallback* newTokenCallback();

	bool hasActiveMetadataTransfers(const std::string& sharename, const std::string& server_token);

	bool registerFnRedirect(const std::string& source_fn, const std::string& target_fn);

private:
	bool *dostop;
	THREADPOOL_TICKET serverticket;
	std::string servername;

	static std::vector<std::string> identities;
	static bool pause;

	struct SScriptMapping
	{
		SScriptMapping()
			: tar_file(false)
		{

		}

		SScriptMapping(std::string script_fn,
			bool tar_file)
			: script_fn(script_fn), tar_file(tar_file)
		{}

		std::string script_fn;
		bool tar_file;
	};

	static std::map<std::string, SScriptMapping> script_mappings;
	
	static IMutex *mutex;

	static ITokenCallbackFactory* token_callback_factory;
};
