#pragma once
#include "IOnlineKvStore.h"
#include "IKvStoreBackend.h"
#include "IKvStoreFrontend.h"
#include "KvStoreDao.h"
#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/SharedMutex.h"
#include "../stringtools.h"
#include <memory>
#include <set>
#include <queue>
#include "../common/relaxed_atomic.h"

enum class ScrubAction
{
	Balance,
	Rebuild,
	Scrub
};

namespace
{
	std::string strScrubAction(ScrubAction a) {
		switch (a) {
		case ScrubAction::Balance: return "balance";
		case ScrubAction::Rebuild: return "rebuild";
		case ScrubAction::Scrub: return "scrub";
		default: return "undef";
		}
	}

	std::string strScrubActionC(ScrubAction a)
	{
		std::string ret = strScrubAction(a);
		if (!ret.empty())
			ret[0] = static_cast<char>(toupper(ret[0]));
		return ret;
	}
}

class IBackupFileSystem;

class KvStoreFrontend : public IOnlineKvStore, public IThread, public IKvStoreFrontend
{
public:
	KvStoreFrontend(const std::string& db_path, IKvStoreBackend* backend, bool import, 
		const std::string& scrub_continue, const std::string& scrub_continue_position, IKvStoreBackend* backend_mirror, std::string mirror_window,
		bool background_worker_manual_run, bool background_worker_multi_trans_delete, IBackupFileSystem* cachefs);
	~KvStoreFrontend();

	virtual IFsFile* get(const std::string& key, int64 transid,
		bool prioritize_read, IFsFile* tmpl_file, bool allow_error_event, bool& not_found,
		int64* get_transid = nullptr) {
		return get(0, key, transid, prioritize_read, tmpl_file, allow_error_event, not_found, get_transid);
	}
	
	IFsFile* get(int64 cd_id, const std::string& key, int64 transid,
		bool prioritize_read, IFsFile* tmpl_file, bool allow_error_event, bool& not_found,
		int64* get_transid=nullptr);

	virtual int64 get_transid(const std::string& key, int64 transid) {
		return get_transid(0, key, transid);
	}

	int64 get_transid(int64 cd_id, const std::string& key, int64 transid);

	virtual bool reset(const std::string& key, int64 transid);

	virtual bool put(const std::string& key, int64 transid, IFsFile* src,
		unsigned int flags,
		bool allow_error_event, int64& compressed_size)
	{
		return put(0, key, transid, 0, src, flags,
			allow_error_event, compressed_size);
	}

	bool put(int64 cd_id, const std::string& key, int64 transid, int64 generation, IFsFile* src,
		unsigned int flags,
		bool allow_error_event, int64& compressed_size);

	virtual int64 new_transaction(bool allow_error_event)
	{
		return new_transaction(0, allow_error_event);
	}

	int64 new_transaction(int64 cd_id, bool allow_error_event);

	virtual bool transaction_finalize(int64 transid, bool complete, bool allow_error_event)
	{
		return transaction_finalize(0, transid, complete, allow_error_event);
	}

	bool transaction_finalize(int64 cd_id, int64 transid, bool complete, bool allow_error_event);

	virtual bool set_active_transactions(const std::vector<int64>& active_transactions)
	{
		return set_active_transactions(0, active_transactions);
	}

	bool set_active_transactions(int64 cd_id, const std::vector<int64>& active_transactions);

	virtual bool del(const std::vector<std::string>& keys, int64 transid)
	{
		return del(0, keys, transid);
	}

	virtual bool want_put_metadata()
	{
		return backend->want_put_metadata();
	}

	bool del(int64 cd_id, const std::vector<std::string>& keys, int64 transid);

	virtual size_t max_del_size();

	virtual int64 generation_inc( int64 inc );

	int64 get_generation(int64 cd_id);

	virtual std::string get_stats();

	virtual bool sync();

	virtual bool sync_db();

	virtual bool sync_lock(IScopedWriteLock& lock);

	virtual bool is_put_sync();

	std::string prefixKey(const std::string& key) override;

	static std::string encodeKey(const std::string& key, int64 transid);

	std::string encodeKey(int64 cd_id, const std::string& key, int64 transid) override;

	static std::string encodeKeyStatic(int64 cd_id, const std::string& key, int64 transid);

	IKvStoreBackend* getBackend();

	void start_scrub(ScrubAction action, const std::string& position);

	std::string scrub_position();

	std::string scrub_stats();

	void stop_scrub();

	bool is_background_worker_enabled();

	bool is_background_worker_running();

	void enable_background_worker(bool b);

	void set_background_worker_result_fn(const std::string& result_fn);

	bool start_background_worker();

	bool has_background_task();

	static void start_scrub_sync_test1();
	static void start_scrub_sync_test2();

	bool reupload(int64 transid_start, int64 transid_stop,
		IKvStoreBackend* old_backend);

	std::string meminfo();

	void retry_all_deletion();

	int64 get_total_balance_ops();

	void incr_total_del_ops() override;

	int64 get_total_del_ops();

	virtual bool has_backend_key(const std::string& key, std::string& md5sum, bool update_md5sum);

	bool log_del_mirror(const std::string& fn) override;

	std::string next_log_del_mirror_item();

	void set_backend_mirror_del_log_rpos(int64 p);
	int64 get_backend_mirror_del_log_rpos();
	void stop_defrag();

	bool set_all_mirrored(bool b);

	std::string mirror_stats();

	bool start_defrag(const std::string& settings);

	void operator()();

	virtual bool fast_write_retry();

	virtual bool submit_del_cd(int64 cd_id, IHasKeyCallback* p_has_key_callback, int64 ctransid, bool& need_flush);

	virtual bool submit_del(IHasKeyCallback* p_has_key_callback, int64 ctransid, bool& need_flush) override;

	virtual void submit_del_post_flush() override;

private:
	bool backend_del_parallel(const std::vector<IKvStoreBackend::key_next_fun_t>& key_next_funs,
		const std::vector<IKvStoreBackend::locinfo_next_fun_t>& locinfo_next_funs,
		bool background_queue);

	void add_last_modified_column();

	void add_cd_id_tasks_column();

	void add_created_column();

	void add_mirrored_column();

	int64 get_backend_mirror_del_log_wpos();

	bool update_total_num(int64 num);

	class BackgroundWorker : public IThread
	{
	public:
		BackgroundWorker(IKvStoreBackend* backend, KvStoreFrontend* frontend,
			bool manual_run, bool multi_trans_delete);

		void operator()();

		void quit();

		void set_scrub_pause(bool b)
		{
			IScopedLock lock(pause_mutex.get());
			scrub_pause = b;
		}

		void set_pause(bool b)
		{
			IScopedLock lock(pause_mutex.get());
			pause_set = true;
			pause = b;
		}

		void set_mirror_pause(bool b)
		{
			IScopedLock lock(pause_mutex.get());
			mirror_pause = b;
		}

		bool get_pause()
		{
			IScopedLock lock(pause_mutex.get());
			return pause || scrub_pause || mirror_pause;
		}

		bool is_paused()
		{
			IScopedLock lock(pause_mutex.get());
			return paused;
		}

		int64 get_nwork()
		{
			IScopedLock lock(pause_mutex.get());
			return nwork;
		}

		bool is_runnnig()
		{
			return running;
		}

		bool is_startup_finished()
		{
			IScopedLock lock(pause_mutex.get());
			return startup_finished;
		}

		bool is_manual_run()
		{
			return manual_run;
		}

		std::string meminfo();

		void set_result_fn(const std::string& fn) {
			result_fn = fn;
		}

	private:
		bool removeOldObjects(KvStoreDao& dao, const std::vector<int64>& trans_ids, int64 cd_id);

		bool removeTransaction(KvStoreDao& dao, int64 trans_id, int64 cd_id);

		volatile bool do_quit;
		bool pause_set = false;
		bool pause;
		bool scrub_pause;
		bool mirror_pause;
		bool paused;
		bool manual_run;
		bool multi_trans_delete;
		int64 nwork;
		bool startup_finished;
		std::unique_ptr<IMutex> pause_mutex;
		IKvStoreBackend* backend;
		KvStoreFrontend* frontend;
		int64 object_collector_size;
		int64 object_collector_size_uncompressed;
		relaxed_atomic<bool> running;
		std::string result_fn;
	};

	class ScrubQueue
	{
		std::unique_ptr<IMutex> mutex;
		std::unique_ptr<ICondition> cond;
	public:
		relaxed_atomic<size_t> scrub_errors;
		relaxed_atomic<size_t> scrub_oks;
		relaxed_atomic<size_t> scrub_repaired;

		ScrubQueue()
		: mutex(Server->createMutex()),
		cond(Server->createCondition()),
			do_stop(false), error(false) {

		}

		void add(KvStoreDao::CdIterObject item)
		{
			IScopedLock lock(mutex.get());
			items.push(item);
			cond->notify_all();
		}

		KvStoreDao::CdIterObject get()
		{
			IScopedLock lock(mutex.get());
			while (!do_stop
				&& items.empty())
			{
				cond->wait(&lock);
			}

			if (do_stop
				&& items.empty())
			{
				return KvStoreDao::CdIterObject();
			}

			KvStoreDao::CdIterObject ret = items.front();
			items.pop();
			return ret;
		}

		void stop()
		{
			IScopedLock lock(mutex.get());
			do_stop = true;
			cond->notify_all();
		}

		void set_error(bool b)
		{
			IScopedLock lock(mutex.get());
			error = b;
		}

		bool has_error()
		{
			IScopedLock lock(mutex.get());
			return error;
		}

		void reset()
		{
			IScopedLock lock(mutex.get());
			do_stop = false;
			error = false;
			scrub_oks = 0;
			scrub_errors = 0;
			scrub_repaired = 0;
		}
		
	private:
		bool do_stop;
		bool error;
		std::queue<KvStoreDao::CdIterObject> items;
	};


	class ScrubThread : public IThread
	{
	public:
		ScrubThread(ScrubQueue& scrub_queue,
			std::vector<KvStoreDao::CdIterObject>& new_md5sums,
			IMutex* new_md5sums_mutex,
			bool& has_changes,
			relaxed_atomic<int64>& done_size,
			bool with_last_modified,
			ScrubAction scrub_action,
			IKvStoreBackend* backend,
			KvStoreFrontend* frontend,
			IKvStoreBackend* backend_mirror)
			: scrub_queue(scrub_queue), new_md5sums(new_md5sums),
			has_changes(has_changes), done_size(done_size),
			backend(backend), scrub_action(scrub_action),
			frontend(frontend), new_md5sums_mutex(new_md5sums_mutex),
			with_last_modified(with_last_modified), backend_mirror(backend_mirror) {

		}


		void operator()();

	private:
		bool& has_changes;
		relaxed_atomic<int64>& done_size;
		bool with_last_modified;
		ScrubAction scrub_action;
		ScrubQueue& scrub_queue;
		std::vector<KvStoreDao::CdIterObject>& new_md5sums;
		IMutex* new_md5sums_mutex;
		IKvStoreBackend* backend;
		KvStoreFrontend* frontend;
		IKvStoreBackend* backend_mirror;
	};

	class ScrubWorker : public IThread
	{
	public:
		ScrubWorker(ScrubAction balance, IKvStoreBackend* backend,
			IKvStoreBackend* backend_mirror,
			KvStoreFrontend* frontend, BackgroundWorker& background_worker,
			const std::string& position, bool has_last_modified);

		void operator()();

		void quit()
		{
			IScopedLock lock(mutex.get());
			do_quit = true;
		}

		std::string stats()
		{
			return "{ \"done_size\": " + convert(done_size) + "\n,"
				"\"total_size\": " + convert(total_size) + "\n,"
				"\"paused\": " + convert(curr_paused ? 1 : 0) + "\n,"
				"\"complete_pc\": " + convert(complete_pc) + " }\n";
		}

		ScrubAction get_scrub_action()
		{
			return scrub_action;
		}

		std::string get_position()
		{
			IScopedLock lock(mutex.get());
			return position;
		}

		void add_deleted_objects(int64 cd_id, const std::vector<std::pair<int64, std::string> >& toadd)
		{
			//TODO: handle cd_id
			IScopedLock lock(mutex.get());
			for (auto& it : toadd) {
				deleted_objects.insert(it);
			}
		}

		std::string meminfo();

		THREADPOOL_TICKET ticket;
	private:

		void set_allow_defrag(bool b);

		bool do_quit;
		bool has_last_modified;
		relaxed_atomic<int> complete_pc;
		relaxed_atomic<int64> done_size;
		relaxed_atomic<int64> total_size;
		relaxed_atomic<bool> curr_paused;
		std::string position;
		std::unique_ptr<IMutex> mutex;
		ScrubAction scrub_action;
		IKvStoreBackend* backend;
		IKvStoreBackend* backend_mirror;
		KvStoreFrontend* frontend;
		BackgroundWorker& background_worker;
		std::set<std::pair<int64, std::string> > deleted_objects;
	};

	class PutDbWorker : public IThread
	{
	public:
		PutDbWorker(std::string db_path);

		int64 add(int64 cd_id, int64 transid, const std::string& key, int64 generation);
		void add(int64 cd_id, int64 transid, const std::string& key,
			const std::string& md5sum, int64 size, int64 last_modified, int64 generation);
		void flush();
		void update(int64 cd_id, int64 objectid, int64 size,
			const std::string& md5sum, int64 last_modified);
		void quit();

		void set_with_last_modified(bool b) {
			with_last_modified = b;
		}

		void set_do_synchonous(bool b) {
			do_synchronous = b;
		}

		void set_db_wal_file(IFsFile* f) {
			db_wal_file = f;
		}

		void operator()();

		std::string meminfo();

	private:
		void wait_queue(IScopedLock& lock);

		void worker_abort();

		std::unique_ptr<IMutex> mutex;
		std::unique_ptr<ICondition> add_cond;
		std::unique_ptr<ICondition> commit_cond;

		enum class EType
		{
			Add,
			Add2,
			Update,
			Flush
		};

		struct SItem
		{
			SItem(int64 cd_id, int64 transid, const std::string& key, int64 generation, int64* rowid)
				: type(EType::Add),
				cd_id(cd_id), transid(transid), key(key), rowid(rowid), generation(generation)
			{}

			SItem(int64 cd_id, int64 transid, const std::string& key,
				const std::string& md5sum, int64 size, int64 last_modified, int64 generation)
				: type(EType::Add2),
				cd_id(cd_id), transid(transid), key(key), md5sum(md5sum), size(size),
				last_modified(last_modified), generation(generation)
			{}

			SItem(int64 cd_id, int64 objectid, int64 size, const std::string& md5sum,
				int64 last_modified)
				: type(EType::Update),
				cd_id(cd_id), objectid(objectid), transid(size), key(md5sum),
				last_modified(last_modified)
			{}

			explicit SItem(int64* rowid) 
				: type(EType::Flush),
				rowid(rowid)
			{}

			EType type;
			int64 cd_id;
			int64 transid;
			std::string key;
			int64 size;
			int64 last_modified;
			std::string md5sum;
			int64* rowid;
			int64 objectid;
			int64 generation;
		};

		std::vector<SItem> items_a;
		std::vector<SItem> items_b;
		std::vector<SItem>* curr_items;
		bool do_quit;
		bool do_synchronous;
		IFsFile* db_wal_file;
		bool with_last_modified;
		std::string db_path;
	};

	class MirrorWorker : public IThread
	{
	public:
		MirrorWorker(KvStoreFrontend* frontend,
			BackgroundWorker& background_worker)
			: frontend(frontend),
			background_worker(background_worker),
			do_quit(false),
			mutex(Server->createMutex())
		{}		

		void operator()();

		void quit()
		{
			IScopedLock lock(mutex.get());
			do_quit = true;
		}
		THREADPOOL_TICKET ticket;
	private:
		bool do_quit;
		std::unique_ptr<IMutex> mutex;
		BackgroundWorker& background_worker;
		KvStoreFrontend* frontend;
	};

	class MirrorThread : public IThread
	{
	public:
		MirrorThread(std::vector<KvStoreDao::CdIterObject2>& objs, IPipe* rpipe,
			IKvStoreBackend* backend,
		IKvStoreBackend* backend_mirror,
			KvStoreFrontend* frontend)
			: objs(objs), rpipe(rpipe),
			backend(backend),
			backend_mirror(backend_mirror),
			frontend(frontend),
			has_error(false) {}

		void operator()();

		bool get_has_error() {
			return has_error;
		}

	private:
		std::vector<KvStoreDao::CdIterObject2>& objs;
		IPipe* rpipe;
		IKvStoreBackend* backend;
		IKvStoreBackend* backend_mirror;
		KvStoreFrontend* frontend;
		bool has_error;
	};

	IKvStoreBackend* backend;
	IKvStoreBackend* backend_mirror;
	std::string mirror_window;
	std::unique_ptr<IMutex> mirror_del_log_mutex;
	std::unique_ptr<IFsFile> backend_mirror_del_log;
	int64 backend_mirror_del_log_wpos;
	int64 backend_mirror_del_log_rpos;

	std::unique_ptr<IFsFile> db_wal_file;
	IDatabase* getDatabase();

	bool importFromBackend(KvStoreDao& dao);

	BackgroundWorker background_worker;
	THREADPOOL_TICKET background_worker_ticket;

	MirrorWorker mirror_worker;
	THREADPOOL_TICKET mirror_worker_ticket;

	std::string empty_file_path;

	std::unique_ptr<IMutex> scrub_mutex;
	ScrubWorker* scrub_worker;

	PutDbWorker put_db_worker;
	THREADPOOL_TICKET put_db_worker_ticket;

	bool with_prefix;

	std::unique_ptr<IMutex> gen_mutex;
	int64 current_generation;
	int64 last_persisted_generation;
	int64 last_update_generation;

	std::unique_ptr<IMutex> unsynced_keys_mutex;

	struct SUnsyncedKey
	{
		SUnsyncedKey() 
			: transid(0) { }

		SUnsyncedKey(int64 transid,
			std::string md5sum)
			: transid(transid),
			md5sum(md5sum) { }

		int64 transid;
		std::string md5sum;
	};

	std::map<std::pair<int64, std::string>, SUnsyncedKey> unsynced_keys_a;
	std::map<std::pair<int64, std::string>, SUnsyncedKey> unsynced_keys_b;
	std::map<std::pair<int64, std::string>, SUnsyncedKey>* curr_unsynced_keys;
	std::map<std::pair<int64, std::string>, SUnsyncedKey>* other_unsynced_keys;

	std::unique_ptr<ISharedMutex> put_shared_mutex;

	static bool scrub_sync_test1;
	static bool scrub_sync_test2;

	bool has_last_modified;

	relaxed_atomic<int64> total_balance_ops;

	relaxed_atomic<int64> total_del_ops;

	std::string db_path;

	relaxed_atomic<int64> objects_total_size;
	relaxed_atomic<int64> objects_total_num;
	relaxed_atomic<bool> objects_init_complete;
	THREADPOOL_TICKET objects_init_ticket;

	int64 task_delay;

	// Inherited via IOnlineKvStore
	virtual int64 get_uploaded_bytes() override;
	virtual int64 get_downloaded_bytes() override;

	relaxed_atomic<int> mirror_state;
	relaxed_atomic<int64> mirror_curr_pos;
	relaxed_atomic<int64> mirror_curr_total;
	relaxed_atomic<int64> mirror_items;

	bool allow_import;

	IBackupFileSystem* cachefs;
};