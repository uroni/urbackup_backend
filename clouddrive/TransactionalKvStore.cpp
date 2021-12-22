/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifndef _WIN32
#include <sys/mount.h>
#endif
#include "TransactionalKvStore.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#include "../Interface/File.h"
#include <assert.h>
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../Interface/Thread.h"
#include <stdexcept>
#include <math.h>
#include <memory.h>
#include <limits.h>
#include <chrono>
#include "ICompressEncrypt.h"
#include "CloudFile.h"
#include "../common/data.h"
#ifdef HAS_LINUX_MEMORY_FILE
#include "LinuxMemFile.h"
#endif
#ifndef _WIN32
#include <sys/types.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/file.h>
#endif
#include "../urbackupcommon/events.h"

#ifdef HAS_FILE_SIZE_CACHE
#define FILE_SIZE_CACHE(x) x
#else
#define FILE_SIZE_CACHE(x) 
#endif

using namespace std::chrono_literals;

#ifdef _WIN32
typedef int ssize_t;
ssize_t sendfile(IFsFile::os_file_handle out_fd, IFsFile::os_file_handle in_fd, off_t* offset, size_t count)
{
	return -1;
}
#define POSIX_FADV_RANDOM 1
#endif

#define TRACE_CACHESIZE(x)

//#define OPEN_DIRECT MODE_RW_DIRECT
//#define CREATE_DIRECT MODE_RW_CREATE_DIRECT

#define OPEN_DIRECT MODE_RW
#define CREATE_DIRECT MODE_RW_CREATE

namespace
{
#ifdef _WIN32
	int get_file_fd(IFsFile::os_file_handle h)
	{
		return reinterpret_cast<int>(h);
	}
#else
	int get_file_fd(IFsFile::os_file_handle h)
	{
		return h;
	}
#endif


	const unsigned int max_retry_waittime = 30 * 60 * 1000;
	const size_t max_retry_n = 20;
	const size_t retry_log_n = 8;
	const int64 preload_once_removal_delay_ms = 30000;
	const int64 max_cachesize_throttle_size = 10LL * 1024 * 1024 * 1024;

	void retryWait(size_t n)
	{
		unsigned int waittime = (std::min)(static_cast<unsigned int>(1000.*pow(2., static_cast<double>(n))), (unsigned int)30*60*1000); //30min
		if(n>max_retry_n)
		{
			waittime = max_retry_waittime;
		}
		Server->Log("Waiting "+PrettyPrintTime(waittime));
		Server->wait(waittime);
	}

	char compressed_evicted_dirty = 2;

	bool sync_link(IBackupFileSystem* cachefs, std::string source, std::string dest)
	{
		if (!cachefs->reflinkFile(source, dest + ".tmp"))
		{
			return false;
		}

		std::unique_ptr<IFile> f(cachefs->openFile(dest + ".tmp", MODE_RW));
		if (f.get() == nullptr)
		{
			return false;
		}
		if (!f->Sync())
		{
			return false;
		}
		f.reset();

		return cachefs->rename((dest + ".tmp").c_str(), dest.c_str());
	}
	
	bool compare_files(IBackupFileSystem* cachefs, std::string filea, IFile* fb)
	{
		std::unique_ptr<IFile> fa(cachefs->openFile(filea, MODE_READ));

		if (fa.get() == nullptr)
		{
			Server->Log("Could not open " + filea, LL_ERROR);
			return false;
		}

		fb->Seek(0);

		char bufa[32768];
		char bufb[32768];

		while (true)
		{
			_u32 reada = fa->Read(bufa, 32768);
			_u32 readb = fb->Read(bufb, 32768);

			if (reada != readb || memcmp(bufa, bufb, reada) != 0)
			{
				Server->Log(filea+" != "+fb->getFilename(), LL_ERROR);
				return false;
			}

			if (reada == 0)
			{
				return true;
			}
		}
	}

	int64 get_compsize_file(IBackupFileSystem* cachefs, const std::string& path)
	{
		std::string val;
		if (cachefs->getXAttr(path, "user.cs", val))
		{
			if (val.size() != sizeof(int64))
			{
				return -1;
			}

			int64 ret;
			memcpy(&ret, val.data(), sizeof(ret));
			ret = little_endian(ret);

			return ret;
		}

		return -1;
	}

	bool set_compsize_file(IBackupFileSystem* cachefs, const std::string& path, int64 compsize)
	{
		compsize = little_endian(compsize);
		char * buf = reinterpret_cast<char*>(&compsize);
		std::string val(buf, buf+sizeof(compsize));
		return cachefs->setXAttr(path, "user.cs", val);
	}

	TransactionalKvStore::SCacheVal* cache_get(common::lrucache<std::string, TransactionalKvStore::SCacheVal>& lru_cache,
		const std::string& key, std::unique_lock<cache_mutex_t>& lock, bool bring_front = true)
	{
		assert(lock.owns_lock());
		return lru_cache.get(key, bring_front);
	}

	void cache_put(common::lrucache<std::string, TransactionalKvStore::SCacheVal>& lru_cache,
		const std::string& key, TransactionalKvStore::SCacheVal val, std::unique_lock<cache_mutex_t>& lock)
	{
		assert(lock.owns_lock());
		lru_cache.put(key, val);
	}

	void cache_put_back(common::lrucache<std::string, TransactionalKvStore::SCacheVal>& lru_cache,
		const std::string& key, TransactionalKvStore::SCacheVal val, std::unique_lock<cache_mutex_t>& lock)
	{
		assert(lock.owns_lock());
		lru_cache.put_back(key, val);
	}

	void cache_del(common::lrucache<std::string, TransactionalKvStore::SCacheVal>& lru_cache,
		const std::string& key, std::unique_lock<cache_mutex_t>& lock)
	{
		assert(lock.owns_lock());
		lru_cache.del(key);
	}

	std::pair<std::string, TransactionalKvStore::SCacheVal> cache_eviction_candidate(common::lrucache<std::string, TransactionalKvStore::SCacheVal>& lru_cache,
		std::unique_lock<cache_mutex_t>& lock, size_t skip = 0)
	{
		assert(lock.owns_lock());
		return lru_cache.eviction_candidate(skip);
	}

	std::list<std::pair<std::string const *, TransactionalKvStore::SCacheVal> >::iterator cache_eviction_iterator_start(common::lrucache<std::string, TransactionalKvStore::SCacheVal>& lru_cache,
		std::unique_lock<cache_mutex_t>& lock)
	{
		assert(lock.owns_lock());
		return lru_cache.eviction_iterator_start();
	}

	std::list<std::pair<std::string const *, TransactionalKvStore::SCacheVal> >::iterator cache_eviction_iterator_finish(common::lrucache<std::string, TransactionalKvStore::SCacheVal>& lru_cache,
		std::unique_lock<cache_mutex_t>& lock)
	{
		assert(lock.owns_lock());
		return lru_cache.eviction_iterator_finish();
	}

	class Bitmap
	{
	public:
		Bitmap(int64 n, bool init_set)
		{
			resize(n, init_set);
		}

		void resize(int64 n, bool init_set)
		{
			total_size = n;
			bitmap_size = static_cast<size_t>(n / 8 + (n % 8 == 0 ? 0 : 1));

			data.resize(bitmap_size);

			if (init_set)
			{
				memset(&data[0], 255, bitmap_size);
			}
		}

		void set(int64 i, bool v)
		{
			size_t bitmap_byte = (size_t)(i / 8);
			size_t bitmap_bit = i % 8;

			unsigned char b = data[bitmap_byte];

			if (v == true)
				b = b | (1 << (7 - bitmap_bit));
			else
				b = b&(~(1 << (7 - bitmap_bit)));

			data[bitmap_byte] = b;
		}

		size_t set_range(int64 start, int64 end, bool v)
		{
			size_t set_bits = 0;
			for (; start<end; ++start)
			{
				if (get(start) != v)
				{
					set(start, v);
					++set_bits;
				}
			}
			return set_bits;
		}

		char getb(int64 i) const
		{
			size_t bitmap_byte = (size_t)(i / 8);
			return data[bitmap_byte];
		}

		bool get(int64 i) const
		{
			size_t bitmap_byte = (size_t)(i / 8);
			size_t bitmap_bit = i % 8;

			unsigned char b = data[bitmap_byte];

			bool has_bit = ((b & (1 << (7 - bitmap_bit)))>0);

			return has_bit;
		}

		size_t count_bits()
		{
			size_t set_count = 0;
			for (int64 i = 0; i<total_size;)
			{
				if (i % 8 == 0
					&& getb(i) == 0)
				{
					i += 8;
					continue;
				}

				if (get(i))
				{
					++set_count;
				}

				++i;
			}

			return set_count;
		}

		bool get_range(int64 start, int64 end) const
		{
			for (; start<end; ++start)
			{
				if (get(start))
				{
					return true;
				}
			}
			return false;
		}

		int64 size()
		{
			return total_size;
		}

		size_t meminfo()
		{
			return bitmap_size;
		}


	private:
		size_t bitmap_size;
		int64 total_size;
		std::vector<unsigned char> data;
	};

	cache_mutex_t* g_cache_mutex = nullptr;
}


class TransactionalKvStore::SubmitWorker : IThread
{
public:
	friend class TransactionalKvStore;

	SubmitWorker(TransactionalKvStore& kv_store, bool no_compress, bool less_throttle)
		: kv_store(kv_store), no_compress(no_compress)
	{

	}

	void operator()()
	{
		bool background_prio=false;
		bool slight_background_prio = false;
		SPrioInfo prio_info;

		bool put_is_sync = kv_store.online_kv_store->is_put_sync();

		while(true)
		{
			std::string path;
			bool do_stop=false;
			SMemFile* memf = nullptr;
			std::list<SSubmissionItem>::iterator item = kv_store.next_submission_item(no_compress, no_compress, no_compress, path, do_stop, memf);
			IFsFile* memf_file = memf==nullptr ? nullptr : memf->file.get();

			if(do_stop)
			{
				break;
			}

			if(item->action==SubmissionAction_Working_Delete)
			{
				if(background_prio)
				{
					os_disable_background_priority(prio_info);
					background_prio=false;
				}

				if (!slight_background_prio)
				{
					slight_background_prio = os_enable_prioritize(prio_info, Prio_SlightBackground);
				}

				size_t n=0;
				while(!kv_store.online_kv_store->del(item->keys, item->transid) )
				{
					Server->Log("Error deleting blocks. Retrying...", LL_ERROR);
					retryWait(n++);

					if(do_stop)
					{
						break;
					}
				}
			}
			else if(item->action==SubmissionAction_Working_Compress)
			{
				if (slight_background_prio)
				{
					os_disable_prioritize(prio_info);
					slight_background_prio = false;
				}
				if(!background_prio)
				{
					background_prio = os_enable_background_priority(prio_info);
				}

				int64 size_diff;
				int64 dst_size;
				bool compression_ok = kv_store.compress_item(item->key, item->transid, memf_file, size_diff, dst_size, !item->compressed);
				kv_store.item_compressed(item, compression_ok, size_diff, dst_size, memf!=nullptr);
				continue;
			}
			else
			{
				if(background_prio)
				{
					os_disable_background_priority(prio_info);
					background_prio=false;
				}

				if (!slight_background_prio)
				{
					slight_background_prio = os_enable_prioritize(prio_info, Prio_SlightBackground);
				}

				if(!item->key.empty())
				{
					size_t n=0;
					int64 compressed_size;
					assert(memf_file == nullptr || !kv_store.cacheFileExists(path));

					unsigned int flags = 0;
					if (item->compressed)
						flags |= IOnlineKvStore::PutAlreadyCompressedEncrypted;
					if (kv_store.online_kv_store->want_put_metadata()
						&& kv_store.num_second_chances_cb != nullptr
						&& kv_store.num_second_chances_cb->is_metadata(item->key))
						flags |= IOnlineKvStore::PutMetadata;

					IFsFile* putf = memf_file;
					std::unique_ptr<IFsFile> putf_holder;
					while(putf==nullptr)
					{
						putf_holder.reset(kv_store.cachefs->openFile(path, MODE_READ));

						if(!putf_holder)
						{
							std::string syserr = kv_store.cachefs->lastError();
							Server->Log("Cannot open file to submit: " + path+". "+ syserr, LL_ERROR);
							addSystemEvent("online_kv_store",
								"Cannot open file to submit",
								"Cannot open file to submit: " + path + ". " + syserr, LL_ERROR);

							retryWait(n++);
						}
						else
						{
							putf = putf_holder.get();
						}
					}

					while(!kv_store.online_kv_store->put(item->key, item->transid, putf, flags, n>retry_log_n, compressed_size) )
					{
						Server->Log("Error submitting dirty data block "+kv_store.hex(item->key)+" transid "+convert(item->transid)+". Retrying...", LL_ERROR);
						if (kv_store.online_kv_store->fast_write_retry())
						{
							Server->wait(1000);
						}
						else
						{
							retryWait(n++);
						}

						if(do_stop)
						{
							break;
						}
					}

					putf_holder.reset();

					++kv_store.total_put_ops;

					if (!item->compressed
						&& kv_store.comp_percent>0
						&& kv_store.resubmit_compressed_ratio<1)
					{
						if (memf == nullptr)
						{
							set_compsize_file(kv_store.cachefs, path, compressed_size);
						}
						else
						{
							memf->compsize = compressed_size;
						}
					}
				}
			}
			

			if(do_stop)
			{
				break;
			}

			size_t n=0;
			while(!kv_store.item_submitted(item, put_is_sync, memf!=nullptr))
			{
				Server->Log("Error setting item to submitted", LL_ERROR);
				retryWait(n++);

				if(do_stop)
				{
					break;
				}
			}
		}

		if (background_prio)
		{
			os_disable_background_priority(prio_info);
		}

		if (slight_background_prio)
		{
			os_disable_prioritize(prio_info);
		}

		delete this;
	}

private:
	TransactionalKvStore& kv_store;
	bool no_compress;
	bool less_throttle;
};

class TransactionalKvStore::RetrievalOperation
{
public:
	friend class TransactionalKvStore;

	RetrievalOperation(std::unique_lock<cache_mutex_t>& lock, TransactionalKvStore& kv_store, const std::string& key)
		: lock(lock), kv_store(kv_store), key(key)
	{
		assert(fuse_io_context::is_sync_thread());
		kv_store.initiate_retrieval(key);
		lock.unlock();
	}

	~RetrievalOperation()
	{
		stop();
	}

	void stop() {
		assert(fuse_io_context::is_sync_thread());
		if (lock.owns_lock())
			return;

		lock.lock();
		kv_store.finish_retrieval(key);
	}

private:
	std::unique_lock<cache_mutex_t>& lock;
	TransactionalKvStore& kv_store;
	const std::string& key;
};

class TransactionalKvStore::RetrievalOperationUnlockOnly
{
public:

	explicit RetrievalOperationUnlockOnly(std::unique_lock<cache_mutex_t>& lock)
		: lock(lock)
	{
		lock.unlock();
	}

	~RetrievalOperationUnlockOnly()
	{
		stop();
	}

	void stop() {
		if (lock.owns_lock())
			return;
		lock.lock();
	}

private:
	std::unique_lock<cache_mutex_t>& lock;
};

class TransactionalKvStore::RetrievalOperationNoLock
{
public:
	friend class TransactionalKvStore;

	RetrievalOperationNoLock( TransactionalKvStore& kv_store, const std::string& key)
		:  kv_store(kv_store), key(key), running(true)
	{
		assert(fuse_io_context::is_sync_thread());
		kv_store.initiate_retrieval(key);
	}

	~RetrievalOperationNoLock()
	{
		stop();
	}

	void stop() {
		assert(fuse_io_context::is_sync_thread());
		if (!running) return;

		kv_store.finish_retrieval(key);
		running = false;
	}

private:
	TransactionalKvStore& kv_store;
	const std::string& key;
	bool running;
};

namespace
{

	class ReadOnlyFileWrapper : public IFsFile
	{
	public:
		ReadOnlyFileWrapper(IFsFile* wrapped_file)
			: wrapped_file(wrapped_file)
		{

		}

		virtual std::string Read( _u32 tr, bool* has_error=nullptr)
		{
			return wrapped_file->Read(tr, has_error);
		}

		virtual std::string Read(int64 spos, _u32 tr, bool* has_error = nullptr)
		{
			return wrapped_file->Read(spos, tr, has_error);
		}

		virtual _u32 Read( char* buffer, _u32 bsize, bool* has_error = nullptr)
		{
			return wrapped_file->Read(buffer, bsize, has_error);
		}

		virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool* has_error = nullptr)
		{
			return wrapped_file->Read(spos, buffer, bsize, has_error);
		}

		virtual _u32 Write( const std::string &tw, bool* has_error = nullptr)
		{
			assert(false);
			return 0;
		}

		virtual _u32 Write(int64 spos, const std::string &tw, bool* has_error = nullptr)
		{
			assert(false);
			return 0;
		}

		virtual _u32 Write( const char* buffer, _u32 bsize, bool* has_error = nullptr)
		{
			assert(false);
			return 0;
		}

		virtual _u32 Write( int64 spos, const char* buffer, _u32 bsize, bool* has_error = nullptr)
		{
			assert(false);
			return 0;
		}

		virtual bool Seek( _i64 spos )
		{
			return wrapped_file->Seek(spos);
		}

		virtual _i64 Size( void )
		{
			return wrapped_file->Size();
		}

		virtual _i64 RealSize(void)
		{
			return wrapped_file->RealSize();
		}

		virtual bool PunchHole( _i64 spos, _i64 size )
		{
			return false;
		}
		
		virtual bool Resize(int64 nsize, bool set_sparse = true)
		{
			assert(false);
			return false;
		}

		virtual std::string getFilename( void )
		{
			return wrapped_file->getFilename();
		}

		virtual bool Sync()
		{
			assert(false);
			return false;
		}

		virtual void resetSparseExtentIter()
		{
		}

		virtual SSparseExtent nextSparseExtent()
		{
			return SSparseExtent();
		}

		virtual IFsFile::os_file_handle getOsHandle(bool release_handle)
		{
			return wrapped_file->getOsHandle();
		}

		virtual std::vector<SFileExtent> getFileExtents(int64 starting_offset, int64 block_size, bool& more_data, unsigned int flags)
		{
			return wrapped_file->getFileExtents(starting_offset, block_size, more_data, flags);
		}

		virtual IVdlVolCache* createVdlVolCache() override
		{
			return nullptr;
		}

		virtual int64 getValidDataLength(IVdlVolCache* vol_cache) override
		{
			return int64();
		}
		
		virtual void setCachedSize(int64 size)
		{
			assert(false);
		}

		virtual void increaseCachedSize(int64 size)
		{
			assert(false);
		}

	private:
		IFsFile* wrapped_file;
	};
}


TransactionalKvStore::~TransactionalKvStore()
{
	Server->Log("Shutting down TransactionalKvStore...");
	stop();
	Server->Log("Deleting TransactionalKvStore...");
	for (auto it : read_only_open_files)
	{
		Server->destroy(it.second);
	}

	std::scoped_lock lock(cache_mutex);
	for (auto it : open_files)
	{
		if (!fd_cache.has_key(it.first))
		{
			std::scoped_lock lock(memfiles_mutex);
			SMemFile* memfile_nf = memfiles.get(std::make_pair(transid, it.first), false);
			if (memfile_nf == nullptr
				|| memfile_nf->file.get() != it.second.fd)
			{
				assert(dynamic_cast<IMemFile*>(it.second.fd)==nullptr);
				Server->destroy(it.second.fd);
			}
		}
	}

	while (!fd_cache.empty())
	{
		std::pair<std::string, SFdKey> candidate = fd_cache.evict_one();
		assert(dynamic_cast<IMemFile*>(candidate.second.fd) == nullptr);
		Server->destroy(candidate.second.fd);
	}
}

IFsFile* TransactionalKvStore::get(const std::string& key, BitmapInfo bitmap_present,
	unsigned int flags, int64 size_hint, int preload_tag)
{
	IFsFile* ret = get_internal(key, bitmap_present,
		flags, size_hint, preload_tag);

	if (ret != nullptr)
	{
		if (cachefs->filePath(ret) != keypath2(key, transid)
			&& ret->getFilename() != key)
		{
			Server->Log("Unexpected file name in ::get. Got " + cachefs->filePath(ret)
				+ " Expected " + keypath2(key, transid), LL_ERROR);
			abort();
		}

		if (cachefs->filePath(ret) == keypath2(key, transid))
		{
			if ( (cachefs->getFileType(cachefs->filePath(ret)) & EFileType_File)==0)
			{
				Server->Log("File does not exist (was deleted) in ::get: "+ cachefs->filePath(ret) +". "+os_last_error_str(), LL_ERROR);
				Server->Log(std::string(" base path - ") + (cachefs->getFileType("") == 0 ? "does not exist" : "exists"), LL_ERROR);
				Server->Log("trans_" + convert(transid) + " - " + (cachefs->getFileType("trans_" + convert(transid)) == 0 ? "does not exist" : "exists"), LL_ERROR);
				Server->Log(ExtractFilePath(cachefs->filePath(ret)) + " - " + (cachefs->getFileType(ExtractFilePath(cachefs->filePath(ret))) == 0 ? "does not exist" : "exists"), LL_ERROR);
				Server->Log(ExtractFilePath(ExtractFilePath(cachefs->filePath(ret))) + " - " + (cachefs->getFileType(ExtractFilePath(ExtractFilePath(cachefs->filePath(ret)))) == 0 ? "does not exist" : "exists"), LL_ERROR);
				abort();
			}

#ifndef NDEBUG
			std::scoped_lock memfile_lock(memfiles_mutex);
			assert(memfiles.get(std::make_pair(transid, key))==nullptr);
#endif
		}
		else
		{
			wait_for_del_file(keypath2(ret->getFilename(), transid));
			if ((cachefs->getFileType(keypath2(cachefs->filePath(ret), transid)) & EFileType_File) != 0)
			{
				Server->Log("File does exist for memfile in ::get: " + keypath2(ret->getFilename(), transid), LL_ERROR);
				abort();
			}
#ifndef NDEBUG
			std::scoped_lock memfile_lock(memfiles_mutex);
			assert(memfiles.get(std::make_pair(transid, key)) != nullptr);
#endif
		}
	}

	return ret;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<IFsFile*> TransactionalKvStore::get_async(fuse_io_context& io, const std::string& key,
	BitmapInfo bitmap_present, unsigned int flags, int64 size_hint,
	int preload_tag)
{
	bool cache_fd = (flags & Flag::disable_fd_cache) == 0;
	const bool can_block = (flags & Flag::disable_throttling) == 0;
	const bool prioritize_read = (flags & Flag::prioritize_read) > 0;
	const bool read_random = (flags & Flag::read_random) > 0;
	const bool read_only = (flags & Flag::read_only) > 0;
	
	if (can_block)
		co_await only_memfiles_throttle_async(io, key);

	std::unique_lock lock(cache_mutex);

	if (has_new_remaining_gets)
	{
		remaining_gets = new_remaining_gets;
		has_new_remaining_gets = false;
	}

	if (!read_only
		|| (remaining_gets == 0 && can_block && cache_get(lru_cache, key, lock, false) == NULL))
	{
		while (remaining_gets == 0
			&& can_block)
		{
			lock.unlock();

			co_await io.sleep_ms(1000);

			lock.lock();

			if (has_new_remaining_gets)
			{
				remaining_gets = new_remaining_gets;
				has_new_remaining_gets = false;
			}
		}

		if (remaining_gets != std::string::npos)
		{
			if (remaining_gets > 0)
			{
				--remaining_gets;
			}
		}
		else
		{
			++unthrottled_gets;
		}
	}

	lock = std::move((co_await wait_for_retrieval_async(io, std::move(lock), key)).lock);

	if(flags & Flag::preload_once)
	{
		Server->Log("Preload add " + hex(key));
		preload_once_items[key] = preload_tag;
		auto it = preload_once_delayed_removal.find(key);
		if (it != preload_once_delayed_removal.end())
			preload_once_delayed_removal.erase(it);
	}
	else
	{
		auto it = preload_once_items.find(key);
		if (it != preload_once_items.end())
		{
			Server->Log("Preload removal schedule of " + hex(key));
			preload_once_delayed_removal[key] = Server->getTimeMS() + preload_once_removal_delay_ms;
		}
	}

	std::map<std::string, SFdKey>::iterator it_open_file = open_files.find(key);

	if (it_open_file != open_files.end())
	{
		if (it_open_file->first != key)
			abort();

		if (cachefs->filePath(it_open_file->second.fd) != keypath2(key, transid)
			&& it_open_file->second.fd->getFilename() != key)
		{
			Server->Log("Filename of file from open files list: " + cachefs->filePath(it_open_file->second.fd)
				+ " Expected: " + keypath2(key, transid), LL_ERROR);
			abort();
		}

		if (!read_only && it_open_file->second.read_only)
		{
			++it_open_file->second.refcount;
		}
		else
		{
			++it_open_file->second.refcount;

			if (read_only)
			{
				std::map<IFsFile*, ReadOnlyFileWrapper*>::iterator wrapper_it = read_only_open_files.find(it_open_file->second.fd);
				if (wrapper_it != read_only_open_files.end())
				{
					if (cachefs->filePath(wrapper_it->second) != keypath2(key, transid)
						&& wrapper_it->second->getFilename() != key)
					{
						Server->Log("Filename of file from read only open files list: " + cachefs->filePath(wrapper_it->second) 
							+ " Expected: " + keypath2(key, transid), LL_ERROR);
						abort();
					}

					co_return wrapper_it->second;
				}
				else
				{
					ReadOnlyFileWrapper* nf_rdonly = new ReadOnlyFileWrapper(it_open_file->second.fd);
					read_only_open_files[it_open_file->second.fd] = nf_rdonly;
					co_return nf_rdonly;
				}
			}

			co_return it_open_file->second.fd;
		}
	}
	else
	{
		++total_hits;
	}

	SFdKey* res = NULL;

	if (it_open_file == open_files.end())
	{
		res = fd_cache.get(key);

		if (res != NULL)
		{
			if (read_only || !res->read_only)
			{
				open_files[key] = SFdKey(res->fd, res->fd->Size(), read_only);

				if (cachefs->filePath(res->fd) != keypath2(key, transid))
				{
					Server->Log("Filename of file from fd cache: " + cachefs->filePath(res->fd) +
						" Expected: " + keypath2(key, transid), LL_ERROR);
					abort();
				}

				assert(dynamic_cast<IMemFile*>(res->fd) == nullptr);

				if (read_only)
				{
					ReadOnlyFileWrapper* nf_rdonly = new ReadOnlyFileWrapper(res->fd);
					read_only_open_files[res->fd] = nf_rdonly;
					co_return nf_rdonly;
				}

				co_return res->fd;
			}
		}
	}

	if (!read_only)
	{
		std::set<std::string>::iterator it_untouched = nosubmit_untouched_items.find(key);
		if (it_untouched != nosubmit_untouched_items.end())
		{
			nosubmit_untouched_items.erase(it_untouched);
		}
	}

	SCacheVal* dirty = cache_get(lru_cache, key, lock);
	IFsFile* nf = NULL;
	bool has_initiate_retrieval = false;
	if (dirty != NULL)
	{
		bool new_dirty = false;
		if (!read_only && !dirty->dirty)
		{
			dirty->dirty = true;
			addDirtyItem(transid, key);
			new_dirty = true;

			auto missing_it = missing_items.find(key);
			if (missing_it != missing_items.end())
			{
				cachefs->deleteFile("missing_" + hex(key));
				missing_items.erase(missing_it);
			}
		}

		if (it_open_file != open_files.end())
		{
			if (!read_only)
			{
				if (dynamic_cast<IMemFile*>(it_open_file->second.fd) != nullptr)
				{
					assert(it_open_file->second.fd->getFilename() == key);
					SMemFile* memfile_nf;
					{
						std::scoped_lock memfile_lock(memfiles_mutex);
						memfile_nf = memfiles.get(std::make_pair(transid, key));
					}
					if (memfile_nf->key != key)
					{
						Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
						abort();
					}
					assert(memfile_nf != nullptr);
					if (memfile_nf->cow)
					{
						{
							if (!has_initiate_retrieval)
							{
								initiate_retrieval(key);
								has_initiate_retrieval = true;
							}
							RetrievalOperationUnlockOnly retrieval_operation(lock);

							while (!(co_await cow_mem_file_async(io, memfile_nf, true)) )
							{

								Server->Log("Waiting for memory for " + keypath2(key, transid) + " -1 ...", LL_INFO);
								co_await io.sleep_ms(1000);
							}
						}
						it_open_file->second.fd = memfile_nf->file.get();
					}
				}
				else
				{
					assert(cachefs->filePath(it_open_file->second.fd) == keypath2(key, transid));
				}
				nf = it_open_file->second.fd;
				it_open_file->second.read_only = false;
			}
		}
		else if (res != NULL)
		{
			nf = res->fd;
			assert(dynamic_cast<IMemFile*>(nf) == nullptr);
			if (!read_only)
			{
				res->read_only = false;
			}
		}
		else
		{
			SMemFile* memfile_nf;
			{
				std::scoped_lock memfile_lock(memfiles_mutex);
				memfile_nf = memfiles.get(std::make_pair(transid, key));
			}
			if (memfile_nf != nullptr)
			{
				if (memfile_nf->key != key)
				{
					Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
					abort();
				}

				if (!read_only
					&& memfile_nf->cow)
				{
					if (!has_initiate_retrieval)
					{
						initiate_retrieval(key);
						has_initiate_retrieval = true;
					}
					RetrievalOperationUnlockOnly retrieval_operation(lock);
					
					while (!(co_await cow_mem_file_async(io, memfile_nf, false)) )
					{
						
						Server->Log("Waiting for memory for " + keypath2(key, transid) + " -2 ...", LL_INFO);
						co_await io.sleep_ms(1000);
					}
				}
				nf = memfile_nf->file.get();
				cache_fd = false;

				++total_memory_hits;
			}
			else
			{
				if (!has_initiate_retrieval)
				{
					initiate_retrieval(key);
					has_initiate_retrieval = true;
				}
				RetrievalOperationUnlockOnly retrieval_operation(lock);

				nf = co_await cachefs->openFileAsync(io, keypath2(key, transid), OPEN_DIRECT);

				if (read_random
					&& nf != nullptr)
				{
					co_await set_read_random_async(io, nf);
				}
			}
		}


		if (nf == NULL)
		{
			std::string err;
			std::string msg = "Could not open cached file " + keypath2(key, transid) + ". " + os_last_error_str();
			Server->Log(msg, LL_ERROR);
			err += msg;

			if (!cachefs->directoryExists(transpath2()))
			{
				msg = "Transaction path " + transpath2() + " is offline";
				Server->Log(msg, LL_ERROR);
				err += "\n" + msg;
			}

			if (!cachefs->directoryExists(std::string()))
			{
				msg = "Cache base path " + cachefs->getName() + " is offline";
				Server->Log(msg, LL_ERROR);
				err += "\n" + msg;
			}

			addSystemEvent("cache_err_fatal",
				"Error opening file on cache device", err, LL_ERROR);

			abort();
		}

		if (new_dirty)
		{
			add_dirty_bytes(transid, key, nf->Size());
		}

		if (it_open_file == open_files.end())
		{
			open_files[key] = SFdKey(nf, nf->Size(), read_only);
		}
	}
	else
	{
		assert(it_open_file == open_files.end());
		if (!has_initiate_retrieval)
		{
			initiate_retrieval(key);
			has_initiate_retrieval = true;
		}
		
		RetrievalOperationUnlockOnly retrieval_operation(lock);

		auto kv_retrieve = [key, bitmap_present, flags, size_hint, res, cache_fd, this, prioritize_read]() {

			SPrioInfo prio_info;
			bool unprio = false;;
			if (prioritize_read)
			{
				unprio = os_enable_prioritize(prio_info, Prio_SlightPrioritize);
			}

			std::unique_lock lock(this->cache_mutex);
			bool l_cache_fd = cache_fd;
			IFsFile* nf = get_retrieve(key, bitmap_present, flags, size_hint, res, l_cache_fd, lock);

			if (unprio)
			{
				os_disable_prioritize(prio_info);
			}

			return std::make_pair(nf, l_cache_fd);
		};

		auto [nf_fd, n_cache_fd] = co_await io.run_in_threadpool(kv_retrieve, "kv retrieve");

		nf = nf_fd;
		cache_fd = n_cache_fd;
	}

	assert(nf != NULL);

	std::vector<IFsFile*> cache_free_list;
	if (cache_fd && it_open_file == open_files.end()
		&& fd_cache_size > 0)
	{
		assert(dynamic_cast<IMemFile*>(nf) == nullptr);
		SFdKey fd_cache_val(nf, -1, read_only);
		fd_cache.put(key, fd_cache_val);

		int retries = static_cast<int>(fd_cache_size - 1);
		while (fd_cache.size() > fd_cache_size)
		{
			std::pair<std::string, SFdKey> candidate = fd_cache.evict_one();

			if (open_files.find(candidate.first) == open_files.end())
			{
				cache_free_list.push_back(candidate.second.fd);
			}
			else
			{
				fd_cache.put(candidate.first, candidate.second);
				--retries;
				if (retries < 0) break;
			}
		}
	}

	IFsFile* ret = NULL;
	if (read_only)
	{
		if (it_open_file != open_files.end())
		{
			std::map<IFsFile*, ReadOnlyFileWrapper*>::iterator wrapper_it = read_only_open_files.find(nf);
			if (wrapper_it != read_only_open_files.end())
			{
				ret = wrapper_it->second;
			}
		}

		if (ret == NULL)
		{
			ReadOnlyFileWrapper* nf_rdonly = new ReadOnlyFileWrapper(nf);
			read_only_open_files[nf] = nf_rdonly;
			ret = nf_rdonly;
		}
	}
	else
	{
		ret = nf;
	}

#ifndef NDEBUG
	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		SMemFile* memfile_nf = memfiles.get(std::make_pair(transid, key), false);
		if (memfile_nf != nullptr)
		{
			if (memfile_nf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
				abort();
			}

			assert(nf == memfile_nf->file.get());
			if (!read_only)
			{
				assert(!memfile_nf->cow);
				assert(!memfile_nf->evicted);
			}
		}
	}
#endif

	if (has_initiate_retrieval)
	{
		finish_retrieval_async(io, std::move(lock), key);
	}
	else
	{
		lock.unlock();
	}

	for (IFsFile* f : cache_free_list)
	{
		assert(dynamic_cast<IMemFile*>(f) == nullptr);
		co_await drop_cache_async(io, f);
		cachefs->closeAsync(io, f);
		Server->destroy(f);
	}

	co_return ret;
}
#endif

void TransactionalKvStore::check_mutex_not_held()
{
#ifndef NDEBUG
	if (cache_mutex.has_lock())
		abort();
#endif
}

void g_check_mutex_not_held()
{
#ifndef NDEBUG
	if (g_cache_mutex == nullptr)
		return;

	if (g_cache_mutex->has_lock())
		abort();
#endif
}

IFsFile* TransactionalKvStore::get_internal( const std::string& key, BitmapInfo bitmap_present, 
	unsigned int flags, int64 size_hint, int preload_tag)
{
	assert(fuse_io_context::is_sync_thread());

	bool cache_fd = (flags & Flag::disable_fd_cache) == 0;
	const bool can_block = (flags & Flag::disable_throttling) == 0;
	const bool prioritize_read = (flags & Flag::prioritize_read) > 0;
	const bool read_random = (flags & Flag::read_random) > 0;
	const bool read_only = (flags & Flag::read_only) > 0;

	std::unique_lock lock(cache_mutex);

	if(can_block)
		only_memfiles_throttle(key, lock);

	if (has_new_remaining_gets)
	{
		remaining_gets = new_remaining_gets;
		has_new_remaining_gets = false;
	}

	if (!read_only
		|| (remaining_gets==0 && can_block && cache_get(lru_cache, key, lock, false)==nullptr ) )
	{
		while (remaining_gets == 0
			&& can_block)
		{
			lock.unlock();
			Server->wait(1000);
			lock.lock();

			if (has_new_remaining_gets)
			{
				remaining_gets = new_remaining_gets;
				has_new_remaining_gets = false;
			}
		}

		if (remaining_gets != std::string::npos)
		{
			if (remaining_gets > 0)
			{
				--remaining_gets;
			}
		}
		else
		{
			++unthrottled_gets;
		}
	}

	wait_for_retrieval(lock, key);

	if (flags & Flag::preload_once)
	{
		Server->Log("Preload add " + hex(key));
		preload_once_items[key] = preload_tag;
		auto it = preload_once_delayed_removal.find(key);
		if (it != preload_once_delayed_removal.end())
			preload_once_delayed_removal.erase(it);
	}
	else
	{
		auto it = preload_once_items.find(key);
		if (it != preload_once_items.end())
		{
			Server->Log("Preload removal schedule of " + hex(key));
			preload_once_delayed_removal[key] = Server->getTimeMS() + preload_once_removal_delay_ms;
		}
	}

	std::map<std::string, SFdKey>::iterator it_open_file = open_files.find(key);

	if(it_open_file!=open_files.end())
	{
		if (it_open_file->first != key)
			abort();

		if (cachefs->filePath(it_open_file->second.fd) != keypath2(key, transid)
			&& it_open_file->second.fd->getFilename() != key)
		{
			Server->Log("Filename of file from open files list: " + cachefs->filePath(it_open_file->second.fd)
				+ " Expected: " + keypath2(key, transid), LL_ERROR);
			abort();
		}

		if(!read_only && it_open_file->second.read_only)
		{
			++it_open_file->second.refcount;
		}
		else
		{
			++it_open_file->second.refcount;

			if(read_only)
			{
				std::map<IFsFile*, ReadOnlyFileWrapper*>::iterator wrapper_it = read_only_open_files.find(it_open_file->second.fd);
				if(wrapper_it!=read_only_open_files.end())
				{
					if (cachefs->filePath(wrapper_it->second) != keypath2(key, transid)
						&& wrapper_it->second->getFilename() != key)
					{
						Server->Log("Filename of file from read only open files list: " + wrapper_it->second->getFilename() 
							+ " Expected: " + keypath2(key, transid), LL_ERROR);
						abort();
					}

					return wrapper_it->second;
				}
				else
				{
					ReadOnlyFileWrapper* nf_rdonly = new ReadOnlyFileWrapper(it_open_file->second.fd);
					read_only_open_files[it_open_file->second.fd] = nf_rdonly;
					return nf_rdonly; 
				}
			}

			return it_open_file->second.fd;
		}
	}
	else
	{
		++total_hits;
	}

	SFdKey* res=nullptr;

	if(it_open_file==open_files.end())
	{
		res = fd_cache.get(key);

		if(res!=nullptr)
		{
			if(read_only || !res->read_only)
			{
				open_files[key] = SFdKey(res->fd, res->fd->Size(), read_only);

				if (cachefs->filePath(res->fd) != keypath2(key, transid))
				{
					Server->Log("Filename of file from fd cache: " + cachefs->filePath(res->fd)
						+ " Expected: " + keypath2(key, transid), LL_ERROR);
					abort();
				}

				assert(dynamic_cast<IMemFile*>(res->fd) == nullptr);

				if(read_only)
				{
					ReadOnlyFileWrapper* nf_rdonly = new ReadOnlyFileWrapper(res->fd);
					read_only_open_files[res->fd] = nf_rdonly;
					return nf_rdonly;
				}

				return res->fd;
			}
		}
	}
	
	if(!read_only)
	{
		std::set<std::string>::iterator it_untouched = nosubmit_untouched_items.find(key);
		if(it_untouched!=nosubmit_untouched_items.end())
		{
			nosubmit_untouched_items.erase(it_untouched);
		}
	}

	SCacheVal* dirty = cache_get(lru_cache, key, lock);
	IFsFile* nf = nullptr;
	if(dirty!=nullptr)
	{
		bool new_dirty = false;
		if(!read_only && !dirty->dirty )
		{
			dirty->dirty = true;
			addDirtyItem(transid, key);
			new_dirty=true;

			auto missing_it = missing_items.find(key);
			if (missing_it != missing_items.end())
			{
				cachefs->deleteFile("missing_" + hex(key));
				missing_items.erase(missing_it);
			}
		}

		if(it_open_file!=open_files.end())
		{
			if(!read_only)
			{
				if (dynamic_cast<IMemFile*>(it_open_file->second.fd) != nullptr)
				{
					assert(it_open_file->second.fd->getFilename() == key);
					SMemFile* memfile_nf;
					{
						std::scoped_lock memfile_lock(memfiles_mutex);
						memfile_nf = memfiles.get(std::make_pair(transid, key));
					}
					if (memfile_nf->key != key)
					{
						Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
						abort();
					}
					assert(memfile_nf != nullptr);
					if (memfile_nf->cow)
					{
						while (!cow_mem_file(memfile_nf, true))
						{
							RetrievalOperation retrieval_operation(lock, *this, key);
							Server->Log("Waiting for memory for " + keypath2(key, transid) + " -1 ...", LL_INFO);
							Server->wait(1000);
						}
						it_open_file->second.fd = memfile_nf->file.get();
					}
				}
				else
				{
					assert(it_open_file->second.fd->getFilename() == keypath2(key, transid));
				}
				nf = it_open_file->second.fd;
				it_open_file->second.read_only = false;
			}
		}
		else if(res!=nullptr)
		{
			nf = res->fd;
			assert(dynamic_cast<IMemFile*>(nf)==nullptr);
			if(!read_only)
			{
				res->read_only=false;
			}			
		}
		else
		{
			SMemFile* memfile_nf;
			{
				std::scoped_lock memfile_lock(memfiles_mutex);
				memfile_nf = memfiles.get(std::make_pair(transid, key));
			}
			if (memfile_nf != nullptr)
			{
				if (memfile_nf->key != key)
				{
					Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
					abort();
				}

				if (!read_only
					&& memfile_nf->cow)
				{
					while (!cow_mem_file(memfile_nf, false))
					{
						RetrievalOperation retrieval_operation(lock, *this, key);
						Server->Log("Waiting for memory for " + keypath2(key, transid) + " -2 ...", LL_INFO);
						Server->wait(1000);
					}
				}
				nf = memfile_nf->file.get();
				cache_fd = false;

				++total_memory_hits;
			}
			else
			{
				RetrievalOperation retrieval_operation(lock, *this, key);

				nf = cachefs->openFile(keypath2(key, transid), OPEN_DIRECT);

				if (read_random
					&& nf != nullptr)
				{
					set_read_random(nf);
				}

#ifdef HAS_FILE_SIZE_CACHE
				if (nf != nullptr)
					nf->setCachedSize(nf->Size());
#endif
			}
		}
		

		if(nf==nullptr)
		{
			std::string err;
			std::string msg = "Could not open cached file " + keypath2(key, transid) + ". " + cachefs->lastError();
			Server->Log(msg, LL_ERROR);
			err += msg;

			if (!cachefs->directoryExists(transpath2()))
			{
				msg = "Transaction path " + transpath2() + " is offline";
				Server->Log(msg, LL_ERROR);
				err += "\n" + msg;
			}

			if (!cachefs->directoryExists(""))
			{
				msg = "Cache base path is offline";
				Server->Log(msg, LL_ERROR);
				err += "\n" + msg;
			}

			addSystemEvent("cache_err_fatal",
				"Error opening file on cache device", err, LL_ERROR);

			abort();
		}

		if(new_dirty)
		{
			add_dirty_bytes(transid, key, nf->Size());
		}

		if(it_open_file==open_files.end())
		{
			open_files[key] = SFdKey(nf, nf->Size(), read_only);
		}
	}
	else
	{
		assert(it_open_file == open_files.end());

		RetrievalOperationNoLock retrieval_operation(*this, key);

		nf = get_retrieve(key, bitmap_present, flags, size_hint, res, cache_fd, lock);
	}

	assert(nf!=nullptr);

	std::vector<IFsFile*> cache_free_list;
	if(cache_fd && it_open_file==open_files.end()
		&& fd_cache_size>0)
	{
		assert(dynamic_cast<IMemFile*>(nf) == nullptr);
		SFdKey fd_cache_val(nf, -1, read_only);
		fd_cache.put(key, fd_cache_val);

		int retries=static_cast<int>(fd_cache_size-1);
		while(fd_cache.size()>fd_cache_size)
		{
			std::pair<std::string, SFdKey> candidate = fd_cache.evict_one();

			if(open_files.find(candidate.first)==open_files.end())
			{
				cache_free_list.push_back(candidate.second.fd);
			}
			else
			{
				fd_cache.put(candidate.first, candidate.second);
				--retries;
				if(retries<0) break;
			}
		}
	}

	IFsFile* ret = nullptr;
	if(read_only)
	{
		if(it_open_file!=open_files.end())
		{
			std::map<IFsFile*, ReadOnlyFileWrapper*>::iterator wrapper_it = read_only_open_files.find(nf);
			if(wrapper_it!=read_only_open_files.end())
			{
				ret = wrapper_it->second;
			}
		}

		if (ret == nullptr)
		{
			ReadOnlyFileWrapper* nf_rdonly = new ReadOnlyFileWrapper(nf);
			read_only_open_files[nf] = nf_rdonly;
			ret = nf_rdonly;
		}
	}
	else
	{
		ret=nf;
	}

#ifndef NDEBUG
	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		SMemFile* memfile_nf = memfiles.get(std::make_pair(transid, key), false);
		if (memfile_nf != nullptr)
		{
			if (memfile_nf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
				abort();
			}

			assert(nf == memfile_nf->file.get());
			if (!read_only)
			{
				assert(!memfile_nf->cow);
				assert(!memfile_nf->evicted);
			}
		}
	}	
#endif

	lock.unlock();
	for (IFsFile* f : cache_free_list)
	{
		assert(dynamic_cast<IMemFile*>(f) == nullptr);
		drop_cache(f);
		Server->destroy(f);
	}

	return ret;
}

IFsFile* TransactionalKvStore::get_retrieve(const std::string& key, BitmapInfo bitmap_present, unsigned int flags,
	int64 size_hint, SFdKey* res, bool& cache_fd, std::unique_lock<cache_mutex_t>& cache_lock)
{
	const bool read_random = (flags & Flag::read_random) > 0;
	const bool read_only = (flags & Flag::read_only) > 0;
	const bool prioritize_read = (flags & Flag::prioritize_read) > 0;

	IFsFile* nf = nullptr;

	std::set<std::string>::iterator del_queue_it = queued_dels.find(key);

	if (del_queue_it != queued_dels.end())
	{
		queued_dels.erase(del_queue_it);
	}

	std::unique_lock lock_evict(submission_mutex);

	std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator >::iterator it = submission_items.find(std::make_pair(transid, key));

	while (it != submission_items.end()
		&& (it->second->action == SubmissionAction_Working_Compress || it->second->action == SubmissionAction_Working_Evict))
	{
		lock_evict.unlock();
		{
			RetrievalOperationUnlockOnly retrieval_operation(cache_lock);
			Server->wait(50);
		}
		lock_evict.lock();

		it = submission_items.find(std::make_pair(transid, key));
	}

	bool retrieve_file = true;
	if (it != submission_items.end()
		&& (it->second->action == SubmissionAction_Evict || it->second->action == SubmissionAction_Compress))
	{
		bool submission_dirty = true;

		if (it->second->action == SubmissionAction_Compress)
		{
			submission_dirty = it->second->compressed;
		}

		if (it->second->action == SubmissionAction_Evict
			&& it->second->compressed)
		{			
			retrieve_file = true;
		}
		else
		{
			retrieve_file = false;
		}

		if (it->second->action == SubmissionAction_Evict)
		{
			submitted_bytes -= (*it->second).size;
		}

		rm_submission_item(it);
		lock_evict.unlock();

		SCacheVal curr_cache_val;
		{
			RetrievalOperationUnlockOnly retrieval_operation(cache_lock);
			if(retrieve_file)
			{
				curr_cache_val = cache_val(key, submission_dirty);
			}
			else
			{
				curr_cache_val = cache_val(key, submission_dirty || !read_only);
			}
		}

		if(retrieve_file)
		{
			cache_put(compressed_items, key, curr_cache_val, cache_lock);
		}
		else
		{
			cache_put(lru_cache, key, curr_cache_val, cache_lock);
		}

		if (!retrieve_file)
		{
			if (res != nullptr)
			{
				nf = res->fd;
				if (!read_only)
				{
					res->read_only = false;
				}
			}
			else
			{
				SMemFile* memfile_nf;
				{
					std::scoped_lock memfile_lock(memfiles_mutex);
					memfile_nf = memfiles.get(std::make_pair(transid, key));
				}
				if (memfile_nf != nullptr)
				{
					if (memfile_nf->key != key)
					{
						Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
						abort();
					}

					assert(!memfile_nf->evicted);
					if (!read_only
						&& memfile_nf->cow)
					{
						while (!cow_mem_file(memfile_nf, false))
						{
							RetrievalOperationUnlockOnly retrieval_operation(cache_lock);
							Server->Log("Waiting for memory for " + keypath2(key, transid) + " -3 ...", LL_INFO);
							Server->wait(1000);
						}
					}

					nf = memfile_nf->file.get();
					cache_fd = false;
				}
				else
				{
					RetrievalOperationUnlockOnly retrieval_operation(cache_lock);

					nf = cachefs->openFile(keypath2(key, transid), OPEN_DIRECT);

					if (read_random
						&& nf != nullptr)
					{
						set_read_random(nf);
					}

#ifdef HAS_FILE_SIZE_CACHE
					if(nf!=nullptr)
						nf->setCachedSize(nf->Size());
#endif

					assert(nf != nullptr);
				}
			}

			open_files[key] = SFdKey(nf, nf->Size(), read_only);

			if (!submission_dirty
				&& !read_only)
			{
				addDirtyItem(transid, key);
				assert(nf != nullptr);
				if (nf != nullptr)
				{
					add_dirty_bytes(transid, key, nf->Size());
				}
				remove_missing(key);
			}
		}
	}

	if (retrieve_file)
	{
		if(lock_evict.owns_lock())
			lock_evict.unlock();

		bool not_found = false;
		IFsFile* online_file = nullptr;
		bool decompressed_ok = false;
		bool evicted_dirty = false;
		SCacheVal curr_cache_val;

		SCacheVal* compressed_orig = cache_get(compressed_items, key, cache_lock);
		SCacheVal compressed_src;
		SCacheVal* compressed = nullptr;
		if (compressed_orig != nullptr)
		{
			compressed = &compressed_src;
			*compressed = *compressed_orig;
			not_found = false;

			int64 size_diff;
			int64 src_size = 0;

			IFsFile* memfile = nullptr;
			if (!(flags & Flag::disable_memfiles))
			{
				memfile = get_mem_file(key, size_hint, read_only);
			}
			{
				bool nosubmit_untouched = nosubmit_untouched_items.find(key) != nosubmit_untouched_items.end();
				wait_for_del_file(keypath2(key, transid));
				assert(!cacheFileExists(keypath2(key, transid)));
				RetrievalOperationUnlockOnly retrieval_operation(cache_lock);

				Server->Log("Decompressing cache item " + keypath2(key, transid) + ".comp ...", LL_INFO);

				decompressed_ok = decompress_item(key, transid, memfile, size_diff, src_size, with_prev_link && (!compressed->dirty || nosubmit_untouched));

				if (compressed->dirty && !decompressed_ok)
				{
					addSystemEvent("cache_err",
						"Cannot decompress dirty cache item on cache",
						"Cannot decompress dirty cache item at " + keypath2(key, transid) + ".comp. \n\n"
						"Last error:\n"
						+ extractLastLogErrors(), LL_ERROR);
					Server->Log("Cannot decompress dirty data block " + keypath2(key, transid) + ".comp", LL_ERROR);
					abort();
				}
				else if (!decompressed_ok)
				{
					retrieval_operation.stop();
					delete_item(nullptr, key, true, cache_lock, nosubmit_untouched ? basetrans : 0, 
						0, DeleteImm::Unlock);
				}

				curr_cache_val = cache_val(key, !read_only || compressed->dirty);

				if (decompressed_ok)
				{
					if (with_prev_link
						&& memfile == nullptr)
					{
						if (!compressed->dirty
							|| nosubmit_untouched)
						{
							std::string path = keypath2(key, basetrans);
							if (!sync_link(cachefs, keypath2(key, transid), path))
							{
								nosubmit_untouched = false;
							}
						}
					}
					else
					{
						nosubmit_untouched = false;
					}

					if (memfile == nullptr)
					{
						online_file = cachefs->openFile(keypath2(key, transid), OPEN_DIRECT);

						if (online_file == nullptr)
						{
							std::string syserr = os_last_error_str();
							Server->Log("Error opening decompressed file " + keypath2(key, transid) + ". " + syserr, LL_ERROR);
							addSystemEvent("cache_err_fatal",
								"Error opening decompressed file on cache",
								"Error opening decompressed file " + keypath2(key, transid) + ". " + syserr, LL_ERROR);
							abort();
						}
						else
						{
							FILE_SIZE_CACHE(online_file->setCachedSize(online_file->Size()));
						}
					}
					else
					{
						online_file = memfile;
						cache_fd = false;
					}

					retrieval_operation.stop();
					delete_item(nullptr, key, true, cache_lock, nosubmit_untouched ? basetrans : 0, 
						0, DeleteImm::Unlock);
				}
			}

			if (!decompressed_ok && memfile != nullptr)
			{
				rm_mem_file(nullptr, transid, key, false);
				memfile = nullptr;
			}

			if (decompressed_ok)
			{
				++total_cache_miss_decompress;
				if (compressed->dirty)
				{
					add_dirty_bytes(transid, key, size_diff);
				}

				cache_del(compressed_items, key, cache_lock);

				sub_cachesize(src_size);
				comp_bytes -= src_size;
			}
		}

		if (!decompressed_ok && (bitmap_present == BitmapInfo::Present || bitmap_present == BitmapInfo::Unknown))
		{
			wait_for_del_file(keypath2(key, transid));
#ifndef NDEBUG
			wait_for_del_file(keypath2(key, transid) + ".comp");
#endif
			assert(!cacheFileExists(keypath2(key, transid) + ".comp"));
			assert(!cacheFileExists(keypath2(key, transid)));
			RetrievalOperationUnlockOnly retrieval_operation(cache_lock);

			++total_cache_miss_backend;

			bool in_submission;
			bool logged_in_submission = false;
			int64 max_transid = -1;

			do
			{
				in_submission = false;

				{
					std::scoped_lock lock(submission_mutex);
					std::scoped_lock dirty_lock(dirty_item_mutex);

					//TODO: We can copy/decompress directly from the highest transaction here

					for (std::map<int64, std::set<std::string> >::iterator it = nosubmit_dirty_items.begin(); it != nosubmit_dirty_items.end(); ++it)
					{
						if (!it->second.empty())
						{
							auto it_submission = submission_items.find(std::make_pair(it->first, key));
							if (it_submission != submission_items.end())
							{
								if (max_transid == -1)
								{
									max_transid = online_kv_store->get_transid(key, transid);
								}
								if (max_transid > 0
									&& max_transid > it->first)
								{
									if (!logged_in_submission)
									{
										Server->Log("Ignoring eviction of item " + hex(key) + " trans " + convert(it->first) + " because max_transid is " + convert(max_transid));
									}
									continue;
								}

								bool copy_src = false;
								std::shared_ptr<IFsFile> memf_src;
								if (max_transid > 0 &&
									!it_submission->second->compressed)
								{
									copy_src = true;
								}

								in_submission = true;

								SMemFile* memf;
								{
									std::scoped_lock memfile_lock(memfiles_mutex);
									memf = memfiles.get(std::make_pair(it->first, key), false);
									if (memf != nullptr
										&& memf->key != key)
									{
										Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memf->key), LL_ERROR);
										abort();
									}

									if (copy_src && memf != nullptr)
									{
										memf_src = memf->file;
									}
								}

								if (copy_src && memf_src)
									break;

								if (copy_src)
								{
									keypath2(key, it->first);
								}

								if (memf != nullptr)
								{
									submission_queue_memfile_first = submission_queue.end();
								}

								//Do not evict non-dirty item after submission
								if (it_submission->second->keys.empty())
									it_submission->second->keys.push_back(std::string());

								submission_queue.splice(submission_queue.begin(), submission_queue, it_submission->second);

								if (!logged_in_submission)
								{
									Server->Log("Waiting for eviction of item " + hex(key) + " trans " + convert(it->first) + " to finish before retrieving object...");
									logged_in_submission = true;
								}
							}
						}
					}

					for (std::map<int64, size_t>::iterator it = num_dirty_items.begin(); it != num_dirty_items.end(); ++it)
					{
						if (it->second > 0)
						{
							auto it_submission = submission_items.find(std::make_pair(it->first, key));
							if (it_submission != submission_items.end())
							{
								if (max_transid == -1)
								{
									max_transid = online_kv_store->get_transid(key, transid);
								}
								if (max_transid > 0
									&& max_transid > it->first)
								{
									if (!logged_in_submission)
									{
										Server->Log("Ignoring submission of item " + hex(key) + " trans " + convert(it->first) + " because max_transid is " + convert(max_transid));
									}
									continue;
								}

								in_submission = true;

								//Do not evict non-dirty item after submission
								if (it_submission->second->keys.empty())
									it_submission->second->keys.push_back(std::string());

								SMemFile* memf;
								{
									std::scoped_lock memfile_lock(memfiles_mutex);
									memf = memfiles.get(std::make_pair(it->first, key), false);
									if (memf != nullptr
										&& memf->key != key)
									{
										Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memf->key), LL_ERROR);
										abort();
									}
								}
								if (memf != nullptr)
								{
									submission_queue_memfile_first = submission_queue.end();
								}

								submission_queue.splice(submission_queue.begin(), submission_queue, it_submission->second);

								if (!logged_in_submission)
								{
									Server->Log("Waiting for submission of item " + hex(key) + " trans " + convert(it->first) + " to finish before retrieving object...");
									logged_in_submission = true;
								}
							}
						}
					}
				}

				if (in_submission)
				{
					Server->wait(10);
				}

			} while (in_submission);

			IFsFile* memfile = nullptr;
			if (max_memfile_size > 0
				&& !(flags & Flag::disable_memfiles))
			{
				std::scoped_lock lock(cache_mutex);
				memfile = get_mem_file(key, size_hint, read_only);
				if (memfile != nullptr)
				{
					cache_fd = false;
				}
			}

			size_t retry_n = 0;
			do
			{
				not_found = false;

				IFsFile* online_file_tmpl = memfile;
				if (online_file_tmpl == nullptr)
				{
					std::string online_file_path = keypath2(key, transid);
					bool path_err = false;
					if (!cachefs->directoryExists(ExtractFilePath(online_file_path)))
					{
						if (!cachefs->createDir(ExtractFilePath(online_file_path)))
						{
							std::string syserr = os_last_error_str();
							Server->Log("Error creating directory for GET-file with key " + bytesToHex(key) + " path " + ExtractFilePath(online_file_path) + ". " + syserr, LL_ERROR);
							addSystemEvent("cache_err_retry",
								"Error creating directory for get file on cache",
								"Error creating directory for GET-file with key " + bytesToHex(key) + " path " + ExtractFilePath(online_file_path) + ". " + syserr, LL_ERROR);
							path_err = true;
						}
					}

					if (!path_err)
					{
						online_file_tmpl = cachefs->openFile(online_file_path, CREATE_DIRECT);

						if (online_file_tmpl == nullptr)
						{
							std::string syserr = os_last_error_str();
							Server->Log("Error opening output file " + online_file_path + ". " + syserr, LL_ERROR);
							addSystemEvent("online_kv_store",
								"Error opening output file",
								"Error opening output file " + online_file_path + ". " + syserr, LL_ERROR);
						}
						else
						{
							set_cache_file_compression(key, online_file_path);
						}
					}
				}

				if (online_file_tmpl != nullptr)
				{
					online_file = online_kv_store->get(key, transid,
						prioritize_read, online_file_tmpl, retry_n > retry_log_n, not_found);
					assert(online_file == nullptr || memfile == nullptr || online_file == memfile);

					if (online_file == nullptr &&
						online_file_tmpl != memfile)
					{
						Server->destroy(online_file_tmpl);
						cachefs->deleteFile(keypath2(key, transid));
					}
				}

				if (online_file != nullptr)
				{
					FILE_SIZE_CACHE(online_file->setCachedSize(online_file->Size()));
				}

				if (online_file == nullptr)
				{
					std::string reset_retries = readCacheFile("clouddrive_reset_retries");

					size_t retry_max = 12;
					if (!trim(reset_retries).empty())
					{
						retry_max = watoi(trim(reset_retries));
					}

					if (retry_n + 1 > 10)
					{
						Server->Log("Error getting online file " + hex(key) + ". Retrying... Try " + convert(retry_n), LL_ERROR);
					}
					else
					{
						Server->Log("Error getting online file " + hex(key) + ". Retrying...", LL_WARNING);
					}
					if (retry_n > retry_max
						&& cacheFileExists("clouddrive_reset_unreadable"))
					{
						Server->Log("CRITICAL: Removing object " + hex(key) + " which cannot be read.", LL_ERROR);
						online_kv_store->reset(key, transid);
					}
					retryWait(++retry_n);
				}
				else if (not_found && bitmap_present == BitmapInfo::Present)
				{
					if (online_file != nullptr)
					{
						std::string reset_retries = readCacheFile("clouddrive_reset_retries");

						size_t retry_max = 12;
						if (!trim(reset_retries).empty())
						{
							retry_max = watoi(trim(reset_retries));

							if (retry_max < 2)
								retry_max = 2;
						}

						bool missing_file = cacheFileExists("missing_" + hex(key));
						if (retry_n > retry_max - 2
							|| missing_file)
						{
							addSystemEvent("kv_store_critical",
								"Error getting object",
								"Error getting online file " + hex(key) + " (not found). Assuming file was deleted.",
								LL_ERROR + 1);
							Server->Log("CRITICAL: Error getting online file " + hex(key) + " (not found). Assuming file was deleted. missing_file=" + convert(missing_file), LL_ERROR);
							not_found = false;
							if (!missing_file
								&& read_only)
							{
								writeToCacheFile("", "missing_" + hex(key));
								std::scoped_lock lock(cache_mutex);
								missing_items.insert(key);
							}
							break;
						}
						else
						{
							if (memfile == nullptr)
							{
								std::string fname = cachefs->filePath(online_file);
								Server->destroy(online_file);
								cachefs->deleteFile(fname);
							}
							online_file = nullptr;
						}
					}
					if (retry_n + 1 > 5)
					{
						Server->Log("Error getting online file " + hex(key) + " (not found). Retrying... Try " + convert(retry_n), LL_ERROR);
					}
					else
					{
						Server->Log("Error getting online file " + hex(key) + " (not found). Retrying...", LL_WARNING);
					}
					retryWait(++retry_n);
				}

			} while (online_file == nullptr);

			if (read_random
				&& online_file != nullptr
				&& memfile == nullptr)
			{
				set_read_random(online_file);
			}

			std::unique_lock sub_lock(submission_mutex);
			std::set<std::string>::iterator it_dirty = dirty_evicted_items.find(key);
			if (it_dirty == dirty_evicted_items.end()
				&& bitmap_present == BitmapInfo::Present)
			{
				sub_lock.unlock();

				if (with_prev_link
					&& memfile == nullptr)
				{
					std::unique_ptr<IFsFile> base_file(cachefs->openFile(keypath2(key, basetrans), MODE_READ));
					if (base_file.get() == nullptr)
					{
						online_file->Sync();

						std::string path = keypath2(key, basetrans);
						if (!cachefs->directoryExists(ExtractFilePath(path)))
						{
							if (!cachefs->createDir(ExtractFilePath(path))
								&& !cachefs->directoryExists(ExtractFilePath(path)))
							{
								Server->Log("Error creating directory " + path + " for reflink-file with key " + hexpath(key) + ". " + os_last_error_str(), LL_ERROR);
								addSystemEvent("cache_err_fatal",
									"Error creating directory for reflink-file on cache",
									"Error creating directory " + path + " for reflink-file with key " + hexpath(key) + ". " + os_last_error_str(), LL_ERROR);
								abort();
							}
						}

						sync_link(cachefs, keypath2(key, transid), path);
					}
				}
			}
			else if (it_dirty != dirty_evicted_items.end())
			{
				evicted_dirty = true;
				dirty_evicted_items.erase(it_dirty);

				sub_lock.unlock();
			}
			else
			{
				sub_lock.unlock();
			}

			curr_cache_val = cache_val(key, !read_only || (compressed != nullptr && compressed->dirty) || evicted_dirty);
		}
		else if (!decompressed_ok)
		{
			std::string path = keypath2(key, transid);
			wait_for_del_file(path);
			IFsFile* memfile = nullptr;
			if (!(flags & Flag::disable_memfiles))
			{
				memfile = get_mem_file(key, size_hint, read_only);
			}
			RetrievalOperationUnlockOnly retrieval_operation(cache_lock);

			not_found = true;

			size_t retry_n = 0;
			do
			{
				if (!cachefs->directoryExists(ExtractFilePath(path)))
				{
					if (!cachefs->createDir(ExtractFilePath(path)))
					{
						std::string syserr = os_last_error_str();
						Server->Log("Error creating directory " + path + " for GET-file with key " + hexpath(key) + ". " + syserr, LL_ERROR);
						addSystemEvent("cache_err_fatal",
							"Error creating directory for GET-file on cache",
							"Error creating directory " + path + " for GET-file with key " + hexpath(key) + ". " + syserr, LL_ERROR);
						abort();
					}
				}

				if (memfile != nullptr)
				{
					online_file = memfile;
					cache_fd = false;
				}
				else
				{
					online_file = cachefs->openFile(path, CREATE_DIRECT);

					if (online_file == nullptr)
					{
						Server->Log("Error creating file. Retrying...", LL_WARNING);
						retryWait(++retry_n);
					}
					FILE_SIZE_CACHE(online_file->setCachedSize(0));

					set_cache_file_compression(key, path);
				}

			} while (online_file == nullptr);

			if (read_random
				&& online_file != nullptr
				&& memfile == nullptr)
			{
				set_read_random(online_file);
			}

			curr_cache_val = cache_val(key, !read_only || (compressed != nullptr && compressed->dirty) || evicted_dirty);
		}

		if (read_only && not_found)
		{
			Server->Log("Not found and read only: " + keypath2(key, transid), LL_WARNING);
			if (dynamic_cast<IMemFile*>(online_file) == nullptr)
			{
#ifndef NDEBUG
				{
					std::scoped_lock memfile_lock(memfiles_mutex);
					SMemFile* f = memfiles.get(std::make_pair(transid, key), false);
					assert(f == nullptr);
				}
#endif
				Server->destroy(online_file);
				cachefs->deleteFile(keypath2(key, transid));
			}
			else
			{
#ifndef NDEBUG
				{
					std::scoped_lock memfile_lock(memfiles_mutex);
					SMemFile* f = memfiles.get(std::make_pair(transid, key), false);
					assert(f != nullptr);
				}
#endif
				rm_mem_file(nullptr, transid, key, false);
			}
			return nullptr;
		}
		else if (!decompressed_ok && (!read_only || evicted_dirty))
		{
			addDirtyItem(transid, key);
			add_dirty_bytes(transid, key, online_file->Size());
			remove_missing(key);
		}
		else if (decompressed_ok && !read_only && !compressed->dirty)
		{
			addDirtyItem(transid, key);
			add_dirty_bytes(transid, key, online_file->Size());
			remove_missing(key);
		}

		cache_put(lru_cache, key, curr_cache_val, cache_lock);

		nf = online_file;

		open_files[key] = SFdKey(nf, nf->Size(), read_only);

		add_cachesize(nf->Size());
	}

	return nf;
}

bool TransactionalKvStore::del( const std::string& key )
{
	assert(fuse_io_context::is_sync_thread());

	std::unique_lock lock(cache_mutex);

	if(queued_dels.find(key)!=queued_dels.end())
	{
		return true;
	}

	Server->Log("Del key="+hex(key)+" transid="+convert(transid), LL_INFO);

	while (open_files.find(key) != open_files.end() || wait_for_retrieval(lock, key))
	{
		lock.unlock();
		Server->wait(1);
		lock.lock();
	}

	IFsFile* fd = nullptr;
	bool is_memfile = false;

	SFdKey* res = fd_cache.get(key);
	if(res!=nullptr)
	{
		if (cachefs->filePath(res->fd) != keypath2(key, transid))
		{
			Server->Log("Fd cache path wrong. Got " + cachefs->filePath(res->fd) + " Expected " + keypath2(key, transid), LL_ERROR);
			abort();
		}

		fd = res->fd;
		fd_cache.del(key);
	}
	else
	{
		SMemFile* memfile_nf;
		{
			std::scoped_lock memfile_lock(memfiles_mutex);
			memfile_nf = memfiles.get(std::make_pair(transid, key), false);
		}
		if (memfile_nf != nullptr)
		{
			if (memfile_nf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
				abort();
			}

			fd = memfile_nf->file.get();
			is_memfile = true;
		}
		else
		{
			RetrievalOperation retrieval_operation(lock, *this, key);

			fd = cachefs->openFile(keypath2(key, transid), MODE_RW);
		}
	}

	bool accounted_for=false;

	SCacheVal* dirty = cache_get(lru_cache, key, lock, false);
	SCacheVal* compressed = nullptr;
	if(dirty!=nullptr)
	{
		assert(fd != nullptr);
		if(dirty->dirty)
		{
			removeDirtyItem(transid, key);
			if(fd!=nullptr)
			{
				rm_dirty_bytes(transid, key, fd->Size(), true);
				sub_cachesize(fd->Size());
			}
			accounted_for=true;
		}

		cache_del(lru_cache, key, lock);
	}
	else
	{
		compressed = cache_get(compressed_items, key, lock, false);
		if (compressed != nullptr)
		{
			assert(fd == nullptr);
			{
				RetrievalOperation retrieval_operation(lock, *this, key);

				fd = cachefs->openFile(keypath2(key, transid) + ".comp", MODE_RW);
				assert(fd != nullptr);
			}

			if (compressed->dirty)
			{
				removeDirtyItem(transid, key);
				if (fd != nullptr)
				{
					rm_dirty_bytes(transid, key, fd->Size(), true);
					sub_cachesize(fd->Size());
				}
				accounted_for = true;
			}

			cache_del(compressed_items, key, lock);
			
			comp_bytes -= fd->Size();
		}
	}

	bool delete_it = true;
	{
		std::scoped_lock lock_evict(submission_mutex);
		auto it = submission_items.find(std::make_pair(transid, key));
		if (it != submission_items.end()
			&& (it->second->action == SubmissionAction_Evict 
				|| it->second->action == SubmissionAction_Compress
				|| it->second->action == SubmissionAction_Working_Compress ))
		{
			if(it->second->action == SubmissionAction_Evict)
				submitted_bytes -= (*it->second).size;

			if (!accounted_for
				&& (it->second->action == SubmissionAction_Evict || it->second->compressed))
			{
				rm_dirty_bytes(transid, key, it->second->size, true);
				sub_cachesize(it->second->size);
				removeDirtyItem(transid, key);
				accounted_for = true;
			}
			else if (!accounted_for)
			{
				sub_cachesize(it->second->size);
				accounted_for = true;
			}

			if (it->second->action == SubmissionAction_Working_Compress)
			{
				Server->Log("Don't finish compression key=" + hex(key) + " transid=" + convert(transid), LL_INFO);
				it->second->finish = false;
			}
			else
			{
				rm_submission_item(it);
			}
		}
		else if (it != submission_items.end()
			&& (it->second->action == SubmissionAction_Working_Evict))
		{
			accounted_for = true;
			delete_it = false;
		}
	}

	if (!accounted_for
		&& fd!=nullptr)
	{
		sub_cachesize(fd->Size());
	}

	if (!is_memfile
		&& fd!=nullptr)
	{
		assert(dynamic_cast<IMemFile*>(fd) == nullptr);
		Server->destroy(fd);
	}

	if (delete_it)
	{
		delete_item(nullptr, key, compressed != nullptr, lock,
			0, 0, DeleteImm::None, allow_evict ? 0 : transid);
	}

	queued_dels.insert(key);

	if (delete_it)
	{
		lock.unlock();

		run_del_file_queue();
	}

	return true;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> TransactionalKvStore::del_async(fuse_io_context& io, const std::string key)
{
	assert(fuse_io_context::is_async_thread());

	std::unique_lock lock(cache_mutex);

	if (queued_dels.find(key) != queued_dels.end())
	{
		co_return true;
	}

	Server->Log("Del key=" + hex(key) + " transid=" + convert(transid), LL_INFO);

	bool retry;
	do
	{
		retry = false;

		while (open_files.find(key) != open_files.end())
		{
			retry = true;

			lock.unlock();

			co_await io.sleep_ms(10);

			lock.lock();
		}

		auto p = co_await wait_for_retrieval_async(io, std::move(lock), key);
		lock = std::move(p.lock);

		if (p.waited)
			retry = true;
	} while (retry);

	IFsFile* fd = NULL;
	bool is_memfile = false;

	bool has_initiate_retrieval = false;
	SFdKey* res = fd_cache.get(key);
	if (res != NULL)
	{
		if (cachefs->filePath(res->fd) != keypath2(key, transid))
		{
			Server->Log("Fd cache path wrong. Got " + cachefs->filePath(res->fd)
				+ " Expected " + keypath2(key, transid), LL_ERROR);
			abort();
		}

		fd = res->fd;
		fd_cache.del(key);
	}
	else
	{
		SMemFile* memfile_nf;
		{
			std::scoped_lock memfile_lock(memfiles_mutex);
			memfile_nf = memfiles.get(std::make_pair(transid, key), false);
		}
		if (memfile_nf != nullptr)
		{
			if (memfile_nf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
				abort();
			}

			fd = memfile_nf->file.get();
			is_memfile = true;
		}
		else
		{
			if (!has_initiate_retrieval)
			{
				has_initiate_retrieval = true;
				initiate_retrieval(key);
			}
			
			RetrievalOperationUnlockOnly retrieval_operation(lock);

			fd = co_await cachefs->openFileAsync(io, keypath2(key, transid), MODE_RW);
		}
	}

	bool accounted_for = false;

	SCacheVal* dirty = cache_get(lru_cache, key, lock, false);
	SCacheVal* compressed = NULL;
	if (dirty != NULL)
	{
		assert(fd != NULL);
		if (dirty->dirty)
		{
			removeDirtyItem(transid, key);
			if (fd != NULL)
			{
				rm_dirty_bytes(transid, key, fd->Size(), true);
				sub_cachesize(fd->Size());
			}
			accounted_for = true;
		}

		cache_del(lru_cache, key, lock);
	}
	else
	{
		compressed = cache_get(compressed_items, key, lock, false);
		if (compressed != NULL)
		{
			assert(fd == NULL);
			{
				if (!has_initiate_retrieval)
				{
					has_initiate_retrieval = true;
					initiate_retrieval(key);
				}

				RetrievalOperationUnlockOnly retrieval_operation(lock);

				fd = co_await cachefs->openFileAsync(io, keypath2(key, transid) + ".comp", MODE_RW);
				assert(fd != NULL);
			}

			if (compressed->dirty)
			{
				removeDirtyItem(transid, key);
				if (fd != NULL)
				{
					rm_dirty_bytes(transid, key, fd->Size(), true);
					sub_cachesize(fd->Size());
				}
				accounted_for = true;
			}

			cache_del(compressed_items, key, lock);

			comp_bytes -= fd->Size();
		}
	}

	bool delete_it = true;
	{
		std::scoped_lock lock_evict(submission_mutex);
		auto it = submission_items.find(std::make_pair(transid, key));
		if (it != submission_items.end()
			&& (it->second->action == SubmissionAction_Evict
				|| it->second->action == SubmissionAction_Compress
				|| it->second->action == SubmissionAction_Working_Compress))
		{
			if (it->second->action == SubmissionAction_Evict)
				submitted_bytes -= (*it->second).size;

			if (!accounted_for
				&& (it->second->action == SubmissionAction_Evict || it->second->compressed))
			{
				rm_dirty_bytes(transid, key, it->second->size, true);
				sub_cachesize(it->second->size);
				removeDirtyItem(transid, key);
				accounted_for = true;
			}
			else if (!accounted_for)
			{
				sub_cachesize(it->second->size);
				accounted_for = true;
			}

			if (it->second->action == SubmissionAction_Working_Compress)
			{
				Server->Log("Don't finish compression key=" + hex(key) + " transid=" + convert(transid), LL_INFO);
				it->second->finish = false;
			}
			else
			{
				rm_submission_item(it);
			}
		}
		else if (it != submission_items.end()
			&& (it->second->action == SubmissionAction_Working_Evict))
		{
			accounted_for = true;
			delete_it = false;
		}
	}

	if (!accounted_for
		&& fd != NULL)
	{
		sub_cachesize(fd->Size());
	}

	if (!is_memfile
		&& fd != nullptr)
	{
		assert(dynamic_cast<IMemFile*>(fd) == nullptr);
		cachefs->closeAsync(io, fd);
		Server->destroy(fd);
	}

	if (delete_it)
	{
		delete_item(&io, key, compressed != NULL, lock,
			0, 0, DeleteImm::None, allow_evict ? 0 : transid);
	}

	queued_dels.insert(key);

	if (has_initiate_retrieval)
	{
		finish_retrieval_async(io, std::move(lock), key);
		assert(!cache_mutex.has_lock());
	}
	else
	{
		lock.unlock();
	}

	if (delete_it)
	{
		co_await run_del_file_queue_async(io);
	}

	co_return true;
}
#endif

bool TransactionalKvStore::set_second_chances(const std::string & key, unsigned int chances)
{
	if (chances >= 128)
		chances = 127;

	std::unique_lock lock(cache_mutex);

	SCacheVal* cval = cache_get(lru_cache, key, lock, false);

	if (cval != nullptr)
	{
		cval->chances = chances;
		return true;
	}

	cval = cache_get(compressed_items, key, lock, false);

	if (cval != nullptr)
	{
		cval->chances = chances;
		return true;
	}

	return false;
}

bool TransactionalKvStore::has_preload_once(const std::string & key)
{
	std::scoped_lock lock(cache_mutex);

	auto it = preload_once_items.find(key);
	return it != preload_once_items.end();
}

bool TransactionalKvStore::has_item_cached(const std::string & key)
{
	std::unique_lock lock(cache_mutex);

	auto it = preload_once_items.find(key);
	if (it != preload_once_items.end())
		return true;

	if (open_files.find(key) != open_files.end())
		return true;

	SCacheVal* dirty = cache_get(lru_cache, key, lock);
	return dirty!=nullptr;
}

void TransactionalKvStore::remove_preload_items(int preload_tag)
{
	std::scoped_lock lock(cache_mutex);
	for(auto it=preload_once_items.begin();it!=preload_once_items.end();)
	{
		if (it->second == preload_tag
			|| preload_tag==0)
		{
			Server->Log("Preload (1) removal of " + hex(it->first));
			auto it_curr = it;
			++it;
			preload_once_items.erase(it_curr);
		}
		else
		{
			++it;
		}
	}
}

void TransactionalKvStore::dirty_all()
{
	bool retry = true;
	while (retry)
	{
		retry = false;
		std::unique_lock lock(cache_mutex);
		for (std::pair<const std::string*, SCacheVal>& item : lru_cache.get_list())
		{
			if (!item.second.dirty)
			{
				int64 fsize = 0;
				std::string key = *item.first;
				std::map<std::string, SFdKey>::iterator it_open_file = open_files.find(key);

				SFdKey* res;
				if (it_open_file != open_files.end())
				{
					if (dynamic_cast<IMemFile*>(it_open_file->second.fd) != nullptr)
					{
						SMemFile* memfile_nf;
						{
							std::scoped_lock memfile_lock(memfiles_mutex);
							memfile_nf = memfiles.get(std::make_pair(transid, key), false);
						}
						assert(memfile_nf != nullptr);
						if (memfile_nf->cow)
						{
							if (!cow_mem_file(memfile_nf, true))
							{
								retry = true;
								break;
							}
							it_open_file->second.fd = memfile_nf->file.get();
						}
					}
					fsize = it_open_file->second.fd->Size();
					it_open_file->second.read_only = false;
				}
				else if ((res = fd_cache.get(key, false)) != nullptr)
				{
					fsize = res->fd->Size();
					res->read_only = false;
				}
				else
				{
					SMemFile* memfile_nf;
					{
						std::scoped_lock memfile_lock(memfiles_mutex);
						memfile_nf = memfiles.get(std::make_pair(transid, key), false);
					}
					if (memfile_nf != nullptr)
					{
						if (memfile_nf->cow)
						{
							if (!cow_mem_file(memfile_nf, false))
							{
								retry = true;
								break;
							}
						}
						fsize = memfile_nf->file->Size();
					}
					else
					{
						std::unique_ptr<IFile> nf(cachefs->openFile(keypath2(key, transid), OPEN_DIRECT));

						if (nf != nullptr)
						{
							fsize = nf->Size();
						}
					}
				}

				item.second.dirty = true;
				std::set<std::string>::iterator it_untouched = nosubmit_untouched_items.find(key);
				if (it_untouched != nosubmit_untouched_items.end())
				{
					nosubmit_untouched_items.erase(it_untouched);
				}

				addDirtyItem(transid, key);

				remove_missing(key);

				add_dirty_bytes(transid, key, fsize);
			}
		}

		if (retry)
		{
			lock.unlock();
			Server->wait(1000);
		}
	}
}


TransactionalKvStore::TransactionalKvStore(IBackupFileSystem* cachefs, int64 min_cachesize, int64 min_free_size, int64 critical_free_size,
	int64 throttle_free_size, int64 min_metadata_cache_free, float comp_percent, int64 comp_start_limit, IOnlineKvStore* online_kv_store,
	const std::string& encryption_key, ICompressEncryptFactory* compress_encrypt_factory,
	bool verify_cache, float cpu_multiplier, size_t no_compress_mult, bool with_prev_link,
	bool allow_evict, bool with_submitted_files, float resubmit_compressed_ratio, int64 max_memfile_size, 
	std::string memcache_path, float memory_usage_factor,
	bool only_memfiles, unsigned int background_comp_method,
	unsigned int cache_comp, unsigned int meta_cache_comp)
	: min_cachesize(min_cachesize), min_free_size(min_free_size), critical_free_size(critical_free_size),
	comp_percent(comp_percent), comp_start_limit(comp_start_limit), throttle_free_size(throttle_free_size),
	do_stop(false),
	online_kv_store(online_kv_store),
	submitted_bytes(0), dirty_bytes(0), compress_encrypt_factory(compress_encrypt_factory),
	encryption_key(encryption_key), comp_bytes(0), curr_submit_compress_evict(true),
	remaining_gets(std::string::npos), unthrottled_gets(0), unthrottled_gets_avg(0),
	do_evict(false), do_evict_starttime(0),
	submit_bundle_starttime(0), del_file_mutex(Server->createMutex()),
	del_file_single_mutex(Server->createMutex()),
	curr_submit_bundle_items(&submit_bundle_items_a), other_submit_bundle_items(&submit_bundle_items_b),
	with_prev_link(with_prev_link), allow_evict(allow_evict),
	with_submitted_files(with_submitted_files),
	regular_submit_bundle_thread(this), throttle_thread(this),
	evicted_mutex(Server->createMutex()), prio_del_file_cond(Server->createCondition()),
	resubmit_compressed_ratio(resubmit_compressed_ratio), max_memfile_size(max_memfile_size),
	memcache_path(memcache_path), memfile_size(0), submitted_memfiles(0), submitted_memfile_size(0),
	submission_queue_memfile_first(submission_queue.end()),
	fd_cache_size(1000), submit_bundle_item_mutex(Server->createMutex()),
	evict_non_dirty_memfiles(false), total_hits(0), total_memory_hits(0), total_cache_miss_backend(0), total_cache_miss_decompress(0),
	total_dirty_ops(0), total_put_ops(0), metadata_cache_free(-1), min_metadata_cache_free(min_metadata_cache_free),
	metadata_update_thread(this), compression_starttime(0),
	num_second_chances_cb(nullptr), only_memfiles(only_memfiles),
	max_cachesize(LLONG_MAX), background_comp_method(background_comp_method),
	disable_read_memfiles(false), disable_write_memfiles(false),
	total_submitted_bytes(0),
	retrieval_waiters_async(0),
	retrieval_waiters_sync(0),
	cachefs(cachefs),
	cache_comp(cache_comp), meta_cache_comp(meta_cache_comp)
{
	g_cache_mutex = &cache_mutex;

	cache_lock_file.reset(cachefs->openFile("lock", MODE_RW_CREATE));
	
	if (cache_lock_file.get() == nullptr)
	{
		std::string msg = "Error opening cache lock file at " + cachefs->getName() + "/lock . " + os_last_error_str();
		Server->Log(msg, LL_ERROR);
		throw std::runtime_error(msg);
	}

#ifndef _WIN32
	{
		IFsFile::os_file_handle cache_lock_fd = cache_lock_file->getOsHandle();
		if (cache_lock_fd != IFsFile::os_file_handle())
		{
			int rc = flock(cache_lock_fd, LOCK_EX | LOCK_NB);

			if (rc != 0)
			{
				std::string msg;
				if (errno == EWOULDBLOCK)
				{
					msg = "Error locking cache lock file at " + cachefs->getName() + "/lock . Another cloud drive may already be active using the same location. " + os_last_error_str();
				}
				else
				{
					msg = "Error locking cache lock file at " + cachefs->getName() + "/lock . " + os_last_error_str();
				}
				Server->Log(msg, LL_ERROR);
				throw std::runtime_error(msg);
			}
		}
	}
#endif

	std::string log_level_override = trim(readCacheFile("log_level_override"));
	if (!log_level_override.empty())
	{
		int ll = LL_WARNING;
		if (log_level_override == "debug") ll = LL_DEBUG;
		else if (log_level_override == "info") ll = LL_INFO;
		else if (log_level_override == "error") ll = LL_ERROR;
		else if (log_level_override == "warn") ll = LL_WARNING;
		else ll = watoi(log_level_override);

		Server->setLogLevel(ll);

		std::string log_rotation_files = trim(readCacheFile("log_rotation_files"));
		if (log_rotation_files.empty())
		{
			Server->setLogRotationFiles(100);
		}
		else
		{
			Server->setLogRotationFiles(watoi(log_rotation_files));
		}
	}

	fd_cache_size = (std::max)(static_cast<size_t>(10), static_cast<size_t>(memory_usage_factor*fd_cache_size));

	setMountStatus("{\"state\": \"update_trans\"}");

	update_transactions();

	std::unique_lock lock(cache_mutex);
	int64 maxtrans = set_active_transactions(lock, true);

	setMountStatus("{\"state\": \"start_trans\"}");

	if(maxtrans>=0)
	{
		transid = online_kv_store->new_transaction(true);

		if( transid==0)
		{
			throw std::runtime_error("Error requesting new transaction");
		}

		if(!cachefs->createSnapshot("trans_"+std::to_string(maxtrans),
			"trans_"+std::to_string(transid)))
		{
			Server->Log("Error creating new transaction subvolume", LL_ERROR);
			throw std::runtime_error("Error creating new transaction subvolume");
		}
		else
		{
			update_transactions();
			clean_snapshot(transid);
		}

		basetrans = maxtrans;
	}
	else
	{
		basetrans = online_kv_store->new_transaction(true);

		if( basetrans==0)
		{
			throw std::runtime_error("Error requesting new transaction");
		}

		if(!cachefs->createSubvol("trans_"+std::to_string(basetrans)))
		{
			Server->Log("Error creating initial transaction directory", LL_ERROR);
			throw std::runtime_error("Error creating initial transaction directory");
		}
		update_transactions();

		std::unique_ptr<IFsFile> tmp(cachefs->openFile(basepath2()+ cachefs->fileSep() + "commited", MODE_WRITE));

		transid = online_kv_store->new_transaction(true);

		if( transid==0)
		{
			throw std::runtime_error("Error requesting new transaction");
		}

		if(!cachefs->createSnapshot("trans_"+std::to_string(basetrans),
			"trans_"+std::to_string(transid)))
		{
			Server->Log("Error creating initial transaction directory -2", LL_ERROR);
			throw std::runtime_error("Error creating initial transaction directory -2");
		}
		else
		{
			update_transactions();
			clean_snapshot(transid);
		}		
	}

	cleanup(true);

	setMountStatus("{\"state\": \"enum_cache\"}");

	cachesize=0;
	read_keys(lock, transpath2(), verify_cache);
	read_missing();

	while(!read_dirty_items(lock, basetrans, transid))
	{
		Server->Log("Error reading dirty items. Retrying...", LL_ERROR);
		Server->wait(10000);
	}

	threads.push_back(Server->getThreadPool()->execute(this, "evict worker"));
	threads.push_back(Server->getThreadPool()->execute(&regular_submit_bundle_thread, "regular submit"));
	threads.push_back(Server->getThreadPool()->execute(&throttle_thread, "get throttle"));
	threads.push_back(Server->getThreadPool()->execute(&metadata_update_thread, "meta update"));
	threads.push_back(Server->getThreadPool()->execute(&memfd_del_thread, "memfd del"));

	size_t num_cpus = static_cast<size_t>(os_get_num_cpus()*cpu_multiplier+0.5f);

	if(num_cpus>10000 || num_cpus<1)
	{
		num_cpus = 1;
	}

	for(size_t i=0;i<num_cpus;++i)
	{
		threads.push_back(Server->getThreadPool()->execute(new TransactionalKvStore::SubmitWorker(*this, false, false), "submit worker"));
	}

	for (size_t j = 0; j < no_compress_mult; ++j)
	{
		for (size_t i = 0; i < num_cpus; ++i)
		{
			threads.push_back(Server->getThreadPool()->execute(new TransactionalKvStore::SubmitWorker(*this, true, j==0 && i==0), "+submit worker"));
		}
	}

	evict_queue_depth = num_cpus + no_compress_mult*num_cpus + 20;
	compress_queue_depth = num_cpus + 10;
}

std::string TransactionalKvStore::hex( const std::string& key )
{
	return bytesToHex(reinterpret_cast<const unsigned char*>(key.c_str()), key.size());
}

std::string TransactionalKvStore::hexpath( const std::string& key )
{
	std::string hexkey = bytesToHex(reinterpret_cast<const unsigned char*>(key.c_str()), key.size());

	if(hexkey.size()<=4)
	{
		return "s"+cachefs->fileSep() + hexkey;
	}

	return hexkey.substr(0, 4) + cachefs->fileSep() + hexkey;
}

std::string TransactionalKvStore::transpath2()
{
	return "trans_" + convert(transid);
}

void TransactionalKvStore::operator()()
{
	int64 total_cache_size = cachefs->totalSpace();

	if (critical_free_size > total_cache_size)
	{
		critical_free_size = total_cache_size - 300LL * 1024 * 1024;
	}

	if (throttle_free_size > total_cache_size)
	{
		throttle_free_size = total_cache_size - 500LL * 1024 * 1024;
	}

	size_t last_n_eviction = 0;
	size_t last_n_compression = 0;
	size_t last_n_chances = 0;
	bool last_evict_stop_prem = false;
	bool last_compress_stop_prem = false;
	bool has_del_files = false;
	bool last_only_chances = true;
	
	while(true)
	{
		if (has_del_files)
		{
			has_del_files = false;
			run_del_file_queue();
		}

		Server->wait(1000);

		int64 orig_free_space = cache_free_space();
		int64 free_space = orig_free_space;

		

		std::unique_lock lock(cache_mutex);

		if(do_stop)
		{
			return;
		}

		if(!preload_once_delayed_removal.empty())
		{
			int64 ctime = Server->getTimeMS();
			for (auto it =preload_once_delayed_removal.begin();it!=preload_once_delayed_removal.end();)
			{
				if (it->second < ctime)
				{
					auto it_rm = preload_once_items.find(it->first);
					if (it_rm != preload_once_items.end())
					{
						Server->Log("Preload (2) removal of " + hex(it_rm->first));
						preload_once_items.erase(it_rm);
					}

					auto it_curr = it;
					++it;
					preload_once_delayed_removal.erase(it_curr);
				}
				else
					++it;
			}
		}

		if (curr_submit_compress_evict)
		{
			if (evict_non_dirty_memfiles)
			{
				evict_non_dirty_memfiles = false;

				if (evict_memfiles(lock, false))
				{
					has_del_files = true;
				}
			}

			if (evict_memfiles(lock, true))
			{
				has_del_files = true;
			}
		}

		do_evict = false;
		
		bool evicting = false;
		bool do_start_compress = true;
		size_t curr_evict_n = 0;
		size_t curr_last_n_eviction = last_n_eviction;
		size_t curr_last_n_compression = last_n_compression;
		size_t curr_last_n_chances = last_n_chances;
		bool curr_last_evict_stop_prem = last_evict_stop_prem;
		bool curr_last_compress_stop_prem = last_compress_stop_prem;
		bool curr_last_only_chances = last_only_chances;
		last_n_eviction = 0;
		last_n_compression = 0;
		last_n_chances = 0;
		last_evict_stop_prem = false;
		last_compress_stop_prem = false;
		last_only_chances = true;
		bool curr_doubled = false;
		int64 freed_space = 0;

		bool evict_use_chances = true;
		if (curr_last_evict_stop_prem &&
			curr_last_n_chances>0 &&
			curr_last_only_chances)
		{
			evict_use_chances = false;
		}

		common::lrucache<std::string, SCacheVal>* evict_target_cache = &lru_cache;

		if (comp_bytes>0)
		{
			evict_target_cache = &compressed_items;
		}

		std::list<std::pair<std::string const *, SCacheVal> >::iterator evict_it;

		int64 orig_cachesize = cachesize;
		int64 curr_cachesize = orig_cachesize;

		int64 max_cachesize_lower = (std::max)(min_cachesize, max_cachesize - max_cachesize_throttle_size*2);

		while( allow_evict && 
			( (free_space<min_free_size
			&& cachesize>=min_cachesize) 
			|| free_space<critical_free_size
			|| free_space<throttle_free_size
			|| curr_cachesize>max_cachesize_lower)
			   && curr_submit_compress_evict )
		{
			if (!do_evict)
			{
				do_evict = true;
				evict_it = cache_eviction_iterator_start(*evict_target_cache, lock);

				if (evict_it == cache_eviction_iterator_finish(*evict_target_cache, lock))
				{
					break;
				}

				--evict_it;
			}

			size_t n_evicting = 0;

			{
				std::scoped_lock submission_lock(submission_mutex);

				do_evict_starttime = Server->getTimeMS();

				for (std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator >::iterator it = submission_items.begin();
					it != submission_items.end(); ++it)
				{
					if (it->second->action == SubmissionAction_Evict
						|| it->second->action == SubmissionAction_Dirty)
					{
						++n_evicting;

						if (n_evicting > evict_queue_depth)
						{
							do_start_compress = false;
							break;
						}
					}
				}
			}

			if (n_evicting == 0
				&& curr_last_n_eviction>0
				&& !curr_last_evict_stop_prem
				&& evict_queue_depth<1000000
				&& !curr_doubled)
			{
				Server->Log("Doubling eviction queue depth from " + convert(evict_queue_depth) + " to " + convert(evict_queue_depth * 2), LL_WARNING);
				evict_queue_depth *= 2;
				curr_doubled = true;
			}
			
			std::vector<std::list<std::pair<std::string const *, SCacheVal> >::iterator> cache_move_front;
			bool used_chance;
			if(n_evicting<evict_queue_depth)
			{
				Server->Log("Starting eviction of cache items... "
					"free_space="+PrettyPrintBytes(free_space)
					+" min_free_size="+PrettyPrintBytes(min_free_size)
					+" cachesize="+PrettyPrintBytes(cachesize)
					+" min_cachesize="+PrettyPrintBytes(min_cachesize)
					+(max_cachesize!=LLONG_MAX ? (" max_cachesize="+PrettyPrintBytes(max_cachesize)+" max_cachesize_lower="+PrettyPrintBytes(max_cachesize_lower)) : "" )
					+" freed_space="+PrettyPrintBytes(freed_space), LL_INFO);

				
				if(!evict_one(lock, false, false, 
					evict_it, *evict_target_cache, evict_use_chances, freed_space,
					has_del_files, cache_move_front, used_chance))
				{
					evict_move_front(*evict_target_cache, cache_move_front);
					last_evict_stop_prem = true;
					break;
				}
				else if (!used_chance)
				{
					last_only_chances = false;

					++last_n_eviction;
					evicting = true;
				}
				else
				{
					++last_n_chances;
				}
			}
			else
			{
				if (!evict_one(lock, true, true,
					evict_it, *evict_target_cache, evict_use_chances, freed_space,
					has_del_files, cache_move_front, used_chance))
				{
					evict_move_front(*evict_target_cache, cache_move_front);
					last_evict_stop_prem = true;
					break;
				}
				else if (!used_chance)
				{
					last_only_chances = false;
					evicting = true;
				}
				else
				{
					++last_n_chances;
				}
			}

			evict_move_front(*evict_target_cache, cache_move_front);

			free_space = orig_free_space - freed_space;
			curr_cachesize = orig_cachesize - freed_space;

			if (evicting)
			{
				++curr_evict_n;
				if (curr_evict_n>2000)
				{
					last_evict_stop_prem = true;
					break;
				}
			}
		}

		if (do_evict && free_space >= min_free_size
			&& free_space >= critical_free_size
			&& free_space >= throttle_free_size
			&& cachesize <= max_cachesize_lower)
		{
			do_evict = false;
		}

		float curr_comp_pc = ((float)comp_bytes)/cachesize;

		if( !evicting && do_start_compress 
			&& comp_percent>0
			&& curr_comp_pc<comp_percent
			&& cachesize>comp_start_limit
			&& curr_submit_compress_evict
			&& free_space>critical_free_size
			&& compression_starttime<=Server->getTimeMS())
		{
			std::scoped_lock submission_lock(submission_mutex);

			bool curr_submitting=false;

			for(std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator >::iterator it=submission_items.begin();
				it!=submission_items.end();++it)
			{
				if(it->second->action!=SubmissionAction_Compress &&
					it->second->action!=SubmissionAction_Working_Compress)
				{
					curr_submitting=true;
					break;
				}
			}

			if(!curr_submitting)
			{
				size_t n_compressing=0;
				for(std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator >::iterator it=submission_items.begin();
					it!=submission_items.end();++it)
				{
					if(it->second->action==SubmissionAction_Compress )
					{
						++n_compressing;
					}
				}

				if (n_compressing == 0
					&& curr_last_n_compression>0
					&& !curr_last_compress_stop_prem
					&& compress_queue_depth<1000000
					&& !curr_doubled)
				{
					Server->Log("Doubling compression queue depth from " + convert(compress_queue_depth) + " to " + convert(compress_queue_depth * 2), LL_WARNING);
					compress_queue_depth *= 2;
					curr_doubled = true;
				}

				std::list<std::pair<std::string const *, SCacheVal> >::iterator
					compress_it = cache_eviction_iterator_start(lru_cache, lock);

				if (compress_it != cache_eviction_iterator_finish(lru_cache, lock))
				{
					--compress_it;
					for (; n_compressing < compress_queue_depth; ++n_compressing)
					{
						Server->Log("Starting compression of cache items... curr_comp_pc=" + convert(curr_comp_pc) + " comp_percent=" + convert(comp_percent) + " cachesize=" + PrettyPrintBytes(cachesize) + " comp_start_limit=" + PrettyPrintBytes(comp_start_limit), LL_INFO);

						if (!compress_one(lock, compress_it))
						{
							last_compress_stop_prem = true;
							break;
						}
						else
						{
							++last_n_compression;
						}
					}
				}
			}			
		}
	}
}

bool TransactionalKvStore::read_dirty_items(std::unique_lock<cache_mutex_t>& cache_lock, int64 transaction_id, int64 attibute_to_trans_id)
{
	std::unique_ptr<IFsFile> nosubmit(cachefs->openFile("trans_"+convert(transaction_id)+ cachefs->fileSep() + "dirty.nosubmit", MODE_READ));
	
	if(nosubmit.get())
	{
		Server->Log("Transaction "+convert(attibute_to_trans_id)+" basetrans "+convert(transaction_id)+
			" was not submitted. Marking items as dirty...", LL_INFO);
		
		bool ret = read_from_deleted_file("trans_"+convert(transaction_id)+ cachefs->fileSep() + "deleted",
			attibute_to_trans_id, false);

		ret = ret && read_from_dirty_file(cache_lock, "trans_"+convert(transaction_id)+cachefs->fileSep() + "dirty",
					    attibute_to_trans_id, false, transaction_id);
						
		return ret;
	}
	
	return true;
}

void TransactionalKvStore::release( const std::string& key )
{
	std::unique_lock lock(cache_mutex);

	std::map<std::string, SFdKey>::iterator it = open_files.find(key);

	if (it->first != key)
	{
		abort();
	}

	if (cachefs->filePath(it->second.fd) != keypath2(key, transid)
		&& it->second.fd->getFilename() != key)
	{
		Server->Log("Unexpected fn in release. Expected " + keypath2(key, transid) + " Got " + cachefs->filePath(it->second.fd), LL_ERROR);
		abort();
	}

	if (dynamic_cast<IMemFile*>(it->second.fd) == nullptr)
	{
		if ((cachefs->getFileType(cachefs->filePath(it->second.fd)) & EFileType_File) == 0)
		{
			Server->Log("File does not exist (was deleted) in ::release: " + cachefs->filePath(it->second.fd), LL_ERROR);
			abort();
		}
#ifndef NDEBUG
		std::scoped_lock memfile_lock(memfiles_mutex);
		assert(memfiles.get(std::make_pair(transid, key)) == nullptr);
#endif
	}
	else
	{
		wait_for_del_file(keypath2(it->second.fd->getFilename(), transid));
		if ((os_get_file_type(keypath2(it->second.fd->getFilename(), transid)) & EFileType_File) != 0)
		{
			Server->Log("File does does exist for memfile in ::release: " + keypath2(it->second.fd->getFilename(), transid), LL_ERROR);
			abort();
		}
#ifndef NDEBUG
		std::scoped_lock memfile_lock(memfiles_mutex);
		assert(memfiles.get(std::make_pair(transid, key)) != nullptr);
#endif
	}

	assert(it!=open_files.end());

	--it->second.refcount;

	if(it->second.refcount>0)
	{
		return;
	}

	int64 fsize = it->second.fd->Size();

	if (fsize != it->second.size)
	{
		add_dirty_bytes(transid, key, fsize - it->second.size);
		add_cachesize(fsize - it->second.size);
	}


	std::map<IFsFile*, ReadOnlyFileWrapper*>::iterator it_rdonly = read_only_open_files.find(it->second.fd);
	if(it_rdonly!=read_only_open_files.end())
	{
		if (it_rdonly->first != it->second.fd)
			abort();

		Server->destroy(it_rdonly->second);

		read_only_open_files.erase(it_rdonly);
	}


	IFsFile* close_fd = nullptr;
	std::shared_ptr<IFsFile> close_shared_fd;

	if(!fd_cache.has_key(key))
	{
		SMemFile* memfile_nf;
		{
			std::scoped_lock memfile_lock(memfiles_mutex);
			memfile_nf = memfiles.get(std::make_pair(transid, key), false);
		}

		if (memfile_nf != nullptr
			&& memfile_nf->file.get() != it->second.fd)
		{
			Server->Log("Memfile fd differs from open_files fd " + it->second.fd->getFilename(), LL_WARNING);
		}

		if (memfile_nf==nullptr
			|| (memfile_nf->file.get() != it->second.fd &&
				memfile_nf->old_file.get()!=it->second.fd) )
		{
			assert(dynamic_cast<IMemFile*>(it->second.fd) == nullptr);
			close_fd = it->second.fd;
		}

		if (memfile_nf != nullptr)
		{
			if (memfile_nf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
				abort();
			}

			memfile_nf->old_file.swap(close_shared_fd);
		}
	}

	open_files.erase(it);

	lock.unlock();

	if (close_fd != nullptr)
	{
		Server->destroy(close_fd);
	}
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<void> TransactionalKvStore::release_async(fuse_io_context& io, const std::string& key)
{
	std::unique_lock lock(cache_mutex);

	std::map<std::string, SFdKey>::iterator it = open_files.find(key);

#ifndef NDEBUG
	if (it == open_files.end())
	{
		Server->Log("Key not found in release_async in open_files. Key " + keypath2(key, transid), LL_ERROR);
		abort();
	}

	if (it->first != key)
	{
		abort();
	}


	if (cachefs->filePath(it->second.fd) != keypath2(key, transid)
		&& it->second.fd->getFilename() != key)
	{
		Server->Log("Unexpected fn in release. Expected " + keypath2(key, transid) +
			" Got " + it->second.fd->getFilename(), LL_ERROR);
		abort();
	}

	if (dynamic_cast<IMemFile*>(it->second.fd) == nullptr)
	{

		std::scoped_lock memfile_lock(memfiles_mutex);
		assert(memfiles.get(std::make_pair(transid, key)) == nullptr);
	}
	else
	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		assert(memfiles.get(std::make_pair(transid, key)) != nullptr);
	}
#endif

	assert(it != open_files.end());

	--it->second.refcount;

	if (it->second.refcount > 0)
	{
		co_return;
	}

	int64 fsize = it->second.fd->Size();

	if (fsize != it->second.size)
	{
		assert(!it->second.read_only);
		add_dirty_bytes(transid, key, fsize - it->second.size);
		add_cachesize(fsize - it->second.size);
	}


	std::map<IFsFile*, ReadOnlyFileWrapper*>::iterator it_rdonly = read_only_open_files.find(it->second.fd);
	if (it_rdonly != read_only_open_files.end())
	{
		if (it_rdonly->first != it->second.fd)
			abort();

		Server->destroy(it_rdonly->second);

		read_only_open_files.erase(it_rdonly);
	}

	IFsFile* close_fd = nullptr;
	std::shared_ptr<IFsFile> close_shared_fd;

	if (!fd_cache.has_key(key))
	{
		SMemFile* memfile_nf;
		{
			std::scoped_lock memfile_lock(memfiles_mutex);
			memfile_nf = memfiles.get(std::make_pair(transid, key), false);
		}

		if (memfile_nf != nullptr
			&& memfile_nf->file.get() != it->second.fd)
		{
			if(memfile_nf->old_file.get() == it->second.fd)
				Server->Log("Memfile fd differs from open_files fd " + it->second.fd->getFilename()+" (is old_file)", LL_WARNING);
			else
				Server->Log("Memfile fd differs from open_files fd " + it->second.fd->getFilename(), LL_WARNING);
		}

		if (memfile_nf == nullptr
			|| (memfile_nf->file.get() != it->second.fd &&
				memfile_nf->old_file.get()!= it->second.fd ) )
		{
			assert(dynamic_cast<IMemFile*>(it->second.fd) == nullptr);
			close_fd = it->second.fd;
		}

		if (memfile_nf != nullptr)
		{
			if (memfile_nf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
				abort();
			}

			memfile_nf->old_file.swap(close_shared_fd);
		}
	}

	open_files.erase(it);

	lock.unlock();

	if (close_fd != nullptr)
	{
		cachefs->closeAsync(io, close_fd);
		Server->destroy(close_fd);
	}

	if (close_shared_fd.get() != nullptr &&
		close_shared_fd.use_count() <= 1)
	{
		cachefs->closeAsync(io, close_shared_fd.get());
	}
}
#endif

void TransactionalKvStore::read_keys(std::unique_lock<cache_mutex_t>& cache_lock, const std::string& tpath, bool verify)
{
	std::vector<SFile> files = cachefs->listFiles(tpath);
	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir)
		{
			read_keys(cache_lock, tpath+ cachefs->fileSep() +files[i].name, verify);
		}
		else
		{
			size_t tmp_pos = files[i].name.find(".tmp");
			if (tmp_pos != std::string::npos
				&& tmp_pos == files[i].name.size() - 4)
			{
				continue;
			}

			bool compressed=false;
			size_t comp_pos = files[i].name.find(".comp");
			if(comp_pos!=std::string::npos
				&& comp_pos==files[i].name.size()-5)
			{
				compressed=true;
				SFile uncomp;
				uncomp.name = files[i].name.substr(0, files[i].name.size()-5);

				if (std::binary_search(files.begin(), files.end(), uncomp))
				{
					//uncompressed file exists
					Server->Log(keypath2(hexToBytes(uncomp.name), transid) + ": Both compressed and uncompressed present. "
						"Deleting compressed file "+ tpath + cachefs->fileSep() + files[i].name);
					if (!cachefs->deleteFile(tpath + cachefs->fileSep() + files[i].name))
					{
						Server->Log("Failed to delete " + tpath + cachefs->fileSep() + files[i].name + ". " + os_last_error_str(), LL_ERROR);
						abort();
					}
					continue;
				}

				files[i].name = uncomp.name;
				comp_bytes += files[i].size;
			}

			add_cachesize(files[i].size);

			std::string key = hexToBytes(files[i].name);
			bool dirty = submission_items.find(std::make_pair(transid, key))!=submission_items.end();
			if(compressed)
			{
				cache_put(compressed_items, key, cache_val(key, dirty), cache_lock);
			}
			else
			{
				cache_put(lru_cache, key, cache_val(key, dirty), cache_lock);
			}
			
			if(dirty)
			{
				addDirtyItem(transid, key, false);
				add_dirty_bytes(transid, key, files[i].size);
			}
			else if(verify)
			{
				Server->Log("Verifying " + keypath2(key, transid), LL_DEBUG);

				if (compressed)
				{
					int64 size_diff;
					int64 src_size;
					if (!decompress_item(key, transid, nullptr, size_diff, src_size, false))
					{
						Server->Log("Decompressing item " + keypath2(key, transid) + " failed /verify", LL_ERROR);
						assert(false);
						throw std::runtime_error("Decompressing item " + keypath2(key, transid) + " failed /verify");
					}
				}

				std::string tmpfn = "verify.file";
				IFsFile* tmpl_file = cachefs->openFile(tmpfn, CREATE_DIRECT);
				if (tmpl_file == nullptr)
				{
					Server->Log("Could not verify.file on cache fs. "+cachefs->lastError(), LL_ERROR);
					assert(false);
					throw std::runtime_error("Could not verify.file on cache fs. " + cachefs->lastError());
				}

				bool not_found = false;
				IFile* online_file = online_kv_store->get(key, transid, false, tmpl_file, true, not_found);

				if (online_file == nullptr)
				{
					Server->destroy(tmpl_file);
					cachefs->deleteFile(tmpfn);
				}

				if (online_file == nullptr || not_found)
				{
					Server->Log("Could not get " + keypath2(key, transid) + " from online store", LL_ERROR);
					assert(false);
					throw std::runtime_error("Could not get " + keypath2(key, transid) + " from online store");
				}

				if (!compare_files(cachefs, keypath2(key, transid), online_file))
				{
					assert(false);
					throw std::runtime_error("Cache file " + keypath2(key, transid) + " differs from online version");
				}

				if (compressed)
				{
					cachefs->deleteFile(keypath2(key, transid));
				}
				Server->destroy(online_file);
				cachefs->deleteFile(tmpfn);
			}
		}
	}
}

void TransactionalKvStore::read_missing()
{
	std::vector<SFile> files = cachefs->listFiles("");
	for (size_t i = 0; i < files.size(); ++i)
	{
		if (!files[i].isdir
			&& next(files[i].name, 0, "missing_"))
		{
			std::string hexkey = getafter("missing_", files[i].name);
			if (IsHex(hexkey))
			{
				missing_items.insert(hexToBytes(hexkey));
			}
		}
	}
}

void TransactionalKvStore::remove_transaction( int64 transid )
{
	Server->Log("Removing transaction " + convert(transid), LL_INFO);

	{
		std::scoped_lock dirty_lock(dirty_item_mutex);
		std::map<int64, std::set<std::string> >::iterator it = nosubmit_dirty_items.find(transid);
		if (it != nosubmit_dirty_items.end())
		{
			nosubmit_dirty_items.erase(it);
		}
	}

	if (!cachefs->deleteSubvol("trans_" + convert(transid)))
	{
		Server->Log("Error removing transaction subvolume", LL_ERROR);
	}

	update_transactions();
}

std::string TransactionalKvStore::basepath2()
{
	return "trans_" + convert(basetrans);
}

bool TransactionalKvStore::evict_one(std::unique_lock<cache_mutex_t>& cache_lock, bool break_on_skip, bool only_non_dirty,
	std::list<std::pair<std::string const *, SCacheVal> >::iterator& evict_it,
	common::lrucache<std::string, SCacheVal>& target_cache, bool use_chances, int64& freed_space,
	bool& run_del_items, std::vector<std::list<std::pair<std::string const *, SCacheVal> >::iterator>& move_front,
	bool& used_chance)
{
	used_chance = false;
	bool compressed = (&target_cache==&compressed_items);

	while(true)
	{
		if(open_files.find(*evict_it->first)!=open_files.end()
			|| in_retrieval.find(*evict_it->first)!= in_retrieval.end())
		{
			if (break_on_skip)
			{
				return false;
			}
		}
		else if (!is_congested_nolock()
			&& preload_once_items.find(*evict_it->first) != preload_once_items.end())
		{
			move_front.push_back(evict_it);

			if (break_on_skip)
			{
				return false;
			}
		}
		else
		{
			std::unique_lock lock(submission_mutex);
			
			bool in_submission=false;
			{
				std::scoped_lock dirty_lock(dirty_item_mutex);
				for (std::map<int64, size_t>::iterator it = num_dirty_items.begin(); it != num_dirty_items.end(); ++it)
				{
					if (it->second>0)
					{
						if (submission_items.find(std::make_pair(it->first, *evict_it->first)) != submission_items.end())
						{
							in_submission = true;
						}
					}
				}
			}
			
			if(in_submission)
			{
				if (break_on_skip)
				{
					return false;
				}
			}
			else
			{
				lock.unlock();
				
				if (is_sync_wait_item(*evict_it->first))
				{
					if (break_on_skip)
					{
						return false;
					}
				}
				else
				{
					break;
				}
			}
		}

		if (evict_it == cache_eviction_iterator_finish(target_cache, cache_lock))
		{
			return false;
		}

		--evict_it;
	}

	if (only_non_dirty
		&& evict_it->second.dirty)
	{
		return false;
	}

	bool last = evict_it == cache_eviction_iterator_finish(target_cache, cache_lock);

	if (use_chances && evict_it->second.chances > 0)
	{
		--evict_it->second.chances;
		used_chance = true;

		auto evict_it_prev = evict_it;
		if (!last)
		{
			--evict_it;
		}

		target_cache.bring_to_front(evict_it_prev);
	}
	else
	{
		if (!compressed)
		{
			SFdKey* res = fd_cache.get(*evict_it->first);
			if (res != nullptr)
			{
				if (cachefs->filePath(res->fd) != keypath2(*evict_it->first, transid))
				{
					Server->Log("Fd cache filename not as expected. Got " + cachefs->filePath(res->fd)
						+ " Expected " + keypath2(*evict_it->first, transid), LL_ERROR);
					abort();
				}
				assert(dynamic_cast<IMemFile*>(res->fd) == nullptr);
				Server->destroy(res->fd);
				fd_cache.del(*evict_it->first);
			}
		}
		else
		{
			assert(!fd_cache.has_key(*evict_it->first));
		}

		evict_item(*evict_it->first, evict_it->second.dirty,
			target_cache, &evict_it, cache_lock, std::string(), freed_space);

		run_del_items = true;
	}

	std::scoped_lock lock(submission_mutex);
	check_submission_items();

	return !last;
}

void TransactionalKvStore::evict_move_front(common::lrucache<std::string, SCacheVal>& target_cache, std::vector<std::list<std::pair<std::string const*, SCacheVal>>::iterator>& move_front)
{
	for (auto it : move_front)
	{
		target_cache.bring_to_front(it);
	}
}

void TransactionalKvStore::evict_item(const std::string & key, bool dirty,
	common::lrucache<std::string, SCacheVal>& target_cache,
	std::list<std::pair<std::string const *, SCacheVal> >::iterator* evict_it,
	std::unique_lock<cache_mutex_t>& cache_lock, const std::string& from, int64& freed_space)
{
	bool compressed = (&target_cache == &compressed_items);

	std::set<std::string>::iterator it_untouched = nosubmit_untouched_items.find(key);

	if (dirty)
	{
		std::unique_lock dirty_lock(dirty_item_mutex);

		std::map<int64, std::set<std::string> >::iterator it_di = nosubmit_dirty_items.find(basetrans);

		if (it_di != nosubmit_dirty_items.end())
		{
			std::set<std::string>::iterator it2 = it_di->second.find(key);

			if (it2 != it_di->second.end())
			{
				dirty_lock.unlock();

				std::unique_ptr<IFsFile> file_base(cachefs->openFile(basepath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : ""), MODE_READ));

				bool base_compressed = compressed;

				if (file_base.get() == nullptr)
				{
					file_base.reset(cachefs->openFile(basepath2() + cachefs->fileSep() + hexpath(key) + ((!compressed) ? ".comp" : ""), MODE_READ));
					base_compressed = !compressed;
				}

				assert(file_base.get() != nullptr);

				if (file_base.get() != nullptr)
				{
					Server->Log("Evicting dirty base cache item " + basepath2() + cachefs->fileSep() + hexpath(key) + (base_compressed ? ".comp" : "") + " via submission first" + from, LL_INFO);

					std::scoped_lock lock(submission_mutex);

					if (submission_items.find(std::make_pair(basetrans, key))
						!= submission_items.end())
					{
						Server->Log(basepath2() + cachefs->fileSep() + hexpath(key) + (base_compressed ? ".comp" : "") + " is already being evicted" + from, LL_INFO);
					}
					else
					{
						SSubmissionItem sub_item;
						sub_item.key = key;
						sub_item.action = SubmissionAction_Evict;
						sub_item.transid = basetrans;
						sub_item.size = file_base->Size();
						sub_item.compressed = base_compressed;

						submission_items.insert(std::make_pair(std::make_pair(basetrans, key),
							submission_queue_add(sub_item, false)));

						evict_cond.notify_one();
					}
				}
				else
				{
					Server->Log("Base trans item at " + basepath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : "") + " does not exist", LL_ERROR);
					addSystemEvent("cache_err_fatal",
						"Base trans item does not exist on cache",
						"Base trans item at " + basepath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : "") + " does not exist", LL_ERROR);
					abort();
				}
			}
		}
	}

	bool last = evict_it!=nullptr
		&& *evict_it == cache_eviction_iterator_finish(target_cache, cache_lock);

	if (!dirty
		|| it_untouched != nosubmit_untouched_items.end())
	{
		if (dirty)
		{
			Server->Log("Evicting dirty but untouched cache item " + transpath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : "")+ from, LL_INFO);
		}
		else
		{
			Server->Log("Evicting non-dirty cache item " + transpath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : "")+ from, LL_INFO);
		}

		std::unique_ptr<IFsFile> file_src;
		SMemFile* memf;
		{
			std::scoped_lock memfile_lock(memfiles_mutex);
			memf = memfiles.get(std::make_pair(transid, key), false);
		}
		IFsFile* file;
		if (memf != nullptr)
		{
			if (memf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memf->key), LL_ERROR);
				abort();
			}
			file = memf->file.get();
		}
		else
		{
			file = cachefs->openFile(transpath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : ""), MODE_READ);
			file_src.reset(file);
		}

		assert(file != nullptr);
		int64 fsize = 0;
		if (file != nullptr)
		{
			fsize = file->Size();
			sub_cachesize(fsize);

			if (compressed)
			{
				comp_bytes -= fsize;
			}
		}

		file_src.reset();

		delete_item(nullptr, key, compressed, cache_lock);

		check_deleted(transid, key, compressed);

		if (dirty)
		{
			removeDirtyItem(transid, key);
			rm_dirty_bytes(transid, key, fsize, true);
		}

		freed_space += fsize;

		std::string item_key = key;
		if (!last
			&& evict_it!=nullptr)
		{
			--(*evict_it);
		}
		cache_del(target_cache, key, cache_lock);
	}
	else
	{
		Server->Log("Evicting dirty cache item " + transpath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : "") + " via submission"+ from, LL_INFO);

		std::unique_ptr<IFsFile> file_src;
		SMemFile* memf;
		{
			std::scoped_lock memfile_lock(memfiles_mutex);
			memf = memfiles.get(std::make_pair(transid, key), false);
		}
		IFsFile* file;
		if (memf != nullptr)
		{
			if (memf->key != key)
			{
				Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(memf->key), LL_ERROR);
				abort();
			}

			assert(!memf->cow);
			file = memf->file.get();
		}
		else
		{
			file = cachefs->openFile(transpath2() + cachefs->fileSep() + hexpath(key) + (compressed ? ".comp" : ""), MODE_READ);
			file_src.reset(file);
		}

		assert(file != nullptr);

		std::scoped_lock lock(submission_mutex);

		SSubmissionItem sub_item;
		sub_item.key = key;
		sub_item.action = SubmissionAction_Evict;
		sub_item.transid = transid;
		sub_item.size = file->Size();
		sub_item.compressed = compressed;

		submission_items.insert(std::make_pair(std::make_pair(transid, key), 
			submission_queue_add(sub_item, memf!=nullptr)));

		if (memf != nullptr)
		{
			++submitted_memfiles;
			submitted_memfile_size += memf->size;
			if (evict_it != nullptr)
			{
				memf->evicted = true;
			}
		}

		submitted_bytes += sub_item.size;

		evict_cond.notify_one();

		if (!last
			&& evict_it!=nullptr)
		{
			--(*evict_it);
		}
		cache_del(target_cache, key, cache_lock);
	}
}

bool TransactionalKvStore::evict_memfiles(std::unique_lock<cache_mutex_t>& cache_lock, bool evict_dirty)
{
	bool has_eviction = false;
	if (memfile_size - submitted_memfile_size > (max_memfile_size * 2) / 3
		|| !evict_dirty)
	{
		if (evict_dirty)
		{
			Server->Log("Having " + PrettyPrintBytes(memfile_size - submitted_memfile_size) + " of memfiles... Starting eviction...");
		}
		else
		{
			Server->Log("Having " + PrettyPrintBytes(memfile_size - submitted_memfile_size) + " of memfiles... Evicting all non-dirty...");
		}

		std::unique_lock memfile_lock(memfiles_mutex);

		auto it = memfiles.eviction_iterator_start();

		if (it == memfiles.eviction_iterator_finish())
		{
			return has_eviction;
		}

		--it;

		do
		{
			while (true)
			{
				const std::string& key = it->first->second;
				const int64& ttransid = it->first->first;
				if (ttransid != transid
					|| it->second.evicted)
				{
					//nobr
				}
				else if (open_files.find(key) != open_files.end()
					|| in_retrieval.find(key) != in_retrieval.end())
				{
					//nobr
				}
				else
				{
					if (is_sync_wait_item(key))
					{
						//nobr
					}
					else if (ttransid == transid
						&& num_mem_files[transid] < 10)
					{
						//nobr
					}
					else
					{
						break;
					}
				}

				if (it == memfiles.eviction_iterator_finish())
				{
					return has_eviction;
				}

				--it;
			}

			SCacheVal* dirty = cache_get(lru_cache, it->first->second, cache_lock, false);

			if (dirty != nullptr
				&& dirty->chances > 0)
			{
				--dirty->chances;

				auto it_prev = it;
				bool last = it == memfiles.eviction_iterator_finish();
				if (!last)
				{
					--it;
				}

				memfiles.bring_to_front(it_prev);

				if (last)
				{
					return has_eviction;
				}
				else
				{
					assert(it->first->second != std::string());
				}
			}
			else if (dirty != nullptr)
			{
				int64 freed_space;

				if (evict_dirty || !dirty->dirty)
				{
					it->second.evicted = true;
				}

				std::string key = it->first->second;
				bool last = it == memfiles.eviction_iterator_finish();
				if (!last)
				{
					--it;
				}

				std::string next_key = it->first->second;

				if (evict_dirty || !dirty->dirty)
				{
					memfile_lock.unlock();
					assert(!fd_cache.has_key(key));
					evict_item(key, dirty->dirty, lru_cache, nullptr, cache_lock, " [ememfiles]", freed_space);
					memfile_lock.lock();
					has_eviction = true;
				}

				if (last)
				{
					return has_eviction;
				}
				else
				{
					if (it->first->second != next_key)
					{
						Server->Log("memfile iter error", LL_ERROR);
						abort();
					}

					assert(it->first->second != std::string());
				}
			}
			else
			{
				assert(false);
				if (it == memfiles.eviction_iterator_finish())
				{
					return has_eviction;
				}
				--it;
			}
		} while (memfile_size - submitted_memfile_size > (max_memfile_size*2)/3
			|| !evict_dirty);
	}

	return has_eviction;
}

void TransactionalKvStore::check_deleted(int64 transid, const std::string & key, bool comp)
{
#ifndef NDEBUG
	std::string ipath = "trans_" + convert(transid) + cachefs->fileSep() + hexpath(key) + (comp ? ".comp" : "");

	{
		IScopedLock lock(del_file_mutex.get());
		if (del_file_queue.find(ipath) != del_file_queue.end())
		{
			return;
		}
	}

	assert(!cacheFileExists(ipath));
#endif
}

bool TransactionalKvStore::compress_one(std::unique_lock<cache_mutex_t>& cache_lock,
	std::list<std::pair<std::string const *, SCacheVal> >::iterator& compress_it)
{
	while(true)
	{
		if(open_files.find(*compress_it->first)!=open_files.end()
			|| in_retrieval.find(*compress_it->first)!= in_retrieval.end())
		{
			if (compress_it == cache_eviction_iterator_finish(lru_cache, cache_lock))
			{
				return false;
			}

			--compress_it;
		}
		else
		{
			break;
		}
	}

	SFdKey* res = fd_cache.get(*compress_it->first);
	if(res!=nullptr)
	{
		if (cachefs->filePath(res->fd) != keypath2(*compress_it->first, transid))
		{
			Server->Log("Fd cache filename not as expected. Got " + cachefs->filePath(res->fd) +
				" Expected " + keypath2(*compress_it->first, transid), LL_ERROR);
			abort();
		}
		assert(dynamic_cast<IMemFile*>(res->fd) == nullptr);
		Server->destroy(res->fd);
		fd_cache.del(*compress_it->first);
	}

	Server->Log("Compressing cache item "+transpath2() + cachefs->fileSep() + hexpath(*compress_it->first), LL_INFO);

	SMemFile* memf;
	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		memf = memfiles.get(std::make_pair(transid, *compress_it->first), false);
	}
	int64 fsize=0;
	if (memf != nullptr)
	{
		if (memf->key != *compress_it->first)
		{
			Server->Log("Memfile key wrong. Expected " + hexpath(*compress_it->first) + " got " + hexpath(memf->key), LL_ERROR);
			abort();
		}
		fsize = memf->file->Size();
	}
	else
	{
		std::unique_ptr<IFsFile> file(cachefs->openFile(transpath2() + cachefs->fileSep() + hexpath(*compress_it->first), MODE_READ));
		assert(file.get() != nullptr);
		if (file.get() != nullptr)
		{
			fsize = file->Size();
		}
	}	

	std::scoped_lock lock(submission_mutex);

	SSubmissionItem sub_item;
	sub_item.key = *compress_it->first;
	sub_item.action = SubmissionAction_Compress;
	sub_item.transid = transid;
	sub_item.size = 0;
	sub_item.compressed = compress_it->second.dirty;
	sub_item.size=fsize;

	submission_items.insert(std::make_pair(std::make_pair(transid, *compress_it->first), 
		submission_queue_add(sub_item, memf!=nullptr)));

	if (memf != nullptr)
	{
		memf->evicted = true;
		++submitted_memfiles;
		submitted_memfile_size += memf->size;
	}

	check_submission_items();

	evict_cond.notify_one();

	std::string key = *compress_it->first;
	bool last = compress_it == cache_eviction_iterator_finish(lru_cache, cache_lock);
	if (!last)
	{
		--compress_it;
	}
	cache_del(lru_cache, key, cache_lock);

	return !last;
}

std::list<SSubmissionItem>::iterator TransactionalKvStore::next_submission_item(bool no_compress, bool prefer_non_delete, bool prefer_mem, std::string& path, bool& p_do_stop, SMemFile*& memf)
{
	std::unique_lock lock(submission_mutex);

	bool skip_wait = false;
	while(true)
	{
		int first_wait = 0;
		while(!skip_wait
			&& submission_queue.empty()
			&& !do_stop)
		{
			if (first_wait==0)
			{
				first_wait = 1;
				evict_cond.wait_for(lock, 10s);
			}
			else if (first_wait == 1)
			{
				lock.unlock();
				first_wait = 2;
				Server->mallocFlushTcache();
				lock.lock();
			}
			else
			{
				if (submission_queue.empty())
					evict_cond.wait(lock);
				else
					evict_cond.wait_for(lock, 10s);
			}
		}

		if(do_stop)
		{
			p_do_stop=do_stop;
			return submission_queue.end();
		}

		skip_wait = false;

		bool has_delete = false;

		std::list<SSubmissionItem>::iterator it = submission_queue.begin();
		if (prefer_mem
			&& submitted_memfiles > 0
			&& submission_queue_memfile_first!=submission_queue.end())
		{
			it = submission_queue_memfile_first;
		}

		for(;it!=submission_queue.end();++it)
		{
			if (no_compress
				&& it->action == SubmissionAction_Compress)
			{
				continue;
			}

			if (prefer_non_delete
				&& it->action == SubmissionAction_Delete)
			{
				has_delete = true;
				continue;
			}

			if(it->action==SubmissionAction_Evict ||
				it->action==SubmissionAction_Dirty ||
				it->action==SubmissionAction_Compress )
			{
				if (it->action == SubmissionAction_Compress
					|| !it->compressed)
				{
					std::scoped_lock memfile_lock(memfiles_mutex);
					memf = memfiles.get(std::make_pair(it->transid, it->key), false);
				}

				if (prefer_mem
					&& memf == nullptr
					&& submitted_memfiles > 0)
				{
					continue;
				}

				if(it->action==SubmissionAction_Dirty)
					it->action=SubmissionAction_Working_Dirty;

				if (it->action == SubmissionAction_Evict)
				{
					it->action = SubmissionAction_Working_Evict;
					if(it->transid==transid)
						dirty_evicted_items.insert(it->key);
					assert(memf == nullptr || (!memf->cow && memf->evicted));
				}

				if(it->action==SubmissionAction_Compress)
					it->action=SubmissionAction_Working_Compress;

				path = keypath2(it->key, it->transid);

				if ((it->action == SubmissionAction_Working_Evict
					 || it->action == SubmissionAction_Working_Dirty)
					&& it->compressed)
				{
					path += ".comp";
				}

				if (memf != nullptr)
				{
					if (memf->key != it->key)
					{
						Server->Log("Memfile key wrong. Expected " + hexpath(it->key) + " got " + hexpath(memf->key), LL_ERROR);
						abort();
					}

					assert(submitted_memfiles > 0);
					--submitted_memfiles;

					if (submitted_memfiles == 0)
					{
						submission_queue_memfile_first = submission_queue.end();
					}
					else
					{
						submission_queue_memfile_first = it;
						++submission_queue_memfile_first;
					}
				}

				return it;
			}
			else if(it->action==SubmissionAction_Delete)
			{
				it->action=SubmissionAction_Working_Delete;
				return it;
			}
		}

		if (prefer_non_delete
			&& has_delete)
		{
			skip_wait = true;
			prefer_non_delete = false;
			continue;
		}

		if (!submission_queue.empty())
		{
			evict_cond.notify_one();
		}

		lock.unlock();
		Server->wait(100);
		lock.lock();
	}
	
}

bool TransactionalKvStore::item_submitted( std::list<SSubmissionItem>::iterator it, bool can_delete, bool is_memf)
{
	int64 curr_transid;
	{
		std::unique_lock lock(cache_mutex);

		if (is_memf)
		{
#ifndef NDEBUG
			if (it->action == SubmissionAction_Working_Evict
				&& it->transid == transid)
			{
				assert(cache_get(lru_cache, it->key, lock, false) == nullptr);
				assert(cache_get(compressed_items, it->key, lock, false) == nullptr);
			}
#endif
			//TODO: if !can_delete move this to regular submit bundle and resubmit if sync() fails
			if (rm_mem_file(nullptr, it->transid, it->key, true)
				&& it->transid<transid)
			{
				lock.unlock();
				online_kv_store->sync();

				cachefs->rename("trans_" + convert(it->transid) + cachefs->fileSep() + "dirty.mem",
					"trans_" + convert(it->transid) + cachefs->fileSep() + "dirty");

				lock.lock();

				cleanup(false);
			}
		}
		
		curr_transid = transid;

		if (it->action == SubmissionAction_Working_Delete)
		{
			std::scoped_lock dirty_lock(dirty_item_mutex);

			--num_delete_items[it->transid];

			if (num_delete_items[it->transid] == 0)
			{
				cachefs->deleteFile("trans_" + convert(it->transid) + cachefs->fileSep() + "deleted");
			}
		}

		if(it->action!=SubmissionAction_Working_Evict
			|| it->transid==curr_transid)
		{
			removeDirtyItem(it->transid, it->key);			
			rm_dirty_bytes(it->transid, it->key, it->size, true);

			submitted_bytes-=it->size;
			total_submitted_bytes += it->size;
			
			if(it->transid==curr_transid
				&& it->compressed)
			{
				comp_bytes -= it->size;
			}
		}

		std::unique_lock dirty_lock(dirty_item_mutex);
		if (it->action != SubmissionAction_Working_Evict
			 && it->transid<transid
				&& num_dirty_items[it->transid] == 0
				&& num_delete_items[it->transid] == 0 )
		{
			//TODO: if !can_delete move this to regular_submit_bundle
#ifdef DIRTY_ITEM_CHECK
			assert(dirty_items[it->transid].empty());
			dirty_items.erase(dirty_items.find(it->transid));
			assert(dirty_items_size[it->transid].empty());
			dirty_items_size.erase(dirty_items_size.find(it->transid));
#endif
			num_dirty_items.erase(num_dirty_items.find(it->transid));
			num_delete_items.erase(num_delete_items.find(it->transid));
			bool has_nosubmit = nosubmit_dirty_items.find(it->transid) != nosubmit_dirty_items.end();
			dirty_lock.unlock();

			std::unique_ptr<IFsFile> invalid(cachefs->openFile("trans_" + convert(it->transid) + cachefs->fileSep() + "invalid", MODE_READ));
			lock.unlock();

			if (invalid.get() == nullptr)
			{
				std::unique_ptr<IFsFile> nosubmit(cachefs->openFile("trans_" + convert(it->transid) + cachefs->fileSep() + "dirty.nosubmit", MODE_READ));
				if (nosubmit.get() == nullptr
					&& !has_nosubmit)
				{
					online_kv_store->sync();
					if (online_kv_store->transaction_finalize(it->transid, true, true))
					{
						lock.lock();

						std::unique_ptr<IFsFile> tmp(cachefs->openFile("trans_" + convert(it->transid) + cachefs->fileSep() + "commited", MODE_WRITE));
						cachefs->deleteFile("trans_" + convert(it->transid) + cachefs->fileSep() + "dirty");
						tmp.reset();

						cleanup(false);
					}
					else
					{
						lock.lock();

						Server->Log("Error marking transaction " + convert(it->transid) + " as complete", LL_ERROR);

						addDirtyItem(it->transid, it->key, false);

						return false;
					}
				}
			}
			else
			{
				lock.lock();

				Server->Log("Removing invalid transaction " + std::to_string(it->transid)+ " (1)", LL_INFO);
				remove_transaction(it->transid);
			}
		}
	}

	bool do_submit_bundle = false;
	bool submit_evict = false;
	bool run_del_item = false;
	if(!it->key.empty() && it->transid<curr_transid 
		&& it->action== SubmissionAction_Working_Dirty)
	{
		if (can_delete)
		{
			if (with_submitted_files)
			{
				add_submitted_item(it->transid, it->key, nullptr);
			}
		}
		else
		{
			do_submit_bundle = true;
		}
	}
	
	if (it->action != SubmissionAction_Working_Delete
		&& it->action != SubmissionAction_Working_Evict)
	{
		std::lock_guard lock(submission_mutex);
		submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));
	}

	if(it->action==SubmissionAction_Working_Evict)
	{		
		std::unique_lock lock(cache_mutex, std::defer_lock);
		if(it->transid==curr_transid)
		{
			lock.lock();
			sub_cachesize(it->size);
		}
		else
		{
			add_evicted_item(it->transid, it->key);
			lock.lock();
		}

		{
			std::lock_guard lock(submission_mutex);
			submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));
		}
		
		//TODO: if !can_delete move this to regular_submit_bundle
		std::unique_lock dirty_lock(dirty_item_mutex);
		std::map<int64, std::set<std::string> >::iterator it_ld = nosubmit_dirty_items.find(it->transid);
		
		if(it_ld!=nosubmit_dirty_items.end())
		{
			std::set<std::string>::iterator it2 = it_ld->second.find(it->key);
			if(it2!=it_ld->second.end())
			{
				it_ld->second.erase(it2);
				
				if(it_ld->second.empty() 
					&& it->transid<transid
					&& num_dirty_items.find(it->transid)== num_dirty_items.end()
					&& num_delete_items.find(it->transid)== num_delete_items.end())
				{
					nosubmit_dirty_items.erase(it_ld);
					dirty_lock.unlock();

					//TODO: Sync this with memfile dirty submission so transaction also commits if memfiles are submitted later than the evicted items
					
					std::unique_ptr<IFsFile> invalid(cachefs->openFile("trans_"+convert(it->transid)+ cachefs->fileSep() + "invalid", MODE_READ));
					lock.unlock();

					if(invalid.get()==nullptr)
					{
						online_kv_store->sync();
						if(online_kv_store->transaction_finalize(it->transid, true, true))
						{
							lock.lock();

							std::unique_ptr<IFsFile> tmp(cachefs->openFile("trans_"+convert(it->transid)+ cachefs->fileSep() + "commited", MODE_WRITE));
							cachefs->deleteFile("trans_"+convert(it->transid)+ cachefs->fileSep() + "dirty");
							cachefs->deleteFile("trans_"+convert(it->transid)+ cachefs->fileSep() + "dirty.nosubmit");
							tmp.reset();
						}
						else
						{
							lock.lock();

							Server->Log("Error marking finalized transaction "+convert(it->transid)+" as complete", LL_ERROR);
						}
					}
					else
					{
						lock.lock();
						Server->Log("Removing invalid transaction " + std::to_string(it->transid) + " (2)", LL_INFO);
						remove_transaction(it->transid);
					}
				}
			}
		}

		dirty_lock.unlock();
		
		delete_item(nullptr, it->key, it->compressed, lock, 0, it->transid!=curr_transid ? curr_transid : 0);
		run_del_item = true;
	}
	else if (it->action == SubmissionAction_Working_Dirty
		&& it->transid < curr_transid)
	{
		std::unique_lock lock(cache_mutex);
		
		std::string item_path = keypath2(it->key, it->transid);

		if (it->compressed)
		{
			item_path += ".comp";
		}
		
		common::lrucache<std::string, SCacheVal>* target_cache = &lru_cache;
		common::lrucache<std::string, SCacheVal>* other_target_cache = &compressed_items;
		
		if(it->compressed)
		{
			target_cache = &compressed_items;
			other_target_cache = &lru_cache;
		}
		
		bool compressed = it->compressed;
		
		SCacheVal* dirty = cache_get(*target_cache, it->key, lock, false);
		
		if(dirty==nullptr)
		{
			compressed = !compressed;
			dirty = cache_get(*other_target_cache, it->key, lock, false);
			target_cache = other_target_cache;
		}
		
		bool del_item = false;
		if( do_evict
			&& dirty!=nullptr && !dirty->dirty
			&& it->keys.empty()
			&& open_files.find(it->key)==open_files.end()
			&& in_retrieval.find(it->key)==in_retrieval.end() )
		{
			bool in_submission=false;
			{
				std::lock_guard lock(submission_mutex);
				std::scoped_lock dirty_lock(dirty_item_mutex);
				for(std::map<int64, size_t>::iterator itd=num_dirty_items.begin();itd!=num_dirty_items.end();++itd)
				{
					if(itd->second>0)
					{
						if(submission_items.find(std::make_pair(itd->first, it->key))!=submission_items.end())
						{
							in_submission=true;
						}
					}
				}
			}
			
			if(!in_submission
				&& can_delete )
			{
				Server->Log("Evicting non-dirty cache item "+transpath2() + cachefs->fileSep() + hexpath(it->key)+(compressed?".comp":"")+" after submission", LL_INFO);
				
				int64 fsize = -1;
				if(!compressed)
				{
					SFdKey* res = fd_cache.get(it->key);
					if(res!=nullptr)
					{
						if (cachefs->filePath(res->fd) != keypath2(it->key, transid))
						{
							Server->Log("Fd cache filename not as expected. Got " + cachefs->filePath(res->fd) +
								" Expected " + keypath2(it->key, transid), LL_ERROR);
							abort();
						}
						assert(dynamic_cast<IMemFile*>(res->fd) == nullptr);
						fsize = res->fd->Size();
						Server->destroy(res->fd);
						fd_cache.del(it->key);
					}
				}

				if(fsize==-1
					&& compressed)
				{
					SMemFile* memfile_nf;
					{
						std::scoped_lock memfile_lock(memfiles_mutex);
						memfile_nf = memfiles.get(std::make_pair(transid, it->key), false);
					}
					IFile* fd;
					if (memfile_nf != nullptr)
					{
						if (memfile_nf->key != it->key)
						{
							Server->Log("Memfile key wrong. Expected " + hexpath(it->key) + " got " + hexpath(memfile_nf->key), LL_ERROR);
							abort();
						}

						fd = memfile_nf->file.get();
					}
					else
					{
						fd = cachefs->openFile(keypath2(it->key, transid) + (compressed ? ".comp" : ""), MODE_READ);
					}

					if (fd != nullptr)
					{
						fsize = fd->Size();
						Server->destroy(fd);
					}
				}

				if (fsize == -1)
				{
					fsize = it->size;
				}
			
				sub_cachesize(fsize);

				if(compressed)
				{
					comp_bytes-= fsize;
				}
			
				delete_item(nullptr, it->key, compressed, lock);
				run_del_item = true;

				cache_del(*target_cache, it->key, lock);
			}
			else if (!in_submission)
			{
				del_item = true;
				submit_evict = true;
			}
			else
			{
				del_item = true;
			}
		}	
		else if (do_evict
			|| (dirty == nullptr
				&& allow_evict) )
		{
			del_item = true;
		}

		if(del_item)
		{
			if (can_delete)
			{
				lock.unlock();
				if (!is_memf)
				{
					cachefs->deleteFile(item_path);
				}
			}
			else
			{
				do_submit_bundle = true;
			}
		}
	}

	if (do_submit_bundle)
	{
		std::scoped_lock lock(cache_mutex);
		submit_bundle.push_back(std::make_pair(*it, submit_evict));
		IScopedLock bundle_lock(submit_bundle_item_mutex.get());
		curr_submit_bundle_items->insert(std::make_pair(it->key, it->transid));
		if (submit_bundle_starttime == 0)
		{
			submit_bundle_starttime = Server->getTimeMS();
		}
	}

	if (run_del_item)
	{
		run_del_file_queue();
	}
	
	std::lock_guard lock(submission_mutex);

	submission_queue_rm(it);

	check_submission_items();

	return true;
}

bool TransactionalKvStore::item_compressed( std::list<SSubmissionItem>::iterator it, bool compression_ok, int64 size_diff, int64 add_comp_bytes, bool is_memf)
{
	++total_compress_ops;

	std::unique_lock lock(cache_mutex);
	std::unique_lock submission_lock(submission_mutex);

	bool has_delete_item=false;

	if (!it->finish)
	{
		Server->Log("Not finishing submission item " + hex(it->key) + " transid " + convert(it->transid)+" (was deleted)", LL_INFO);
	}

	if(compression_ok
		&& it->finish)
	{
		if(it->transid==transid)
		{
			has_delete_item=true;
			cache_put(compressed_items, it->key, cache_val(it->key, it->compressed), lock);
			if(it->compressed)
			{
				add_dirty_bytes(transid, it->key, size_diff);
			}
			if(size_diff>=0)
			{
				add_cachesize(size_diff);
			}
			else
			{
				sub_cachesize(-1*size_diff);
			}
			comp_bytes+=add_comp_bytes;
		}
		else
		{
			assert(false);
			cachefs->deleteFile(keypath2(it->key, it->transid)+".comp");
		}
	}
	else if(it->finish)
	{
		cache_put(lru_cache, it->key, cache_val(it->key, it->compressed), lock);
	}

	std::string local_key = it->key;
	int64 curr_basetrans = basetrans;
	int64 curr_transid = it->transid;
	bool compressed = it->compressed;
	bool curr_finish = it->finish;

	bool run_del_item = false;

	if(has_delete_item)
	{
		bool nosubmit_untouched = nosubmit_untouched_items.find(local_key)!=nosubmit_untouched_items.end();
		if( !compressed
			|| nosubmit_untouched )
		{
			submission_lock.unlock();
			lock.unlock();

			if (!compressed 
				&& comp_percent>0
				&& resubmit_compressed_ratio<1)
			{
				int64 submitted_comp_bytes = get_compsize(it->key, it->transid);
				if (submitted_comp_bytes > 0
					&& (submitted_comp_bytes - add_comp_bytes) > 10 * 1024)
				{
					float better_ratio = static_cast<float>(add_comp_bytes) / submitted_comp_bytes;
					if (better_ratio <= resubmit_compressed_ratio)
					{
						Server->Log("Result of background compression is only " + convert(better_ratio*100.f) + "% of submitted size (submitted="
							+ PrettyPrintBytes(submitted_comp_bytes) + " now=" + PrettyPrintBytes(add_comp_bytes)
							+ ". Dirtying item " + hex(it->key) + " transid " + convert(it->transid) + " for resubmission.", LL_INFO);
						std::unique_lock lock2(cache_mutex);
						if (cache_get(compressed_items, it->key, lock2, false) != nullptr)
						{
							cache_put(compressed_items, it->key, cache_val(it->key, true), lock2);
							addDirtyItem(it->transid, it->key);
							add_dirty_bytes(it->transid, it->key, add_comp_bytes);
						}
					}
				}
			}

			if (with_prev_link
				&& !is_memf)
			{
				if (nosubmit_untouched)
				{
					Server->Log("Linking untouched dirty item to " + keypath2(local_key, curr_basetrans) + ".comp");
				}
				else
				{
					Server->Log("Linking non-dirty item to " + keypath2(local_key, curr_basetrans) + ".comp");
				}

				std::unique_ptr<IFsFile> base_file(cachefs->openFile(keypath2(local_key, curr_basetrans) + ".comp", MODE_READ));
				if (base_file.get() == nullptr)
				{
					std::string path = keypath2(local_key, curr_basetrans) + ".comp";
					if (!sync_link(cachefs, keypath2(local_key, curr_transid) + ".comp", path))
					{
						nosubmit_untouched = false;
					}
				}
				else
				{
					nosubmit_untouched = false;
				}
			}
			else
			{
				nosubmit_untouched = false;
			}
			
			lock.lock();
			submission_lock.lock();
	
			submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));
			submission_queue_rm(it);
			
			delete_item(nullptr, local_key, false, lock, nosubmit_untouched ? curr_basetrans : 0,
				0, DeleteImm::None, 0, true);
			run_del_item = true;
		}
		else
		{
			submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));
			submission_queue_rm(it);
	
			delete_item(nullptr, local_key, false, lock, 0, 0, 
				DeleteImm::None, 0, true);
			run_del_item = true;
		}
	}
	else
	{
		submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));
		submission_queue_rm(it);
	}

	if (!curr_finish)
	{
		cachefs->deleteFile(keypath2(local_key, curr_transid) + ".comp");
		delete_item(nullptr, local_key, false, lock, 0, 0, 
			DeleteImm::None, 0, true);
		run_del_item = true;
	}

	check_submission_items();

	if (run_del_item)
	{
		submission_lock.unlock();
		lock.unlock();

		run_del_file_queue();
	}

	return true;
}


void TransactionalKvStore::stop()
{
	regular_submit_bundle_thread.quit();
	throttle_thread.quit();
	metadata_update_thread.quit();
	memfd_del_thread.quit();

	std::unique_lock lock(cache_mutex);

	wait_for_all_retrievals(lock);

	std::unique_lock sub_lock(submission_mutex);

	do_stop=true;

	evict_cond.notify_all();

	sub_lock.unlock();
	lock.unlock();

	Server->getThreadPool()->waitFor(threads);
}

bool TransactionalKvStore::checkpoint(bool do_submit, size_t checkpoint_retry_n)
{
	assert(fuse_io_context::is_sync_thread());

	bool allow_log_events = checkpoint_retry_n > retry_log_n;

	std::unique_lock lock(cache_mutex);

	wait_for_retrieval_poll(lock, std::string());

	std::string empty_key;

	{
		RetrievalOperationNoLock retrieval_op(*this, empty_key);
		while (!open_files.empty())
		{
			lock.unlock();
			Server->wait(10);
			lock.lock();
		}
	}

	wait_for_all_retrievals(lock);

	assert(in_retrieval.empty());
	assert(open_files.empty());

	RetrievalOperationNoLock retrieval_op(*this, empty_key);

	std::pair<std::string, SFdKey> evt;
	do 
	{
		evt = fd_cache.evict_one();
		if (evt != std::pair<std::string, SFdKey>())
		{
			assert(dynamic_cast<IMemFile*>(evt.second.fd) == nullptr);
			drop_cache(evt.second.fd);
			Server->destroy(evt.second.fd);
		}

	} while (evt!=std::pair<std::string, SFdKey>());

	run_del_file_queue();

	Server->Log("Removing compression/eviction submissions...");

	remove_compression_evicition_submissions(lock);

	Server->Log("Waiting for active compressions/evicitions to finish...");
	wait_for_compressions_evictions(lock);

	run_del_file_queue();

	if (!online_kv_store->sync())
	{
		Server->Log("Error syncing online kv store", LL_ERROR);
		return false;
	}

	int64 new_trans = online_kv_store->new_transaction(allow_log_events);

	if(new_trans==0)
	{
		Server->Log("Error requesting new transaction", LL_ERROR);
		return false;
	}

	if(!cachefs->createSnapshot("trans_"+std::to_string(transid), "trans_" + std::to_string(new_trans)))
	{
		Server->Log("Error creating new transaction subvolume", LL_ERROR);
		return false;
	}
	else
	{
		update_transactions();
		clean_snapshot(new_trans);
	}

	update_transactions();

	if(!write_to_deleted_file("trans_" + convert(transid)+ cachefs->fileSep()+"deleted", do_submit))
	{
		return false;
	}

	if (!do_submit)
	{
		size_t retry_n = 0;
		while (!online_kv_store->transaction_finalize(transid, false, retry_n>retry_log_n))
		{
			retryWait(++retry_n);
		}
	}

	while (!cachefs->sync("") || !cachefs->sync("trans_"+std::to_string(transid)))
	{
		Server->Log("Syncing fs failed. Retrying...");
		Server->wait(1000);
	}

	size_t num_memf_dirty=0;
	std::map<std::string, size_t> memf_dirty_items;
	std::map<std::string, int64> memf_dirty_items_size;
	if(!write_to_dirty_file(lock, "trans_" + convert(transid)+ cachefs->fileSep()+"dirty", do_submit, new_trans,
		num_memf_dirty, memf_dirty_items, memf_dirty_items_size))
	{
		return false;
	}
	
	if(do_submit)
	{
		while(!cachefs->createDir("trans_" + convert(transid)+ cachefs->fileSep()+"submitted"))
		{
			Server->Log("Error creating submitted dir. Retrying...");
			Server->wait(1000);
		}
	}
	else
	{
		while(!cachefs->createDir("trans_" + convert(transid)+ cachefs->fileSep()+"evicted"))
		{
			Server->Log("Error creating evicted dir. Retrying...");
			Server->wait(1000);
		}
	}

	basetrans=transid;
	
	transid = new_trans;

	if(!do_submit)
	{
		{
			std::scoped_lock dirty_lock(dirty_item_mutex);
			std::map<int64, size_t>::iterator dirty_it = num_dirty_items.find(basetrans);

			if (dirty_it != num_dirty_items.end())
			{
#ifdef DIRTY_ITEM_CHECK
				std::map<int64, std::map<std::string, size_t> >::iterator dirty2_it = dirty_items.find(basetrans);
				assert(dirty2_it != dirty_items.end());
				dirty_items[new_trans] = dirty2_it->second;
				dirty_items.erase(dirty2_it);

				std::map<int64, std::map<std::string, int64> >::iterator dirty3_it = dirty_items_size.find(basetrans);
				assert(dirty3_it != dirty_items_size.end());
				dirty_items_size[new_trans] = dirty3_it->second;
				dirty_items_size.erase(dirty3_it);
#endif
				num_dirty_items[new_trans] = dirty_it->second;
				num_dirty_items.erase(dirty_it);
			}

			if (num_memf_dirty > 0)
			{
				num_dirty_items[basetrans] = num_memf_dirty;
				DIRTY_ITEM(dirty_items[basetrans] = memf_dirty_items);
				DIRTY_ITEM(dirty_items_size[basetrans] = memf_dirty_items_size);
				for (auto it : memf_dirty_items)
				{
					for(size_t i=0;i<it.second;++i)
						removeDirtyItem(new_trans, it.first);
				}
				for (auto it : memf_dirty_items_size)
				{
					rm_dirty_bytes(new_trans, it.first, it.second, true, false);
				}
			}
		}
	}
	else
	{
		std::scoped_lock lock(submission_mutex);
		dirty_evicted_items.clear();
	}

	cleanup(false);

	return true;
}

std::string TransactionalKvStore::keypath2( const std::string& key, int64 transaction_id )
{
	return "trans_" + std::to_string(transaction_id)
		+ cachefs->fileSep() + hexpath(key);
}

bool TransactionalKvStore::read_submitted_evicted_files(const std::string & fn, std::set<std::string>& sub_evicted)
{
	std::unique_ptr<IFile> f(cachefs->openFile(fn, MODE_READ));
	if (f.get() == nullptr)
	{
		return true;
	}

	std::vector<char> msg;
	while (true)
	{
		unsigned short psize;
		bool has_error = false;
		if (f->Read(reinterpret_cast<char*>(&psize), sizeof(psize), &has_error) == sizeof(psize))
		{
			if (psize < sizeof(unsigned short))
			{
				return false;
			}

			psize -= sizeof(unsigned short);

			if (msg.size() < psize)
			{
				msg.resize(psize);
			}

			_u32 rc = f->Read(msg.data(), psize, &has_error);
			if (rc != psize)
			{
				return !has_error;
			}

			CRData rdata(msg.data(), msg.size());

			std::string tkey;
			if (rdata.getStr2(&tkey))
			{
				sub_evicted.insert(tkey);
			}
		}
		else
		{
			return !has_error;
		}
	}
}

bool TransactionalKvStore::read_from_dirty_file(std::unique_lock<cache_mutex_t>& cache_lock, const std::string& fn, int64 transaction_id, bool do_submit, int64 nosubmit_transid)
{
	std::list<SSubmissionItem>::iterator submit_end = submission_queue.end();

	std::unique_ptr<IFsFile> file(cachefs->openFile(fn, MODE_READ));

	if(file.get()==nullptr)
	{
		Server->Log("Cannot open \""+fn+"\". " + os_last_error_str(), LL_ERROR);
		return false;
	}
	
	if (nosubmit_transid != 0)
	{
		nosubmit_untouched_items.clear();
	}


	std::set<std::string> evicted_tkeys;
	read_submitted_evicted_files(fn + ".evicted", evicted_tkeys);

	std::set<std::string> submitted_tkeys;
	read_submitted_evicted_files(fn + ".submitted", submitted_tkeys);

	std::string dirty_list;
	while(true)
	{
		char compressed=0;

		if(file->Read(&compressed, sizeof(compressed))!=sizeof(compressed))
		{
			break;
		}

		unsigned int key_size;
		if(file->Read(reinterpret_cast<char*>(&key_size), sizeof(key_size))!=sizeof(key_size))
		{
			Server->Log("Error reading key length submission file", LL_ERROR);
			return false;
		}

		std::string key;
		key.resize(key_size);

		if(file->Read(&key[0], key_size)!=key_size)
		{
			Server->Log("Error reading key from submission file", LL_ERROR);
			return false;
		}
		
		if(!dirty_list.empty()) dirty_list+=", ";
		dirty_list+=hex(key);

		if(submitted_tkeys.find(key)!= submitted_tkeys.end())
		{
			continue;
		}
		
		if(evicted_tkeys.find(key)!=evicted_tkeys.end())
		{
			Server->Log("Dirty evicted item " + hex(key) + " transid " + convert(transaction_id));
			
			dirty_evicted_items.insert(key);
				
			continue;
		}

		std::unique_ptr<IFsFile> file;
		if (compressed != compressed_evicted_dirty)
		{
			file.reset(cachefs->openFile(keypath2(key, transaction_id) + (compressed != 0 ? ".comp" : ""), MODE_READ));
			
			if(file.get()==nullptr)
			{
				file.reset(cachefs->openFile(keypath2(key, transaction_id) + (compressed == 0 ? ".comp" : ""), MODE_READ));
				
				if(file.get()!=nullptr)
				{
					compressed = compressed == 0 ? 1 : 0;
				}
			}

			if (!file.get())
			{
				Server->Log("Error opening file to submit (" + keypath2(key, transaction_id) + (compressed != 0 ? ".comp)" : ")"), LL_ERROR);

				bool not_found;
				int64 get_transid = 0;

				std::string tmpfn = "submit.test.file";
				IFsFile* tmpl_file = cachefs->openFile(tmpfn, CREATE_DIRECT);

				if (tmpl_file == nullptr)
				{
					Server->Log("Cannot open submit.test.file "+cachefs->lastError(), LL_ERROR);
					addSystemEvent("cache_err_fatal",
						"Cannot open file",
						"Cannot open submit.test.file " + cachefs->lastError(), LL_ERROR);
					abort();
				}

				IFsFile* f = online_kv_store->get(key, transaction_id, false, tmpl_file, true, not_found, &get_transid);
				if (f == nullptr)
				{
					Server->Log("Cannot get file not_found="+convert(not_found), LL_ERROR);
					addSystemEvent("cache_err_fatal",
						"Cannot get file",
						"Cannot get file not_found=" + convert(not_found), LL_ERROR);
					abort();
				}
				else
				{
					Server->destroy(f);
					cachefs->deleteFile(tmpfn);
					Server->Log("Get ok", LL_WARNING);
				}

				if (get_transid != transaction_id)
				{
					Server->Log("Item from backend has transaction id " + convert(get_transid) + " expected " + convert(transaction_id), LL_ERROR);
					addSystemEvent("cache_err_fatal",
						"Item from backend has wrong transaction id",
						"Item from backend has transaction id " + convert(get_transid) + " expected " + convert(transaction_id), LL_ERROR);
					abort();
				}
				
				if(do_submit)
				{
					Server->Log("Assuming file was already submitted", LL_WARNING);
				}
				else
				{
					Server->Log("Assuming file was evicted", LL_WARNING);
					dirty_evicted_items.insert(key);
				}
				
				continue;
			}
		}
		
		if(do_submit)
		{
			if (compressed != compressed_evicted_dirty)
			{
				SSubmissionItem item;
				item.key = key;
				item.transid = transaction_id;
				item.action = SubmissionAction_Dirty;
				item.size = file->Size();
				item.compressed = compressed != 0;

				Server->Log("Dirty submission item " + hex(key) + " transid " + convert(transaction_id) + " compressed=" + convert(item.compressed));

				submit_end = submission_queue_insert(item, false, submit_end);
				submission_items.insert(std::make_pair(std::make_pair(transaction_id, key), submit_end));

				add_dirty_bytes(transaction_id, key, item.size);

				submitted_bytes += item.size;

				addDirtyItem(transaction_id, key, false);
			}
			else
			{
				Server->Log("Already evicted submission item " + hex(key) + " transid " + convert(transaction_id));
			}
		}
		else
		{
			if (compressed == compressed_evicted_dirty)
			{
				Server->Log("Dirty evicted item " + hex(key) + " transid " + convert(transaction_id));
				dirty_evicted_items.insert(key);
			}
			else
			{
				Server->Log("Dirty item " + hex(key) + " transid " + convert(transaction_id)+" compressed="+ convert(compressed != 0));

				if (compressed != 0)
				{
					cache_put(compressed_items, key, cache_val(key, true), cache_lock);
				}
				else
				{
					cache_put(lru_cache, key, cache_val(key, true), cache_lock);
				}

				add_dirty_bytes(transaction_id, key, file->Size());

				addDirtyItem(transaction_id, key, false);

				if (nosubmit_transid != 0)
				{
					std::scoped_lock dirty_lock(dirty_item_mutex);
					nosubmit_dirty_items[nosubmit_transid].insert(key);
					nosubmit_untouched_items.insert(key);
				}
			}
		}
	}
	
	Server->Log("List of read dirty items for transid "+convert(transaction_id)+": "+dirty_list);

	if(do_submit)
	{
		submit_dummy(transaction_id);
	}

	check_submission_items();
	
	return true;
}

bool TransactionalKvStore::write_to_dirty_file(std::unique_lock<cache_mutex_t>& cache_lock, const std::string& fn, bool do_submit, int64 new_trans,
	size_t& num_memf_dirty, std::map<std::string, size_t>& memf_dirty_items,
	std::map<std::string, int64>& memf_dirty_items_size)
{	
	std::unique_ptr<IFsFile> file(cachefs->openFile(fn+".new", MODE_WRITE));
	int64 file_pos=0;

	if(file.get()==nullptr)
	{
		Server->Log("Error opening submission file for writing", LL_ERROR);
		return false;
	}

	std::lock_guard sub_lock(submission_mutex);
	
	std::list<SSubmissionItem>::iterator submit_end = submission_queue.end();
	nosubmit_untouched_items.clear();

	bool compressed=false;
	
	std::string dirty_list;
	bool has_memf = false;

	{
		std::scoped_lock dirty_lock(dirty_item_mutex);
		std::scoped_lock memfile_lock(memfiles_mutex);

		for (common::lrucache<std::string, SCacheVal>::list_t::iterator it = lru_cache.get_list().begin();
			!compressed || it != compressed_items.get_list().end();)
		{
			if (compressed || it != lru_cache.get_list().end())
			{
				if (it->second.dirty)
				{
					if (!dirty_list.empty()) dirty_list += ", ";
					dirty_list += hex(*it->first);

					SMemFile* memf = nullptr;
					if (!compressed)
					{
						memf = memfiles.get(std::make_pair(transid, *it->first), false);
					}
					if (memf == nullptr)
					{
						char ch_compressed = compressed ? 1 : 0;
						while (file->Write(file_pos, &ch_compressed, sizeof(ch_compressed)) != sizeof(ch_compressed))
						{
							Server->Log("Error writing compressed flag. Retrying...", LL_ERROR);
							Server->wait(1000);
						}
						file_pos += sizeof(ch_compressed);

						unsigned int key_size = static_cast<unsigned int>(it->first->size());
						while (file->Write(file_pos, reinterpret_cast<char*>(&key_size), sizeof(key_size)) != sizeof(key_size))
						{
							Server->Log("Error writing key size. Retrying...", LL_ERROR);
							Server->wait(1000);
						}
						file_pos += sizeof(key_size);

						while (file->Write(file_pos, it->first->c_str(), key_size) != key_size)
						{
							Server->Log("Error writing key. Retrying...", LL_ERROR);
							Server->wait(1000);
						}
						file_pos += key_size;
					}
					else
					{
						if (memf->key != *it->first)
						{
							Server->Log("Memfile key wrong. Expected " + hexpath(*it->first) + " got " + hexpath(memf->key), LL_ERROR);
							abort();
						}

						assert(!memf->cow);
						has_memf = true;
						SMemFile new_memf = *memf;
						new_memf.evicted = false;
						new_memf.cow = true;
						
						memfiles.put_after(std::make_pair(transid, *it->first),
							std::make_pair(new_trans, *it->first), new_memf);
						++num_mem_files[new_trans];
					}

					if (do_submit
						|| memf != nullptr)
					{
						int64 fsize;
						if (memf == nullptr)
						{
							std::unique_ptr<IFsFile> input_file(cachefs->openFile(keypath2(*it->first, transid) + (compressed ? ".comp" : ""), MODE_READ));
							if (input_file.get() == nullptr)
							{
								Server->Log("Error opening file to submit", LL_ERROR);
								assert(false);
								continue;
							}
							fsize = input_file->Size();
						}
						else
						{
							assert(!cacheFileExists(keypath2(*it->first, transid) + (compressed ? ".comp" : "")));
							fsize = memf->file->Size();

							++num_memf_dirty;
							++memf_dirty_items[*it->first];
							memf_dirty_items_size[*it->first] += fsize;
						}

						SSubmissionItem item;
						item.key = *it->first;
						item.transid = transid;
						item.action = SubmissionAction_Dirty;
						item.size = fsize;
						item.compressed = compressed;

						submit_end = submission_queue_insert(item, memf != nullptr, submit_end);
						submission_items.insert(std::make_pair(std::make_pair(transid, *it->first), submit_end));

						if (memf != nullptr)
						{
							submitted_memfiles++;
							submitted_memfile_size += memf->size;
						}

						submitted_bytes += item.size;

						it->second.dirty = false;
					}
					else
					{
						
						nosubmit_dirty_items[transid].insert(*it->first);
						nosubmit_untouched_items.insert(*it->first);
					}
				}
				else if (!compressed)
				{
					if (memfiles.change_key(std::make_pair(transid, *it->first),
						std::make_pair(new_trans, *it->first)))
					{
						assert(num_mem_files[transid] > 0);
						auto it = num_mem_files.find(transid);
						--it->second;
						if (it->second == 0)
						{
							num_mem_files.erase(it);
						}
						++num_mem_files[new_trans];
					}
				}

				++it;
			}

			if (!compressed && it == lru_cache.get_list().end())
			{
				it = compressed_items.get_list().begin();
				compressed = true;
			}
		}
	}

#ifndef NDEBUG
	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		memfiles.check();
	}
#endif

	for (std::set<std::string>::iterator it = dirty_evicted_items.begin();
		it != dirty_evicted_items.end(); ++it)
	{
		if(!dirty_list.empty()) dirty_list+=", ";
		dirty_list+=hex(*it);
		
		char ch_compressed = compressed_evicted_dirty;
		while (file->Write(file_pos, &ch_compressed, sizeof(ch_compressed)) != sizeof(ch_compressed))
		{
			Server->Log("Error writing compressed flag (2). Retrying...", LL_ERROR);
			Server->wait(1000);
		}
		file_pos += sizeof(ch_compressed);

		unsigned int key_size = static_cast<unsigned int>(it->size());
		while (file->Write(file_pos, reinterpret_cast<char*>(&key_size), sizeof(key_size)) != sizeof(key_size))
		{
			Server->Log("Error writing key size (2). Retrying...", LL_ERROR);
			Server->wait(1000);
		}
		file_pos += sizeof(key_size);

		while (file->Write(file_pos, it->c_str(), key_size) != key_size)
		{
			Server->Log("Error writing key (2). Retrying...", LL_ERROR);
			Server->wait(1000);
		}
		file_pos += key_size;
	}

	file->Sync();
	
	Server->Log("List of written dirty items for transid "+convert(transid)+": "+dirty_list);

	if(do_submit)
	{
		submit_dummy(transid);
		dirty_evicted_items.clear();
	}
	else
	{
		while(true)
		{
			std::unique_ptr<IFsFile> file_nosubmit(cachefs->openFile(fn+".nosubmit", MODE_WRITE));
			if(file_nosubmit.get())
			{
				break;
			}
			else
			{
				Server->Log("Error creating "+fn+".nosubmit. Retrying...", LL_WARNING);
				Server->wait(1000);
			}
		}
	}
	
	file.reset();
	
	if(!cachefs->rename(fn+".new", has_memf ? (fn + ".mem") : fn))
	{
		Server->Log("Error renaming "+fn+" to "+fn+".new", LL_ERROR);
		return false;
	}

	evict_cond.notify_all();

	check_submission_items();
	
	return true;
}


bool TransactionalKvStore::read_from_deleted_file( const std::string& fn, int64 transaction_id, bool do_submit )
{
	std::unique_ptr<IFsFile> file(cachefs->openFile(fn, MODE_READ));

	if(file.get()==nullptr)
	{
		Server->Log("Cannot open \""+fn+"\". " + os_last_error_str(), LL_INFO);
		return true;
	}

	std::vector<std::string> del_keys;

	while(true)
	{
		unsigned int key_size;
		if(file->Read(reinterpret_cast<char*>(&key_size), sizeof(key_size))!=sizeof(key_size))
		{
			break;
		}

		std::string key;
		key.resize(key_size);

		if(file->Read(&key[0], key_size)!=key_size)
		{
			Server->Log("Error reading key from deleted file", LL_ERROR);
			return false;
		}

		if(do_submit)
		{
			del_keys.push_back(key);

			if(del_keys.size()>online_kv_store->max_del_size())
			{
				SSubmissionItem item;
				item.keys = del_keys;
				item.transid = transaction_id;
				item.action = SubmissionAction_Delete;
				item.size = 0;
				item.compressed=false;

				submission_queue_add(item, false);

				addDirtyItem(transaction_id, item.key, false);
				{
					std::scoped_lock dirty_lock(dirty_item_mutex);
					++num_delete_items[transaction_id];
				}

				del_keys.clear();
			}			
		}
		else
		{
			queued_dels.insert(key);
		}
	}

	if(do_submit && !del_keys.empty())
	{
		SSubmissionItem item;
		item.keys = del_keys;
		item.transid = transaction_id;
		item.action = SubmissionAction_Delete;
		item.size = 0;
		item.compressed=false;

		submission_queue_add(item, false);

		addDirtyItem(transaction_id, item.key, false);
		std::scoped_lock dirty_lock(dirty_item_mutex);
		++num_delete_items[transaction_id];
	}
	
	return true;
}

bool TransactionalKvStore::write_to_deleted_file(const std::string& fn, bool do_submit)
{
	std::unique_ptr<IFsFile> file(cachefs->openFile(fn, MODE_WRITE));
	int64 file_pos=0;

	if(file.get()==nullptr)
	{
		Server->Log("Error opening deleted file for writing", LL_ERROR);
		return false;
	}

	std::lock_guard sub_lock(submission_mutex);

	std::vector<std::string> del_keys;

	for(std::set<std::string>::iterator it=queued_dels.begin();it!=queued_dels.end();++it)
	{
		unsigned int key_size = static_cast<unsigned int>(it->size());
		while(file->Write(file_pos, reinterpret_cast<char*>(&key_size), sizeof(key_size))!=sizeof(key_size))
		{
			Server->Log("Error writing key size. Retrying...", LL_ERROR);
			Server->wait(1000);
		}
		file_pos+=sizeof(key_size);

		while(file->Write(file_pos, it->c_str(), key_size)!=key_size)
		{
			Server->Log("Error writing key. Retrying...", LL_ERROR);
			Server->wait(1000);
		}
		file_pos+=key_size;

		if(do_submit)
		{
			del_keys.push_back(*it);

			if(del_keys.size()>online_kv_store->max_del_size())
			{
				SSubmissionItem item;
				item.keys = del_keys;
				item.transid = transid;
				item.action = SubmissionAction_Delete;
				item.size=0;
				item.compressed=false;

				submission_queue_add(item, false);

				addDirtyItem(transid, item.key, false);
				{
					std::scoped_lock dirty_lock(dirty_item_mutex);
					++num_delete_items[transid];
				}

				del_keys.clear();
			}
		}
	}

	if(do_submit && !del_keys.empty())
	{
		SSubmissionItem item;
		item.keys = del_keys;
		item.transid = transid;
		item.action = SubmissionAction_Delete;
		item.size=0;
		item.compressed=false;

		submission_queue_add(item, false);

		addDirtyItem(transid, item.key, false);
		std::scoped_lock dirty_lock(dirty_item_mutex);
		++num_delete_items[transid];
	}

	file->Sync();

	if(do_submit)
	{
		queued_dels.clear();
	}
	
	return true;
}


void TransactionalKvStore::delete_item(fuse_io_context* io, const std::string& key, bool compressed_item,
		std::unique_lock<cache_mutex_t>& cache_lock,
		int64 force_delete_transid, int64 skip_transid, DeleteImm del_imm, int64 delete_only,
	    bool rm_submitted, int64 ignore_sync_wait_transid)
{
	assert(cache_lock.owns_lock());
	assert(io == nullptr || del_imm== DeleteImm::None);

	Server->Log("Deleting object "+hex(key)+" compressed="+convert(compressed_item)+
				" force_delete_transid="+convert(force_delete_transid), LL_INFO);

	std::vector<std::string> del_queue_local;

	std::unique_lock submission_lock(submission_mutex);

	for(size_t i=0;i<transactions.size();++i)
	{
		int64 ctransid = watoi64(getafter("trans_", transactions[i].name));

		if (skip_transid == ctransid)
		{
			continue;
		}

		if (delete_only != 0
			&& ctransid != delete_only)
		{
			continue;
		}

		if(transactions[i].isdir &&
			transactions[i].name.find("trans_")!=std::string::npos)
		{
			bool nosubmit_dirty = false;
			if (submission_items.find(std::make_pair(ctransid, key)) == submission_items.end()
				&& force_delete_transid != ctransid)
			{
				std::scoped_lock dirty_lock(dirty_item_mutex);
				auto it_ld = nosubmit_dirty_items.find(ctransid);
				if (it_ld != nosubmit_dirty_items.end()
					&& it_ld->second.find(key) != it_ld->second.end())
				{
					nosubmit_dirty = true;
				}
			}

			if(submission_items.find(std::make_pair(ctransid, key))==submission_items.end()
				&& !nosubmit_dirty
				&& (ctransid == transid || 
					ctransid == ignore_sync_wait_transid ||
					!is_sync_wait_item(key, ctransid) ) )
			{
				if (!compressed_item
					&& ctransid==transid)
				{
					rm_mem_file(io, ctransid, key, rm_submitted);
				}

				if (ctransid == transid
					&& fd_cache.has_key(key))
				{
					Server->Log("Found key " + keypath2(key, transid) + " in fd cache during deletion", LL_ERROR);
					abort();
				}

				std::string del_path = 
					transactions[i].name + cachefs->fileSep() + hexpath(key) + (compressed_item ? ".comp" : "");
				if (del_imm!=DeleteImm::None)
				{
					del_queue_local.push_back(del_path);
				}
				else
				{
					IScopedLock lock(del_file_mutex.get());
					del_file_queue.insert(del_path);
				}
			}
		}
	}

	if (!del_queue_local.empty())
	{
		submission_lock.unlock();

		if (del_imm == DeleteImm::Unlock)
		{
			cache_lock.unlock();
		}

		for (size_t i = 0; i < del_queue_local.size(); ++i)
		{
			cachefs->deleteFile(del_queue_local[i]);
		}

		if (del_imm == DeleteImm::Unlock)
		{
			cache_lock.lock();
		}
	}
}

void TransactionalKvStore::run_del_file_queue()
{
	IScopedLock lock_single(del_file_single_mutex.get());
	IScopedLock lock(del_file_mutex.get());
	while (!del_file_queue.empty())
	{
		std::string fn;
		bool is_begin;
		if (!prio_del_file.empty()
			&& del_file_queue.find(prio_del_file) != del_file_queue.end())
		{
			fn = prio_del_file;
			is_begin = false;
		}
		else
		{
			fn = *del_file_queue.begin();
			is_begin = true;
		}

		lock.relock(nullptr);
		cachefs->deleteFile(fn);
		lock.relock(del_file_mutex.get());
		if (is_begin
			&& *del_file_queue.begin() == fn)
		{
			del_file_queue.erase(del_file_queue.begin());
		}
		else
		{
			del_file_queue.erase(fn);
		}

		if (!is_begin)
		{
			prio_del_file_cond->notify_all();
		}
	}
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<void> TransactionalKvStore::run_del_file_queue_async(fuse_io_context& io)
{
	co_await io.run_in_threadpool([this]() {
		this->run_del_file_queue();
		}, "rdel file");
}
#endif

void TransactionalKvStore::wait_for_del_file(const std::string & fn)
{
	IScopedLock lock(del_file_mutex.get());
	bool did_wait = false;
	while (del_file_queue.find(fn)!= del_file_queue.end())
	{
		prio_del_file = fn;
		did_wait = true;
		prio_del_file_cond->wait(&lock, 1);
	}

	if (did_wait
		&& prio_del_file == fn)
	{
		prio_del_file.clear();
	}
}

void TransactionalKvStore::cleanup(bool init)
{
	std::vector<SFile> files = cachefs->listFiles(std::string());

	int64 maxsubmitted=-1;
	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir &&
			files[i].name.find("trans_")==0)
		{
			int64 ctransid = watoi64(getafter("trans_", files[i].name));

			if(ctransid==transid)
			{
				continue;
			}

			std::unique_ptr<IFsFile> commited(cachefs->openFile(files[i].name + cachefs->fileSep() + "commited", MODE_READ));
			std::unique_ptr<IFsFile> dirty(cachefs->openFile(files[i].name+ cachefs->fileSep()+"dirty", MODE_READ));
			std::unique_ptr<IFsFile> nosubmit(cachefs->openFile(files[i].name+ cachefs->fileSep()+"dirty.nosubmit", MODE_READ));
			std::unique_ptr<IFsFile> dirty_mem(cachefs->openFile(files[i].name + cachefs->fileSep() + "dirty.mem", MODE_READ));

			if( (dirty.get()!=nullptr || nosubmit.get()!=nullptr || commited.get()!=nullptr)
				&& dirty_mem.get()==nullptr)
			{
				maxsubmitted = (std::max)(maxsubmitted, ctransid);
			}
		}
	}

	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir &&
			files[i].name.find("trans_")==0)
		{
			int64 ctransid = watoi64(getafter("trans_", files[i].name));

			std::unique_ptr<IFsFile> dirty(cachefs->openFile(files[i].name+ cachefs->fileSep()+"dirty", MODE_READ));
			std::unique_ptr<IFsFile> dirty_mem(cachefs->openFile(files[i].name + cachefs->fileSep() + "dirty.mem", MODE_READ));
			std::unique_ptr<IFsFile> invalid(cachefs->openFile(files[i].name+ cachefs->fileSep()+"invalid", MODE_READ));

			if(ctransid==maxsubmitted || ctransid==transid)
			{
				continue;
			}

			{
				std::scoped_lock dirty_lock(dirty_item_mutex);
				std::map<int64, size_t>::iterator it_di = num_dirty_items.find(ctransid);
				if (it_di != num_dirty_items.end())
				{
					continue;
				}
			}

			assert(num_mem_files.find(ctransid) == num_mem_files.end());

			if( (dirty.get()==nullptr && dirty_mem.get()==nullptr)
				|| invalid.get()!=nullptr )
			{
				if(invalid)
					Server->Log("Removing invalid transaction " + std::to_string(ctransid) + " (3)", LL_INFO);

				remove_transaction(ctransid);
			}
			else if(ctransid<maxsubmitted
				&& dirty_mem.get()==nullptr)
			{
				std::unique_lock dirty_lock(dirty_item_mutex);

				if ((num_dirty_items.find(ctransid) == num_dirty_items.end()
					|| num_dirty_items[ctransid] == 0)
					&& (num_delete_items.find(ctransid) == num_delete_items.end()
						|| num_delete_items[ctransid] == 0))
				{
					dirty_lock.unlock();

					if (!init)
					{
						std::unique_ptr<IFsFile> commited(cachefs->openFile(files[i].name + cachefs->fileSep() + "commited", MODE_READ));
						std::unique_ptr<IFsFile> nosubmit(cachefs->openFile(files[i].name + cachefs->fileSep() + "dirty.nosubmit", MODE_READ));
						if (dirty.get() == nullptr
							|| nosubmit.get() != nullptr
							|| commited.get() != nullptr)
						{
							remove_transaction(ctransid);
						}
					}
					else
					{
						remove_transaction(ctransid);
					}
				}
			}
		}
	}

	update_transactions();
}

void TransactionalKvStore::clean_snapshot( int64 new_trans )
{
	cachefs->deleteFile("trans_"+convert(new_trans)+ cachefs->fileSep() + "deleted");
	cachefs->deleteFile("trans_"+convert(new_trans)+ cachefs->fileSep() + "dirty");
	cachefs->deleteFile("trans_" + convert(new_trans) + cachefs->fileSep() + "dirty.evicted");
	cachefs->deleteFile("trans_" + convert(new_trans) + cachefs->fileSep() + "dirty.submitted");
	cachefs->deleteFile("trans_"+convert(new_trans)+ cachefs->fileSep() + "dirty.nosubmit");
	cachefs->deleteFile("trans_"+convert(new_trans)+ cachefs->fileSep() + "commited");

	if(cachefs->directoryExists("trans_"+convert(new_trans)+ cachefs->fileSep() + "submitted"))
	{
		cachefs->removeDirRecursive( "trans_"+convert(new_trans)+ cachefs->fileSep() + "submitted");
	}
	
	if(cachefs->directoryExists( "trans_"+convert(new_trans)+ cachefs->fileSep() + "evicted"))
	{
		cachefs->removeDirRecursive( "trans_"+convert(new_trans)+ cachefs->fileSep() + "evicted");
	}
}

int64 TransactionalKvStore::get_dirty_bytes()
{
	return dirty_bytes;
}

int64 TransactionalKvStore::get_submitted_bytes()
{
	return submitted_bytes;	
}

int64 TransactionalKvStore::get_total_submitted_bytes()
{
	return total_submitted_bytes;
}

std::map<int64, size_t> TransactionalKvStore::get_num_dirty_items()
{
	std::scoped_lock lock(dirty_item_mutex);

	return num_dirty_items;	
}

std::map<int64, size_t> TransactionalKvStore::get_num_memfile_items()
{
	std::scoped_lock lock(cache_mutex);
	return num_mem_files;
}

void TransactionalKvStore::submit_dummy(int64 transaction_id)
{
	SSubmissionItem item;
	item.key = std::string();
	item.transid = transaction_id;
	item.action = SubmissionAction_Dirty;
	item.size=0;
	item.compressed=false;

	submission_items.insert(std::make_pair(std::make_pair(transaction_id, std::string()), 
		submission_queue_add(item, false)));

	addDirtyItem(transaction_id, item.key, false);
}

int64 TransactionalKvStore::get_cache_size()
{
	return cachesize;	
}

void TransactionalKvStore::reset()
{
	assert(fuse_io_context::is_sync_thread());

	std::unique_lock lock(cache_mutex);

	wait_for_all_retrievals(lock);

	std::unique_lock sub_lock(submission_mutex);

	assert(open_files.empty());

	fd_cache.clear();

	queued_dels.clear();

	std::unique_lock dirty_lock(dirty_item_mutex);

	bool compressed=false;
	size_t curr_dirty_items=num_dirty_items[transid];
#ifdef DIRTY_ITEM_CHECK
	std::map<std::string, size_t> curr_dirty_item_keys = dirty_items[transid];
#endif
	for(common::lrucache<std::string, SCacheVal>::list_t::iterator it=lru_cache.get_list().begin();
		!compressed || it!=lru_cache.get_list().end();)
	{
		if(compressed || it!=lru_cache.get_list().end())
		{
			if(it->second.dirty)
			{
				assert(curr_dirty_items > 0);
				--curr_dirty_items;
#ifdef DIRTY_ITEM_CHECK
				assert(curr_dirty_item_keys[*it->first] > 0);
				--curr_dirty_item_keys[*it->first];
				if (curr_dirty_item_keys[*it->first] == 0)
				{
					curr_dirty_item_keys.erase(curr_dirty_item_keys.find(*it->first));
				}
#endif

				std::unique_ptr<IFsFile> input_file(cachefs->openFile(keypath2(*it->first, transid) + (compressed?".comp":""), MODE_READ));

				if(input_file.get()==nullptr)
				{
					Server->Log("Error opening file to reset", LL_ERROR);
					continue;
				}

				dirty_bytes-=input_file->Size();
			}

			++it;
		}
		
		if(it==lru_cache.get_list().end())
		{
			it=compressed_items.get_list().begin();
			compressed=true;
		}
	}

	if(curr_dirty_items==0)
	{
		num_dirty_items.erase(num_dirty_items.find(transid));
#ifdef DIRTY_ITEM_CHECK
		assert(curr_dirty_item_keys.empty());
		dirty_items.erase(dirty_items.find(transid));
#endif
	}
	else
	{
		num_dirty_items[transid] = curr_dirty_items;
#ifdef DIRTY_ITEM_CHECK
		dirty_items[transid] = curr_dirty_item_keys;
#endif
	}

	lru_cache.clear();
	compressed_items.clear();

	for(std::list<SSubmissionItem>::iterator it=submission_queue.begin();
		it!=submission_queue.end();)
	{
		if(it->action==SubmissionAction_Evict ||
			it->action==SubmissionAction_Dirty ||
			it->action==SubmissionAction_Delete ||
			it->action==SubmissionAction_Compress )
		{
			if(it->action!=SubmissionAction_Delete)
			{
				submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));
			}

			removeDirtyItem(it->transid, it->key);
			std::map<int64, size_t>::iterator di = num_dirty_items.find(it->transid);
			if(di!=num_dirty_items.end())
			{
				if(di->second==0)
				{
					num_dirty_items.erase(di);
#ifdef DIRTY_ITEM_CHECK
					assert(dirty_items[it->transid].empty());
					dirty_items.erase(dirty_items.find(it->transid));
#endif
				}
			}
			SMemFile* f;
			{
				std::scoped_lock memfile_lock(memfiles_mutex);
				f = memfiles.get(std::make_pair(it->transid, it->key), false);
			}
			if (f != nullptr)
			{
				assert(submitted_memfiles > 0);
				--submitted_memfiles;
				rm_mem_file(nullptr, it->transid, it->key, true);
			}

			dirty_bytes-=it->size;

			submitted_bytes-=it->size;

			std::list<SSubmissionItem>::iterator it_del = it;
			++it;
			submission_queue_rm(it_del);
		}
		else
		{
			++it;
		}
	}

	dirty_lock.unlock();

	check_submission_items();

	int64 maxtrans = set_active_transactions(lock, false);

	Server->Log("Resetting to transaction "+convert(maxtrans));

	assert(maxtrans>=0);

	size_t retry_n=0;
	do
	{
		transid = online_kv_store->new_transaction(retry_n>retry_log_n);
		if(transid==0)
		{
			Server->Log("Error requesting new transaction. Retrying...", LL_WARNING);
			retryWait(++retry_n);
		}
	}
	while(transid==0);

	retry_n=0;
	while(!cachefs->createSnapshot("trans_"+std::to_string(maxtrans), "trans_"+std::to_string(transid)))
	{
		Server->Log("Error creating new transaction subvolume. Retrying...", LL_ERROR);
		retryWait(++retry_n);
	}
	update_transactions();
	clean_snapshot(transid);

	cachesize=0;
	read_keys(lock, transpath2(), false);

	basetrans = maxtrans;
	
	retry_n=0;
	while(!read_dirty_items(lock, basetrans, transid))
	{
		Server->Log("Error reading dirty items. Retrying...");
		retryWait(++retry_n);
	}
}

bool TransactionalKvStore::is_congested()
{
	std::scoped_lock lock(cache_mutex);
	return is_congested_nolock();
}

bool TransactionalKvStore::is_congested_nolock()
{
	return remaining_gets != std::string::npos;
}

bool TransactionalKvStore::is_congested_async()
{
	return remaining_gets != std::string::npos;
}

int64 TransactionalKvStore::get_memfile_bytes()
{
	return memfile_size;
}

int64 TransactionalKvStore::get_submitted_memfile_bytes()
{
	return submitted_memfile_size;
}

bool TransactionalKvStore::is_memfile_complete(int64 ttransid)
{
	std::scoped_lock lock(cache_mutex);
	return num_mem_files.find(ttransid) == num_mem_files.end();
}

std::string TransactionalKvStore::meminfo()
{
	assert(fuse_io_context::is_sync_thread());

	std::string ret;
	{
		std::unique_lock cache_lock(cache_mutex);

		ret += "##TransactionalKvStore:\n";
#define MEMINFO_ITEM_SIZE(name, itemsize) ret += "  "  #name ": " + convert(name.size()) + " * " + PrettyPrintBytes(itemsize)+" = "+PrettyPrintBytes(name.size()*(itemsize)) + "\n"
		MEMINFO_ITEM_SIZE(lru_cache, sizeof(std::string) + sizeof(bool) + 3 * sizeof(void*));
		size_t lru_second_chances = 0;
		for (auto it : lru_cache.get_list())
		{
			if (it.second.chances > 0)
			{
				++lru_second_chances;
			}
		}
		ret += "  lru_cache items with more chances: " + convert(lru_second_chances) + (lru_cache.size()>0 ? (" ("+ convert(lru_second_chances*100/lru_cache.size())+"%)\n") :"\n");
		MEMINFO_ITEM_SIZE(compressed_items, sizeof(std::string) + sizeof(bool) + 3 * sizeof(void*));
		lru_second_chances = 0;
		for (auto it : compressed_items.get_list())
		{
			if (it.second.chances > 0)
			{
				++lru_second_chances;
			}
		}
		ret += "  compressed_items items with more chances: " + convert(lru_second_chances) + (compressed_items.size()>0 ? (" (" + convert(lru_second_chances * 100 / compressed_items.size()) + "%)\n") : "\n");
		MEMINFO_ITEM_SIZE(open_files, sizeof(std::string) + sizeof(SFdKey));
		MEMINFO_ITEM_SIZE(read_only_open_files, sizeof(IFsFile*) + sizeof(ReadOnlyFileWrapper*) + sizeof(ReadOnlyFileWrapper));
		MEMINFO_ITEM_SIZE(preload_once_items, sizeof(std::string)+sizeof(int));
		MEMINFO_ITEM_SIZE(preload_once_delayed_removal, sizeof(std::string)+sizeof(int64));

		{
			std::scoped_lock submission_lock(submission_mutex);
			MEMINFO_ITEM_SIZE(submission_queue, sizeof(SSubmissionItem));
			MEMINFO_ITEM_SIZE(submission_items, sizeof(int64) + sizeof(std::string) + sizeof(std::list<SSubmissionItem>::iterator));
			MEMINFO_ITEM_SIZE(dirty_evicted_items, sizeof(std::string));

			std::scoped_lock dirty_lock(dirty_item_mutex);
			MEMINFO_ITEM_SIZE(nosubmit_dirty_items, sizeof(int64) + sizeof(std::set<std::string>));
			for (auto it : nosubmit_dirty_items)
			{
				ret+= "  nosubmit_dirty_items["+convert(it.first)+"]: "+convert(it.second.size())+" * " + PrettyPrintBytes(sizeof(std::string)) + " = " + PrettyPrintBytes(it.second.size()*sizeof(std::string))+"\n";
			}
		}

		MEMINFO_ITEM_SIZE(nosubmit_untouched_items, sizeof(std::string));

		{
			std::scoped_lock dirty_lock(dirty_item_mutex);
			MEMINFO_ITEM_SIZE(num_dirty_items, sizeof(int64) + sizeof(size_t));
#ifdef DIRTY_ITEM_CHECK
			for (auto it : dirty_items)
			{
				ret += "  dirty_items[" + convert(it.first) + "]: " + convert(it.second.size()) + " * " + PrettyPrintBytes(sizeof(std::string) + sizeof(size_t)) + " = "+PrettyPrintBytes(it.second.size()*(sizeof(std::string) + sizeof(size_t)))+"\n";
			}
			MEMINFO_ITEM_SIZE(dirty_items, sizeof(int64) + sizeof(size_t));
#endif
			MEMINFO_ITEM_SIZE(num_delete_items, sizeof(int64) + sizeof(size_t));
		}
		
		ret += "  fd_cache: " + convert(fd_cache.size()) + "/" + convert(fd_cache_size) + " * " + PrettyPrintBytes(sizeof(std::string) + sizeof(SFdKey) + 3 * sizeof(void*)) + " = " + PrettyPrintBytes(fd_cache.size()*(sizeof(std::string) + sizeof(SFdKey) + 3 * sizeof(void*))) + "\n";
		MEMINFO_ITEM_SIZE(queued_dels, sizeof(std::string));
		MEMINFO_ITEM_SIZE(in_retrieval, sizeof(std::string));
		MEMINFO_ITEM_SIZE(transactions, sizeof(SFile));
		
		{
			std::scoped_lock memfile_lock(memfiles_mutex);
			MEMINFO_ITEM_SIZE(memfiles, sizeof(int64) + sizeof(std::string) + sizeof(SMemFile) + 3 * sizeof(void*));
			ret += "  memfile used size: " + PrettyPrintBytes(memfile_size) + "\n";

			int64 memfile_size_check = 0;
			for (auto it : memfiles.get_list())
			{
				if(it.second.file!=nullptr)
					memfile_size_check+=it.second.file->Size();
			}

			ret += "  memfile_size_check: " + PrettyPrintBytes(memfile_size_check) + "\n";

			int64 memfile_size_non_dirty = 0;
			int64 memfile_size_dirty = 0;
			for (auto it : memfiles.get_list())
			{
				if (it.second.file != nullptr)
				{
					SCacheVal* dirty = cache_get(lru_cache, it.first->second, cache_lock, false);
					if (dirty != nullptr
						&& !dirty->dirty)
					{
						memfile_size_non_dirty += it.second.file->Size();
					}
					else if (dirty != nullptr
						&& dirty->dirty)
					{
						memfile_size_dirty+= it.second.file->Size();
					}
				}
			}

			ret += "  memfile_size_non_dirty: " + PrettyPrintBytes(memfile_size_non_dirty) + "\n";
			ret += "  memfile_size_dirty: " + PrettyPrintBytes(memfile_size_dirty) + "\n";

			ret += "  memfile_stat_bitmaps: " + convert(memfile_stat_bitmaps.capacity()) + " * " + PrettyPrintBytes(sizeof(int64) + sizeof(Bitmap) + sizeof(std::unique_ptr<Bitmap>)) + "\n";
			for (auto& it : memfile_stat_bitmaps)
			{
				ret += "  memfile_stat_bitmaps["+convert(it.first)+"]: "+ PrettyPrintBytes(it.second->meminfo()) + "\n";
			}
			MEMINFO_ITEM_SIZE(num_mem_files, sizeof(int64) + sizeof(size_t));
		}

		ret += "  submit_bundle: " + convert(submit_bundle.capacity()) + " * " + PrettyPrintBytes(sizeof(SSubmissionItem)+sizeof(bool)) + " = " + PrettyPrintBytes(submit_bundle.capacity()*(sizeof(SSubmissionItem) + sizeof(bool)))+"\n";
		IScopedLock bundle_lock(submit_bundle_item_mutex.get());
		MEMINFO_ITEM_SIZE(submit_bundle_items_a, sizeof(std::string) + sizeof(int64));
		MEMINFO_ITEM_SIZE(submit_bundle_items_b, sizeof(std::string) + sizeof(int64));
	}

	{
		IScopedLock lock(del_file_mutex.get());
		MEMINFO_ITEM_SIZE(del_file_queue, sizeof(std::string));
	}

	ret += online_kv_store->meminfo();

	return ret;
}

void TransactionalKvStore::shrink_mem()
{
	assert(fuse_io_context::is_sync_thread());

	std::scoped_lock cache_lock(cache_mutex);

	evict_non_dirty_memfiles = true;

	common::lrucache<std::string, SFdKey>::list_t::iterator it = fd_cache.eviction_iterator_start();
	if (it == fd_cache.eviction_iterator_finish())
	{
		return;
	}

	--it;

	while (true)
	{
		bool last = it == fd_cache.eviction_iterator_finish();

		if (open_files.find(*it->first) == open_files.end())
		{
			assert(dynamic_cast<IMemFile*>(it->second.fd) == nullptr);
			Server->destroy(it->second.fd);
			std::string item_key = *it->first;

			if (!last)
				--it;

			fd_cache.del(item_key);
		}
		else
		{
			if (!last)
				--it;
		}

		if (last)
			break;
	}
}

int64 TransactionalKvStore::get_total_cache_miss_backend()
{
	return total_cache_miss_backend;
}

int64 TransactionalKvStore::get_total_cache_miss_decompress()
{
	return total_cache_miss_decompress;
}

int64 TransactionalKvStore::get_total_dirty_ops()
{
	return total_dirty_ops;
}

int64 TransactionalKvStore::get_total_put_ops()
{
	return total_put_ops;
}

int64 TransactionalKvStore::get_total_compress_ops()
{
	return total_compress_ops;
}

void TransactionalKvStore::disable_compression(int64 disablems)
{
	std::scoped_lock lock(cache_mutex);
	compression_starttime = Server->getTimeMS() + disablems;
}

void TransactionalKvStore::set_num_second_chances_callback(INumSecondChancesCallback * cb)
{
	std::scoped_lock lock(cache_mutex);

	num_second_chances_cb = cb;

	if (num_second_chances_cb == nullptr)
		return;

	for (auto& it : lru_cache.get_list())
	{
		it.second = cache_val(*it.first, it.second.dirty);
	}

	for (auto& it : compressed_items.get_list())
	{
		it.second = cache_val(*it.first, it.second.dirty);
	}
}

int64 TransactionalKvStore::get_total_hits()
{
	std::scoped_lock lock(cache_mutex);
	return total_hits;
}

int64 TransactionalKvStore::get_total_hits_async()
{
	return total_hits;
}

int64 TransactionalKvStore::get_total_memory_hits()
{
	std::scoped_lock lock(cache_mutex);
	return total_memory_hits;
}

int64 TransactionalKvStore::get_total_memory_hits_async()
{
	return total_memory_hits;
}

int64 TransactionalKvStore::set_active_transactions(std::unique_lock<cache_mutex_t>& cache_lock, bool continue_incomplete)
{
	std::vector<SFile> files = cachefs->listFiles(std::string());

	std::vector<int64> active_transactions;
	int64 maxtrans=-1;
	int64 mintrans = -1;
	for (size_t i = 0; i < files.size(); ++i)
	{
		if (files[i].isdir &&
			files[i].name.find("trans_") == 0)
		{
			int64 ctransid = watoi64(getafter("trans_", files[i].name));

			std::unique_ptr<IFsFile> dirty_mem(cachefs->openFile(files[i].name + cachefs->fileSep() + "dirty.mem", MODE_READ));
			if (dirty_mem.get() != nullptr
				&& (mintrans == -1 || ctransid < mintrans) )
			{
				mintrans = ctransid;
			}
		}
	}

	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir &&
			files[i].name.find("trans_")==0)
		{
			int64 ctransid = watoi64(getafter("trans_", files[i].name));
			std::unique_ptr<IFsFile> dirty(cachefs->openFile(files[i].name+ cachefs->fileSep()+"dirty", MODE_READ));
			std::unique_ptr<IFsFile> commited(cachefs->openFile(files[i].name+ cachefs->fileSep()+"commited", MODE_READ));
			std::unique_ptr<IFsFile> invalid(cachefs->openFile(files[i].name+ cachefs->fileSep()+"invalid", MODE_READ));
			std::unique_ptr<IFsFile> dirty_mem(cachefs->openFile(files[i].name + cachefs->fileSep() + "dirty.mem", MODE_READ));

			if (dirty_mem.get() != nullptr)
			{
				continue;
			}

			if(dirty.get()!=nullptr 
				&& commited.get()==nullptr
				&& continue_incomplete 
				&& invalid.get()==nullptr
				&& (mintrans==-1 || ctransid<mintrans) )
			{
				std::unique_ptr<IFsFile> nosubmit(cachefs->openFile(files[i].name+ cachefs->fileSep()+"dirty.nosubmit", MODE_READ));
				if(nosubmit.get()==nullptr)
				{
					dirty.reset();

					while(!read_from_deleted_file(files[i].name+ cachefs->fileSep()+"deleted", ctransid, true))
					{
						Server->Log("Error reading from deleted file. Retrying...", LL_ERROR);
						Server->wait(10000);
					}
					while(!read_from_dirty_file(cache_lock, files[i].name+ cachefs->fileSep()+"dirty", ctransid, true, 0))
					{
						Server->Log("Error reading from dirty file. Retrying...", LL_ERROR);
						Server->wait(10000);
					}
				}
				
				maxtrans = (std::max)(ctransid, maxtrans);

				active_transactions.push_back(ctransid);
			}
			else if(commited.get()!=nullptr 
				&& invalid.get()==nullptr
				&& (mintrans == -1 || ctransid<mintrans) )
			{
				maxtrans = (std::max)(ctransid, maxtrans);
				active_transactions.push_back(ctransid);
			}
			else
			{
				std::unique_lock dirty_lock(dirty_item_mutex);
				if(num_dirty_items.find(ctransid)==num_dirty_items.end())
				{
					DIRTY_ITEM(assert(dirty_items.find(ctransid) == dirty_items.end()));
					dirty_lock.unlock();
					Server->Log("Removing transaction "+convert(ctransid)+" (no dirty items 1)");
					remove_transaction(ctransid);
				}
				else
				{
					std::unique_ptr<IFsFile> invalid(cachefs->openFile(files[i].name+ cachefs->fileSep()+"invalid", MODE_WRITE));
				}
			}
		}
	}

	for (size_t i = 0; i < files.size(); ++i)
	{
		if (files[i].isdir &&
			files[i].name.find("trans_") == 0)
		{
			int64 ctransid = watoi64(getafter("trans_", files[i].name));
			std::unique_ptr<IFsFile> dirty_mem(cachefs->openFile(files[i].name + cachefs->fileSep() + "dirty.mem", MODE_READ));
			if (dirty_mem.get() != nullptr)
			{
				std::scoped_lock dirty_lock(dirty_item_mutex);
				if (num_dirty_items.find(ctransid) == num_dirty_items.end())
				{
					DIRTY_ITEM(assert(dirty_items.find(ctransid) == dirty_items.end()));
					Server->Log("Removing transaction " + convert(ctransid)+" (no dirty items 2)");
					remove_transaction(ctransid);
				}
				else
				{
					std::unique_ptr<IFsFile> invalid(cachefs->openFile(files[i].name + cachefs->fileSep() + "invalid", MODE_WRITE));
				}
			}
		}
	}

	if(!online_kv_store->set_active_transactions(active_transactions))
	{
		throw std::runtime_error("Error setting active transactions");
	}

	return maxtrans;
}

bool TransactionalKvStore::wait_for_retrieval(std::unique_lock<cache_mutex_t>& lock, const std::string& key )
{
	bool waited=false;
	std::map<std::string, size_t>::iterator it;
	while((it=in_retrieval.find(key))!=in_retrieval.end()
		|| (it=in_retrieval.find(std::string())) != in_retrieval.end())
	{
		++it->second;
		++retrieval_waiters_sync;
		retrieval_cond.wait(lock);
		--retrieval_waiters_sync;
		waited=true;
	}
	return waited;
}

bool TransactionalKvStore::wait_for_retrieval_poll(std::unique_lock<cache_mutex_t>& lock, const std::string& key)
{
	bool waited = false;
	std::map<std::string, size_t>::iterator it;
	while ((it = in_retrieval.find(key)) != in_retrieval.end()
		|| (it = in_retrieval.find(std::string())) != in_retrieval.end())
	{
		lock.unlock();

		Server->wait(100);

		lock.lock();
		waited = true;
	}
	return waited;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<TransactionalKvStore::RetrievalRes> TransactionalKvStore::wait_for_retrieval_async(fuse_io_context& io, 
	std::unique_lock<cache_mutex_t> lock, const std::string key)
{
	RetrievalRes res;
	res.lock = std::move(lock);
	res.waited = false;
	std::map<std::string, size_t>::iterator it;
	while ( (it=in_retrieval.find(key)) != in_retrieval.end()
		|| (it=in_retrieval.find(std::string())) != in_retrieval.end())
	{
		++it->second;
		++retrieval_waiters_async;
		res.lock.unlock();

		co_await RetrievalAwaiter(*this);

		res.lock.lock();
		--retrieval_waiters_async;
		res.waited = true;
	}
	co_return res;
}
#endif

void TransactionalKvStore::initiate_retrieval( const std::string& key )
{
	assert(in_retrieval.find(key) == in_retrieval.end());
	in_retrieval.insert(std::make_pair(key, 0));
}

void TransactionalKvStore::finish_retrieval(const std::string& key )
{
	auto it = in_retrieval.find(key);
	if (it == in_retrieval.end())
		abort();
	assert(it != in_retrieval.end());
	size_t r_waiters = it->second;
	in_retrieval.erase(it);

	assert(retrieval_waiters_async == 0);

	if (r_waiters > 0 )
	{
		if (retrieval_waiters_async>0)
			abort();

		retrieval_cond.notify_all();
	}
}

#ifdef HAS_ASYNC
void TransactionalKvStore::finish_retrieval_async(fuse_io_context& io, std::unique_lock<cache_mutex_t> lock, const std::string& key)
{
	auto it = in_retrieval.find(key);
	if (it == in_retrieval.end())
		abort();
	assert(it != in_retrieval.end());
	size_t r_waiters = it->second;
	in_retrieval.erase(it);

	assert(retrieval_waiters_sync == 0);

	if (r_waiters > 0 )
	{
		if (retrieval_waiters_sync > 0)
			abort();

		lock.unlock();

		resume_retrieval_awaiters();
	}
}
#endif

void TransactionalKvStore::wait_for_all_retrievals(std::unique_lock<cache_mutex_t>& lock )
{
	while(!in_retrieval.empty())
	{
		lock.unlock();

		Server->wait(10);

		lock.lock();
	}
}

bool TransactionalKvStore::compress_item( const std::string& key, int64 transaction_id, IFsFile* src,
	int64& size_diff, int64& dst_size, bool sync)
{
	if (!with_prev_link)
	{
		sync = false;
	}
	else if(!sync)
	{
		std::scoped_lock lock(cache_mutex);
		if (nosubmit_untouched_items.find(key) != nosubmit_untouched_items.end())
		{
			sync = true;
		}
	}

	wait_for_del_file(keypath2(key, transaction_id) + ".comp");

	std::unique_ptr<IFsFile> src_del;
	if (src == nullptr)
	{
		src_del.reset(cachefs->openFile(keypath2(key, transaction_id), MODE_READ_SEQUENTIAL));
		src = src_del.get();
	}
	else
	{
		src->Seek(0);
	}

	if(src==nullptr)
	{
		Server->Log("Error opening source file "+keypath2(key, transaction_id)+" in compress_item. "+os_last_error_str(), LL_ERROR);
		assert(false);
		return false;
	}

	IFsFile* dst = cachefs->openFile(keypath2(key, transaction_id)+".comp", MODE_WRITE);

	if(dst==nullptr)
	{
		if (!cachefs->directoryExists(ExtractFilePath(keypath2(key, transaction_id) + ".comp")))
		{
			cachefs->createDir(ExtractFilePath(keypath2(key, transaction_id) + ".comp"));
			dst = cachefs->openFile(keypath2(key, transaction_id) + ".comp", MODE_WRITE);
		}
		if (dst == nullptr)
		{
			Server->Log("Error opening dest file " + keypath2(key, transaction_id) + ".comp in compress_item. " + os_last_error_str(), LL_ERROR);
			assert(false);
			return false;
		}
	}

	ScopedDeleteFile del_comp_f(dst);

	std::unique_ptr<ICompressAndEncrypt> compress_encrypt(compress_encrypt_factory->createCompressAndEncrypt(encryption_key, src, online_kv_store, background_comp_method));

	std::vector<char> buf;
	buf.resize(32768);

	char dig[16] = {};
	if(dst->Write(dig, sizeof(dig))!=sizeof(dig))
	{
		Server->Log("Error writing md5 placeholder", LL_WARNING);
		assert(false);
		return false;
	}

	while(true)
	{
		size_t read = compress_encrypt->read(buf.data(), static_cast<_u32>(buf.size()));

		if(read!=0 && read!=std::string::npos)
		{
			if(dst->Write(buf.data(), static_cast<_u32>(read))!= static_cast<_u32>(read))
			{
				Server->Log("Error writing compressed data", LL_WARNING);
				assert(false);
				return false;
			}
		}
		else if (read == std::string::npos)
		{
			Server->Log("Error compressing data", LL_WARNING);
			assert(false);
			return false;
		}
		else
		{
			break;
		}
	}

	std::string md5sum = compress_encrypt->md5sum();

	assert(md5sum.size()==sizeof(dig));

	if(dst->Write(0, md5sum.data(), sizeof(dig))!=sizeof(dig))
	{
		Server->Log("Error writing md5sum", LL_WARNING);
		assert(false);
		return false;
	}

	size_diff = dst->Size() - src->Size();
	dst_size = dst->Size();

	if(sync)
	{
		dst->Sync();
	}

	del_comp_f.release();
	Server->destroy(dst);

	return true;
}

bool TransactionalKvStore::decompress_item( const std::string& key, int64 transaction_id, IFsFile* tmpl_file, int64& size_diff, int64& src_size, bool sync)
{
	std::unique_ptr<IFsFile> src(cachefs->openFile(keypath2(key, transaction_id)+".comp", MODE_READ_SEQUENTIAL));

	if(!src.get())
	{
		Server->Log("Error opening source file "+keypath2(key, transaction_id)+".comp in decompress_item. "+os_last_error_str(), LL_ERROR);
		return false;
	}

	src_size = src->Size();

	IFsFile* dst;
	if (tmpl_file == nullptr)
	{
		dst = cachefs->openFile(keypath2(key, transaction_id), MODE_WRITE);
	}
	else
	{
		dst = tmpl_file;
	}

	if(dst==nullptr)
	{
		std::string errmsg;
		int64 err = os_last_error(errmsg);
		Server->Log("Error opening dest file "+keypath2(key, transaction_id)+" in decompress_item. "+errmsg+" (code "+convert(err)+")", LL_ERROR);
		return false;
	}

	ScopedDeleteFile del_dst_f(nullptr);
	if (tmpl_file == nullptr)
	{
		del_dst_f.reset(dst);
	}

	std::unique_ptr<IDecryptAndDecompress> decompress_decrypt(compress_encrypt_factory->createDecryptAndDecompress(encryption_key, dst));

	std::vector<char> buf;
	buf.resize(32768);

	char dig[16] = {};
	if(src->Read(dig, sizeof(dig))!=sizeof(dig))
	{
		Server->Log("Error reading md5 placeholder", LL_WARNING);
		return false;
	}
	
	int64 read_size=0;

	while(true)
	{
		_u32 read = src->Read(buf.data(), static_cast<_u32>(buf.size()));

		if(read>0)
		{
			read_size+=read;
			if(!decompress_decrypt->put(buf.data(), read))
			{
				Server->Log("Error during decompression (of "+ keypath2(key, transaction_id) + ".comp)", LL_WARNING);
				return false;
			}
		}
		else
		{
			if(!decompress_decrypt->finalize())
			{
				Server->Log("Error during decompression finalize (of "+ keypath2(key, transaction_id) + ".comp)", LL_WARNING);
				return false;
			}

			break;
		}
	}

	std::string md5sum = decompress_decrypt->md5sum();

	if(hex(std::string(dig, sizeof(dig)))!=md5sum)
	{
		Server->Log("Checksum wrong after decompression. Decompressed "
			+convert(read_size)+" bytes to "+convert(dst->Size())+" bytes (of "+ keypath2(key, transaction_id) + ".comp)", LL_WARNING);
		return false;
	}

	int64 dst_size = dst->Size();
	size_diff = dst_size - src->Size();
	if(sync)
	{
		dst->Sync();
	}

#ifdef HAS_FILE_SIZE_CACHE
	if(tmpl_file!=nullptr)
		dst->setCachedSize(dst_size);
#endif

	del_dst_f.release();
	if (tmpl_file == nullptr)
	{
		Server->destroy(dst);
	}

	return true;
}

int64 TransactionalKvStore::get_comp_bytes()
{
	return comp_bytes;
}

void TransactionalKvStore::remove_compression_evicition_submissions(std::unique_lock<cache_mutex_t>& cache_lock)
{
	std::scoped_lock submission_lock(submission_mutex);

	for(std::list<SSubmissionItem>::iterator it=submission_queue.begin();
		it!=submission_queue.end();)
	{
		if( (it->action==SubmissionAction_Compress
			|| it->action==SubmissionAction_Evict) 
			&& it->transid==transid )
		{
			submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));

			Server->Log("Removed submission (" + convert(it->transid) + "," + hex(it->key)+", "
				+ (it->action == SubmissionAction_Compress ? "compression" : "eviction" ) + 
				+ ", compressed="+convert(it->compressed)+")");
			
			if (it->action == SubmissionAction_Evict)
			{
				submitted_bytes -= it->size;
			}

			if (it->action == SubmissionAction_Evict
				&& it->compressed)
			{
				cache_put_back(compressed_items, it->key, cache_val_nc(true), cache_lock);
			}
			else if(it->action == SubmissionAction_Evict)
			{
				cache_put_back(lru_cache, it->key, cache_val_nc(true), cache_lock);
			}
			else
			{
				cache_put_back(lru_cache, it->key, cache_val_nc(it->compressed), cache_lock);
			}

			SMemFile* memf;
			{
				std::scoped_lock memfile_lock(memfiles_mutex);
				memf = memfiles.get(std::make_pair(it->transid, it->key));
			}
			if (memf != nullptr)
			{
				if (memf->key != it->key)
				{
					Server->Log("Memfile key wrong. Expected " + hexpath(it->key) + " got " + hexpath(memf->key), LL_ERROR);
					abort();
				}

				assert(submitted_memfiles > 0);
				--submitted_memfiles;
				submitted_memfile_size -= memf->size;
				memf->evicted = false;
			}

			std::list<SSubmissionItem>::iterator it_del = it;
			++it;
			submission_queue_rm(it_del);
		}
		else if( it->action==SubmissionAction_Evict
				&& it->transid==basetrans )
		{
			SMemFile* memf;
			{
				std::scoped_lock memfile_lock(memfiles_mutex);
				memf = memfiles.get(std::make_pair(it->transid, it->key));
			}
			bool nosubmit_untouched = nosubmit_untouched_items.find(it->key) != nosubmit_untouched_items.end();
			Server->Log("Removed basetrans eviction (" + convert(it->transid) + "," + hex(it->key)
				+ ", compressed=" + convert(it->compressed) + ", nosubmit_untouched="+convert(nosubmit_untouched)+")");
			if (nosubmit_untouched)
			{		
				wait_for_del_file(transpath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : ""));
				//During eviction the dirty in basetrans but untouched in current trans item got deleted in the current trans
				//We need to copy it back such that it gets submitted with the current trans
				bool ok = false;
				if (memf == nullptr)
				{
					ok = sync_link(cachefs, basepath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : ""),
						transpath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : ""));

					if (!ok)
					{
						ok = cachefs->copyFile(basepath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : ""),
							transpath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : ""));
					}
				}
				else
				{
					if (memf->key != it->key)
					{
						Server->Log("Memfile key wrong. Expected " + hexpath(it->key) + " got " + hexpath(memf->key), LL_ERROR);
						abort();
					}

					std::unique_ptr<IFsFile> fdst(cachefs->openFile(transpath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : ""), MODE_WRITE));
					if (fdst.get() != nullptr)
					{
						ok = copy_file(memf->file.get(), fdst.get());
					}
				}

				if (ok)
				{
					submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));


					int64 fsize = it->size;
					add_cachesize(fsize);

					if (it->compressed)
					{
						comp_bytes += fsize;
						cache_put_back(compressed_items, it->key, cache_val_nc(true), cache_lock);
					}
					else
					{
						cache_put_back(lru_cache, it->key, cache_val_nc(true), cache_lock);
					}

					if (memf != nullptr)
					{
						assert(submitted_memfiles > 0);
						--submitted_memfiles;
						submitted_memfile_size -= memf->size;
						memf->evicted = false;
					}

					addDirtyItem(transid, it->key);
					add_dirty_bytes(transid, it->key, fsize);

					std::list<SSubmissionItem>::iterator it_del = it;
					++it;
					submission_queue_rm(it_del);
				}
				else
				{
					std::string syserr = os_last_error_str();
					Server->Log("Cannot synclink base item "+
						basepath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : "")+ " to "
						+ transpath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : "")
						+" in current trans. "+ syserr, LL_ERROR);
					addSystemEvent("cache_err_fatal",
						"Cannot synclink base item",
						"Cannot synclink base item " +
						basepath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : "") + " to "
						+ transpath2() + cachefs->fileSep() + hexpath(it->key) + (it->compressed ? ".comp" : "")
						+ " in current trans. " + syserr, LL_ERROR);
					abort();
					++it;
				}
			}
			else
			{
				if (memf != nullptr)
				{
					assert(submitted_memfiles > 0);
					--submitted_memfiles;

					rm_mem_file(nullptr, it->transid, it->key, true);
				}
				//Transaction of eviction will be deleted. Item is dirty in current trans and will be submitted
				//with current trans
				submission_items.erase(submission_items.find(std::make_pair(it->transid, it->key)));
				std::list<SSubmissionItem>::iterator it_del = it;
				++it;
				submission_queue_rm(it_del);
			}
		}
		else
		{
			++it;
		}
	}

	check_submission_items();
}

void TransactionalKvStore::wait_for_compressions_evictions(std::unique_lock<cache_mutex_t>& lock)
{
	curr_submit_compress_evict = false;

	std::unique_lock lock_submit(submission_mutex);

	bool retry = true;
	while (retry)
	{
		retry = false;

		for (std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator >::iterator it = submission_items.begin();
			it != submission_items.end(); ++it)
		{
			if (it->second->action == SubmissionAction_Working_Compress
				|| it->second->action == SubmissionAction_Working_Evict )
			{
				Server->Log(std::string("Waiting for ")
					+ (it->second->action == SubmissionAction_Working_Compress ? "compression" : "eviction" )
					+ " of (" + convert(it->second->transid) + "," + hex(it->second->key) + ") to finish...");

				lock_submit.unlock();
				
				{
					RetrievalOperationUnlockOnly retrieve_op(lock);
					Server->wait(50);
				}

				lock_submit.lock();
				retry = true;
				break;
			}
		}
	}

	curr_submit_compress_evict = true;
}

void TransactionalKvStore::addDirtyItem(int64 transid, std::string key, bool with_stats)
{
	Server->Log("Incr dirty item " + hex(key) + " transid " + convert(transid));
	if (!key.empty()
		&& with_stats)
	{
		++total_dirty_ops;
	}
	std::scoped_lock lock(dirty_item_mutex);
	++num_dirty_items[transid];
	DIRTY_ITEM(++dirty_items[transid][key];)
}

void TransactionalKvStore::removeDirtyItem(int64 transid, std::string key)
{
	Server->Log("Decr dirty item " + hex(key) + " transid " + convert(transid));
	std::scoped_lock lock(dirty_item_mutex);
	--num_dirty_items[transid];
#ifdef DIRTY_ITEM_CHECK
	std::map<std::string, size_t>::iterator it = dirty_items[transid].find(key);
	assert(it != dirty_items[transid].end());
	assert(it->second > 0);
	--it->second;
	if (it->second == 0)
	{
		dirty_items[transid].erase(it);
	}
#endif
}

void TransactionalKvStore::add_dirty_bytes(int64 transid, std::string key, int64 b)
{
	dirty_bytes += b;

#ifdef DIRTY_ITEM_CHECK
	if (key.empty())
		return;

	Server->Log("Add dirty bytes " + hex(key) + " transid " + convert(transid) + " b " + convert(b));
	std::scoped_lock lock(dirty_item_mutex);
	dirty_items_size[transid][key] += b;
#endif
}

void TransactionalKvStore::rm_dirty_bytes(int64 transid, std::string key, int64 b, bool rm,
	bool change_size)
{
	if(change_size)
		dirty_bytes -= b;

#ifdef DIRTY_ITEM_CHECK
	if (rm)
	{
		if (key.empty())
		{
			assert(b == 0);
			return;
		}

		std::scoped_lock lock(dirty_item_mutex);
		std::map<std::string, int64>::iterator it = dirty_items_size[transid].find(key);
		if (it == dirty_items_size[transid].end())
			Server->Log("Dirty item " + hex(key) + " transid " + convert(transid) + " not found", LL_ERROR);
		assert(it != dirty_items_size[transid].end());
		it->second -= b;
		if (it->second != 0)
		{
			Server->Log("Rm_dirty_bytes failed. Has " + convert(it->second) + " bytes "
				"transid=" + convert(transid) + " key=" + hex(key)+" rmb="+convert(b)+
				" change_size="+convert(change_size), LL_ERROR);
			assert(false);
		}
		dirty_items_size[transid].erase(it);
	}
#endif
}

void TransactionalKvStore::add_cachesize(int64 toadd)
{
	if (toadd > 0)
	{
		TRACE_CACHESIZE(Server->Log("Cachesize +" + convert(toadd) + " cachesize=" + PrettyPrintBytes(cachesize), LL_DEBUG));
		cachesize += toadd;
	}
}

void TransactionalKvStore::sub_cachesize(int64 tosub)
{
	if (tosub > 0)
	{
		TRACE_CACHESIZE(Server->Log("Cachesize -" + convert(tosub) + " cachesize=" + PrettyPrintBytes(cachesize), LL_DEBUG));
		cachesize -= tosub;
	}
}

bool TransactionalKvStore::is_sync_wait_item(const std::string & key)
{
	IScopedLock bundle_lock(submit_bundle_item_mutex.get());

	auto it1 = curr_submit_bundle_items->lower_bound(std::make_pair(key, -1));
	if (it1 != curr_submit_bundle_items->end()
		&& it1->first == key)
	{
		return true;
	}

	auto it2 = other_submit_bundle_items->lower_bound(std::make_pair(key, -1));
	if (it2 != other_submit_bundle_items->end()
		&& it2->first == key)
	{
		return true;
	}

	return false;
}

bool TransactionalKvStore::is_sync_wait_item(const std::string & key, int64 transid)
{
	IScopedLock bundle_lock(submit_bundle_item_mutex.get());

	return curr_submit_bundle_items->find(std::make_pair(key, transid)) != curr_submit_bundle_items->end()
		|| other_submit_bundle_items->find(std::make_pair(key, transid)) != other_submit_bundle_items->end();
}

void TransactionalKvStore::check_submission_items()
{
#ifndef NDEBUG
	size_t max_check = 0;
	for (auto it = submission_items.begin(); it != submission_items.end(); ++it)
	{
		assert(it->first.first == it->second->transid);
		assert(it->first.second == it->second->key);
		if (++max_check > 10)
		{
			break;
		}
	}
	max_check = 0;
	for (auto it = submission_items.rbegin(); it != submission_items.rend(); ++it)
	{
		assert(it->first.first == it->second->transid);
		assert(it->first.second == it->second->key);
		if (++max_check > 10)
		{
			break;
		}
	}
#endif
}

void TransactionalKvStore::update_transactions()
{
	std::scoped_lock submission_lock(submission_mutex);
	transactions = cachefs->listFiles(std::string());
}

void TransactionalKvStore::drop_cache(IFsFile * fd)
{
#ifndef _WIN32
	//This causes Linux to immediately do blocking writes if file is dirty -- not good
	//posix_fadvise64(fd->getOsHandle(), 0, 0, POSIX_FADV_DONTNEED);
#endif
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<void> TransactionalKvStore::drop_cache_async(fuse_io_context& io, IFsFile* fd)
{
	/*
	io_uring_sqe* sqe = io.get_sqe();

	io_uring_prep_fadvise(sqe, get_file_fd(fd->getOsHandle()), 0, 0, POSIX_FADV_DONTNEED);

	co_return co_await io.complete(sqe);*/
	co_return;
}
#endif

void TransactionalKvStore::set_read_random(IFsFile * fd)
{
#ifndef _WIN32
	posix_fadvise64(fd->getOsHandle(), 0, 0, POSIX_FADV_RANDOM);
#endif
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<void> TransactionalKvStore::set_read_random_async(fuse_io_context& io, IFsFile* fd)
{
	io_uring_sqe* sqe = io.get_sqe();

	io_uring_prep_fadvise(sqe, get_file_fd(fd->getOsHandle()), 0, 0, POSIX_FADV_RANDOM);

	co_await io.complete(sqe);
}
#endif

bool TransactionalKvStore::add_submitted_item(int64 transid, std::string tkey,
	std::unique_ptr<IFile>* fd_cache)
{
	return add_item("trans_" + convert(transid) + cachefs->fileSep() + "dirty.submitted",
		transid, tkey, fd_cache);
}

bool TransactionalKvStore::add_evicted_item(int64 transid, std::string tkey)
{
	return add_item("trans_" + convert(transid) + cachefs->fileSep() + "dirty.evicted",
		transid, tkey, nullptr);
}

bool TransactionalKvStore::add_item(std::string fn, int64 transid, std::string tkey,
	std::unique_ptr<IFile>* fd_cache)
{
	IScopedLock lock(evicted_mutex.get());
	IFile* evicted_f;
	std::unique_ptr<IFile> evicted_f_uptr;
	if (fd_cache != nullptr)
	{
		if (fd_cache->get() == nullptr)
		{
			fd_cache->reset(cachefs->openFile(fn, MODE_RW_CREATE));
		}
		evicted_f = fd_cache->get();
	}
	else
	{
		evicted_f_uptr.reset(cachefs->openFile(fn, MODE_RW_CREATE));
		evicted_f = evicted_f_uptr.get();
	}
	if (evicted_f != nullptr)
	{
		CWData wdata;
		wdata.addUShort(0);
		wdata.addString2(tkey);
		unsigned short msize = static_cast<unsigned short>(wdata.getDataSize());
		memcpy(wdata.getDataPtr(), &msize, sizeof(msize));

		if (evicted_f->Write(evicted_f->Size(), wdata.getDataPtr(), wdata.getDataSize()) == wdata.getDataSize())
		{
			return true;
		}
	}

	return false;
}

IFsFile * TransactionalKvStore::get_mem_file(const std::string & key, int64 size_hint, bool for_read)
{
#ifndef NDEBUG
	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		assert(memfiles.get(std::make_pair(transid, key), false) == nullptr);
	}
#endif

	if (size_hint <= 0)
	{
		return nullptr;
	}

	if (memfile_size + size_hint >= max_memfile_size)
	{
		return nullptr;
	}

	if (for_read && disable_read_memfiles)
	{
		return nullptr;
	}

	if (!for_read && disable_write_memfiles)
	{
		return nullptr;
	}

	if ( (for_read && memfile_size > (max_memfile_size * 3) / 4))
	{
		return nullptr;
	}

	if (!only_memfiles && has_memfile_stat(key))
	{
		return nullptr;
	}

	add_memfile_stat(key);

	IMemFile* ret;
#ifdef HAS_LINUX_MEMORY_FILE
	ret = new LinuxMemoryFile(key, memcache_path);
#else
	ret = Server->openMemoryFile(key, false);
#endif
	memfile_size += size_hint;
	ret->Resize(size_hint);

	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		memfiles.put(std::make_pair(transid, key), SMemFile(ret, key, size_hint));
	}

	assert(num_mem_files.find(transid) == num_mem_files.end()
		|| num_mem_files[transid] > 0);
	++num_mem_files[transid];

	return ret;
}

bool TransactionalKvStore::has_memfile_stat(const std::string & key)
{
	std::string md5 = Server->GenerateBinaryMD5(key);
	int64 val;
	memcpy(&val, md5.data(), sizeof(val));
	if (val < 0) val *= -1;

	for (auto& it : memfile_stat_bitmaps)
	{
		if (it.second->get(val % it.second->size()))
		{
			return true;
		}
	}

	return false;
}

void TransactionalKvStore::add_memfile_stat(const std::string & key)
{
	if (only_memfiles)
		return;

	if (memfile_stat_bitmaps.empty()
		|| (Server->getTimeMS() - (memfile_stat_bitmaps.end() - 1)->first) > 6 * 60 * 60 * 1000)
	{
		memfile_stat_bitmaps.push_back(std::make_pair(Server->getTimeMS(), std::unique_ptr<Bitmap>(new Bitmap((5 * 1024 * 1024) * 8, false))));

		while (memfile_stat_bitmaps.size() > 8)
		{
			memfile_stat_bitmaps.erase(memfile_stat_bitmaps.begin());
		}
	}

	std::string md5 = Server->GenerateBinaryMD5(key);
	int64 val;
	memcpy(&val, md5.data(), sizeof(val));
	if (val < 0) val *= -1;

	Bitmap* bm = (memfile_stat_bitmaps.end() - 1)->second.get();
	bm->set(val % bm->size(), true);
}

bool TransactionalKvStore::rm_mem_file(fuse_io_context* io, int64 transid, const std::string & key, bool rm_submitted)
{
	bool is_last = false;
	std::scoped_lock memfile_lock(memfiles_mutex);
	SMemFile* f = memfiles.get(std::make_pair(transid, key), false);
	if (f != nullptr)
	{
		if (f->key != key)
		{
			Server->Log("Memfile key wrong. Expected " + hexpath(key) + " got " + hexpath(f->key), LL_ERROR);
			abort();
		}
		if (rm_submitted)
		{
			submitted_memfile_size -= f->size;
		}
		assert(num_mem_files[transid] > 0);
		auto it = num_mem_files.find(transid);
		--it->second;
		if (it->second == 0)
		{
			num_mem_files.erase(it);
			is_last = true;
		}
		if (f->file.use_count()<=1)
		{
			memfile_size -= f->size;
			if (io != nullptr)
			{
#ifdef HAS_ASYNC
				cachefs->closeAsync(*io, f->file.get());
#else
				assert(false);
#endif
			}
			else
			{
				memfd_del_thread.del(std::move(f->file));
			}
		}
		memfiles.del(std::make_pair(transid, key));
	}
	return is_last;
}

void TransactionalKvStore::rm_submission_item(std::map<std::pair<int64, std::string>, std::list<SSubmissionItem>::iterator>::iterator it)
{
	SMemFile* memf;
	{
		std::scoped_lock memfile_lock(memfiles_mutex);
		memf = memfiles.get(std::make_pair(it->first.first, it->first.second), false);
	}
	if (memf != nullptr)
	{
		if (memf->key != it->first.second)
		{
			Server->Log("Memfile key wrong. Expected " + hexpath(it->first.second) + " got " + hexpath(memf->key), LL_ERROR);
			abort();
		}
		assert(submitted_memfiles > 0);
		--submitted_memfiles;
		submitted_memfile_size -= memf->size;
		memf->evicted = false;
	}

	submission_queue_rm(it->second);
	submission_items.erase(it);
}

bool TransactionalKvStore::cow_mem_file(SMemFile * memf, bool with_old_file)
{
	assert(memf->cow);

	if (memf->file.use_count() == 1)
	{
		memf->cow = false;
		return true;
	}

	if (memfile_size + memf->size
		> max_memfile_size + max_memfile_size / 2)
	{
		return false;
	}

#ifdef HAS_LINUX_MEMORY_FILE
	IMemFile* nf = new LinuxMemoryFile(memf->key, memcache_path);
#else
	IMemFile* nf = Server->openMemoryFile(memf->key, false);
#endif
	int64 nf_size = memf->file->Size();
	nf->Resize(nf_size);
	nf->unprotect_mem();
#ifdef HAS_LINUX_MEMORY_FILE
	off_t offset = 0;
	ssize_t rc = sendfile(nf->getOsHandle(), memf->file->getOsHandle(), &offset, nf_size);
	if (rc != nf_size)
	{
		nf->protect_mem();
		assert(false);
		Server->destroy(nf);
		return false;
	}
#else
	if (memf->file->Read(0, nf->getDataPtr(), static_cast<_u32>(nf_size)) != nf_size)
	{
		nf->protect_mem();
		assert(false);
		Server->destroy(nf);
		return false;
	}
#endif
	nf->protect_mem();

	memfile_size += memf->size;

	if (with_old_file)
	{
		//mem file could be open read-only... so keep the old one around
		memf->old_file = memf->file;
	}

	memf->file = std::shared_ptr<IFsFile>(nf);
	memf->cow = false;

	return true;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> TransactionalKvStore::cow_mem_file_async(fuse_io_context& io, SMemFile* memf, bool with_old_file)
{
	assert(memf->cow);

	if (memf->file.use_count() == 1)
	{
		memf->cow = false;
		co_return true;
	}

	if (memfile_size + memf->size
		> max_memfile_size + max_memfile_size / 2)
	{
		co_return false;
	}

	IMemFile* nf = co_await io.run_in_threadpool([memf, this]() {
#ifdef HAS_LINUX_MEMORY_FILE
		//TODO: Open via uring
		std::unique_ptr<IMemFile> nf = std::make_unique<LinuxMemoryFile>(memf->key, this->memcache_path);
#else
		std::unique_ptr<IMemFile> nf(Server->openMemoryFile(memf->key, false));
#endif
		int64 nf_size = memf->file->Size();
		nf->Resize(nf_size);
		nf->unprotect_mem();
#ifdef HAS_LINUX_MEMORY_FILE
		off_t offset = 0;
		//TODO: Use uring splice here
		ssize_t rc = sendfile(nf->getOsHandle(), memf->file->getOsHandle(), &offset, nf_size);
		nf->protect_mem();
		if (rc < 0)
		{
			assert(false);
			nf.reset();
		}
#else
		_u32 rc = memf->file->Read(0, nf->getDataPtr(), static_cast<_u32>(nf_size));
		nf->protect_mem();
#endif
		if (rc < nf_size)
		{
			assert(false);
			nf.reset();
		}

		return nf.release();
		}, "cow memf");

	if (nf == nullptr)
	{
		assert(false);
		co_return false;
	}
	
	memfile_size += memf->size;

	if (with_old_file)
	{
		//mem file could be open read-only... so keep the old one around
		memf->old_file = memf->file;
	}

	memf->file = std::shared_ptr<IFsFile>(nf);
	memf->cow = false;

	co_return true;
}
#endif

void TransactionalKvStore::remove_missing(const std::string & key)
{
	auto missing_it = missing_items.find(key);
	if (missing_it != missing_items.end())
	{
		cachefs->deleteFile("missing_" + hex(key));
		missing_items.erase(missing_it);
	}
}

int64 TransactionalKvStore::get_compsize(const std::string & key, int64 transid)
{
	{
		std::scoped_lock lock(memfiles_mutex);
		SMemFile* f = memfiles.get(std::make_pair(transid, key), false);
		if (f != nullptr
			&& f->compsize>=0)
		{
			return f->compsize;
		}
	}

	return get_compsize_file(cachefs, keypath2(key, transid));
}

void TransactionalKvStore::only_memfiles_throttle(const std::string& key, std::unique_lock<cache_mutex_t>& lock)
{
	if (!only_memfiles)
		return;

	bool cache_check = false;

	while (memfile_size > (max_memfile_size * 3) / 4)
	{
		if (!cache_check
			&& cache_get(lru_cache, key, lock, false)!=nullptr)
		{
			return;
		}
		else
		{
			cache_check = true;
		}
;
		lock.unlock();
		Server->wait(100);
		lock.lock();
	}
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<void> TransactionalKvStore::only_memfiles_throttle_async(fuse_io_context& io, const std::string key)
{
	if (!only_memfiles)
		co_return;

	bool cache_check = false;

	std::unique_lock lock(cache_mutex);

	while (memfile_size > (max_memfile_size * 3) / 4)
	{
		if (!cache_check
			&& cache_get(lru_cache, key, lock, false) != nullptr)
		{
			co_return;
		}
		else
		{
			cache_check = true;
		}

		lock.unlock();

		co_await io.sleep_ms(100);

		lock.lock();
	}
}
#endif

std::string TransactionalKvStore::readCacheFile(const std::string& fn)
{
	std::unique_ptr<IFile> f(cachefs->openFile(fn, MODE_READ));
	if (!f)
		return std::string();

	char buf[4096];

	_u32 r;
	std::string ret;
	while ((r = f->Read(buf, sizeof(buf))) > 0)
	{
		ret.append(buf, r);

		if (r < sizeof(buf))
			break;
	}

	return ret;
}

bool TransactionalKvStore::cacheFileExists(const std::string& fn)
{
	std::unique_ptr<IFile> f(cachefs->openFile(fn, MODE_READ));
	return f.get() != nullptr;
}

bool TransactionalKvStore::writeToCacheFile(const std::string& str, const std::string& fn)
{
	std::unique_ptr<IFile> f(cachefs->openFile(fn, MODE_WRITE));
	if (!f)
		return false;
	return f->Write(str) == str.size();
}

TransactionalKvStore::SCacheVal TransactionalKvStore::cache_val(const std::string & key, bool dirty)
{
	unsigned int chances = 0;
	if (num_second_chances_cb != nullptr)
		chances = num_second_chances_cb->get_num_second_chances(key);
	TransactionalKvStore::SCacheVal ret;
	ret.dirty = dirty;
	if (chances >= 128)
		ret.chances = 127;
	else
		ret.chances = chances;

	return ret;
}

TransactionalKvStore::SCacheVal TransactionalKvStore::cache_val_nc(bool dirty)
{
	TransactionalKvStore::SCacheVal ret;
	ret.dirty = dirty;
	return ret;
}

std::list<SSubmissionItem>::iterator TransactionalKvStore::submission_queue_add(SSubmissionItem & item, bool memfile)
{
	submission_queue.push_back(item);
	auto it=--submission_queue.end();
	if (memfile
		&& submitted_memfiles==0)
	{
		submission_queue_memfile_first = it;
	}
	return it;
}

std::list<SSubmissionItem>::iterator TransactionalKvStore::submission_queue_insert(SSubmissionItem & item, bool memfile, std::list<SSubmissionItem>::iterator it)
{
	it = submission_queue.insert(it, item);
	if (memfile)
	{
		submission_queue_memfile_first = submission_queue.end();
	}
	return it;
}

void TransactionalKvStore::submission_queue_rm(std::list<SSubmissionItem>::iterator it)
{
	if (submission_queue_memfile_first != submission_queue.end() &&
		it == submission_queue_memfile_first)
	{
		submission_queue_memfile_first = submission_queue.end();
	}
	submission_queue.erase(it);
}

int64 TransactionalKvStore::cache_free_space()
{
	int64 free_space = cachefs->freeSpace();

	if (free_space > critical_free_size
		&& metadata_cache_free!=-1
		&& metadata_cache_free < min_metadata_cache_free)
	{
		free_space = critical_free_size - 500 * 1024 * 1024;
	}

	return free_space;
}

void TransactionalKvStore::RegularSubmitBundleThread::regular_submit_bundle(std::unique_lock<cache_mutex_t>& cache_lock)
{
	if (!kv_store->submit_bundle.empty()
		&& (Server->getTimeMS() - kv_store->submit_bundle_starttime > 5 * 60 * 1000
			|| kv_store->submit_bundle.size()>20000))
	{
		std::unique_ptr<IFile> submitted_fd_cache;
		int64 submitted_fd_cache_transid = 0;
		{
			IScopedLock bundle_lock(kv_store->submit_bundle_item_mutex.get());
			std::swap(kv_store->curr_submit_bundle_items, kv_store->other_submit_bundle_items);
		}
		size_t submit_bundle_n = kv_store->submit_bundle.size();
		cache_lock.unlock();
		if (!kv_store->online_kv_store->sync())
		{
			Server->Log("Kv store sync failed. Not doing regular submit bundle...", LL_ERROR);
			submit_bundle_n = 0;
			IScopedLock bundle_lock(kv_store->submit_bundle_item_mutex.get());
			if (!kv_store->curr_submit_bundle_items->empty())
			{
				kv_store->other_submit_bundle_items->insert(kv_store->curr_submit_bundle_items->begin(), kv_store->curr_submit_bundle_items->end());
				kv_store->curr_submit_bundle_items->clear();
			}
			std::swap(kv_store->curr_submit_bundle_items, kv_store->other_submit_bundle_items);

			//TODO: Resubmit all submit bundle items
		}
		cache_lock.lock();

		bool run_del_path = false;

		for (size_t i = 0; i < submit_bundle_n; ++i)
		{
			SSubmissionItem* it = &kv_store->submit_bundle[i].first;
			std::string key = it->key;
			int64 transid = it->transid;

			{
				cache_lock.unlock();
				if (run_del_path)
				{
					run_del_path = false;
					kv_store->run_del_file_queue();
				}
				if (kv_store->with_submitted_files)
				{
					if (submitted_fd_cache_transid != 0
						&& submitted_fd_cache_transid != transid)
					{
						submitted_fd_cache.reset();
					}
					kv_store->add_submitted_item(transid, key, &submitted_fd_cache);
					submitted_fd_cache_transid = transid;
				}
				cache_lock.lock();
			}

			it = &kv_store->submit_bundle[i].first;
			if (kv_store->submit_bundle[i].second)
			{
				common::lrucache<std::string, SCacheVal>* target_cache = &kv_store->lru_cache;
				common::lrucache<std::string, SCacheVal>* other_target_cache = &kv_store->compressed_items;

				if (it->compressed)
				{
					target_cache = &kv_store->compressed_items;
					other_target_cache = &kv_store->lru_cache;
				}

				bool compressed = it->compressed;

				SCacheVal* dirty = cache_get(*target_cache, it->key, cache_lock, false);

				if (dirty == nullptr)
				{
					compressed = !compressed;
					dirty = cache_get(*other_target_cache, it->key, cache_lock, false);
					target_cache = other_target_cache;
				}

				bool del_item = false;
				if (dirty != nullptr && !dirty->dirty
					&& kv_store->open_files.find(it->key) == kv_store->open_files.end()
					&& kv_store->in_retrieval.find(it->key) == kv_store->in_retrieval.end())
				{
					bool in_submission = false;
					{
						std::scoped_lock lock(kv_store->submission_mutex);
						std::scoped_lock dirty_lock(kv_store->dirty_item_mutex);
						for (std::map<int64, size_t>::iterator itd = kv_store->num_dirty_items.begin(); itd != kv_store->num_dirty_items.end(); ++itd)
						{
							if (itd->second>0)
							{
								if (kv_store->submission_items.find(std::make_pair(itd->first, it->key)) != kv_store->submission_items.end())
								{
									in_submission = true;
								}
							}
						}
					}

					if (!in_submission)
					{
						Server->Log("Evicting non-dirty cache item " + kv_store->transpath2() + kv_store->cachefs->fileSep() + kv_store->hexpath(it->key) + (compressed ? ".comp" : "") + " after submission (bundled)", LL_INFO);

						int64 fsize = -1;
						if (!compressed)
						{
							SFdKey* res = kv_store->fd_cache.get(it->key);
							if (res != nullptr)
							{
								if (kv_store->cachefs->filePath(res->fd) != kv_store->keypath2(it->key, kv_store->transid))
								{
									Server->Log("Fd cache filename not as expected. Got " + kv_store->cachefs->filePath(res->fd) 
										+ " Expected " + kv_store->keypath2(it->key, kv_store->transid), LL_ERROR);
									abort();
								}

								assert(dynamic_cast<IMemFile*>(res->fd) == nullptr);
								if(res->fd!=nullptr)
									fsize = res->fd->Size();
								Server->destroy(res->fd);
								kv_store->fd_cache.del(it->key);
							}
						}

						if (fsize == -1
							&& compressed)
						{
							SMemFile* memfile_nf;
							{
								std::scoped_lock memfile_lock(kv_store->memfiles_mutex);
								memfile_nf = kv_store->memfiles.get(std::make_pair(kv_store->transid, it->key), false);
							}
							IFile* fd;
							if (memfile_nf != nullptr)
							{
								if (memfile_nf->key != it->key)
								{
									Server->Log("Memfile key wrong. Expected " + kv_store->hexpath(it->key) + " got " + kv_store->hexpath(memfile_nf->key), LL_ERROR);
									abort();
								}

								fd = memfile_nf->file.get();
							}
							else
							{
								fd = kv_store->cachefs->openFile(kv_store->keypath2(it->key, kv_store->transid) + (compressed ? ".comp" : ""), MODE_READ);
							}

							if (fd != nullptr)
							{
								fsize = fd->Size();
								Server->destroy(fd);
							}
						}

						if (fsize == -1)
						{
							fsize = it->size;
						}

						kv_store->sub_cachesize(fsize);

						if (compressed)
						{
							kv_store->comp_bytes -= fsize;
						}

						kv_store->delete_item(nullptr, it->key, compressed, cache_lock,
							0, 0, TransactionalKvStore::DeleteImm::None, 0, false, it->transid);
						run_del_path = true;

						cache_del(*target_cache, it->key, cache_lock);
					}
					else
					{
						del_item = true;
					}
				}
				else
				{
					del_item = true;
				}

				if (del_item)
				{
					std::string item_path = kv_store->keypath2(it->key, it->transid);

					if (it->compressed)
					{
						item_path += ".comp";
					}

					IScopedLock del_file_lock(kv_store->del_file_mutex.get());
					kv_store->del_file_queue.insert(item_path);
					run_del_path = true;
				}
			}
		}

		if (submit_bundle_n > 0)
		{
			IScopedLock bundle_lock(kv_store->submit_bundle_item_mutex.get());
			kv_store->other_submit_bundle_items->clear();
		}
		kv_store->submit_bundle.erase(kv_store->submit_bundle.begin(), kv_store->submit_bundle.begin() + submit_bundle_n);
		if (kv_store->submit_bundle.empty())
		{
			kv_store->submit_bundle_starttime = 0;
		}
		else
		{
			kv_store->submit_bundle_starttime = Server->getTimeMS();
		}

		if (run_del_path)
		{
			cache_lock.unlock();
			kv_store->run_del_file_queue();
			cache_lock.lock();
		}
	}
}

void TransactionalKvStore::RegularSubmitBundleThread::operator()()
{
	std::unique_lock lock(kv_store->cache_mutex);
	while (!do_quit)
	{
		cond.wait_for(lock, 1000ms);

		if (do_quit) 
			break;

		regular_submit_bundle(lock);
	}
}

void TransactionalKvStore::RegularSubmitBundleThread::quit()
{
	std::scoped_lock lock(kv_store->cache_mutex);
	do_quit = true;
	cond.notify_all();
}

void TransactionalKvStore::ThrottleThread::operator()()
{
	std::unique_lock lock(kv_store->cache_mutex);
	while (!do_quit)
	{
		cond.wait_for(lock, 100ms);

		if (do_quit)
			break;

		lock.unlock();
		int64 free_space = kv_store->cache_free_space();
		lock.lock();

		if (kv_store->unthrottled_gets>0)
		{
			if (kv_store->remaining_gets == std::string::npos)
			{
				if (kv_store->unthrottled_gets_avg == 0)
				{
					kv_store->unthrottled_gets_avg = static_cast<double>(kv_store->unthrottled_gets);
				}
				else
				{
					kv_store->unthrottled_gets_avg = 0.9*kv_store->unthrottled_gets_avg + 0.1*static_cast<double>(kv_store->unthrottled_gets);
				}
			}

			kv_store->unthrottled_gets = 0;
		}

		if (kv_store->cachesize<kv_store->min_cachesize)
		{
			kv_store->has_new_remaining_gets = true;
			kv_store->new_remaining_gets = std::string::npos;
			continue;
		}

		if (!kv_store->curr_submit_compress_evict)
		{
			kv_store->has_new_remaining_gets = true;
			kv_store->new_remaining_gets = std::string::npos;
			continue;
		}

		if (free_space >= kv_store->throttle_free_size
			&& kv_store->cachesize<kv_store->max_cachesize)
		{
			kv_store->has_new_remaining_gets = true;
			kv_store->new_remaining_gets = std::string::npos;
		}

		if (!kv_store->allow_evict)
		{
			if (free_space < kv_store->critical_free_size)
			{
				Server->Log("Critical free space. Not allowed to evict. Throttling... Please free some space.", LL_ERROR);
				kv_store->has_new_remaining_gets = true;
				kv_store->new_remaining_gets = 0;
			}

			if (kv_store->cachesize > kv_store->max_cachesize)
			{
				Server->Log("Critical cache size. Not allowed to evict. Throttling... Please free some space.", LL_ERROR);
				kv_store->has_new_remaining_gets = true;
				kv_store->new_remaining_gets = 0;
			}
		}

		int64 max_cachesize_lower = (std::max)(kv_store->min_cachesize, kv_store->max_cachesize - max_cachesize_throttle_size);

		if (kv_store->allow_evict
			&& free_space < kv_store->throttle_free_size
			&& kv_store->curr_submit_compress_evict)
		{
			if (kv_store->remaining_gets == std::string::npos)
			{
				Server->Log("Critical free space (" + PrettyPrintBytes(free_space) + "). Beginning throttling...", LL_WARNING);
			}

			double throttle_pc = 0;
			if (free_space > kv_store->critical_free_size)
			{
				throttle_pc = 1.0 - static_cast<double>(kv_store->throttle_free_size - free_space) / (kv_store->throttle_free_size - kv_store->critical_free_size);
			}

			kv_store->has_new_remaining_gets = true;
			kv_store->new_remaining_gets = static_cast<size_t>(kv_store->unthrottled_gets_avg*throttle_pc + 0.5);

			Server->Log("Throttling to " + convert(kv_store->remaining_gets) + " gets/s from avg " + convert(kv_store->unthrottled_gets_avg) + " gets/s");
		}
		else if (kv_store->allow_evict
			&& kv_store->cachesize > max_cachesize_lower
			&& kv_store->curr_submit_compress_evict)
		{
			if (kv_store->remaining_gets == std::string::npos)
			{
				Server->Log("Critical cache size (" + PrettyPrintBytes(kv_store->cachesize) + " max "+PrettyPrintBytes(max_cachesize_lower)+"). Beginning throttling...", LL_WARNING);
			}

			double throttle_pc = 0;
			int64 cachesize_over = kv_store->cachesize - max_cachesize_lower;
			if (cachesize_over <max_cachesize_throttle_size)
			{
				throttle_pc = 1.0 - static_cast<double>(cachesize_over) / max_cachesize_throttle_size;
			}

			kv_store->has_new_remaining_gets = true;
			kv_store->new_remaining_gets = static_cast<size_t>(kv_store->unthrottled_gets_avg*throttle_pc + 0.5);

			Server->Log("Throttling to " + convert(kv_store->remaining_gets) + " gets/s from avg " + convert(kv_store->unthrottled_gets_avg) + " gets/s b/c max cachesize");
		}
	}
}

void TransactionalKvStore::ThrottleThread::quit()
{
	std::scoped_lock lock(kv_store->cache_mutex);
	do_quit = true;
	cond.notify_all();
}

int64 TransactionalKvStore::get_transid()
{
	std::scoped_lock lock(cache_mutex);
	return transid;
}

int64 TransactionalKvStore::get_basetransid()
{
	std::scoped_lock lock(cache_mutex);
	return basetrans;
}

void TransactionalKvStore::set_max_cachesize(int64 ns)
{
	max_cachesize = ns;
}

int64 TransactionalKvStore::cache_total_space()
{
	return cachefs->totalSpace();
}

void TransactionalKvStore::set_disable_read_memfiles(bool b)
{
	std::scoped_lock lock(cache_mutex);
	disable_read_memfiles = b;
}

void TransactionalKvStore::set_disable_write_memfiles(bool b)
{
	std::scoped_lock lock(cache_mutex);
	disable_write_memfiles = b;
}

namespace
{
	relaxed_atomic<int64> last_metadata_balance_enospc(0);
	relaxed_atomic<int64> last_data_balance_enospc(0);

	relaxed_atomic<int64> last_metadata_balance_enospc_cnt(0);
	relaxed_atomic<int64> last_data_balance_enospc_cnt(0);

	class RebalanceThread : public IThread
	{
		bool metadata;
		IBackupFileSystem* cachefs;
	public:
		RebalanceThread(IBackupFileSystem* cachefs, bool metadata)
			: cachefs(cachefs), metadata(metadata) {}
		void operator()()
		{
			if (metadata && cachefs->forceAllocMetadata())
			{
				delete this;
				return;
			}

			for (int i = 1; i < 50; ++i)
			{
				bool enospc;
				size_t relocated;
				if (!cachefs->balance(i, 1, metadata, enospc, relocated))
				{
					if (enospc)
					{
						if (metadata)
						{
							last_metadata_balance_enospc = Server->getTimeMS();
							++last_metadata_balance_enospc_cnt;
						}
						else
						{
							last_data_balance_enospc = Server->getTimeMS();
							++last_data_balance_enospc_cnt;
						}
					}
					break;
				}

				if (relocated==0)
					continue;

				if (metadata)
				{
					last_metadata_balance_enospc = 0;
					last_metadata_balance_enospc_cnt = 0;
				}
				else
				{
					last_data_balance_enospc = 0;
					last_data_balance_enospc_cnt = 0;
				}

				if (metadata && !cachefs->forceAllocMetadata())
				{
					--i;
					continue;
				}
				else
				{
					break;
				}
			}
			delete this;
		}
	};
}


void TransactionalKvStore::MetadataUpdateThread::operator()()
{
	task_set_less_throttle();

	bool throttle_warn = false;

	THREADPOOL_TICKET rebalance_tt = ILLEGAL_THREADPOOL_TICKET;

	std::unique_lock lock(kv_store->cache_mutex);
	while (!do_quit)
	{
		cond.wait_for(lock, 60s);

		if (do_quit)
			break;

		lock.unlock();

		int64 unallocated_space = kv_store->cachefs->unallocatedSpace();

		int64 free_space = kv_store->cachefs->freeMetadataSpace();
		if (free_space < 0)
		{
			lock.lock();
			continue;
		}

		Server->Log("Free metadata space: " + PrettyPrintBytes(free_space));

		if (free_space < kv_store->min_metadata_cache_free)
		{
			if (rebalance_tt == ILLEGAL_THREADPOOL_TICKET ||
				Server->getThreadPool()->waitFor(rebalance_tt, 0))
			{
				rebalance_tt = Server->getThreadPool()->execute(new RebalanceThread(kv_store->cachefs, true),
					"ch dat rebalance");
			}

			if (!throttle_warn)
			{
				Server->Log(PrettyPrintBytes(free_space) + " free metdata space on " 
					+ kv_store->cachefs->getName() + ". Throttling...", LL_WARNING);
				throttle_warn = true;
			}
		}
		else
		{
			if (free_space < kv_store->min_metadata_cache_free +
				(std::min)(kv_store->min_metadata_cache_free / 4, 500LL * 1024 * 1024) &&
				(rebalance_tt == ILLEGAL_THREADPOOL_TICKET ||
					Server->getThreadPool()->waitFor(rebalance_tt, 0)) &&
				Server->getTimeMS()-last_metadata_balance_enospc > last_metadata_balance_enospc_cnt * 60*60*1000)
			{
				rebalance_tt = Server->getThreadPool()->execute(new RebalanceThread(kv_store->cachefs, true),
					"chp dat rebalance");
			}
			else if(unallocated_space>=0 && 
				free_space-unallocated_space >(std::max)(kv_store->min_metadata_cache_free*4,
					1LL * 1024 * 1024 * 1024) &&
				Server->getTimeMS() - last_data_balance_enospc > last_data_balance_enospc_cnt * 60 * 60 * 1000)
			{
				rebalance_tt = Server->getThreadPool()->execute(new RebalanceThread(kv_store->cachefs, false),
					"chp met rebalance");
			}

			throttle_warn = false;
		}

		kv_store->metadata_cache_free = free_space;

		lock.lock();
	}

	task_unset_less_throttle();
}

void TransactionalKvStore::MetadataUpdateThread::quit()
{
	std::scoped_lock lock(kv_store->cache_mutex);
	do_quit = true;
	cond.notify_all();
}

void TransactionalKvStore::MemfdDelThread::operator()()
{
	std::unique_lock lock(mutex);
	while (!do_quit)
	{
		cond.wait(lock, [&]() {return do_quit || !del_fds.empty(); });
	
		if (do_quit)
			break;

		std::shared_ptr<IFsFile> fd = std::move(del_fds.back());
		del_fds.pop_back();

		lock.unlock();

		fd.reset();

		lock.lock();
	}
}

bool TransactionalKvStore::set_cache_file_compression(const std::string& key, const std::string& fpath)
{
	if (meta_cache_comp != CompressionNone &&
		num_second_chances_cb != nullptr &&
		num_second_chances_cb->is_metadata(key))
	{
		return cachefs->setXAttr(fpath, "btrfs.compression", CompressionMethodToBtrfsString(meta_cache_comp));
	}
	else if (cache_comp != CompressionNone)
	{
		return cachefs->setXAttr(fpath, "btrfs.compression", CompressionMethodToBtrfsString(cache_comp));
	}

	return true;
}