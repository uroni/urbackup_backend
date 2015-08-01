#ifndef IFILESERV_H
#define IFILESERV_H

#include <string>

#include "../Interface/Object.h"
#include "../Interface/File.h"

class IPipe;

class IFileServ : public IObject
{
public:

	class IMetadataCallback
	{
	public:
		virtual ~IMetadataCallback() {}

		virtual IFile* getMetadata(const std::string& path, std::string& orig_path, int64& offset, int64& length)=0;
	};

	class ITokenCallback
	{
	public:
		virtual std::string getFileTokens(const std::wstring& fn) = 0;
	};

	class ITokenCallbackFactory
	{
	public:
		virtual ITokenCallback* getTokenCallback() = 0;
	};


	virtual void shareDir(const std::wstring &name, const std::wstring &path, const std::string& identity)=0;
	virtual void removeDir(const std::wstring &name, const std::string& identity)=0;
	virtual std::wstring getServerName(void)=0;
	virtual void stopServer(void)=0;
	virtual std::wstring getShareDir(const std::wstring &name, const std::string& identity)=0;
	virtual void addIdentity(const std::string &pIdentity)=0;
	virtual bool removeIdentity(const std::string &pIdentity)=0;
	virtual void setPause(bool b)=0;
	virtual bool getPause(void)=0;
	virtual void runClient(IPipe *cp)=0;
	virtual bool getExitInformation(const std::wstring& cmd, std::string& stderr_data, int& exit_code) = 0;
	virtual void addScriptOutputFilenameMapping(const std::wstring& script_output_fn, const std::wstring& script_fn) = 0;
	virtual void registerMetadataCallback(const std::wstring &name, const std::string& identity, IMetadataCallback* callback) = 0;
	virtual void removeMetadataCallback(const std::wstring &name, const std::string& identity) = 0;
	virtual void registerTokenCallbackFactory(ITokenCallbackFactory* callback_factory) = 0;
};

#endif //IFILESERV_H