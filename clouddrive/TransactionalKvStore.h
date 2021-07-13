#pragma once
#include <list>
#include <atomic>
#include "../common/lrucache.h"
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/BackupFileSystem.h"
#include <set>
#include "../Interface/Condition.h"
#include <memory>
#include <deque>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "IOnlineKvStore.h"
#include "../urbackupcommon/os_functions.h"
#ifdef HAS_ASYNC
#include "fuse_io_context.h"
#endif
#include "../common/relaxed_atomic.h"

class IFsFile;
class ICompressEncryptFactory;

#ifndef NDEBUG
#define DIRTY_ITEM_CHECK
#endif

#ifdef DIRTY_ITEM_CHECK
#define DIRTY_ITEM(x) x
#else
#define DIRTY_ITEM(x)
#endif

#ifndef HAS_ASYNC
class fuse_io_context
{
public:
	static bool is_sync_thread() {
		return true;
	}
};
#endif

namespace
{
	class SubmitWorker;
	class ReadOnlyFileWrapper;
	class Bitmap;

	enum ESubmissionAction
	{
		SubmissionAction_Working_Evict,
		SubmissionAction_Working_Dirty,
		SubmissionAction_Working_Delete,
		SubmissionAction_Working_Compress,
		SubmissionAction_Evict,
		SubmissionAction_Dirty,
		SubmissionAction_Delete,
		SubmissionAction_Compress
	};

	struct SSubmissionItem
	{
		SSubmissionItem()
			: finish(true)
		{}

		std::string key;
		std::vector<std::string> keys;
		ESubmissionAction action;
		int64 transid;
		int64 size;
		bool compressed;
		bool finish;
	};

	struct SFdKey
	{
		SFdKey(IFsFile* fd, int64 size, bool read_only)
			: fd(fd), size(size), read_only(read_only), refcount(1)
		{
		}

		SFdKey()
			: fd(NULL), size(0), read_only(true), refcount(1)
		{

		}

		IFsFile* fd;
		int64 size;
		bool read_only;
		int refcount;

		bool operator<(const SFdKey& other) const
		{
			if(other.fd==fd)
			{
				return read_only<other.read_only;
			}
			else
			{
				return fd < other.fd;
			}
		}

		bool operator==(const SFdKey& other) const
		{
			return other.fd==fd && other.read_only==read_only;
		}
	};

	struct SubmittedItem
	{
		std::string key;
		int64 submittime;
	};

	struct SMemFile
	{
		SMemFile(IFsFile* file, const std::string& key, int64 size)
			: file(file), size(size), compsize(-1), evicted(false), cow(false), key(key) {}

		SMemFile()
			: file(nullptr), size(0), compsize(-1), evicted(false), cow(false) {}

		std::shared_ptr<IFsFile> file;
		std::shared_ptr<IFsFile> old_file;
		int64 size;
		int64 compsize;
		bool evicted;
		bool cow;
		std::string key;
	};
}

#ifndef NDEBUG
class test_mutex : public std::mutex
{
public:
	test_mutex() noexcept 
		: std::mutex() {}

	test_mutex(const test_mutex&) = delete;
	test_mutex& operator=(const test_mutex&) = delete;

	void lock() {
		std::mutex::lock();
		if (lid != std::thread::id())
			abort();
		lid = std::this_thread::get_id();
	}

	void unlock() {
		if (lid != std::this_thread::get_id())
			abort();
		lid = std::thread::id();
		std::mutex::unlock();
	}

	bool has_lock() {
		return lid == std::this_thread::get_id();
	}

private:
	std::thread::id lid = std::thread::id();
};

using cache_mutex_t = test_mutex;
#else
using cache_mutex_t = std::recursive_mutex;
#endif

class TransactionalKvStore : public IThread
{
	class SubmitWorker;
	class RetrievalOperation;
	class RetrievalOperationNoLock;
	class RetrievalOperationUnlockOnly;

public:

	struct SCacheVal
	{
		SCacheVal()
			: dirty(false), chances(0)
		{}

		bool dirty : 1;
		unsigned char chances : 7;
	};

	class INumSecondChancesCallback
	{
	public:
		virtual unsigned int get_num_second_chances(const std::string& key) = 0;

		virtual bool is_metadata(const std::string& key) = 0;
	};

	TransactionalKvStore(IBackupFileSystem* cachefs, int64 min_cachesize, int64 min_free_size, int64 critical_free_size,
		int64 throttle_free_size, int64 min_metadata_cache_free, float comp_percent, int64 comp_start_limit,
		IOnlineKvStore* online_kv_store, const std::string& encryption_key, ICompressEncryptFactory* compress_encrypt_factory, bool verify_cache,
		float cpu_multiplier, size_t no_compress_mult, bool with_prev_link, bool allow_evict, bool with_submitted_files,
		float resubmit_compressed_ratio, int64 max_memfile_size, std::string memcache_path,
		float memory_usage_factor, bool only_memfiles, unsigned int background_comp_method,
		unsigned int cache_comp, unsigned int meta_cache_comp);

	~TransactionalKvStore();

	enum class BitmapInfo
	{
		Present,
		NotPresent,
		Unknown
	};

	class Flag
	{
	public:
		static constexpr unsigned int disable_fd_cache = 1;
		static constexpr unsigned int disable_throttling = 2;
		static constexpr unsigned int prioritize_read = 4;
		static constexpr unsigned int read_random = 8;
		static constexpr unsigned int read_only = 16;
		static constexpr unsigned int preload_once = 32;
		static constexpr unsigned int disable_memfiles = 64;
	};

	IFsFile* get(const std::string& key, 
		BitmapInfo bitmap_present, unsigned int flags, int64 size_hint,
		int preload_tag=0);

	void release(const std::string& key);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<void> release_async(fuse_io_context& io, const std::string& key);
#endif

	bool del(const std::string& key);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<bool> del_async(fuse_io_context& io, const std::string key);
#endif

	bool set_second_chances(const std::string& key, unsigned int chances);

	bool has_preload_once(const std::string& key);

	bool has_item_cached(const std::string& key);

	void remove_preload_items(int preload_tag);

	void dirty_all();

	bool checkpoint(bool do_submit, size_t checkpoint_retry_n);

	void operator()();

	void stop();

	int64 get_dirty_bytes();

	int64 get_submitted_bytes();

	int64 get_total_submitted_bytes();

	std::map<int64, size_t> get_num_dirty_items();

	std::map<int64, size_t> get_num_memfile_items();

	int64 get_cache_size();

	int64 get_comp_bytes();

	void reset();

	bool is_congested();

	bool is_congested_nolock();

	bool is_congested_async();

	int64 get_memfile_bytes();

	int64 get_submitted_memfile_bytes();

	bool is_memfile_complete(int64 ttransid);

	std::string meminfo();

	void shrink_mem();

	int64 get_total_hits();

	int64 get_total_hits_async();

	int64 get_total_memory_hits();

	int64 get_total_memory_hits_async();

	int64 get_total_cache_miss_backend();

	int64 get_total_cache_miss_decompress();

	int64 get_total_dirty_ops();

	int64 get_total_put_ops();

	int64 get_total_compress_ops();

	void disable_compression(int64 disablems);

	void set_num_second_chances_callback(INumSecondChancesCallback* cb);

	int64 get_transid();

	int64 get_basetransid();

	void set_max_cachesize(int64 ns);

	int64 cache_total_space();

	void set_disable_read_memfiles(bool b);

	void set_disable_write_memfiles(bool b);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<IFsFile*> get_async(fuse_io_context& io, const std::string& key,
		BitmapInfo bitmap_present, unsigned int flags, int64 size_hint,
		int preload_tag=0);
#endif

	void check_mutex_not_held();

private:

	IFsFile* get_internal(const std::string& key,
		BitmapInfo bitmap_present, unsigned int flags, int64 size_hint,
		int preload_tag);

	IFsFile* get_retrieve(const std::string& key, BitmapInfo bitmap_present, unsigned int flags,
		int64 size_hint, SFdKey* res, bool& cache_fd, std::unique_lock<cache_mutex_t>& cache_lock);

	int64 set_active_transactions(std::unique_lock<cache_mutex_t>& cache_lock, bool continue_incomplete);

	void cleanup(bool init);

	void read_keys(std::unique_lock<cache_mutex_t>& cache_lock, const std::string& tpath, bool verify);

	void read_missing();

	void remove_transaction(int64 transid);

	std::string keypath2(const std::string& key, int64 transaction_id);

	std::string transpath2();

	std::string basepath2();

	std::string hex(const std::string& key);

	std::string hexpath(const std::string& key);

	bool evict_one(std::unique_lock<cache_mutex_t>& cache_lock, bool break_on_skip, bool only_non_dirty,
		std::list<std::pair<std::string const *, SCacheVal> >::iterator& evict_it,
		common::lrucache<std::string, SCacheVal>& target_cache, bool use_chances,
		int64& freed_space,
		bool& run_del_items, std::vector<std::list<std::pair<std::string const *, SCacheVal> >::iterator>& move_front,
		bool& used_chance);

	void evict_move_front(common::lrucache<std::string, SCacheVal>& target_cache,
		std::vector<std::list<std::pair<std::string const *, SCacheVal> >::iterator>& move_front);

	void evict_item(const std::string& key, bool dirty,
		common::lrucache<std::string, SCacheVal>& target_cache,
		std::list<std::pair<std::string const *, SCacheVal> >::iterator* evict_it,
		std::unique_lock<cache_mutex_t>& cache_lock, const std::string& from, int64& freed_space);

	bool evict_memfiles(std::unique_lock<cache_mutex_t>& cache_lock, bool evict_dirty);

	void check_deleted(int64 transid, const std::string& key, bool comp);

	bool compress_one(std::unique_lock<cache_mutex_t>& cache_lock,
		std::list<std::pair<std::string const *, SCacheVal> >::iterator& compress_it);

	std::list<SSubmissionItem>::iterator next_submission_item(bool no_compress, bool prefer_non_delete, bool prefer_mem, std::string& path, bool& p_do_stop, SMemFile*& memf);

	std::list<SSubmissionItem>::iterator no_submission_item();

	bool item_submitted(std::list<SSubmissionItem>::iterator it, bool can_delete, bool is_memf);

	bool item_compressed(std::list<SSubmissionItem>::iterator it, bool compression_error, int64 size_diff, int64 add_comp_bytes, bool is_memf);

	enum class DeleteImm
	{
		None,
		NoUnlock,
		Unlock
	};

	void delete_item(fuse_io_context* io, const std::string& key, bool compressed_item,
		std::unique_lock<cache_mutex_t>& cache_lock,
		int64 force_delete_transid=0, int64 skip_transid=0, DeleteImm del_imm = DeleteImm::None,
		int64 delete_only=0, bool rm_submitted=false, int64 ignore_sync_wait_transid=0);

	void run_del_file_queue();

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<void> run_del_file_queue_async(fuse_io_context& io);
#endif

	void wait_for_del_file(const std::string& fn);
	
	bool read_dirty_items(std::unique_lock<cache_mutex_t>& cache_lock, int64 transaction_id, int64 attibute_to_trans_id);

	bool read_submitted_evicted_files(const std::string& fn, std::set<std::string>& sub_evict);
	bool read_from_dirty_file(std::unique_lock<cache_mutex_t>& cache_lock, const std::string& fn, int64 transaction_id, bool do_submit, int64 nosubmit_transid);
	bool write_to_dirty_file(std::unique_lock<cache_mutex_t>& cache_lock, const std::string& fn, bool do_submit, int64 new_trans,
		size_t& num_memf_dirty, std::map<std::string, size_t>& memf_dirty_items,
		std::map<std::string, int64>& memf_dirty_items_size);

	bool read_from_deleted_file(const std::string& fn, int64 transaction_id, bool do_submit);
	bool write_to_deleted_file(const std::string& fn, bool do_submit);

	void submit_dummy(int64 transaction_id);

	void clean_snapshot( int64 new_trans );

	void wait_for_all_retrievals(std::unique_lock<cache_mutex_t>& lock);
	bool wait_for_retrieval(std::unique_lock<cache_mutex_t>& lock, const std::string& key);
	bool wait_for_retrieval_poll(std::unique_lock<cache_mutex_t>& lock, const std::string& key);

	struct RetrievalRes
	{
		RetrievalRes()
			: waited(false)
		{}

		std::unique_lock<cache_mutex_t> lock;
		bool waited;
	};

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<RetrievalRes> wait_for_retrieval_async(fuse_io_context& io, std::unique_lock<cache_mutex_t> lock, const std::string key);
#endif
	void initiate_retrieval(const std::string& key);
	void finish_retrieval(const std::string& key);
#ifdef HAS_ASYNC
	void finish_retrieval_async(fuse_io_context& io, std::unique_lock<cache_mutex_t> lock, const std::string& key);
#endif

	bool compress_item(const std::string& key, int64 transaction_id, IFsFile* src, int64& size_diff, int64& dst_size, bool sync);
	bool decompress_item(const std::string& key, int64 transaction_id, IFsFile* tmpl_file, int64& size_diff, int64& src_size, bool sync);

	void remove_compression_evicition_submissions(std::unique_lock<cache_mutex_t>& cache_lock);

	void remove_curr_trans_submission(std::unique_lock<cache_mutex_t>& cache_lock, std::list<SSubmissionItem>::iterator it);

	void wait_for_compressions_evictions(std::unique_lock<cache_mutex_t>& lock);

	void addDirtyItem(int64 transid, std::string key, bool with_stats=true);
	void removeDirtyItem(int64 transid, std::string key);

	void add_dirty_bytes(int64 transid, std::string key, int64 b);
	void rm_dirty_bytes(int64 transid, std::string key, int64 b, bool rm, bool change_size=true);

	void add_cachesize(int64 toadd);
		
	void sub_cachesize(int64 tosub);

	bool is_sync_wait_item(const std::string& key);

	bool is_sync_wait_item(const std::string& key, int64 transid);

	void check_submission_items();

	void update_transactions();

	void drop_cache(IFsFile* fd);
#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<void> drop_cache_async(fuse_io_context& io, IFsFile* fd);
#endif


	void set_read_random(IFsFile* fd);
#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<void> set_read_random_async(fuse_io_context& io, IFsFile* fd);
#endif
	

	bool add_submitted_item(int64 transid, std::string tkey, std::unique_ptr<IFile>* fd_cache);

	bool add_evicted_item(int64 transid, std::string tkey);

	bool add_item(std::string fn, int64 transid, std::string tkey, std::unique_ptr<IFile>* fd_cache);

	IFsFile* get_mem_file(const std::string& key, int64 size_hint, bool for_read);

	bool has_memfile_stat(const std::string& key);

	void add_memfile_stat(const std::string& key);

	bool rm_mem_file(fuse_io_context* io, int64 transid, const std::string& key, bool rm_submitted);

	void rm_submission_item(std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator >::iterator it);

	bool cow_mem_file(SMemFile* memf, bool with_old_file);
#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<bool> cow_mem_file_async(fuse_io_context& io, SMemFile* memf, bool with_old_file);
#endif

	void remove_missing(const std::string& key);

	int64 get_compsize(const std::string& key, int64 transid);

	void only_memfiles_throttle(const std::string& key, std::unique_lock<cache_mutex_t>& lock);
#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<void> only_memfiles_throttle_async(fuse_io_context& io, const std::string key);
#endif

	std::string readCacheFile(const std::string& fn);
	bool cacheFileExists(const std::string& fn);
	bool writeToCacheFile(const std::string& str, const std::string& fn);

	TransactionalKvStore::SCacheVal cache_val(const std::string& key, bool dirty);
	TransactionalKvStore::SCacheVal cache_val_nc(bool dirty);

	bool set_cache_file_compression(const std::string& key, const std::string& fpath);

	std::list<SSubmissionItem>::iterator submission_queue_add(SSubmissionItem& item, bool memfile);
	std::list<SSubmissionItem>::iterator submission_queue_insert(SSubmissionItem& item, bool memfile, std::list<SSubmissionItem>::iterator it);

	void submission_queue_rm(std::list<SSubmissionItem>::iterator it);

	int64 cache_free_space();

	class RegularSubmitBundleThread : public IThread
	{
		TransactionalKvStore* kv_store;
		bool do_quit;
		std::condition_variable_any cond;
		void regular_submit_bundle(std::unique_lock<cache_mutex_t>& cache_lock);
	public:
		RegularSubmitBundleThread(TransactionalKvStore* kv_store)
			:kv_store(kv_store), do_quit(false) {}
		void operator()();
		void quit();
	};

	class MetadataUpdateThread : public IThread
	{
	public:
		TransactionalKvStore* kv_store;
		bool do_quit;
		std::condition_variable_any cond;
		MetadataUpdateThread(TransactionalKvStore* kv_store)
		:kv_store(kv_store), do_quit(false) {}
		void operator()();
		void quit();
	};

	class ThrottleThread : public IThread
	{
	public:
		TransactionalKvStore* kv_store;
		bool do_quit;
		std::condition_variable_any cond;
		ThrottleThread(TransactionalKvStore* kv_store)
			:kv_store(kv_store), do_quit(false) {}
		void operator()();
		void quit();
	};

	class MemfdDelThread : public IThread
	{
		std::mutex mutex;
		std::condition_variable cond;
		std::vector<std::shared_ptr<IFsFile> > del_fds;
		bool do_quit = false;
	public:
		MemfdDelThread() {
		}
		
		void quit() {
			std::scoped_lock lock(mutex);
			do_quit = true;
			cond.notify_all();
		}

		void del(std::shared_ptr<IFsFile>&& fd)
		{
			std::unique_lock lock(mutex);

			while (del_fds.size() > 1000)
			{
				lock.unlock();
				Server->wait(100);
				lock.lock();
			}

			del_fds.push_back(fd);
			cond.notify_all();
		}

		void operator()();
	};

#ifdef HAS_ASYNC
	struct AwaiterCoList
	{
		AwaiterCoList* next;
		std::coroutine_handle<> awaiter;
	};

	AwaiterCoList* retrieval_waiters_head = nullptr;

	struct RetrievalAwaiter
	{
		RetrievalAwaiter(TransactionalKvStore& kv_store)
			: kv_store(kv_store) {}
		RetrievalAwaiter(RetrievalAwaiter const&) = delete;
		RetrievalAwaiter(RetrievalAwaiter&& other) = delete;
		RetrievalAwaiter& operator=(RetrievalAwaiter&&) = delete;
		RetrievalAwaiter& operator=(RetrievalAwaiter const&) = delete;

		bool await_ready() const noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> p_awaiter) noexcept
		{
			kv_store.check_mutex_not_held();
			awaiter.awaiter = p_awaiter;
			awaiter.next = kv_store.retrieval_waiters_head;
			kv_store.retrieval_waiters_head = &awaiter;
		}

		void await_resume() const noexcept
		{
		}

	private:
		AwaiterCoList awaiter;
		TransactionalKvStore& kv_store;
	};

	void resume_retrieval_awaiters()
	{
		AwaiterCoList* curr = retrieval_waiters_head;
		retrieval_waiters_head = nullptr;

		while (curr != nullptr)
		{
			AwaiterCoList* next = curr->next;
			curr->awaiter.resume();
			curr = next;
		}
	}
#endif

	common::lrucache<std::string, SCacheVal> lru_cache;
	std::map<std::string, SFdKey> open_files;
	std::map<IFsFile*, ReadOnlyFileWrapper*> read_only_open_files;
	std::map<std::string, int> preload_once_items;
	std::map<std::string, int64> preload_once_delayed_removal;
	std::list<SSubmissionItem> submission_queue;
	std::list<SSubmissionItem>::iterator submission_queue_memfile_first;
	std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator > submission_items;
	std::map<int64, size_t> num_dirty_items;
#ifdef DIRTY_ITEM_CHECK
	std::map<int64, std::map<std::string, size_t> > dirty_items;
	std::map<int64, std::map<std::string, int64> > dirty_items_size;
#endif
	std::map<int64, size_t> num_delete_items;
	common::lrucache<std::string, SFdKey> fd_cache;
	cache_mutex_t cache_mutex;
	std::recursive_mutex submission_mutex;
	std::recursive_mutex dirty_item_mutex;
	std::condition_variable_any evict_cond;
	std::set<std::string> queued_dels;
	std::map<std::string, size_t> in_retrieval;
	std::condition_variable_any retrieval_cond;
	common::lrucache<std::string, SCacheVal> compressed_items;
	std::set<std::string> dirty_evicted_items;
	std::map<int64, std::set<std::string> > nosubmit_dirty_items;
	std::set<std::string> nosubmit_untouched_items;
	std::set<std::string> del_file_queue;
	std::string prio_del_file;
	std::unique_ptr<ICondition> prio_del_file_cond;
	std::unique_ptr<IMutex> del_file_mutex;
	std::unique_ptr<IMutex> del_file_single_mutex;
	std::vector<SFile> transactions;
	std::unique_ptr<IMutex> evicted_mutex;
	std::recursive_mutex memfiles_mutex;
	common::lrucache<std::pair<int64, std::string>, SMemFile> memfiles;
	std::vector<std::pair<int64, std::unique_ptr<Bitmap> > > memfile_stat_bitmaps;
	std::map<int64, size_t> num_mem_files;
	relaxed_atomic<int64> memfile_size;
	relaxed_atomic<int64> submitted_memfile_size;

	int64 submit_bundle_starttime;
	std::unique_ptr<IMutex> submit_bundle_item_mutex;
	std::vector<std::pair<SSubmissionItem, bool> > submit_bundle;
	std::set<std::pair<std::string, int64> > submit_bundle_items_a;
	std::set<std::pair<std::string, int64> > submit_bundle_items_b;
	std::set<std::pair<std::string, int64> >* curr_submit_bundle_items;
	std::set<std::pair<std::string, int64> >* other_submit_bundle_items;

	std::set<std::string> missing_items;

	std::vector<THREADPOOL_TICKET> threads;

	IBackupFileSystem* cachefs;

	relaxed_atomic<int64> cachesize;

	relaxed_atomic<int64> dirty_bytes;

	relaxed_atomic<int64> comp_bytes;

	relaxed_atomic<int64> submitted_bytes;

	relaxed_atomic<int64> total_submitted_bytes;

	int64 total_hits;

	int64 total_memory_hits;

	relaxed_atomic<int64> total_dirty_ops;

	relaxed_atomic<int64> total_cache_miss_backend;

	relaxed_atomic<int64> total_cache_miss_decompress;

	relaxed_atomic<int64> total_put_ops;

	relaxed_atomic<int64> total_compress_ops;

	relaxed_atomic<int64> max_cachesize;

	int64 transid;
	int64 basetrans;

	int64 min_cachesize;
	int64 min_free_size;
	int64 throttle_free_size;
	int64 critical_free_size;
	float comp_percent;
	int64 comp_start_limit;
	bool curr_submit_compress_evict;
	float resubmit_compressed_ratio;
	int64 max_memfile_size;
	size_t submitted_memfiles;

	size_t remaining_gets;
	bool has_new_remaining_gets = false;
	size_t new_remaining_gets = 0;
	size_t unthrottled_gets;
	double unthrottled_gets_avg;
	bool do_evict;
	int64 do_evict_starttime;
	bool do_stop;
	size_t evict_queue_depth;
	size_t compress_queue_depth;
	relaxed_atomic<int64> metadata_cache_free;
	int64 min_metadata_cache_free;

	IOnlineKvStore* online_kv_store;
	std::string encryption_key;
	ICompressEncryptFactory* compress_encrypt_factory;
	RegularSubmitBundleThread regular_submit_bundle_thread;
	ThrottleThread throttle_thread;
	MetadataUpdateThread metadata_update_thread;
	MemfdDelThread memfd_del_thread;

	bool with_sync_wait;
	bool with_prev_link;
	bool allow_evict;
	bool with_submitted_files;

	size_t fd_cache_size;

	bool evict_non_dirty_memfiles;

	int64 compression_starttime;

	INumSecondChancesCallback* num_second_chances_cb;

	bool only_memfiles;
	bool disable_read_memfiles;
	bool disable_write_memfiles;

	unsigned int background_comp_method;

	size_t retrieval_waiters_async;
	size_t retrieval_waiters_sync;

	std::string memcache_path;

	std::unique_ptr<IFsFile> cache_lock_file;

	unsigned int cache_comp;
	unsigned int meta_cache_comp;
};
