#pragma once
#include "../Interface/File.h"
#include "../Interface/Object.h"
#include "../Interface/SharedMutex.h"

class IOnlineKvStore : public IObject
{
public:
	virtual IFsFile* get(const std::string& key, int64 transid,
		bool prioritize_read, IFsFile* tmpl_file, bool allow_error_event,
		bool& not_found, int64* get_transid=nullptr) = 0;

	//0 if not implemented -- allowed to have 0 as false negative
	virtual int64 get_transid(const std::string& key, int64 transid) = 0;

	virtual bool reset(const std::string& key, int64 transid) = 0;

	const static unsigned int PutAlreadyCompressedEncrypted = 1;
	const static unsigned int PutMetadata = 2;

	virtual bool put(const std::string& key, int64 transid, IFsFile* src, 
		unsigned int flags,
		bool allow_error_event, int64& compressed_size) = 0;

	virtual int64 new_transaction(bool allow_error_event) = 0;

	virtual bool transaction_finalize(int64 transid, bool complete, bool allow_error_event) = 0;

	virtual bool set_active_transactions(const std::vector<int64>& active_transactions) = 0;

	virtual bool del(const std::vector<std::string>& keys, int64 transid) = 0;

	virtual size_t max_del_size() = 0;

	virtual int64 generation_inc(int64 inc) = 0;

	virtual std::string get_stats() = 0;

	virtual bool sync() = 0;

	virtual bool sync_db() = 0;

	virtual bool sync_lock(IScopedWriteLock& lock) = 0;

	virtual bool is_put_sync() = 0;

	virtual std::string meminfo() = 0;

	virtual bool has_backend_key(const std::string& key, std::string& md5sum, bool update_md5sum) = 0;

	virtual int64 get_uploaded_bytes() = 0;

	virtual int64 get_downloaded_bytes() = 0;

	virtual bool want_put_metadata() = 0;

	virtual bool fast_write_retry() = 0;

	class IHasKeyCallback
	{
	public:
		virtual bool hasKey(const std::string& key) = 0;
	};
		
	virtual bool submit_del(IHasKeyCallback* has_key_callback, int64 ctransid, bool& need_flush) = 0;

	virtual void submit_del_post_flush() = 0;
};