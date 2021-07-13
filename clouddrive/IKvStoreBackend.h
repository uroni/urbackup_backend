#pragma once
#include <string>
#include <functional>
#include "../Interface/Types.h"
#include "../Interface/File.h"
#include "../Interface/Object.h"

class IOnlineKvStore;

namespace
{
	std::string get_md5sum(const std::string& md5sum)
	{
		if (md5sum.size() == 16)
			return md5sum;
		else if (md5sum.size() > 16)
			return md5sum.substr(0, 16);
		else
			return std::string();
	}

	std::string get_locinfo(const std::string& md5sum)
	{
		if (md5sum.size() == 16)
			return std::string();
		else if (md5sum.size() > 16)
			return md5sum.substr(16);
		else
			return md5sum;
	}
}

class IKvStoreBackend : public IObject
{
public:

	class IListCallback
	{
	public:
		virtual bool onlineItem(const std::string& key, const std::string& md5sum, int64 size, int64 last_modified)=0;
	};

	const static unsigned int GetDecrypted = 1;
	const static unsigned int GetRebalance = 2;
	const static unsigned int GetScrub = 4;
	const static unsigned int GetPrioritize = 8;
	const static unsigned int GetReadahead = 16;
	const static unsigned int GetUnsynced = 32;
	const static unsigned int GetRebuild = 64;
	const static unsigned int GetIgnoreReadErrors = 128;
	const static unsigned int GetPrependMd5sum = 256;
	const static unsigned int GetBackground = 512;
	const static unsigned int GetNoThrottle = 1024;
	const static unsigned int GetMetadata = 2048;

	const static unsigned int GetStatusRepaired = 1;
	const static unsigned int GetStatusEnospc = 2;
	const static unsigned int GetStatusNotFound = 4;
	const static unsigned int GetStatusRepairError = 8;
	const static unsigned int GetStatusSkipped = 16;

	virtual bool get( const std::string& key, const std::string& md5sum, 
		unsigned int flags, bool allow_error_event, IFsFile* ret_file, std::string& ret_md5sum, unsigned int& get_status) = 0;
	virtual bool list(IListCallback* callback) = 0;

	const static unsigned int PutAlreadyCompressedEncrypted = 1;
	const static unsigned int PutMetadata = 2;

	virtual bool put( const std::string& key, IFsFile* src,
		unsigned int flags, bool allow_error_event, std::string& md5sum, int64& size) = 0;

	enum class key_next_action_t
	{
		next,
		reset,
		clear
	};

	using key_next_fun_t = std::function<bool(key_next_action_t action, std::string* key)>;

	virtual bool del(key_next_fun_t key_next_fun,
		bool background_queue) = 0;

	using locinfo_next_fun_t = std::function<bool(key_next_action_t action, std::string* locinfo)>;

	virtual bool del(key_next_fun_t key_next_fun,
		locinfo_next_fun_t locinfo_next_fun,
		bool background_queue) = 0;

	virtual bool need_curr_del() = 0;

	virtual size_t max_del_size() = 0;
	virtual size_t num_del_parallel() = 0;
	virtual size_t num_scrub_parallel() = 0;
	virtual bool sync(bool sync_test, bool background_queue) = 0;
	virtual bool is_put_sync() = 0;
	virtual bool has_transactions() = 0;
	virtual bool prefer_sequential_read() = 0;
	virtual bool del_with_location_info() = 0;
	virtual bool ordered_del() = 0;
	virtual bool can_read_unsynced() = 0;

	virtual void setFrontend(IOnlineKvStore* online_kv_store, bool do_init) = 0;

	virtual std::string meminfo() = 0;

	virtual bool check_deleted(const std::string& key, const std::string& locinfo) = 0;

	virtual int64 get_uploaded_bytes()=0;

	virtual int64 get_downloaded_bytes()=0;

	virtual bool want_put_metadata() = 0;

	virtual bool fast_write_retry() = 0;
};
