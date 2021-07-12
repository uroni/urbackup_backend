#pragma once
#include <memory>
#include "IKvStoreBackend.h"
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/StorageClass.h>
#include "ICompressEncrypt.h"
#include "../Interface/Mutex.h"
#include <stack>
#include "../common/relaxed_atomic.h"

class IOnlineKvStore;
class IBackupFileSystem;

class KvStoreBackendS3 : public IKvStoreBackend
{
public:
	KvStoreBackendS3(const std::string& encryption_key, const std::string& access_key, const std::string& secret_access_key,
		const std::string& bucket_name, ICompressEncryptFactory* compress_encrypt_factory, const std::string& s3_endpoint,
		const std::string& s3_region, const std::string& p_storage_class, unsigned int comp_method, unsigned int comp_method_metadata,
		IBackupFileSystem* cachefs);

	static void init_mutex();
		
	virtual bool get( const std::string& key, const std::string& md5sum, 
				unsigned int flags, bool allow_error_event, IFsFile* ret_file, std::string& ret_md5sum, unsigned int& get_status);

	virtual bool list( IListCallback* callback );

	virtual bool put( const std::string& key, IFsFile* src,
				unsigned int flags, bool allow_error_event, std::string& md5sum, 
				int64& compressed_size) override;

	virtual bool del(key_next_fun_t key_next_fun,
		bool background_queue) {
		return del(key_next_fun, nullptr, background_queue);
	}
						
	virtual bool del(key_next_fun_t key_next_fun,
		locinfo_next_fun_t locinfo_next_fun,
		bool background_queue);
	
	virtual size_t max_del_size() { return 100; }
	
	virtual size_t num_del_parallel() { return 1; }
	
	virtual size_t num_scrub_parallel() { return 1; };

	virtual void setFrontend(IOnlineKvStore* online_kv_store, bool do_init);
	
	virtual bool sync(bool sync_test, bool background_queue) { return true; }
	
	virtual bool is_put_sync() { return true; }
	
	virtual bool has_transactions() { return false; }
	
	virtual bool prefer_sequential_read() { return false; }
	
	virtual bool del_with_location_info() { return true; }
	
	virtual bool ordered_del() { return false; }
	
	virtual bool can_read_unsynced() {
		return true;
	}
	
	virtual std::string meminfo();
	
	virtual bool check_deleted(const std::string& key, const std::string& locinfo)
	{
		//not implemented
		return false;
	}
	
	virtual bool need_curr_del(){ return false; }

	virtual int64 get_uploaded_bytes() {
		return uploaded_bytes;
	}

	virtual int64 get_downloaded_bytes() {
		return downloaded_bytes;
	}

	void add_uploaded_bytes(int64 n) {
		uploaded_bytes+=n;
	}

	virtual bool want_put_metadata() { return false; }
	
	virtual bool fast_write_retry() { return false; }

private:
	virtual bool list_wo_versions(IListCallback* callback);

	std::string encryption_key;
	std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > getS3Client(size_t idx, bool useVirtualAdressing=true);
	std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > newS3Client(size_t idx, int64 curr_requesttimeout, bool useVirtualAdressing);
	void releaseS3Client(size_t idx, std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > client);
	void resetClient();
	
	virtual bool del_int( key_next_fun_t key_next_fun,
		locinfo_next_fun_t locinfo_next_fun, bool shard_optimized);
	
	void fixError(Aws::S3::S3Errors error);
	
	struct SBucket
	{
		std::string name;
		Aws::S3::Model::BucketLocationConstraint location;
	};
		
	std::vector<SBucket> buckets;
	
	std::string s3_endpoint;
	std::string s3_region;
	Aws::S3::Model::StorageClass storage_class;

	static int64 max_request_timems;
	static int64 n_requests;
	static IMutex* client_mutex;
	std::vector<std::stack<std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > > > s3_clients;

	std::shared_ptr<Aws::Auth::AWSCredentialsProvider> credentials_provider;

	ICompressEncryptFactory* compress_encrypt_factory;
	IOnlineKvStore* online_kv_store;
	unsigned int comp_method;
	unsigned int comp_method_metadata;

	relaxed_atomic<int64> uploaded_bytes;

	relaxed_atomic<int64> downloaded_bytes;

	IBackupFileSystem* cachefs;
};
