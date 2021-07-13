#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "IOnlineKvStore.h"
#include "../Interface/Mutex.h"
#include "../Interface/SharedMutex.h"
#include "../Interface/ThreadPool.h"
#include "ICompressEncrypt.h"
#include "../common/data.h"
#include "TransactionalKvStore.h"
#ifdef HAS_MOUNT_SERVICE
#include "btrfs_chunks.h"
#endif
#include "KvStoreFrontend.h"
#ifdef HAS_ASYNC
#include "fuse_io_context.h"
#endif
#include <atomic>
#include <queue>

class IBackupFileSystem;

namespace
{
	class FileBitmap;
	class SparseFileBitmap;
	class UpdateMissingChunksThread;
	class ShareWithUpdater;
}

void task_set_less_throttle();
void task_unset_less_throttle();
bool flush_dev(std::string dev_fn);
void setMountStatus(const std::string& data);
void setMountStatusErr(const std::string& err);

class CloudFile : public IFile, public IThread, public TransactionalKvStore::INumSecondChancesCallback, IOnlineKvStore::IHasKeyCallback
{
public:
	CloudFile(const std::string& cache_path,
		IBackupFileSystem* cachefs,
		int64 cloudfile_size,
		int64 max_cloudfile_size,
		IOnlineKvStore* online_kv_store,
		const std::string& encryption_key,
		ICompressEncryptFactory* compress_encrypt_factory,
		bool verify_cache, float cpu_multiplier,
		bool background_compress,
		size_t no_compress_mult,
		int64 reserved_cache_device_space,
		int64 min_metadata_cache_free,
		bool with_prev_link,
		bool allow_evict,
		bool with_submitted_files,
		float resubmit_compressed_ratio,
		int64 max_memfile_size,
		std::string memcache_path,
		float memory_usage_factor,
		bool only_memfiles,
		std::string mount_path,
		std::string share_with_mount_paths,
		unsigned int background_comp_method,
		const std::string& slog_path,
		int64 slog_max_size,
		bool is_async,
		unsigned int cache_comp, unsigned int meta_cache_comp);

	~CloudFile();

	CloudFile(CloudFile const&) = delete;
	CloudFile(CloudFile&& other) = delete;
	CloudFile& operator=(CloudFile&&) = delete;
	CloudFile& operator=(CloudFile const&) = delete;

	virtual std::string Read( _u32 tr, bool* has_error=NULL);

	virtual std::string Read( int64 pos, _u32 tr, bool* has_error = NULL);

	_u32 Read(int64 pos, char* buffer, _u32 bsize, bool* has_error = NULL);

	virtual _u32 Read( char* buffer, _u32 bsize, bool* has_error = NULL);

	virtual _u32 Write( const std::string &tw, bool* has_error = NULL);

	virtual _u32 Write(int64 pos, const std::string &tw, bool* has_error = NULL);

	virtual _u32 Write( const char* buffer, _u32 bsize, bool* has_error = NULL);

	virtual _u32 Write( int64 pos, const char* buffer, _u32 bsize, bool* has_error = NULL);
	
	_u32 WriteNonBlocking( int64 pos, const char* buffer, _u32 bsize, bool* has_error = NULL);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> ReadAsync(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io, 
		int64 pos, _u32 bsize, unsigned int flags, bool ext_lock);

	fuse_io_context::io_uring_task<int> WriteAsync(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io,
		int64 pos, _u32 bsize, bool new_block, bool ext_lock, bool can_block);
#endif

	virtual bool Seek( _i64 spos );

	virtual _i64 Size( void );

#ifdef HAS_ASYNC
	virtual fuse_io_context::io_uring_task<_i64> SizeAsync(void);
#endif

	virtual _i64 RealSize(void);
	
	virtual bool Resize(int64 nsize);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<bool> ResizeAsync(fuse_io_context& io, int64 nsize);
#endif

	virtual bool PunchHole( _i64 spos, _i64 size );

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> PunchHoleAsync(fuse_io_context& io, _i64 spos, _i64 size);
#endif

	virtual std::string getFilename( void );

	virtual bool Sync();

	bool Flush();
	
	bool Flush(bool do_submit, bool for_slog=false);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<bool> FlushAsync(fuse_io_context& io, bool do_submit);
#endif

	bool close_bitmaps();

	void Reset();

	int64 getDirtyBytes();

	int64 getSubmittedBytes();

	int64 getTotalSubmittedBytes();

	int64 getUsedBytes();

	std::string getNumDirtyItems();

	std::string getNumMemfileItems();

	int64 getCacheSize();

	std::string getStats();

	int64 getCompBytes();

	void setDevNames(std::string bdev, std::string ldev);
	
	static std::string block_key(int64 blocknum);

	static int64 block_key_rev(const std::string& data);
	
	static std::string big_block_key(int64 blocknum);
	
	static std::string small_block_key(int64 blocknum);
	
	static std::string hex_big_block_key(int64 bytenum);
	
	static std::string hex_small_block_key(int64 bytenum);

	int Congested();

	int CongestedAsync();

	std::string get_raid_groups();

	std::string scrub_stats();

	void start_scrub(ScrubAction action);

	void stop_scrub();

	bool add_disk(const std::string& path);

	bool remove_disk(const std::string& path, bool completely);

	std::string disk_error_info();

	int64 current_total_size();

	int64 current_free_space();

	bool set_target_failure_probability(double t);

	bool set_target_overhead(double t);

	std::string get_scrub_position();

	int64 get_memfile_bytes();

	int64 get_submitted_memfile_bytes();

	virtual void operator()();

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task_discard<int> run_async(fuse_io_context& io);
#endif

	void run_cd_fracture();

	bool start_raid_defrag(const std::string& settings);

	bool is_background_worker_enabled();

	bool is_background_worker_running();

	void enable_background_worker(bool b);

	bool start_background_worker();

	void set_background_worker_result_fn(const std::string& result_fn);

	bool has_background_task();

	void preload(int64 start, int64 stop, size_t n_threads);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> preload_async(fuse_io_context& io, int64 start, int64 stop, size_t n_threads);
#endif

	void cmd(const std::string& c);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> cmd_async(fuse_io_context& io, const std::string& c);
#endif

	static int64 key_offset_hex(const std::string& key);

	static int64 key_offset(const std::string& key, bool& big_block);

	static int64 key_offset(const std::string& key);

	std::string meminfo();

	void shrink_mem();

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> shrink_mem_async(fuse_io_context& io);
#endif

	int64 get_total_hits();

	int64 get_total_hits_async();

	int64 get_total_memory_hits();

	int64 get_total_memory_hits_async();

	int64 get_total_cache_miss_backend();

	int64 get_total_cache_miss_decompress();

	int64 get_total_dirty_ops();

	int64 get_total_balance_ops();

	int64 get_total_del_ops();

	int64 get_total_put_ops();

	int64 get_total_compress_ops();

	bool has_preload_item(int64 offset_start, int64 offset_end);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<bool> has_preload_item_async(fuse_io_context& io, int64 offset_start, int64 offset_end);
#endif

	int64 min_block_size();

	void set_is_mounted(const std::string& p_mount_path, IBackupFileSystem* fs);

	bool is_metadata(int64 offset, const std::string& key);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<bool> is_metadata_async(fuse_io_context& io, int64 offset, const std::string& key);
#endif

	virtual unsigned int get_num_second_chances(const std::string& key);

	virtual bool is_metadata(const std::string& key);

	bool update_missing_fs_chunks();

	bool migrate(std::vector<char>& buf, int64 offset, int64 len);

	bool migrate(const std::string& conf_fn, bool continue_migration);

	std::string migration_info();

	void preload_key(const std::string& key, int64 offset, int64 len, int tag, bool disable_memfiles, bool load_only);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> preload_key_async(fuse_io_context& io, const std::string& key, int64 offset, int64 len, int tag, bool disable_memfiles, bool load_only);
#endif

	int64 get_uploaded_bytes();

	int64 get_downloaded_bytes();

	int64 get_written_bytes();

	int64 get_read_bytes();

	std::string get_raid_io_stats();

	bool replay_slog();

	std::string get_mirror_stats();

	std::string get_raid_freespace_stats();

	virtual bool hasKey(const std::string& key) override;

	int64 get_transid();
private:

	void preload_items(const std::string& fn, size_t n_threads, int tag, bool disable_memfiles, bool load_only);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> preload_items_async(fuse_io_context& io, const std::string& fn, 
		size_t n_threads, int tag, bool disable_memfiles, bool load_only);
#endif

	_u32 WriteInt(int64 pos, const char* buffer, _u32 bsize, bool new_block, IScopedLock* ext_lock, bool can_block, bool* has_error);

	_u32 ReadInt(int64 pos, char* buffer, _u32 bsize, IScopedLock* ext_lock, unsigned int flags, bool* has_error);
	
	_u32 ReadAligned(IFile* block, int64 pos, char* buffer, _u32 toread, bool* has_read_error);
	
	_u32 WriteAligned(IFile* block, int64 pos, const char* buffer, _u32 towrite);

	void perform_deletes();

	void open_bitmaps();

	bool bitmap_has_big_block(int64 blocknum);
	bool bitmap_has_small_block(int64 blocknum);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<bool> bitmap_has_big_block_async(fuse_io_context& io, int64 blocknum);
	fuse_io_context::io_uring_task<bool> bitmap_has_small_block_async(fuse_io_context& io, int64 blocknum);
#endif
	
	void lock_extent(IScopedLock& lock, int64 start, int64 length, bool exclusive);
	void unlock_extent(IScopedLock& lock, int64 start, int64 length, bool exclusive);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<size_t> lock_extent_async(int64 start, int64 length, bool exclusive);
#endif
	void unlock_extent_async(int64 start, int64 length, bool exclusive, size_t lock_idx);

	void add_fracture_big_block(int64 b);

	void update_fs_chunks(bool update_time, IScopedWriteLock& lock);

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<void> update_fs_chunks_async(fuse_io_context& io, bool update_time);
#endif

	bool migrate(const std::string& conf_fn, ISettingsReader* settings);

	bool update_migration_settings();

	IOnlineKvStore* create_migration_endpoint(const std::string& conf_fn, ISettingsReader* settings,
		IBackupFileSystem* migrate_cachefs);

	bool slog_write(int64 pos, const char* buffer, _u32 bsize, bool& needs_reset);
	
	bool slog_open();

	void purge_jemalloc();

	void set_jemalloc_dirty_decay(int64 timems);

	/*_u32 prepare_zero_n_sqes(_u32 towrite);

	std::vector<io_uring_sqe*> prepare_zeros(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io, _u32 towrite, _u32 peek_add);*/

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task<int> empty_pipe(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io);

	fuse_io_context::io_uring_task<bool> verify_pipe_empty(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io);

	struct AwaiterCoList
	{
		AwaiterCoList* next;
		std::coroutine_handle<> awaiter;
	};

	fuse_io_context::io_uring_task_discard<int> preload_async_read(fuse_io_context& io, std::vector<std::unique_ptr<fuse_io_context::FuseIo> >& ios, AwaiterCoList*& ios_waiters_head, int64 start, _u32 size);

	fuse_io_context::io_uring_task_discard<int> preload_items_single_async(fuse_io_context& io, size_t& available_workers, AwaiterCoList*& worker_waiters_head,
		std::string key, int64 offset, _u32 len, int tag, bool disable_memfiles, bool load_only);

	struct ReadTask
	{
		ReadTask(int fd, IFsFile* file, std::string key, uint64_t off, _u32 size)
			: fd(fd), file(file), key(std::move(key)), off(off), size(size), flush_mem(false) {}
		int fd;
		IFsFile* file;
		std::string key;
		int64 off;
		_u32 size;
		bool flush_mem;
	};

	fuse_io_context::io_uring_task<int> complete_read_tasks(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io,
		const std::vector<ReadTask>& read_tasks, const size_t flush_mem_n_tasks, const int64 orig_pos, const _u32 bsize,
		const unsigned int flags);
#endif
		

	struct SExtent
	{
		int64 start;
		int64 length;
		ICondition* cond;
		int refcount;
		bool alive;
	};

	std::vector<SExtent> locked_extents;
	
#ifdef HAS_ASYNC
	struct SExtentAsync
	{
		int64 start;
		int64 length;
		bool exclusive;
		AwaiterCoList* awaiters;
		int refcount;
	};

	std::vector<SExtentAsync> async_locked_extents;
#endif
	
	
	size_t locked_extents_max_alive;
	size_t wait_for_exclusive;

	std::unique_ptr<IOnlineKvStore> online_kv_store;
	TransactionalKvStore kv_store;

	IFsFile* big_blocks_bitmap_file;
	IFsFile* bitmap_file;
	IFsFile* new_big_blocks_bitmap_file;
	IFsFile* old_big_blocks_bitmap_file;

	std::unique_ptr<FileBitmap> old_big_blocks_bitmap;
	std::unique_ptr<FileBitmap> new_big_blocks_bitmap;

	std::unique_ptr<SparseFileBitmap> big_blocks_bitmap;
	std::unique_ptr<SparseFileBitmap> bitmap;

	int64 cf_pos;
	int64 active_big_block;

	int64 used_bytes;

	int64 last_stat_update_time;
	int64 last_flush_check;

	int64 cloudfile_size;

	std::unique_ptr<IMutex> mutex;

	std::string bdev_name;
	std::string ldev_name;
	size_t writeback_count;
	std::string bcache_writeback_percent;
	
	unsigned int io_alignment;

	std::map<int64, int64> fracture_big_blogs;
	std::unique_ptr<ICondition> thread_cond;
	bool exit_thread;
	THREADPOOL_TICKET fracture_big_blogs_ticket;

	float memory_usage_factor;

	int64 bitmaps_file_size;

	size_t flush_enabled;

	std::string cache_path;

	std::string mount_path;

	std::unique_ptr<ISharedMutex> chunks_mutex;
	std::vector<IBackupFileSystem::SChunk> fs_chunks;
	int64 last_fs_chunks_update;
	std::vector<std::pair<std::string, int64> > missing_chunk_keys;
	bool updating_fs_chunks;
	std::unique_ptr<UpdateMissingChunksThread> update_missing_chunks_thread;
	THREADPOOL_TICKET update_missing_chunks_thread_ticket;

	std::unique_ptr<CloudFile> migrate_to_cf;
	std::unique_ptr<IThreadPool> migration_thread_pool;
	std::atomic<bool> migration_has_error;
	std::atomic<int64> migration_copy_max;
	std::atomic<int64> migration_copy_done;
	int64 migration_copy_done_lag;
	std::string migration_conf_fn;
	THREADPOOL_TICKET migration_ticket;

	std::set<std::string> in_write_retrieval;
	std::unique_ptr<ICondition> in_write_retrieval_cond;

	THREADPOOL_TICKET share_with_updater_ticket;
	std::unique_ptr<ShareWithUpdater> share_with_updater;

	int64 written_bytes;
	int64 read_bytes;

	std::atomic<int64> slog_size;
	std::atomic<int64> slog_last_sync;
	std::unique_ptr<IFsFile> slog;
	std::string slog_path;
	int64 slog_max_size;

	bool enable_raid_freespace_stats;

	bool is_flushing;

	int zero_memfd;
	_u32 zero_memfd_size;

#ifdef HAS_ASYNC
	AwaiterCoList* exclusive_awaiter_head = nullptr;

	struct ExclusiveAwaiter
	{
		ExclusiveAwaiter(CloudFile& cd)
			: cd(cd) {}
		ExclusiveAwaiter(ExclusiveAwaiter const&) = delete;
	    ExclusiveAwaiter(ExclusiveAwaiter&& other) = delete;
	    ExclusiveAwaiter& operator=(ExclusiveAwaiter&&) = delete;
		ExclusiveAwaiter& operator=(ExclusiveAwaiter const&) = delete;

		bool await_ready() const noexcept
        {
            return false;
        }

		void await_suspend(std::coroutine_handle<> p_awaiter) noexcept
        {
			awaiter.awaiter = p_awaiter;
			awaiter.next = cd.exclusive_awaiter_head;
			cd.exclusive_awaiter_head = &awaiter;
		}

		void await_resume() const noexcept
        {
        }

	private:
		AwaiterCoList awaiter;
		CloudFile& cd;
	};

	void resume_awaiters(AwaiterCoList*& head)
	{
		AwaiterCoList* curr = head;
		head = nullptr;

		while (curr != nullptr)
		{
			AwaiterCoList* next = curr->next;
			curr->awaiter.resume();
			curr = next;
		}
	}

	void resume_exclusive_awaiters()
	{
		resume_awaiters(exclusive_awaiter_head);
	}

	AwaiterCoList* write_retrieval_head = nullptr;

	struct WriteRetrievalAwaiter
	{
		WriteRetrievalAwaiter(CloudFile& cd)
			: cd(cd) {}
		WriteRetrievalAwaiter(WriteRetrievalAwaiter const&) = delete;
		WriteRetrievalAwaiter(WriteRetrievalAwaiter&& other) = delete;
		WriteRetrievalAwaiter& operator=(WriteRetrievalAwaiter&&) = delete;
		WriteRetrievalAwaiter& operator=(WriteRetrievalAwaiter const&) = delete;

		bool await_ready() const noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> p_awaiter) noexcept
		{
			awaiter.awaiter = p_awaiter;
			awaiter.next = cd.write_retrieval_head;
			cd.write_retrieval_head = &awaiter;
		}

		void await_resume() const noexcept
		{
		}

	private:
		AwaiterCoList awaiter;
		CloudFile& cd;
	};

	void resume_write_retrieval_awaiters()
	{
		resume_awaiters(write_retrieval_head);
	}

	struct ExtentAwaiter
	{
		ExtentAwaiter(SExtentAsync& extent)
			: extent(extent) {}
		ExtentAwaiter(ExtentAwaiter const&) = delete;
	    ExtentAwaiter(ExtentAwaiter&& other) = delete;
	    ExtentAwaiter& operator=(ExtentAwaiter&&) = delete;
		ExtentAwaiter& operator=(ExtentAwaiter const&) = delete;

		bool await_ready() const noexcept
        {
            return false;
        }

		void await_suspend(std::coroutine_handle<> p_awaiter) noexcept
        {
			awaiter.awaiter = p_awaiter;
			awaiter.next = extent.awaiters;
			extent.awaiters = &awaiter;
		}

		void await_resume() const noexcept
        {
        }

	private:
		AwaiterCoList awaiter;
		SExtentAsync& extent;
	};

	struct IosAwaiter
	{
		IosAwaiter(std::vector<std::unique_ptr<fuse_io_context::FuseIo> >& ios, AwaiterCoList*& ios_awaiter_head)
			: ios(ios), ios_awaiter_head(ios_awaiter_head) {}
		IosAwaiter(IosAwaiter const&) = delete;
		IosAwaiter(IosAwaiter&& other) = delete;
		IosAwaiter& operator=(IosAwaiter&&) = delete;
		IosAwaiter& operator=(IosAwaiter const&) = delete;

		bool await_ready() const noexcept
		{
			return !ios.empty();
		}

		void await_suspend(std::coroutine_handle<> p_awaiter) noexcept
		{
			awaiter.awaiter = p_awaiter;
			awaiter.next = ios_awaiter_head;
			ios_awaiter_head = &awaiter;
		}

		std::unique_ptr<fuse_io_context::FuseIo> await_resume() noexcept
		{
			std::unique_ptr<fuse_io_context::FuseIo> ret = std::move(ios.back());
			ios.pop_back();
			return std::move(ret);
		}

	private:
		AwaiterCoList awaiter;
		std::vector<std::unique_ptr<fuse_io_context::FuseIo> >& ios;
		AwaiterCoList*& ios_awaiter_head;
	};

	void iosWaitersResume(std::vector<std::unique_ptr<fuse_io_context::FuseIo> >& ios,
		AwaiterCoList*& ios_waiters_head)
	{
		AwaiterCoList* curr = ios_waiters_head;
		ios_waiters_head = nullptr;

		while (curr != nullptr
			&& !ios.empty())
		{
			AwaiterCoList* next = curr->next;
			curr->awaiter.resume();
			curr = next;
		}

		if (curr != nullptr)
		{
			curr->next = ios_waiters_head;
			ios_waiters_head = curr;
		}
	}

	struct WorkerAwaiter
	{
		WorkerAwaiter(size_t& available_workers, AwaiterCoList*& worker_awaiter_head)
			: available_workers(available_workers), worker_awaiter_head(worker_awaiter_head) {}
		WorkerAwaiter(WorkerAwaiter const&) = delete;
		WorkerAwaiter(WorkerAwaiter&& other) = delete;
		WorkerAwaiter& operator=(WorkerAwaiter&&) = delete;
		WorkerAwaiter& operator=(WorkerAwaiter const&) = delete;

		bool await_ready() const noexcept
		{
			return available_workers > 0;
		}

		void await_suspend(std::coroutine_handle<> p_awaiter) noexcept
		{
			awaiter.awaiter = p_awaiter;
			awaiter.next = worker_awaiter_head;
			worker_awaiter_head = &awaiter;
		}

		void await_resume() noexcept
		{
			assert(available_workers > 0);
			--available_workers;
		}

	private:
		AwaiterCoList awaiter;
		size_t& available_workers;
		AwaiterCoList*& worker_awaiter_head;
	};

	void workerWaitersResume(size_t& available_workers,
		AwaiterCoList*& ios_waiters_head)
	{
		AwaiterCoList* curr = ios_waiters_head;
		ios_waiters_head = nullptr;

		while (curr != nullptr
			&& available_workers>0)
		{
			AwaiterCoList* next = curr->next;
			curr->awaiter.resume();
			curr = next;
		}

		if (curr != nullptr)
		{
			curr->next = ios_waiters_head;
			ios_waiters_head = curr;
		}
	}
#endif //HAS_ASYNC

	std::unique_ptr<IFsFile> null_file;
	std::unique_ptr<IFsFile> zero_file;

	bool is_async;

#ifdef HAS_ASYNC
	fuse_io_context::io_uring_task_discard<int> run_async_queue(fuse_io_context& io);

	bool get_async_msg(CWData& msg, CRData& resp);
#endif

	int queue_in_eventfd;

	int update_fs_chunks_eventfd;

	std::unique_ptr<IMutex> msg_queue_mutex;
	std::unique_ptr<ICondition> msg_queue_cond;
	int64 msg_queue_id;
	std::queue<std::vector<char> > msg_queue;
	std::map<size_t, std::vector<char> > msg_queue_responses;

	std::set<int64> consider_big_blocks;
	std::set<int64> curr_consider_big_blocks;

	bool fallocate_block_file = true;

	IBackupFileSystem* cachefs;

	IBackupFileSystem* topfs = nullptr;
};
