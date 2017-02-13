#ifndef IFILESERV_H
#define IFILESERV_H

#include <string>

#include "../Interface/Object.h"
#include "../Interface/File.h"

class IPipe;
class IPipeFileExt;

class IFileServ : public IObject
{
public:

	class IMetadataCallback
	{
	public:
		virtual ~IMetadataCallback() {}

		virtual IFile* getMetadata(const std::string& path, std::string* orig_path, int64* offset, int64* length, _u32* version, bool get_hashdata)=0;
	};

	class ITokenCallback : public IObject
	{
	public:
		virtual std::string getFileTokens(const std::string& fn) = 0;
		virtual std::string translateTokens(int64 uid, int64 gid, int64 mode) = 0;
	};

	class ITokenCallbackFactory
	{
	public:
		virtual ITokenCallback* getTokenCallback() = 0;
	};

	class IReadErrorCallback
	{
	public:
		virtual void onReadError(const std::string& sharename, const std::string& filepath, int64 pos, const std::string& msg) = 0;
	};


	virtual void shareDir(const std::string &name, const std::string &path, const std::string& identity, bool allow_exec)=0;
	virtual bool removeDir(const std::string &name, const std::string& identity)=0;
	virtual std::string getServerName(void)=0;
	virtual void stopServer(void)=0;
	virtual std::string getShareDir(const std::string &name, const std::string& identity)=0;
	virtual void addIdentity(const std::string &pIdentity)=0;
	virtual bool removeIdentity(const std::string &pIdentity)=0;
	virtual void setPause(bool b)=0;
	virtual bool getPause(void)=0;
	virtual void runClient(IPipe *cp, std::vector<char>* extra_buffer)=0;
	virtual bool getExitInformation(const std::string& cmd, std::string& stderr_data, int& exit_code) = 0;
	virtual void addScriptOutputFilenameMapping(const std::string& script_output_fn, const std::string& script_fn, bool tar_file) = 0;
	virtual void registerMetadataCallback(const std::string &name, const std::string& identity, IMetadataCallback* callback) = 0;
	virtual void removeMetadataCallback(const std::string &name, const std::string& identity) = 0;
	virtual void registerTokenCallbackFactory(ITokenCallbackFactory* callback_factory) = 0;
	virtual bool hasActiveTransfers(const std::string& sharename, const std::string& server_token) = 0;
	virtual bool registerFnRedirect(const std::string& source_fn, const std::string& target_fn) = 0;
	virtual void registerReadErrorCallback(IReadErrorCallback* cb) = 0;
	virtual void registerScriptPipeFile(const std::string& script_fn, IPipeFileExt* pipe_file) = 0;
	virtual void clearReadErrors() = 0;

	struct CbtHashFileInfo
	{
		CbtHashFileInfo(IFile* cbt_hash_file,
			int64 blocksize, size_t* snapshot_sequence_id, size_t snapshot_sequence_id_reference)
			: cbt_hash_file(cbt_hash_file),
			blocksize(blocksize), snapshot_sequence_id(snapshot_sequence_id), 
			snapshot_sequence_id_reference(snapshot_sequence_id_reference),
			metadata_offset(-1), metadata_size(-1)
		{}

		CbtHashFileInfo()
			: cbt_hash_file(NULL), snapshot_sequence_id(NULL), blocksize(0)
		{
		}

		IFile* cbt_hash_file;
		volatile size_t* snapshot_sequence_id;
		size_t snapshot_sequence_id_reference;
		int64 blocksize;
		int64 metadata_offset;
		int64 metadata_size;
	};

	virtual void setCbtHashFile(const std::string& sharename, const std::string& identity, CbtHashFileInfo hash_file_info) = 0;
};

#endif //IFILESERV_H