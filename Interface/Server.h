#ifndef INTERFACE_SERVER_H
#define INTERFACE_SERVER_H

#include <string>
#include "Types.h"

#define LL_DEBUG -1
#define LL_INFO 0
#define LL_WARNING 1
#define LL_ERROR 2

class IAction;
class ITemplate;
class IObject;
class IDatabase;
class ISessionMgr;
class IService;
class IPluginMgr;
class IPlugin;
class IMutex;
class ISharedMutex;
class IThread;
class ISettingsReader;
class IPipe;
class IFile;
class IFsFile;
class IOutputStream;
class IThreadPool;
class ICondition;
class IScopedLock;
class IDatabaseFactory;
class IPipeThrottler;
class IPipeThrottlerUpdater;

struct SPostfile
{
	SPostfile(IFile *f, std::string n, std::string ct){ file=f; name=n; contenttype=ct; }
	SPostfile(){ file=NULL; }
	IFile *file;
	std::string name;
	std::string contenttype;
};

struct SCircularLogEntry
{
	SCircularLogEntry(void)
		: loglevel(LL_DEBUG), id(std::string::npos), time(0)
	{
	}

	std::string utf8_msg;
	int loglevel;
	size_t id;
	int64 time;
};

class IServer
{
public:
	virtual void setLogLevel(int LogLevel)=0;
	virtual void setLogFile(const std::string &plf, std::string chown_user="")=0;
	virtual void setLogCircularBufferSize(size_t size)=0;
	virtual std::vector<SCircularLogEntry> getCicularLogBuffer(size_t minid)=0;
	virtual void Log(const std::string &pStr, int LogLevel=LL_INFO)=0;
	virtual bool Write(THREAD_ID tid, const std::string &str, bool cached=true)=0;
	virtual bool WriteRaw(THREAD_ID tid, const char *buf, size_t bsize, bool cached=true)=0;

	virtual std::string getServerParameter(const std::string &key)=0;
	virtual std::string getServerParameter(const std::string &key, const std::string &def)=0;
	virtual void setServerParameter(const std::string &key, const std::string &value)=0;

	virtual void setContentType(THREAD_ID tid, const std::string &str)=0;
	virtual void addHeader(THREAD_ID tid, const std::string &str)=0;

	virtual THREAD_ID Execute(const std::string &action, const std::string &context, str_map &GET, str_map &POST, str_map &PARAMS, IOutputStream *req)=0;
	virtual std::string Execute(const std::string &action, const std::string &context, str_map &GET, str_map &POST, str_map &PARAMS)=0;

	virtual void AddAction(IAction *action)=0;
	virtual bool RemoveAction(IAction *action)=0;
	virtual void setActionContext(std::string context)=0;
	virtual void resetActionContext(void)=0;

	virtual int64 getTimeSeconds(void)=0;
	virtual int64 getTimeMS(void)=0;

	virtual bool LoadDLL(const std::string &name)=0;
	virtual bool UnloadDLL(const std::string &name)=0;

	virtual void destroy(IObject *obj)=0;

	virtual void wait(unsigned int ms)=0;

	virtual ITemplate* createTemplate(std::string pFile)=0;
	virtual IMutex* createMutex(void)=0;
	virtual ISharedMutex* createSharedMutex()=0;
	virtual ICondition* createCondition(void)=0;

	enum CreateThreadFlags
	{
		CreateThreadFlags_None=0,
		CreateThreadFlags_LargeStackSize=1
	};

	virtual bool createThread(IThread *thread, const std::string& name=std::string(), CreateThreadFlags flags = CreateThreadFlags_None)=0;
	virtual void setCurrentThreadName(const std::string& name) = 0;
	virtual IPipe *createMemoryPipe(void)=0;
	virtual IThreadPool *getThreadPool(void)=0;
	virtual ISettingsReader* createFileSettingsReader(const std::string& pFile)=0;
	virtual ISettingsReader* createDBSettingsReader(THREAD_ID tid, DATABASE_ID pIdentifier, const std::string &pTable, const std::string &pSQL="")=0;
	virtual ISettingsReader* createDBSettingsReader(IDatabase *db, const std::string &pTable, const std::string &pSQL="")=0;
	virtual ISettingsReader* createDBMemSettingsReader(THREAD_ID tid, DATABASE_ID pIdentifier, const std::string &pTable, const std::string &pSQL = "") = 0;
	virtual ISettingsReader* createDBMemSettingsReader(IDatabase *db, const std::string &pTable, const std::string &pSQL = "") = 0;
	virtual ISettingsReader* createMemorySettingsReader(const std::string &pData)=0;
	virtual IPipeThrottler* createPipeThrottler(size_t bps, bool percent_max) = 0;
	virtual IPipeThrottler* createPipeThrottler(IPipeThrottlerUpdater* updater) = 0;
	virtual IThreadPool* createThreadPool(size_t max_threads, size_t max_waiting_threads, const std::string& idle_name) = 0;

	virtual bool openDatabase(std::string pFile, DATABASE_ID pIdentifier, const str_map& params = str_map(), std::string pEngine="sqlite")=0;
	virtual IDatabase* getDatabase(THREAD_ID tid, DATABASE_ID pIdentifier)=0;
	virtual void destroyAllDatabases(void)=0;
	virtual void destroyDatabases(THREAD_ID tid)=0;
	virtual void clearDatabases(THREAD_ID tid) = 0;
	virtual ISessionMgr *getSessionMgr(void)=0;
	virtual IPlugin* getPlugin(THREAD_ID tid, PLUGIN_ID pIdentifier)=0;

	virtual THREAD_ID getThreadID(void)=0;
	
	virtual std::string ConvertToUTF16(const std::string &input)=0;
	virtual std::string ConvertToUTF32(const std::string &input)=0;
	virtual std::wstring ConvertToWchar(const std::string &input)=0;
	virtual std::string ConvertFromWchar(const std::wstring &input)=0;
	virtual std::string ConvertFromUTF16(const std::string &input)=0;
	virtual std::string ConvertFromUTF32(const std::string &input)=0;

	virtual std::string GenerateHexMD5(const std::string &input)=0;
	virtual std::string GenerateBinaryMD5(const std::string &input)=0;

	enum BindTarget
	{
		BindTarget_Localhost,
		BindTarget_All
	};

	virtual void StartCustomStreamService(IService *pService, std::string pServiceName, unsigned short pPort, int pMaxClientsPerThread=-1, BindTarget bindTarget=BindTarget_All)=0;
	virtual IPipe* ConnectStream(std::string pServer, unsigned short pPort, unsigned int pTimeoutms=0)=0;
	virtual IPipe *PipeFromSocket(SOCKET pSocket)=0;
	virtual void DisconnectStream(IPipe *pipe)=0;
	virtual std::string LookupHostname(const std::string& pIp)=0;

	virtual bool RegisterPluginPerThreadModel(IPluginMgr *pPluginMgr, std::string pName)=0;
	virtual bool RegisterPluginThreadsafeModel(IPluginMgr *pPluginMgr, std::string pName)=0;

	virtual PLUGIN_ID StartPlugin(std::string pName, str_map &params)=0;  

	virtual bool RestartPlugin(PLUGIN_ID pIdentifier)=0;

	virtual unsigned int getNumRequests(void)=0;
	virtual void addRequest(void)=0;

	virtual IFsFile* openFile(std::string pFilename, int pMode=0)=0;
	virtual IFsFile* openFileFromHandle(void *handle, const std::string& pFilename)=0;
	virtual IFsFile* openTemporaryFile(void)=0;
	virtual IFile* openMemoryFile(void)=0;
	virtual bool deleteFile(std::string pFilename)=0;
	virtual bool fileExists(std::string pFilename)=0;

	virtual POSTFILE_KEY getPostFileKey()=0;
	virtual void addPostFile(POSTFILE_KEY pfkey, const std::string &name, const SPostfile &pf)=0;
	virtual SPostfile getPostFile(POSTFILE_KEY pfkey, const std::string &name)=0;
	virtual void clearPostFiles(POSTFILE_KEY pfkey)=0;

	virtual std::string getServerWorkingDir(void)=0;

	virtual void setTemporaryDirectory(const std::string &dir)=0;

	virtual void registerDatabaseFactory(const std::string &pEngineName, IDatabaseFactory *factory)=0;
	virtual bool hasDatabaseFactory(const std::string &pEngineName)=0;

	virtual bool attachToDatabase(const std::string &pFile, const std::string &pName, DATABASE_ID pIdentifier)=0;
	virtual bool setDatabaseAllocationChunkSize(DATABASE_ID pIdentifier, size_t allocation_chunk_size) = 0;

	virtual void waitForStartupComplete(void)=0;

	virtual void shutdown(void)=0;

	virtual unsigned int getRandomNumber(void)=0;
	virtual std::vector<unsigned int> getRandomNumbers(size_t n)=0;
	virtual void randomFill(char *buf, size_t blen)=0;

	virtual unsigned int getSecureRandomNumber(void)=0;
	virtual std::vector<unsigned int> getSecureRandomNumbers(size_t n)=0;
	virtual void secureRandomFill(char *buf, size_t blen)=0;
	virtual std::string secureRandomString(size_t len)=0;
	 
	static const size_t FAIL_DATABASE_CORRUPTED=1;
	static const size_t FAIL_DATABASE_IOERR=2;
	static const size_t FAIL_DATABASE_FULL = 4;

	virtual void setFailBit(size_t failbit)=0;
	virtual void clearFailBit(size_t failbit)=0;
	virtual size_t getFailBits(void)=0;
};

#ifndef NO_INTERFACE
#ifndef DEF_SERVER
extern IServer* Server;
#endif
#endif

#endif //INTERFACE_SERVER_H
