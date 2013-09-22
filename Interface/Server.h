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
class IThread;
class ISettingsReader;
class IPipe;
class IFile;
class IOutputStream;
class IThreadPool;
class ICondition;
class IScopedLock;
class IDatabaseFactory;
class IPipeThrottler;

struct SPostfile
{
	SPostfile(IFile *f, std::wstring n, std::wstring ct){ file=f; name=n; contenttype=ct; }
	SPostfile(){ file=NULL; }
	IFile *file;
	std::wstring name;
	std::wstring contenttype;
};

struct SCircularLogEntry
{
	SCircularLogEntry(void)
		: loglevel(LL_DEBUG), id(std::string::npos)
	{
	}

	std::string utf8_msg;
	int loglevel;
	size_t id;
	unsigned int time;
};

class IServer
{
public:
	virtual void setLogLevel(int LogLevel)=0;
	virtual void setLogFile(const std::string &plf, std::string chown_user="")=0;
	virtual void setLogCircularBufferSize(size_t size)=0;
	virtual const std::vector<SCircularLogEntry>& getCicularLogBuffer(void)=0;
	virtual void Log(const std::string &pStr, int LogLevel=LL_INFO)=0;
	virtual void Log(const std::wstring &pStr, int LogLevel=LL_INFO)=0;
	virtual void Write(THREAD_ID tid, const std::string &str, bool cached=true)=0;
	virtual void WriteRaw(THREAD_ID tid, const char *buf, size_t bsize, bool cached=true)=0;

	virtual std::string getServerParameter(const std::string &key)=0;
	virtual std::string getServerParameter(const std::string &key, const std::string &def)=0;
	virtual void setServerParameter(const std::string &key, const std::string &value)=0;

	virtual void setContentType(THREAD_ID tid, const std::string &str)=0;
	virtual void addHeader(THREAD_ID tid, const std::string &str)=0;

	virtual THREAD_ID Execute(const std::wstring &action, const std::wstring &context, str_map &GET, str_map &POST, str_nmap &PARAMS, IOutputStream *req)=0;
	virtual std::string Execute(const std::wstring &action, const std::wstring &context, str_map &GET, str_map &POST, str_nmap &PARAMS)=0;

	virtual void AddAction(IAction *action)=0;
	virtual bool RemoveAction(IAction *action)=0;
	virtual void setActionContext(std::wstring context)=0;
	virtual void resetActionContext(void)=0;

	virtual unsigned int getTimeSeconds(void)=0;
	virtual unsigned int getTimeMS(void)=0;

	virtual bool LoadDLL(const std::string &name)=0;
	virtual bool UnloadDLL(const std::string &name)=0;

	virtual void destroy(IObject *obj)=0;

	virtual void wait(unsigned int ms)=0;

	virtual ITemplate* createTemplate(std::string pFile)=0;
	virtual IMutex* createMutex(void)=0;
	virtual ICondition* createCondition(void)=0;
	virtual void createThread(IThread *thread)=0;
	virtual IPipe *createMemoryPipe(void)=0;
	virtual IThreadPool *getThreadPool(void)=0;
	virtual ISettingsReader* createFileSettingsReader(std::string pFile)=0;
	virtual ISettingsReader* createDBSettingsReader(THREAD_ID tid, DATABASE_ID pIdentifier, const std::string &pTable, const std::string &pSQL="")=0;
	virtual ISettingsReader* createDBSettingsReader(IDatabase *db, const std::string &pTable, const std::string &pSQL="")=0;
	virtual ISettingsReader* createMemorySettingsReader(const std::string &pData)=0;
	virtual IPipeThrottler* createPipeThrottler(size_t bps)=0;

	virtual bool openDatabase(std::string pFile, DATABASE_ID pIdentifier, std::string pEngine="sqlite")=0;
	virtual IDatabase* getDatabase(THREAD_ID tid, DATABASE_ID pIdentifier)=0;
	virtual void destroyAllDatabases(void)=0;
	virtual ISessionMgr *getSessionMgr(void)=0;
	virtual IPlugin* getPlugin(THREAD_ID tid, PLUGIN_ID pIdentifier)=0;

	virtual THREAD_ID getThreadID(void)=0;
	
	virtual std::string ConvertToUTF8(const std::wstring &input)=0;
	virtual std::wstring ConvertToUnicode(const std::string &input)=0;
	virtual std::string ConvertToUTF16(const std::wstring &input)=0;
	virtual std::string ConvertToUTF32(const std::wstring &input)=0;
	virtual std::wstring ConvertFromUTF16(const std::string &input)=0;
	virtual std::wstring ConvertFromUTF32(const std::string &input)=0;

	virtual std::string GenerateHexMD5(const std::wstring &input)=0;
	virtual std::string GenerateBinaryMD5(const std::wstring &input)=0;
	virtual std::string GenerateHexMD5(const std::string &input)=0;
	virtual std::string GenerateBinaryMD5(const std::string &input)=0;

	virtual void StartCustomStreamService(IService *pService, std::string pServiceName, unsigned short pPort, int pMaxClientsPerThread=-1)=0;
	virtual IPipe* ConnectStream(std::string pServer, unsigned short pPort, unsigned int pTimeoutms=0)=0;
	virtual IPipe *PipeFromSocket(SOCKET pSocket)=0;
	virtual void DisconnectStream(IPipe *pipe)=0;

	virtual bool RegisterPluginPerThreadModel(IPluginMgr *pPluginMgr, std::string pName)=0;
	virtual bool RegisterPluginThreadsafeModel(IPluginMgr *pPluginMgr, std::string pName)=0;

	virtual PLUGIN_ID StartPlugin(std::string pName, str_map &params)=0;  

	virtual bool RestartPlugin(PLUGIN_ID pIdentifier)=0;

	virtual unsigned int getNumRequests(void)=0;
	virtual void addRequest(void)=0;

	virtual IFile* openFile(std::string pFilename, int pMode=0)=0;
	virtual IFile* openFile(std::wstring pFilename, int pMode=0)=0;
	virtual IFile* openFileFromHandle(void *handle)=0;
	virtual IFile* openTemporaryFile(void)=0;
	virtual IFile* openMemoryFile(void)=0;
	virtual bool deleteFile(std::string pFilename)=0;
	virtual bool deleteFile(std::wstring pFilename)=0;

	virtual POSTFILE_KEY getPostFileKey()=0;
	virtual void addPostFile(POSTFILE_KEY pfkey, const std::string &name, const SPostfile &pf)=0;
	virtual SPostfile getPostFile(POSTFILE_KEY pfkey, const std::string &name)=0;
	virtual void clearPostFiles(POSTFILE_KEY pfkey)=0;

	virtual std::wstring getServerWorkingDir(void)=0;

	virtual void setTemporaryDirectory(const std::wstring &dir)=0;

	virtual void registerDatabaseFactory(const std::string &pEngineName, IDatabaseFactory *factory)=0;
	virtual bool hasDatabaseFactory(const std::string &pEngineName)=0;

	virtual bool attachToDatabase(const std::string &pFile, const std::string &pName, DATABASE_ID pIdentifier)=0;

	virtual void waitForStartupComplete(void)=0;

	virtual void shutdown(void)=0;

	virtual unsigned int getRandomNumber(void)=0;
	virtual std::vector<unsigned int> getRandomNumbers(size_t n)=0;
	virtual void randomFill(char *buf, size_t blen)=0;

	virtual unsigned int getSecureRandomNumber(void)=0;
	virtual std::vector<unsigned int> getSecureRandomNumbers(size_t n)=0;
	virtual void secureRandomFill(char *buf, size_t blen)=0;
};

#ifndef NO_INTERFACE
#ifndef DEF_SERVER
extern IServer* Server;
#endif
#endif

#endif //INTERFACE_SERVER_H
