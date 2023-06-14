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
#include "CloudFile.h"
#include "../stringtools.h"
#include "../common/data.h"
#include "../urbackupcommon/os_functions.h"
#include "KvStoreFrontend.h"
#ifdef HAS_LOCAL_BACKEND
#include "KvStoreBackendLocal.h"
#endif
#include "../Interface/ThreadPool.h"
#include "../Interface/Pipe.h"
#include "../urbackupcommon/events.h"
#include "../common/data.h"
#ifdef HAS_MOUNT_SERVICE
#include "MountServiceClient.h"
#endif
#include "../Interface/SettingsReader.h"
#include "ICompressEncrypt.h"
#ifdef HAS_LOCAL_CACHEFS
#include "PassThroughFileSystem.h"
#endif
#include "Auto.h"
#ifdef WITH_MIGRATION
#include "KvStoreBackendS3.h"
#include "KvStoreBackendAzure.h"
#endif
#include "../urbackupcommon/os_functions.h"
#ifdef WITH_SLOG
#include "../common/crc.h"
#endif
#include "../urbackupcommon/json.h"
#ifdef HAS_ASYNC
#include "fuse_kernel.h"
#endif
#ifdef HAS_LINUX_MEMORY_FILE
#include "LinuxMemFile.h"
#endif

#if !defined(NO_JEMALLOC) && !defined(_WIN32)
#include <jemalloc/jemalloc.h>
#endif

#ifndef _WIN32
#include <sys/mount.h>
#include <sys/file.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#else
typedef int64 ssize_t;
#define aligned_alloc(allign, size) malloc(size)
int fcntl(int fd, int cmd, int val)
{
	return -1;
}
#define F_SETPIPE_SZ 1
int pipe2(int p[2], int flags)
{
	return -1;
}
#define O_NONBLOCK 1
#define O_CLOEXEC 2
int eventfd(unsigned int initval, int flags)
{
	return -1;
}
#define EFD_CLOEXEC 1
#define EFD_SEMAPHORE 2
int eventfd_write(int fd, int value) {
	return -1;
}
#define POSIX_FADV_DONTNEED 1
#endif

#define EXTENSIVE_DEBUG_LOG(x)

#ifndef PR_SET_IO_FLUSHER
#define PR_SET_IO_FLUSHER 57
#endif

#include <memory.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <memory.h>
#include <assert.h>
#include <stdexcept>

const int64 min_cachesize = 1LL * 1024* 1024* 1024LL; // 1GB
const int64 min_free_size = 20LL * 1024* 1024* 1024LL; // 20GB
const int64 critical_free_size = 1000LL * 1024 * 1024LL; //1GB
const int64 throttle_free_size = 5000LL * 1024 * 1024LL; //5GB

const int64 comp_start_limit = 20LL * 1024* 1024* 1024LL; // 20GB
const float comp_percent = 0.5; // 50%

const int64 big_block_size = 20 *1024 *1024; // 20MB
const int64 small_block_size = 512 *1024; // 512KB

const int64 block_size = 4096;

const int64 slog_kernel_buffer_size = 16 * 1024 * 1024;

std::string pbkdf2_sha256(const std::string& key);
bool is_automount_finished();

#ifndef _WIN32
bool flush_dev(std::string dev_fn)
{
	int fd = open64(dev_fn.c_str(), O_RDWR | O_LARGEFILE | O_CLOEXEC);

	if (fd == -1)
	{
		Server->Log("Error opening device file " + dev_fn + ". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	if (fsync(fd) == -1)
	{
		Server->Log("Error fsyncing device file " + dev_fn + ". " + os_last_error_str(), LL_ERROR);
		close(fd);
		return false;
	}

	if (ioctl(fd, BLKFLSBUF, 0) != 0)
	{
		Server->Log("Error flushing device file " + dev_fn + ". " + os_last_error_str(), LL_ERROR);
		close(fd);
		return false;
	}

	close(fd);
	return true;
}
int get_file_fd(IFsFile::os_file_handle h)
{
	return h;
}
#else
bool flush_dev(std::string dev_fn)
{
	return false;
}
#define SPLICE_F_NONBLOCK 256
#define SPLICE_F_MOVE 128
int get_file_fd(IFsFile::os_file_handle h)
{
	return reinterpret_cast<int>(h);
}
#endif

namespace
{
	const char queue_cmd_resize = 1;
	const char queue_cmd_get_size = 2;
	const char queue_cmd_meminfo = 3;

	enum {
		IOPRIO_CLASS_NONE,
		IOPRIO_CLASS_RT,
		IOPRIO_CLASS_BE,
		IOPRIO_CLASS_IDLE,
	};

#define IOPRIO_CLASS_SHIFT	13
	const _u16 io_prio_val = 2 | IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT;

	//divide rounding up
	int64 div_up(int64 num, int64 div)
	{
		if(num%div == 0)
		{
			return num/div;
		}
		else
		{
			return num/div + 1;
		}
	}

	int64 roundUp(int64 numToRound, int64 multiple)
	{
		return ((numToRound + multiple - 1) / multiple) * multiple;
	}

	int64 roundDown(int64 numToRound, int64 multiple)
	{
		return ((numToRound / multiple) * multiple);
	}

	void retryWait(size_t n)
	{
		unsigned int waittime = (std::min)(static_cast<unsigned int>(1000.*pow(2., static_cast<double>(n))), (unsigned int)30*60*1000); //30min
		if(n>20)
		{
			waittime = (unsigned int)30*60*1000;
		}
		Server->Log("Waiting "+PrettyPrintTime(waittime));
		Server->wait(waittime);
	}

	bool writeZeroes(int64 offset, IFile* file, int64 num, int set_val)
	{
		static char buf[4096] = {};

		while(num>0)
		{
			int64 towrite = (std::min)((int64)4096, num);
			if(file->Write(offset, buf, static_cast<_u32>(towrite))!=towrite)
			{
				return false;
			}
			num-=towrite;
			offset+=towrite;
		}
		return true;
	}

#ifndef _WIN32
	void doublefork_writestring(const std::string& str, const std::string& file)
	{
		pid_t pid1;
		pid1 = fork();
		if (pid1 == 0)
		{
			setsid();
			pid_t pid2;
			pid2 = fork();
			if (pid2 == 0)
			{
				writestring(str, file);
				exit(0);
			}
			else
			{
				exit(1);
			}
		}
		else
		{
			int status;
			waitpid(pid1, &status, 0);
		}
	}
#else
	void doublefork_writestring(const std::string& str, const std::string& file)
	{
		writestring(str, file);
	}
#endif

	class FileBitmap
	{
	public:
		FileBitmap(IFile* backing_file, int64 n, bool init_set)
			: backing_file(backing_file)
		{
			resize(n, init_set);
		}

		void resize(int64 n, bool init_set)
		{
			total_size=n;
			bitmap_size = static_cast<size_t>(n/8 + (n%8==0 ? 0 : 1));

			if(backing_file->Size()<static_cast<int64>(bitmap_size))
			{
				while(!writeZeroes(backing_file->Size(), backing_file, bitmap_size-backing_file->Size(), init_set?255:0))
				{
					Server->Log("Error resizing bitmap file. Retrying...", LL_ERROR);
					Server->wait(1000);
				}
			}

			cache.resize(bitmap_size);

			backing_file->Seek(0);
			size_t pos=0;
			while(pos<bitmap_size)
			{
				size_t toread=(std::min)((size_t)4096, bitmap_size-pos);

				if(backing_file->Read(reinterpret_cast<char*>(&cache[pos]), static_cast<_u32>(toread))!=toread)
				{
					throw std::runtime_error("Error reading from backing bitmap file");
				}

				pos+=toread;
			}
		}

		void set(int64 i, bool v)
		{
			size_t bitmap_byte=(size_t)(i/8);
			size_t bitmap_bit=i%8;

			unsigned char b = cache[bitmap_byte];

			if(v==true)
				b=b|(1<<(7-bitmap_bit));
			else
				b=b&(~(1<<(7-bitmap_bit)));

			cache[bitmap_byte] = b;
		}

		size_t set_range(int64 start, int64 end, bool v)
		{
			size_t set_bits = 0;
			for(;start<end;++start)
			{
				if(get(start)!=v)
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
			return cache[bitmap_byte];
		}

		bool get(int64 i) const
		{
			size_t bitmap_byte=(size_t)(i/8);
			size_t bitmap_bit=i%8;

			unsigned char b = cache[bitmap_byte];

			bool has_bit=((b & (1<<(7-bitmap_bit)))>0);

			return has_bit;
		}

		bool flush()
		{
			while(backing_file->Write(0, reinterpret_cast<char*>(cache.data()),
				static_cast<_u32>(cache.size()))!=cache.size())
			{
				Server->Log("Error writing to backing bitmap file. Retrying...", LL_WARNING);
				Server->wait(1000);
			}

			return true;
		}

		size_t count_bits()
		{
			size_t set_count = 0;
			for(int64 i=0;i<total_size;)
			{
				if (i % 8 == 0
					&& getb(i) == 0)
				{
					i += 8;
					continue;
				}

				if(get(i))
				{
					++set_count;
				}

				++i;
			}

			return set_count;
		}

		bool get_range( int64 start, int64 end ) const
		{
			for(;start<end;++start)
			{
				if(get(start))
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
		IFile* backing_file;
		size_t bitmap_size;
		int64 total_size;
		std::vector<unsigned char> cache;
	};

	const size_t bitmap_block_size=4096;
	
	int64 bitmap_assert_limit = 1*1024*1024;

	class SparseFileBitmap
	{
		
	public:
		SparseFileBitmap(IFsFile* backing_file, int64 n, bool init_set,
			size_t bitmap_cache_items)
			: backing_file(backing_file),
			bitmap_cache_items(bitmap_cache_items),
			async_evict_skip_block(std::string::npos)
		{
			resize(n, init_set);
		}

		~SparseFileBitmap()
		{
			flush();
		}

		void resize(int64 n, bool init_set)
		{
			flush();

			total_size=n;
			bitmap_size = static_cast<size_t>(n/8 + (n%8==0 ? 0 : 1));

			bitmap_size += bitmap_block_size - bitmap_size%bitmap_block_size;

			if(backing_file->Size()<static_cast<int64>(bitmap_size))
			{
				while(!writeZeroes(backing_file->Size(), backing_file, bitmap_size-backing_file->Size(), init_set?255:0))
				{
					Server->Log("Error resizing bitmap file", LL_ERROR);
					Server->wait(1000);
				}
			}
		}

		void set(int64 i, bool v)
		{
			size_t bitmap_byte=(size_t)(i/8);
			size_t bitmap_bit=i%8;

			char* entry = cache_entry(bitmap_byte);

			unsigned char b = entry[bitmap_byte%bitmap_block_size];

			if(v==true)
				b=b|(1<<(7-bitmap_bit));
			else
				b=b&(~(1<<(7-bitmap_bit)));

			entry[bitmap_byte%bitmap_block_size] = b;
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<int> set_async(fuse_io_context& io, int64 i, bool v)
		{
			size_t bitmap_byte = (size_t)(i / 8);
			size_t bitmap_bit = i % 8;

			char* entry = co_await cache_entry_async(io, bitmap_byte);

			unsigned char b = entry[bitmap_byte % bitmap_block_size];

			if (v == true)
				b = b | (1 << (7 - bitmap_bit));
			else
				b = b & (~(1 << (7 - bitmap_bit)));

			entry[bitmap_byte % bitmap_block_size] = b;

			co_return 0;
		}
#endif

		size_t set_range(int64 start, int64 end, bool v)
		{
			size_t set_bits = 0;
			for(;start<end;++start)
			{
				if(get(start)!=v)
				{
					set(start, v);
					++set_bits;
				}
			}
			return set_bits;
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<size_t> set_range_async(fuse_io_context& io, int64 start, int64 end, bool v)
		{
			size_t set_bits = 0;
			for (; start < end; ++start)
			{
				if (co_await get_async(io, start) != v)
				{
					co_await set_async(io, start, v);
					++set_bits;
				}
			}
			co_return set_bits;
		}
#endif

		char getb(int64 i)
		{
			size_t bitmap_byte = (size_t)(i / 8);
			char* entry = cache_entry(bitmap_byte);
			return entry[bitmap_byte%bitmap_block_size];
		}

		char* getPtr(int64 i, size_t& bsize)
		{
			size_t bitmap_byte = (size_t)(i / 8);
			char* entry = cache_entry(bitmap_byte);
			bsize = bitmap_block_size - bitmap_byte%bitmap_block_size;
			return &entry[bitmap_byte%bitmap_block_size];
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<std::pair<char*, size_t> > getPtrAsync(fuse_io_context& io, int64 i)
		{
			size_t bitmap_byte = (size_t)(i / 8);
			char* entry = co_await cache_entry_async(io, bitmap_byte);
			size_t bsize = bitmap_block_size - bitmap_byte % bitmap_block_size;
			co_return std::make_pair(&entry[bitmap_byte % bitmap_block_size], bsize);
		}
#endif

		bool get(int64 i)
		{
			size_t bitmap_byte=(size_t)(i/8);
			size_t bitmap_bit=i%8;

			char* entry = cache_entry(bitmap_byte);

			unsigned char b = entry[bitmap_byte%bitmap_block_size];

			bool has_bit=((b & (1<<(7-bitmap_bit)))>0);

			return has_bit;
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<bool> get_async(fuse_io_context& io, int64 i)
		{
			size_t bitmap_byte = (size_t)(i / 8);
			size_t bitmap_bit = i % 8;

			char* entry = co_await cache_entry_async(io, bitmap_byte);

			unsigned char b = entry[bitmap_byte % bitmap_block_size];

			bool has_bit = ((b & (1 << (7 - bitmap_bit))) > 0);

			co_return has_bit;
		}
#endif

		bool flush()
		{
			while(!cache.empty())
			{
				evict_one();
			}

			return true;
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<void> flush_async(fuse_io_context& io)
		{
			while (!cache.empty())
			{
				if (!co_await evict_one_async(io))
				{
					co_return;
				}
			}
		}
#endif

		size_t count_bits_old()
		{
			size_t set_count = 0;
			for (int64 i = 0; i < total_size;++i)
			{
				if (get(i))
				{
					++set_count;
				}
			}
			return set_count;
		}

		size_t count_bits()
		{
			size_t set_count = 0;
			for(int64 i=0;i<total_size;)
			{
				if(i%8==0)
				{
					size_t bsize;
					char* ptr = getPtr(i, bsize);
					char* eptr = ptr + bsize;
					bool skipped = true;
					for (;ptr < eptr && i < total_size;)
					{
						if (*ptr == (char)0xFF)
						{
							set_count += 8;
						}
						else if(*ptr!=0)
						{
							skipped = false;
							break;
						}
						i += 8;
						++ptr;
					}
					if (skipped)
					{
						continue;
					}
				}

				if(get(i))
				{
					++set_count;
				}

				++i;
			}

			assert(total_size>bitmap_assert_limit || set_count == count_bits_old());

			return set_count;
		}

		bool get_range_old( int64 start, int64 end )
		{
			for(;start<end;++start)
			{
				if(get(start))
				{
					return true;
				}
			}
			return false;
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<bool> get_range_old_async(fuse_io_context& io, int64 start, int64 end)
		{
			for (; start < end; ++start)
			{
				if (co_await get_async(io, start))
				{
					co_return true;
				}
			}
			co_return false;
		}
#endif
		
		bool get_range( int64 start, int64 end )
		{
			if(end-start<=8)
			{
				return get_range_old(start, end);
			}
			
			int64 orig_start = start;
			
			for(;start<end && start%8!=0;++start)
			{
				if(get(start))
				{
					assert(total_size>bitmap_assert_limit || get_range_old(orig_start, end));
					return true;
				}
			}
			
			for(;start+8<end;)
			{
				size_t bsize;
				char* ptr = getPtr(start, bsize);
				char* eptr = ptr + bsize;
				for (;ptr < eptr && start+8<end;)
				{
					if(*ptr!=0)
					{
						assert(total_size>bitmap_assert_limit || get_range_old(orig_start, end));
						return true;
					}
					start+=8;
					++ptr;
				}
			}			
			
			for(;start<end;++start)
			{
				if(get(start))
				{
					assert(total_size>bitmap_assert_limit || get_range_old(orig_start, end));
					return true;
				}
			}
			
			assert(total_size>bitmap_assert_limit || !get_range_old(orig_start, end));
			return false;
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<bool> get_range_async(fuse_io_context& io, int64 start, int64 end)
		{
			if (end - start <= 8)
			{
				co_return co_await get_range_old_async(io, start, end);
			}

			int64 orig_start = start;

			for (; start < end && start % 8 != 0; ++start)
			{
				if (co_await get_async(io, start))
				{
					co_return true;
				}
			}

			for (; start + 8 < end;)
			{
				auto [ptr, bsize] = co_await getPtrAsync(io, start);
				char* eptr = ptr + bsize;
				for (; ptr < eptr && start + 8 < end;)
				{
					if (*ptr != 0)
					{
						co_return true;
					}
					start += 8;
					++ptr;
				}
			}

			for (; start < end; ++start)
			{
				if (co_await get_async(io, start))
				{
					co_return true;
				}
			}

			co_return false;
		}
#endif

		size_t meminfo()
		{
			return cache.size()*bitmap_block_size;
		}

		size_t get_max_cache_items()
		{
			return bitmap_cache_items;
		}

		void clear_cache_no_flush()
		{
			while (!cache.empty())
			{
				std::pair<size_t, char*> evicted = cache.evict_one();
				delete[] evicted.second;
			}
		}

	private:

		char* cache_entry(size_t bitmap_byte)
		{
			size_t block = bitmap_byte/bitmap_block_size;

			char** res = cache.get(block);

			if(res!=nullptr)
			{
				return *res;
			}

			return fill_cache(block);
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<char*> cache_entry_async(fuse_io_context& io, size_t bitmap_byte)
		{
			size_t block = bitmap_byte / bitmap_block_size;

			char** res = cache.get(block);

			if (res != NULL)
			{
				co_return *res;
			}

			co_return co_await fill_cache_async(io, block);
		}
#endif

		char* fill_cache(size_t block)
		{
			char* np = new char[bitmap_block_size];

			backing_file->Seek(block*bitmap_block_size);

			if(backing_file->Read(np, bitmap_block_size)!=bitmap_block_size)
			{
				throw std::runtime_error("Error reading from backing bitmap file");
			}

			cache.put(block, np);

			evict_from_cache();

			return np;
		}

#ifdef HAS_ASYNC
		struct BlockAwaitersCo
		{
			BlockAwaitersCo* next;
			std::coroutine_handle<> awaiter;
		};

		BlockAwaitersCo* block_awaiters_head = nullptr;

		struct BlockAwaiters
		{
			BlockAwaiters(SparseFileBitmap& bitmap)
				: bitmap(bitmap) {}
			BlockAwaiters(BlockAwaiters const&) = delete;
			BlockAwaiters(BlockAwaiters&& other) = delete;
			BlockAwaiters& operator=(BlockAwaiters&&) = delete;
			BlockAwaiters& operator=(BlockAwaiters const&) = delete;

			bool await_ready() const noexcept
			{
				return false;
			}

			void await_suspend(std::coroutine_handle<> p_awaiter) noexcept
			{
				awaiter.awaiter = p_awaiter;
				awaiter.next = bitmap.block_awaiters_head;
				bitmap.block_awaiters_head = &awaiter;
			}

			void await_resume() const noexcept
			{
			}

		private:
			BlockAwaitersCo awaiter;
			SparseFileBitmap& bitmap;
		};

		fuse_io_context::io_uring_task<char*> fill_cache_async(fuse_io_context& io, size_t block)
		{
			if (async_retrieval.find(block) != async_retrieval.end())
			{
				while (async_retrieval.find(block) != async_retrieval.end())
				{
					co_await BlockAwaiters(*this);
				}

				char** op = cache.get(block);

				if (op != nullptr)
					co_return *op;
			}

			async_retrieval.insert(block);

			char* np = new char[bitmap_block_size];

			io_uring_sqe* sqe = io.get_sqe();

			io_uring_prep_read(sqe, get_file_fd(backing_file->getOsHandle()), np,
				bitmap_block_size, block * bitmap_block_size);
			
			int rc = co_await io.complete(sqe);

			if (rc < 0 || rc != bitmap_block_size)
			{
				Server->Log("Error reading from backing bitmap file rc="+convert(rc), LL_ERROR);
				throw std::runtime_error("Error reading from backing bitmap file");
			}

#ifndef NDEBUG
			char** op = cache.get(block);
			assert(op == nullptr);
#endif
			cache.put(block, np);

			async_retrieval.erase(block);

			bool has_skip_evict = false;
			bool can_evict = false;
			if (async_evict_skip_block == std::string::npos)
			{
				async_evict_skip_block = block;
				can_evict = true;
			}
			else if (block_awaiters_head!=nullptr)
			{
				skip_evict_blocks.insert(block);
				has_skip_evict = true;
			}

			resume_retrieval_waiters();

			if (can_evict)
			{
				co_await evict_from_cache_async(io);
				async_evict_skip_block = std::string::npos;
			}

			if (has_skip_evict)
			{
				skip_evict_blocks.erase(block);
			}

			co_return np;
		}

		void resume_retrieval_waiters()
		{
			BlockAwaitersCo* block_curr = block_awaiters_head;
			block_awaiters_head = nullptr;

			while (block_curr != nullptr)
			{
				BlockAwaitersCo* next = block_curr->next;
				block_curr->awaiter.resume();
				block_curr = next;
			}
		}
#endif

		void evict_one()
		{
			std::pair<size_t, char*> evicted = cache.evict_one();

			while(backing_file->Write((int64)evicted.first*bitmap_block_size,
				reinterpret_cast<char*>(evicted.second),
				static_cast<_u32>(bitmap_block_size))!=bitmap_block_size)
			{
				Server->Log("Error writing to backing bitmap file. Retrying...", LL_WARNING);
				Server->wait(1000);
			}

			delete[] evicted.second;
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<bool> evict_one_async(fuse_io_context& io)
		{
			std::pair<size_t, char*> evicted = cache.evict_one();

			if (evicted.first == async_evict_skip_block
				|| skip_evict_blocks.find(evicted.first) != skip_evict_blocks.end())
			{
				cache.put(evicted.first, evicted.second);
				co_return false;
			}

			async_retrieval.insert(evicted.first);

			int rc;
			do
			{
				io_uring_sqe* sqe = io.get_sqe();

				io_uring_prep_write(sqe, get_file_fd(backing_file->getOsHandle()),
					evicted.second, bitmap_block_size, evicted.first * bitmap_block_size);

				rc = co_await io.complete(sqe);

				if (rc < 0 || rc != bitmap_block_size)
				{
					Server->Log("Error writing to backing bitmap file. Retrying...", LL_WARNING);
					co_await io.sleep_ms(1000);
				}

			} while (rc < 0 || rc != bitmap_block_size);
			
			delete[] evicted.second;
			async_retrieval.erase(evicted.first);
			resume_retrieval_waiters();

			co_return true;
		}
#endif

		void evict_from_cache()
		{
			while(cache.size()>bitmap_cache_items)
			{
				evict_one();
			}
		}

#ifdef HAS_ASYNC
		fuse_io_context::io_uring_task<void> evict_from_cache_async(fuse_io_context& io)
		{
			while (cache.size() > bitmap_cache_items)
			{
				if (!co_await evict_one_async(io))
				{
					co_return;
				}
			}
		}
#endif

		IFsFile* backing_file;
		size_t bitmap_size;
		int64 total_size;
		common::lrucache<size_t, char*> cache;
		size_t bitmap_cache_items;
		std::set<size_t> async_retrieval;
		size_t async_evict_skip_block;
		std::set<size_t> skip_evict_blocks;
	};

	class PreloadThread : public IThread
	{
		IPipe* pipe;
		CloudFile* cf;
	public:
		PreloadThread(IPipe* pipe, CloudFile* cf)
			: pipe(pipe), cf(cf) {}

		void operator()()
		{
			std::string msg;
			char buffer[block_size];
			while (pipe->Read(&msg))
			{
				if (msg.empty())
				{
					pipe->Write(msg);
					break;
				}

				CRData data(msg.data(), msg.size());
				int64 pos;
				data.getInt64(&pos);

				cf->Read(pos, buffer, block_size);
			}

			delete this;
		}
	};

	class UpdateMissingChunksThread : public IThread
	{
		CloudFile* cf;
		bool do_stop;
		std::unique_ptr<IMutex> mutex;
		std::unique_ptr<ICondition> cond;
	public:
		UpdateMissingChunksThread(CloudFile* cf)
			: mutex(Server->createMutex()),
			cond(Server->createCondition()),
			do_stop(false), cf(cf) {}
		void operator()()
		{
			IScopedLock lock(mutex.get());
			bool no_missing = true;
			while (!do_stop)
			{
				if (no_missing)
				{
					cond->wait(&lock);
				}

				if (do_stop)
					break;

				int64 starttime = Server->getTimeMS();
				while (Server->getTimeMS() - starttime < 10000
					&& !do_stop)
					cond->wait(&lock, 1000);

				if (do_stop)
					break;

				no_missing = cf->update_missing_fs_chunks();
			}
		}

		void stop() {
			IScopedLock lock(mutex.get());
			do_stop = true;
			cond->notify_all();
		}

		void notify() {
			cond->notify_all();
		}
	};

	class MigrationCoordThread : public IThread
	{
		bool has_error;
		CloudFile* cf;
		std::string conf_fn;
		bool continue_migration;
	public:
		MigrationCoordThread(CloudFile* cf, std::string conf_fn, bool continue_migration)
			: has_error(false), cf(cf), conf_fn(conf_fn), continue_migration(continue_migration) {}
		void operator()()
		{
			if (!cf->migrate(conf_fn, continue_migration))
				has_error = true;

			delete this;
		}
	};

	class ShareWithUpdater : public IThread
	{
		int64 get_mount_total_space(const std::string& mount)
		{
			int64 total_size;
			if (os_directory_exists(mount)
				&& (total_size = os_total_space(mount))>0)
			{
				std::unique_ptr<IFile> zero_shrink_file(Server->openFile(ExtractFilePath(mount) + os_file_sep() + "zero.shrink.file", MODE_READ));
				if (zero_shrink_file.get() != nullptr)
				{
					total_size -= zero_shrink_file->Size();
				}

				return total_size;
			}

			return -1;
		}

	public:
		ShareWithUpdater(std::string own_mount, const std::string& mounts, TransactionalKvStore& kv_store)
			:own_mount(std::move(own_mount)), kv_store(kv_store), do_stop(false),
			stop_cond(Server->createCondition()), stop_mutex(Server->createMutex())
		{
			Tokenize(mounts, share_mounts, ";");
		}

		void operator()()
		{
			IScopedLock lock(stop_mutex.get());
			while (!do_stop)
			{
				stop_cond->wait(&lock, 10 * 60 * 1000);

				if (do_stop)
					break;

				lock.relock(nullptr);

				if (!is_automount_finished())
				{
					lock.relock(stop_mutex.get());
					continue;
				}

				int64 own_total_space = get_mount_total_space(own_mount);

				if (own_total_space <= 0)
					continue;


				int64 sum_total_space = 0;
				for (const std::string& mount : share_mounts)
				{
					int64 total_space = get_mount_total_space(mount);

					if (total_space <= 0)
						continue;

					sum_total_space += total_space;
				}

				double prop = static_cast<double>(own_total_space) / (sum_total_space + own_total_space);

				int64 cache_total_space = kv_store.cache_total_space();

				if (cache_total_space > 0)
				{
					int64 new_max_cachesize = static_cast<int64>(static_cast<double>(cache_total_space)*prop);
					Server->Log("Setting max cachesize to " + PrettyPrintBytes(new_max_cachesize) + " cause own file system size " + PrettyPrintBytes(own_total_space) + " other file system size " + PrettyPrintBytes(sum_total_space), LL_INFO);
					kv_store.set_max_cachesize(new_max_cachesize);
				}

				lock.relock(stop_mutex.get());
			}
		}
	private:
		std::vector<std::string> share_mounts;
		std::string own_mount;
		TransactionalKvStore& kv_store;
		std::unique_ptr<ICondition> stop_cond;
		std::unique_ptr<IMutex> stop_mutex;
		bool do_stop;
	};

	bool syncDir(const std::string& fp)
	{
#ifndef _WIN32
		int fd = open(fp.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd == -1)
			return false;

		if (fsync(fd) != 0)
		{
			close(fd);
			return false;
		}

		close(fd);
		return true;
#else
		return false;
#endif
	}
} // namespace {}

std::string getCdInterfacePath();

void setMountStatus(const std::string& data)
{
#ifdef _WIN32
	writestring(data, getCdInterfacePath() + "/mount.status");
#else
	int fd = open((getCdInterfacePath() + "/mount.status").c_str(), O_CREAT|O_WRONLY|O_CLOEXEC, S_IRWXU | S_IRGRP | S_IROTH);
	if (fd != -1)
	{
		flock(fd, LOCK_EX);
		ftruncate(fd, 0);
		write(fd, data.data(), data.size());
		flock(fd, LOCK_UN);
		close(fd);
	}
#endif
}

void setMountStatusErr(const std::string & err)
{
	std::string last_logs = extractLastLogErrors(5, std::string(), true);
	std::vector<std::string> lines;
	Tokenize(last_logs, lines, "\n");

	JSON::Object jerr;
	jerr.set("state", "error");
	jerr.set("err", err);

	JSON::Array jlast_logs;
	for (std::string& line : lines)
		jlast_logs.add(line);

	jerr.set("last_logs", jlast_logs);

	setMountStatus(jerr.stringify(true));
}


CloudFile::CloudFile(const std::string& cache_path,
	IBackupFileSystem* cachefs,
	int64 p_cloudfile_size,
	int64 max_cloudfile_size,
	IOnlineKvStore* online_kv_store,
	const std::string& encryption_key,
	ICompressEncryptFactory* compress_encrypt_factory, bool verify_cache,
	float cpu_multiplier,
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
	unsigned int cache_comp, unsigned int meta_cache_comp)
	: online_kv_store(online_kv_store),
	kv_store(cachefs, min_cachesize, min_free_size + reserved_cache_device_space,
		critical_free_size + reserved_cache_device_space, throttle_free_size + reserved_cache_device_space,
		min_metadata_cache_free, background_compress ? comp_percent : 0, comp_start_limit,
		online_kv_store, encryption_key, compress_encrypt_factory, verify_cache,
		cpu_multiplier, no_compress_mult, with_prev_link, allow_evict, with_submitted_files,
		resubmit_compressed_ratio, max_memfile_size, memcache_path, memory_usage_factor, only_memfiles,
		background_comp_method, cache_comp, meta_cache_comp),
	cf_pos(0), active_big_block(-1),
	last_stat_update_time(0), used_bytes(0), last_flush_check(0),
	mutex(Server->createMutex()),
	new_big_blocks_bitmap_file(nullptr),
	cloudfile_size(p_cloudfile_size),
	writeback_count(0), io_alignment(512),
	locked_extents_max_alive(0), exit_thread(false), thread_cond(Server->createCondition()),
	wait_for_exclusive(0), memory_usage_factor(memory_usage_factor), bitmaps_file_size(0),
	flush_enabled(0), cache_path(cache_path),
	chunks_mutex(Server->createSharedMutex()),
	last_fs_chunks_update(0), updating_fs_chunks(false),
	update_missing_chunks_thread_ticket(ILLEGAL_THREADPOOL_TICKET),
	migrate_to_cf(nullptr),
	migration_ticket(ILLEGAL_THREADPOOL_TICKET),
	in_write_retrieval_cond(Server->createCondition()),
	read_bytes(0),
	written_bytes(0),
	slog_path(slog_path), slog_max_size(slog_max_size),
	enable_raid_freespace_stats(false),
	is_flushing(false),
	is_async(is_async),
	msg_queue_mutex(Server->createMutex()),
	msg_queue_id(0),
	msg_queue_cond(Server->createCondition()),
	fracture_big_blogs_ticket(ILLEGAL_THREADPOOL_TICKET),
	cachefs(cachefs)
{
	setMountStatus("{\"state\": \"get_size\"}");

	IFile* f_cloudfile_size = kv_store.get("cloudfile_size", TransactionalKvStore::BitmapInfo::Unknown,
		TransactionalKvStore::Flag::disable_fd_cache|TransactionalKvStore::Flag::disable_throttling, -1);

	if (f_cloudfile_size == nullptr)
	{
		throw std::runtime_error("Cannot open cloudfile_size");
	}


#ifdef HAS_ASYNC
	zero_file.reset(Server->openTemporaryFile());
	if (zero_file.get() == nullptr)
	{
		throw std::runtime_error("Error opening /dev/zero. " + os_last_error_str());
	}
	if(!zero_file->Resize(100*1024*1024))
	{
		throw std::runtime_error("Error resizing /dev/zero. " + os_last_error_str());
	}
#endif //HAS_ASYNC

	/*zero_file.reset(Server->openFile("/dev/zero", MODE_RW));
	if (zero_file.get() == nullptr)
	{
		throw std::runtime_error("Error opening /dev/zero. " + os_last_error_str());
	}*/

#ifdef HAS_ASYNC
	null_file.reset(Server->openFile("/dev/null", MODE_RW));
	if (null_file.get()==nullptr)
	{
		throw std::runtime_error("Error opening /dev/null. " + os_last_error_str());
	}

	queue_in_eventfd = eventfd(0, EFD_CLOEXEC);
	if (queue_in_eventfd == -1)
	{
		throw std::runtime_error("Error creating queue in eventfd. " + os_last_error_str());
	}

	update_fs_chunks_eventfd = eventfd(0, EFD_CLOEXEC);
	if (update_fs_chunks_eventfd == -1)
	{
		throw std::runtime_error("Error creating update_fs_chunks_eventfd. " + os_last_error_str());
	}
#endif

	int64 read_cloudfile_size;
	bool write_new = false;
	if (f_cloudfile_size->Read(0, reinterpret_cast<char*>(&read_cloudfile_size), sizeof(read_cloudfile_size)) == sizeof(read_cloudfile_size))
	{
		if (cloudfile_size != -1)
		{
			if (read_cloudfile_size != cloudfile_size)
			{
				write_new = true;
			}

			if (read_cloudfile_size > cloudfile_size)
			{
				Server->Log("Cloud drive size is bigger than configured size. Configured=" + PrettyPrintBytes(cloudfile_size) + " actual=" + PrettyPrintBytes(read_cloudfile_size) + ". Using actual size.", LL_WARNING);
				cloudfile_size = read_cloudfile_size;
			}
		}
		else
		{
			cloudfile_size = read_cloudfile_size;
		}
	}
	else
	{
		write_new = true;
	}

	if (write_new)
	{
#ifdef HAS_LOCAL_BACKEND
		if (cloudfile_size == -1)
		{
			auto frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store);
			if (frontend != nullptr)
			{
				auto local = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
				if (local != nullptr)
				{
					int64 free_space;
					local->get_space_info(cloudfile_size, free_space);
				}
			}
		}
#endif

		if (max_cloudfile_size > 0)
		{
			cloudfile_size = (std::min)(cloudfile_size, max_cloudfile_size);
		}

		if (f_cloudfile_size->Write(0, reinterpret_cast<const char*>(&cloudfile_size), sizeof(cloudfile_size)) != sizeof(cloudfile_size))
		{
			Server->Log("Error writing new cloudfile size. " + os_last_error_str(), LL_ERROR);
		}
	}

	kv_store.release("cloudfile_size");

	setMountStatus("{\"state\": \"open_bitmaps\"}");

	open_bitmaps();

	setMountStatus("{\"state\": \"delete_objects\"}");

	bool submit_need_flush = false;
	if (online_kv_store->submit_del(this, kv_store.get_transid(), submit_need_flush) &&
		submit_need_flush)
	{
		if (!close_bitmaps())
			throw std::runtime_error("Error closing bitmaps after submit_del");

		if (!kv_store.checkpoint(true, 0))
			throw std::runtime_error("Error checkpointing after submit_del");

		open_bitmaps();

		online_kv_store->submit_del_post_flush();
	}

	update_missing_chunks_thread.reset(new UpdateMissingChunksThread(this));
	update_missing_chunks_thread_ticket = Server->getThreadPool()->execute(update_missing_chunks_thread.get(), "mchunk update");


	IFsFile* migration_settings_f = kv_store.get("clouddrive_migration_settings", TransactionalKvStore::BitmapInfo::Unknown,
		TransactionalKvStore::Flag::disable_fd_cache | TransactionalKvStore::Flag::disable_throttling, -1);

	if (migration_settings_f != nullptr
		&& migration_settings_f->Size()>0)
	{
		setMountStatus("{\"state\": \"continue_migration\"}");

		Server->Log("Continuing cloud drive migration", LL_WARNING);

		std::unique_ptr<IFile> tmpf(Server->openTemporaryFile());
		std::string tmpfn = tmpf->getFilename();

		if (tmpf != nullptr
			&& copy_file(migration_settings_f, tmpf.get()))
		{
			tmpf.reset();

			migration_ticket = Server->getThreadPool()->execute(new MigrationCoordThread(this, tmpfn, true), "migration coord");
		}
		kv_store.release("clouddrive_migration_settings");
	}
	else if (migration_settings_f != nullptr)
	{
		kv_store.release("clouddrive_migration_settings");
		kv_store.del("clouddrive_migration_settings");
	}
	else
	{
		kv_store.release("clouddrive_migration_settings");
	}

	if (!share_with_mount_paths.empty())
	{
		share_with_updater.reset(new ShareWithUpdater(mount_path, share_with_mount_paths, kv_store));
		share_with_updater_ticket = Server->getThreadPool()->execute(share_with_updater.get(), "share updt");
	}

	setMountStatus("{\"state\": \"ready\"}");

	if (FileExists("/var/urbackup/no_fallocate_block_file"))
	{
		fallocate_block_file = false;
	}
}

CloudFile::~CloudFile()
{
	if (update_missing_chunks_thread.get() != nullptr)
		update_missing_chunks_thread->stop();

	kv_store.set_num_second_chances_callback(nullptr);

	{
		IScopedLock lock(mutex.get());
		exit_thread = true;
		thread_cond->notify_all();
	}

	Server->getThreadPool()->waitFor(fracture_big_blogs_ticket);
	Server->getThreadPool()->waitFor(update_missing_chunks_thread_ticket);

	if (migration_ticket != ILLEGAL_THREADPOOL_TICKET)
		Server->getThreadPool()->waitFor(migration_ticket);
}

std::string CloudFile::Read( _u32 tr, bool* has_error)
{
	std::string ret;
	ret.resize(tr);

	_u32 read = Read(&ret[0], tr, has_error);

	if(read==0)
	{
		return std::string();
	}

	ret.resize(read);
	return ret;
}

std::string CloudFile::Read( int64 pos, _u32 tr, bool* has_error)
{
	std::string ret;
	ret.resize(tr);

	_u32 read = Read(pos, &ret[0], tr, has_error);

	if(read==0)
	{
		return std::string();
	}

	ret.resize(read);
	return ret;
}

_u32 CloudFile::Read(char* buffer, _u32 bsize, bool* has_error)
{
	_u32 ret = Read(cf_pos, buffer, bsize, has_error);
	cf_pos+=ret;
	return ret;
}

_u32 CloudFile::Read(int64 pos, char* buffer, _u32 bsize, bool* has_error)
{
	return ReadInt(pos, buffer, bsize, nullptr, 
		TransactionalKvStore::Flag::prioritize_read|
		TransactionalKvStore::Flag::read_random, has_error);
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::ReadAsync(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io,
		int64 pos, _u32 bsize, unsigned int flags, bool ext_lock)
{
	fuse_in_header* fheader = reinterpret_cast<fuse_in_header*>(fuse_io.header_buf);
	fuse_out_header* out_header = reinterpret_cast<fuse_out_header*>(fuse_io.scratch_buf);
    out_header->error = 0;
    out_header->len = sizeof(fuse_out_header) + bsize;
    out_header->unique = fheader->unique;

	if(pos>cloudfile_size)
	{
		Server->Log("Read position " + convert(pos) + " bigger than cloud drive file " + convert(cloudfile_size), LL_INFO);

		std::vector<ReadTask> read_tasks;
		read_tasks.push_back(ReadTask(get_file_fd(zero_file->getOsHandle()), zero_file.get(), std::string(), 0, bsize));

		int ret = co_await complete_read_tasks(io, fuse_io, read_tasks, 0,
			pos, bsize, flags);

		co_return ret;
	}

	EXTENSIVE_DEBUG_LOG(Server->Log("Reading " + convert(bsize) + " bytes at pos " + convert(pos), LL_DEBUG);)

	EXTENSIVE_DEBUG_LOG(int64 starttime = Server->getTimeMS();)
	
	int64 target = pos + bsize;

	if (target>cloudfile_size)
	{
		target = cloudfile_size;
		bsize = static_cast<_u32>(target - pos);
	}

	size_t lock_idx;
	int64 orig_pos = pos;
	if(!ext_lock)
	{
		lock_idx = co_await lock_extent_async(orig_pos, bsize, false);
	}

	std::vector<ReadTask> read_tasks;

	size_t flush_mem_n_tasks = 0;

	bool io_error = false;
	while(pos<target)
	{
		bool has_block = co_await bitmap->get_async(io, pos/block_size);

		if(!has_block)
		{
			int64 tozero = (std::min)((block_size - pos%block_size), target-pos);
			if (!read_tasks.empty() && read_tasks.back().file == zero_file.get())
			{
				read_tasks.back().size += tozero;
			}
			else
			{
				read_tasks.push_back(ReadTask(get_file_fd(zero_file->getOsHandle()), zero_file.get(), std::string(), 0, static_cast<size_t>(tozero)));
			}
			Server->Log("Adding " + convert(tozero) + " zero bytes at " + convert(pos), LL_INFO);
			EXTENSIVE_DEBUG_LOG(Server->Log("Adding "+convert(tozero)+" zero bytes at "+convert(pos), LL_DEBUG);)
			pos+=tozero;
			continue;
		}

		//TODO: FIXME: Currently it reads the whole block without checking the bitmap further
		//(and returning zeroes), i.e. discard_zeroes_data=0

		bool in_big_block = co_await big_blocks_bitmap->get_async(io, pos/big_block_size);

		int64 curr_block_size;
		std::string key;
		if(in_big_block)
		{
			curr_block_size = big_block_size;
			key = big_block_key(pos/curr_block_size);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(pos/curr_block_size);
		}

		int64 toread = (std::min)((curr_block_size - pos%curr_block_size), target-pos);

		unsigned int curr_flags = flags;
		if (co_await is_metadata_async(io, pos, key))
		{
			curr_flags |= TransactionalKvStore::Flag::disable_memfiles;
		}
		
		IFsFile* block = co_await kv_store.get_async(io, key, TransactionalKvStore::BitmapInfo::Present,
			curr_flags|TransactionalKvStore::Flag::read_only, curr_block_size);
		
		bool has_read_error;
		if(block!=NULL)
		{
			int64 block_pos = pos % curr_block_size;
			read_tasks.push_back(ReadTask(get_file_fd(block->getOsHandle()), block, key, block_pos, static_cast<_u32>(toread)));

			if (block->getFilename()!=key)
			{
				read_tasks.back().flush_mem = true;
				++flush_mem_n_tasks;
			}
		}
		else
		{
			addSystemEvent("cache_err",
				"Error reading",
				"Cannot get block for volume offset " + convert(pos) + ""
				" at block positition " + convert(pos % curr_block_size) + " len " + convert(toread) + ". ", LL_ERROR);
			io_error = true;
			break;
		}

		pos+= toread;
		read_bytes += toread;
	}

	if (io_error)
	{
		if (!ext_lock)
		{
			unlock_extent_async(orig_pos, bsize, false, lock_idx);
		}

		out_header->error = -EIO;
		out_header->len = sizeof(fuse_out_header);

		io_uring_sqe* header_sqe = io.get_sqe(2);
		io_uring_prep_write_fixed(header_sqe, fuse_io.pipe[1],
			fuse_io.scratch_buf, sizeof(fuse_out_header),
			-1, fuse_io.scratch_buf_idx);
		header_sqe->flags |= IOSQE_FIXED_FILE | IOSQE_IO_LINK;

		io_uring_sqe* splice_sqe = io.get_sqe();
		io_uring_prep_splice(splice_sqe, fuse_io.pipe[0],
			-1, io.fuse_ring.fd, -1, out_header->len,
			SPLICE_F_FD_IN_FIXED | SPLICE_F_NONBLOCK);
		splice_sqe->flags |= IOSQE_FIXED_FILE;

		auto [rc1, rc2] = co_await io.complete(std::make_pair(header_sqe, splice_sqe));

		if (rc1 < 0)
		{
			co_return -1;
		}

		if (rc2 < 0 || rc2 < out_header->len)
		{
			co_return -1;
		}

		co_return 0;
	}

	if (read_tasks.empty())
	{
		Server->Log("No read_tasks in ReadAsync", LL_WARNING);
		abort();
		co_return -1;
	}

	//Server->Log("flush_mem_n_tasks=" + convert(flush_mem_n_tasks));

	int ret = co_await complete_read_tasks(io, fuse_io, read_tasks, flush_mem_n_tasks,
		orig_pos, bsize, flags);
		//Maybe needs loop for short splice

	for (ReadTask& task : read_tasks)
	{
		if(!task.key.empty())
			co_await kv_store.release_async(io, task.key);
	}
	
	if(!ext_lock)
	{
		unlock_extent_async(orig_pos, bsize, false, lock_idx);
	}
	
	EXTENSIVE_DEBUG_LOG(Server->Log("Done. " + convert(Server->getTimeMS() - starttime) + "ms", LL_DEBUG);)

	co_return ret;
}
#endif //HAS_ASYNC

_u32 CloudFile::ReadInt(int64 pos, char* buffer, _u32 bsize, IScopedLock* ext_lock, 
	unsigned int flags, bool* has_error)
{
	if (is_async)
		abort();

	IScopedLock lock(nullptr);

	if(ext_lock==nullptr)
	{
		lock.relock(mutex.get());
	}
	
	if (pos > cloudfile_size)
	{
		Server->Log("Read position " + convert(pos) + " bigger than cloud drive file " + convert(cloudfile_size), LL_INFO);
		memset(buffer, 0, bsize);
		return bsize;
	}

	EXTENSIVE_DEBUG_LOG(Server->Log("Reading " + convert(bsize) + " bytes at pos " + convert(pos), LL_DEBUG);)
	
	EXTENSIVE_DEBUG_LOG(int64 starttime = Server->getTimeMS();)

	int64 target = pos + bsize;

	if (target>cloudfile_size)
	{
		target = cloudfile_size;
		bsize = static_cast<_u32>(target - pos);
	}
	
	int64 orig_pos=pos;
	if(ext_lock==nullptr)
	{
		lock_extent(lock, orig_pos, bsize, false);
	}

	while(pos<target)
	{
		bool has_block = bitmap->get(pos/block_size);

		if(!has_block)
		{
			int64 tozero = (std::min)((block_size - pos%block_size), target-pos);
			memset(buffer, 0, static_cast<size_t>(tozero));
			Server->Log("Adding " + convert(tozero) + " zero bytes at " + convert(pos), LL_INFO);
			EXTENSIVE_DEBUG_LOG(Server->Log("Adding "+convert(tozero)+" zero bytes at "+convert(pos), LL_DEBUG);)
			buffer+=tozero;
			pos+=tozero;			
			continue;
		}

		bool in_big_block = big_blocks_bitmap->get(pos/big_block_size);

		int64 curr_block_size;
		std::string key;
		if(in_big_block)
		{
			curr_block_size = big_block_size;
			key = big_block_key(pos/curr_block_size);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(pos/curr_block_size);
		}

		int64 toread = (std::min)((curr_block_size - pos%curr_block_size), target-pos);

		if (ext_lock == nullptr)
		{
			lock.relock(nullptr);
		}
		else
		{
			ext_lock->relock(nullptr);
		}

		unsigned int curr_flags = flags;
		if (is_metadata(pos, key))
		{
			curr_flags |= TransactionalKvStore::Flag::disable_memfiles;
		}
		
		IFile* block = kv_store.get(key, TransactionalKvStore::BitmapInfo::Present,
			curr_flags |TransactionalKvStore::Flag::read_only, curr_block_size);
		
		_u32 read=0;

		bool has_read_error;
		if(block!=nullptr)
		{
			_u32 last_read;
			do
			{
				int64 block_pos = pos%curr_block_size + read;
				/* If Direct-IO is enabled TODO: Fix alignment of buffer
				if (block_pos%io_alignment == 0
					&& toread%io_alignment == 0)
				{
					last_read = block->Read(block_pos, buffer+read, static_cast<_u32>(toread)-read, &has_read_error);
				}
				else
				{
					last_read = ReadAligned(block, block_pos, buffer+read, static_cast<_u32>(toread)-read, &has_read_error);
				}*/

				has_read_error = false;
				last_read = block->Read(block_pos, buffer + read, static_cast<_u32>(toread) - read, &has_read_error);
				read += last_read;

				if (read < toread)
				{
					Server->Log("Short read ("+convert(read)+"/"+convert(toread)+") while reading from file " + block->getFilename() + " (size "+convert(block->Size())+")"
						" at positition " + convert(pos%curr_block_size) + " block device pos " + convert(pos) + " len " + convert(toread) + " errno "+convert((int64)errno)+". Continuing...", LL_WARNING);
				}

			} while (!has_read_error && read<toread && last_read > 0);
		}

		if (has_read_error)
		{
			std::string syserr = os_last_error_str();
			Server->Log("Read error while reading from file "+block->getFilename()+""
				" at positition "+convert(pos%curr_block_size)+" block device pos "+convert(pos)+" len "+convert(toread)+". "
				+ syserr, LL_ERROR);

			addSystemEvent("cache_err",
				"Error reading from block file on cache",
				"Read error while reading from file " + block->getFilename() + ""
				" at positition " + convert(pos%curr_block_size) + " block device pos " + convert(pos) + " len " + convert(toread) + ". "
				+ syserr, LL_ERROR);
		}

	
		if(read<toread
			&& !has_read_error)
		{
			Server->Log("Adding " + convert(toread - read) + " zero bytes at " + convert(pos+read)+" (2)", LL_INFO);
			memset(buffer+read, 0, static_cast<size_t>(toread-read));
			read=static_cast<_u32>(toread);			
		}

		if(block!=nullptr)
		{
			kv_store.release(key);
		}

		if(read!=toread)
		{
			if (has_error) *has_error = true;
			Server->Log("Error reading from block file (read only "+convert(read)+" of "+convert(toread)+" bytes)", LL_ERROR);
			if (!has_read_error)
			{
				addSystemEvent("cache_err",
					"Error reading from block file on cache",
					"Error reading from block file at " + cachefs->getName() + " (read only " + convert(read) + " of " + convert(toread) + " bytes)", LL_ERROR);
			}
			if (ext_lock==nullptr)
			{
				lock.relock(mutex.get());
				unlock_extent(lock, orig_pos, bsize, false);
			}
			else
			{
				ext_lock->relock(mutex.get());
			}
			pos += read;
			return static_cast<_u32>(pos-orig_pos);
		}


		buffer+=read;
		pos+=read;
		read_bytes += read;

		if(ext_lock==nullptr)
		{
			lock.relock(mutex.get());
		}
		else
		{
			ext_lock->relock(mutex.get());
		}
	}
	
	if(ext_lock==nullptr)
	{
		unlock_extent(lock, orig_pos, bsize, false);
	}
	
	EXTENSIVE_DEBUG_LOG(Server->Log("Done. " + convert(Server->getTimeMS() - starttime) + "ms", LL_DEBUG);)

	return bsize;
}

_u32 CloudFile::ReadAligned(IFile* block, int64 pos, char* buffer, _u32 toread, bool* has_read_error)
{
	EXTENSIVE_DEBUG_LOG(Server->Log("Fixing alignment for read at pos "+convert(pos)+" size "+convert(toread), LL_DEBUG);)
	
	int64 startpos = (pos/io_alignment)*io_alignment;
	_u32 alignstartadd = static_cast<_u32>(pos-startpos);
	_u32 alignendadd = io_alignment - (toread+alignstartadd)%io_alignment;
	_u32 alignread = toread + alignstartadd + alignendadd;
	char* buf = reinterpret_cast<char*>(aligned_alloc(io_alignment, alignread) );
	_u32 r = block->Read(startpos, buf, alignread, has_read_error);
	
	if(r<alignstartadd)
	{
		free(buf);
		return 0;
	}
	
	r-=alignstartadd;
	
	if(r>toread)
	{
		r=toread;
	}
	
	memcpy(buffer, buf+alignstartadd, r);
	free(buf);
	
	return r;
}

_u32 CloudFile::Write( const std::string &tw, bool* has_error)
{
	return Write(tw.c_str(), static_cast<_u32>(tw.size()), has_error);
}

_u32 CloudFile::Write(int64 pos, const std::string &tw, bool* has_error)
{
	return Write(pos, tw.c_str(), static_cast<_u32>(tw.size()), has_error);
}

_u32 CloudFile::Write( const char* buffer, _u32 bsize, bool* has_error)
{
	_u32 ret = WriteInt(cf_pos, buffer, bsize, false, nullptr, true, has_error);
	cf_pos += ret;
	return ret;
}

_u32 CloudFile::Write( int64 pos, const char* buffer, _u32 bsize, bool* has_error)
{
	_u32 rc = WriteInt(pos, buffer, bsize, false, nullptr, true, has_error);
	return rc;
}

_u32 CloudFile::WriteNonBlocking( int64 pos, const char* buffer, _u32 bsize, bool* has_error)
{
	EXTENSIVE_DEBUG_LOG(Server->Log("Non-blocking write at pos "+convert(pos)+" size "+convert(bsize));)
	_u32 rc = WriteInt(pos, buffer, bsize, false, nullptr, false, has_error);
	return rc;
}

namespace
{
	class PreloadItemsThread : public IThread
	{
		CloudFile& cf;
		IPipe* pipe;
		int tag;
		bool disable_memfiles;
		bool load_only;
	public:
		PreloadItemsThread(CloudFile& cf, IPipe* pipe, int tag, bool disable_memfiles, bool load_only)
			: cf(cf), pipe(pipe), tag(tag), disable_memfiles(disable_memfiles),
			load_only(load_only) {}

		void operator()()
		{
			while (true)
			{
				std::string data;
				pipe->Read(&data);

				if (data.empty())
				{
					pipe->Write(data);
					break;
				}

				CRData rdata(data.data(), data.size());

				std::string key;
				int64 offset, len;
				if (rdata.getStr2(&key)
					&& rdata.getVarInt(&offset)
					&& rdata.getVarInt(&len))
				{
					cf.preload_key(key, offset, len, tag, disable_memfiles, load_only);
				}
			}

			delete this;
		}
	};

	class UpdateFsChunksThread : public IThread
	{
		std::string path;
		int notify_eventfd;
		IBackupFileSystem* fs;
	public:
		struct SOutput
		{
			std::vector<IBackupFileSystem::SChunk> chunks;
			std::atomic<bool> done;
		};

		UpdateFsChunksThread(IBackupFileSystem* fs,
			std::shared_ptr<SOutput> output,
			std::string path, int notify_eventfd) :
			fs(fs), output(output), path(path),
			notify_eventfd(notify_eventfd) {}

		void operator()() {
#ifdef HAS_MOUNT_SERVICE
			if (!btrfs_get_chunks_via_service(path, output->chunks))
			{
				output->chunks = btrfs_chunks::get_chunks(path);
			}
#else
			output->chunks = fs->getChunks();
#endif
			output->done.store(true, std::memory_order_release);
			if (notify_eventfd != -1)
			{
				eventfd_write(notify_eventfd, 1);
			}
			delete this;
		}
		
	private:
		std::shared_ptr<SOutput> output;
	};
}

void CloudFile::preload_items(const std::string & fn, size_t n_threads, int tag, bool disable_memfiles, bool load_only)
{
	if (is_async)
		abort();

	std::unique_ptr<IFile> f(Server->openFile(fn, MODE_READ));

	if (f.get() == nullptr)
		return;

	Server->Log("Starting preloading fn=" + fn + " size=" + PrettyPrintBytes(f->Size()) +
		" items="+convert(f->Size()/(2*sizeof(int64)))+
		" n_threads=" + convert(n_threads) + " tag=" + convert(tag) +
		" disable_memfiles=" + convert(disable_memfiles) + " load_only=" + convert(load_only), LL_INFO);

	std::vector<THREADPOOL_TICKET> tickets;
	std::unique_ptr<IPipe> pipe(Server->createMemoryPipe());
	for (size_t i = 0; i < n_threads; ++i)
	{
		tickets.push_back(Server->getThreadPool()->execute(new PreloadItemsThread(*this, pipe.get(), tag, disable_memfiles, load_only),
			"preload items"));
	}

	IScopedLock lock(mutex.get());

	char buf[1024];
	_u32 rc;
	while ((rc = f->Read(buf, sizeof(buf))) > 0)
	{
		for (size_t i = 0; i+1 < rc / sizeof(int64); i+=2)
		{
			int64 offset_start;
			memcpy(&offset_start, &buf[i * sizeof(offset_start)], sizeof(offset_start));

			int64 offset_end;
			memcpy(&offset_end, &buf[(i+1) * sizeof(offset_end)], sizeof(offset_end));

			if (offset_start > cloudfile_size)
			{
				continue;
			}

			if (offset_end > cloudfile_size)
				offset_end = cloudfile_size;

			while (offset_start < offset_end)
			{
				Server->Log("Preloading offset " + convert(offset_start) + " size " + convert(offset_end - offset_start), LL_INFO);
				bool has_block = bitmap->get(offset_start / block_size);

				if (!has_block)
				{
					int64 tozero = (std::min)((block_size - offset_start%block_size), offset_end - offset_start);
					offset_start += tozero;
					continue;
				}

				bool in_big_block = big_blocks_bitmap->get(offset_start / big_block_size);

				int64 curr_block_size;
				std::string key;
				if (in_big_block)
				{
					curr_block_size = big_block_size;
					key = big_block_key(offset_start / curr_block_size);
				}
				else
				{
					curr_block_size = small_block_size;
					key = small_block_key(offset_start / curr_block_size);
				}

				int64 toread = (std::min)((curr_block_size - offset_start%curr_block_size), offset_end - offset_start);

				CWData data;
				data.addString2(key);
				data.addVarInt((offset_start / curr_block_size)*curr_block_size);
				data.addVarInt(curr_block_size);
				pipe->Write(std::string(data.getDataPtr(), data.getDataSize()));

				while (pipe->getNumElements() > n_threads * 3)
				{
					lock.relock(nullptr);
					Server->wait(100);
					lock.relock(mutex.get());
				}
				
				offset_start += toread;
			}
		}
	}

	lock.relock(nullptr);

	pipe->Write(std::string());

	Server->getThreadPool()->waitFor(tickets);
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::preload_items_async(fuse_io_context& io, const std::string& fn, size_t n_threads, int tag, bool disable_memfiles, bool load_only)
{
	std::unique_ptr<IFile> f(Server->openFile(fn, MODE_READ));

	if (f.get() == nullptr)
		co_return 0;

	Server->Log("Starting async preloading fn=" + fn + " size=" + PrettyPrintBytes(f->Size()) +
		" items=" + convert(f->Size() / (2 * sizeof(int64))) +
		" n_threads=" + convert(n_threads) + " tag=" + convert(tag) +
		" disable_memfiles=" + convert(disable_memfiles) + " load_only=" + convert(load_only), LL_INFO);

	auto available_workers = std::make_unique<size_t>(n_threads);
	auto worker_waiters_head = std::make_unique<AwaiterCoList*>(nullptr);

	char buf[1024];
	_u32 rc;
	while ((rc = f->Read(buf, sizeof(buf))) > 0)
	{
		for (size_t i = 0; i + 1 < rc / sizeof(int64); i += 2)
		{
			int64 offset_start;
			memcpy(&offset_start, &buf[i * sizeof(offset_start)], sizeof(offset_start));

			int64 offset_end;
			memcpy(&offset_end, &buf[(i + 1) * sizeof(offset_end)], sizeof(offset_end));

			if (offset_start > cloudfile_size)
			{
				continue;
			}

			if (offset_end > cloudfile_size)
				offset_end = cloudfile_size;

			while (offset_start < offset_end)
			{
				size_t lock_idx = co_await lock_extent_async(0, 0, false);

				Server->Log("Preloading offset " + convert(offset_start) + " size " + convert(offset_end - offset_start), LL_INFO);
				bool has_block = co_await bitmap->get_async(io, offset_start / block_size);

				if (!has_block)
				{
					int64 tozero = (std::min)((block_size - offset_start % block_size), offset_end - offset_start);
					offset_start += tozero;
					unlock_extent_async(0, 0, false, lock_idx);
					continue;
				}

				bool in_big_block = co_await big_blocks_bitmap->get_async(io, offset_start / big_block_size);

				unlock_extent_async(0, 0, false, lock_idx);

				int64 curr_block_size;
				std::string key;
				if (in_big_block)
				{
					curr_block_size = big_block_size;
					key = big_block_key(offset_start / curr_block_size);
				}
				else
				{
					curr_block_size = small_block_size;
					key = small_block_key(offset_start / curr_block_size);
				}

				int64 toread = (std::min)((curr_block_size - offset_start % curr_block_size), offset_end - offset_start);


				preload_items_single_async(io, *available_workers, *worker_waiters_head, key,
					(offset_start / curr_block_size) * curr_block_size, curr_block_size,
					tag, disable_memfiles, load_only);

				offset_start += toread;
			}
		}
	}

	while (*available_workers != n_threads)
	{
		co_await io.sleep_ms(100);
	}

	co_return 0;
}
#endif //HAS_ASYNC

bool CloudFile::has_preload_item(int64 offset_start, int64 offset_end)
{
	if (is_async)
		abort();

	IScopedLock lock(mutex.get());

	if (offset_start > cloudfile_size)
	{
		return false;
	}

	if (offset_end > cloudfile_size)
		offset_end = cloudfile_size;

	while (offset_start < offset_end)
	{
		bool has_block = bitmap->get(offset_start / block_size);

		if (!has_block)
		{
			int64 tozero = (std::min)((block_size - offset_start%block_size), offset_end - offset_start);
			offset_start += tozero;
			continue;
		}

		bool in_big_block = big_blocks_bitmap->get(offset_start / big_block_size);

		int64 curr_block_size;
		std::string key;
		if (in_big_block)
		{
			curr_block_size = big_block_size;
			key = big_block_key(offset_start / curr_block_size);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(offset_start / curr_block_size);
		}

		int64 toread = (std::min)((curr_block_size - offset_start%curr_block_size), offset_end - offset_start);

		lock.relock(nullptr);

		if (!kv_store.has_item_cached(key))
		{
			return false;
		}

		lock.relock(mutex.get());

		offset_start += toread;
	}

	return true;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> CloudFile::has_preload_item_async(fuse_io_context& io, int64 offset_start, int64 offset_end)
{
	if (offset_start > cloudfile_size)
	{
		co_return false;
	}

	if (offset_end > cloudfile_size)
		offset_end = cloudfile_size;

	size_t lock_idx = co_await lock_extent_async(0, 0, false);

	while (offset_start < offset_end)
	{
		bool has_block = co_await bitmap->get_async(io, offset_start / block_size);

		if (!has_block)
		{
			int64 tozero = (std::min)((block_size - offset_start % block_size), offset_end - offset_start);
			offset_start += tozero;
			continue;
		}

		bool in_big_block = co_await big_blocks_bitmap->get_async(io, offset_start / big_block_size);

		int64 curr_block_size;
		std::string key;
		if (in_big_block)
		{
			curr_block_size = big_block_size;
			key = big_block_key(offset_start / curr_block_size);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(offset_start / curr_block_size);
		}

		int64 toread = (std::min)((curr_block_size - offset_start % curr_block_size), offset_end - offset_start);

		if (!kv_store.has_item_cached(key))
		{
			unlock_extent_async(0, 0, false, lock_idx);
			co_return false;
		}

		offset_start += toread;
	}

	unlock_extent_async(0, 0, false, lock_idx);

	co_return true;
}
#endif //HAS_ASYNC

int64 CloudFile::min_block_size()
{
	return small_block_size;
}

void CloudFile::set_is_mounted(const std::string & p_mount_path, IBackupFileSystem* fs)
{
	std::vector<IBackupFileSystem::SChunk> new_fs_chunks;
#ifdef HAS_MOUNT_SERVICE
	if (!btrfs_get_chunks_via_service(p_mount_path, new_fs_chunks))
	{
		new_fs_chunks = btrfs_chunks::get_chunks(p_mount_path);
	}
#else
	topfs = fs;
	new_fs_chunks = fs->getChunks();
#endif

	for (auto& it : new_fs_chunks)
	{
		it.offset += 8192;
	}

	{
		IScopedWriteLock lock(chunks_mutex.get());
		fs_chunks = new_fs_chunks;
		mount_path = p_mount_path;
	}


	kv_store.set_num_second_chances_callback(this);
}

void CloudFile::update_fs_chunks(bool update_time, IScopedWriteLock& lock)
{
	lock.relock(chunks_mutex.get());

#ifndef HAS_MOUNT_SERVICE
	if (topfs == nullptr)
		return;
#endif

	if (updating_fs_chunks)
	{
		return;
	}

	std::string local_mount_path;
	updating_fs_chunks = true;
	local_mount_path = mount_path;
	lock.relock(nullptr);

	std::shared_ptr<UpdateFsChunksThread::SOutput> new_fs_chunks = std::make_shared<UpdateFsChunksThread::SOutput>();
	THREADPOOL_TICKET ticket = Server->getThreadPool()->execute(
		new UpdateFsChunksThread(topfs, new_fs_chunks, local_mount_path, -1), "updt fs chunks");

	if (Server->getThreadPool()->waitFor(ticket, 1000))
	{
		lock.relock(chunks_mutex.get());

		fs_chunks = new_fs_chunks->chunks;
		for (auto& it : fs_chunks)
		{
			it.offset += 8192;
		}
	}
	else
	{
		lock.relock(chunks_mutex.get());

		if (update_time)
			last_fs_chunks_update += 60000;

		update_time = false;
	}
	

	if(update_time)
		last_fs_chunks_update = Server->getTimeMS()-10*60*1000;

	updating_fs_chunks = false;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<void> CloudFile::update_fs_chunks_async(fuse_io_context& io, bool update_time)
{
	IScopedWriteLock lock(chunks_mutex.get());

	if (updating_fs_chunks)
	{
		co_return;
	}

	std::string local_mount_path;
	updating_fs_chunks = true;
	local_mount_path = mount_path;
	lock.relock(nullptr);

	std::shared_ptr<UpdateFsChunksThread::SOutput> new_fs_chunks = std::make_shared<UpdateFsChunksThread::SOutput>();
	Server->getThreadPool()->execute(new UpdateFsChunksThread(new_fs_chunks, local_mount_path, update_fs_chunks_eventfd), "updt fs chunks");

	int64 starttime = Server->getTimeMS();
	std::vector<char> buf(sizeof(int64));

	bool thread_done = false;
	
	io_uring_sqe* sqe = io.get_sqe(2);
	io_uring_prep_read(sqe, update_fs_chunks_eventfd, buf.data(), buf.size(), 0);
	sqe->flags |= IOSQE_IO_LINK;

	io_uring_sqe* sqe_timeout = io.get_sqe();
	__kernel_timespec waitt = io.msec_timespec(1000);
	io_uring_prep_link_timeout(sqe_timeout, &waitt, 0);

	auto [rc1, rc2] = co_await io.complete(std::make_pair(sqe, sqe_timeout));

	if (new_fs_chunks->done.load(std::memory_order_acquire))
	{
		lock.relock(chunks_mutex.get());

		fs_chunks = new_fs_chunks->chunks;
		for (auto& it : fs_chunks)
		{
			it.offset += 8192;
		}
	}
	else
	{
		lock.relock(chunks_mutex.get());

		if (update_time)
			last_fs_chunks_update += 60000;

		update_time = false;
	}


	if (update_time)
		last_fs_chunks_update = Server->getTimeMS() - 10 * 60 * 1000;

	updating_fs_chunks = false;
}
#endif //HAS_ASYNC

namespace
{
#ifdef HAS_ONLINE_BACKEND
	class TokenRefreshCallback : public ITokenRefreshCallback
	{
		std::string conf_fn;
	public:
		TokenRefreshCallback(const std::string& conf_fn)
			:conf_fn(conf_fn)
		{
		}

		virtual std::string getNewToken()
		{
			std::unique_ptr<ISettingsReader> settings(Server->createFileSettingsReader(
				conf_fn));

			if (settings.get() != NULL)
			{
				std::string password;
				if (settings->getValue("auth_token", &password))
				{
					return password;
				}
			}

			return std::string();
		}
	};
#endif

	class MigrationThread : public IThread
	{
		IPipe* pipe;
		CloudFile* cf;
		std::atomic<bool>& has_error;
	public:
		MigrationThread(IPipe* pipe, CloudFile* cf, std::atomic<bool>& has_error)
			: pipe(pipe), cf(cf), has_error(has_error) {}
		void operator()()
		{
			std::vector<char> buf;
			buf.resize(block_size);

			std::string msg;
			while (pipe->Read(&msg) > 0)
			{
				CRData data(msg.data(), msg.size());

				int64 offset;
				int64 len;
				if (data.getVarInt(&offset)
					&& data.getVarInt(&len))
				{
					if (!cf->migrate(buf, offset, len))
					{
						has_error = true;
						break;
					}
				}
			}

			pipe->Write(std::string());
			delete this;
		}
	};
}

bool CloudFile::migrate(const std::string & conf_fn, bool continue_migration)
{
	std::unique_ptr<ISettingsReader> settings(Server->createMemorySettingsReader(getFile(conf_fn)));

	if (!continue_migration)
	{
		if (os_directory_exists(cache_path + "/migration"))
		{
			os_remove_nonempty_dir(cache_path + "/migration");
		}

		if (os_directory_exists(cache_path + "/migration"))
		{
			Server->Log(cache_path + "/migration still exists", LL_ERROR);
			return false;
		}

		if (!os_create_dir_recursive(cache_path + "/migration"))
		{
			Server->Log("Error creating " + cache_path + "/migration . " + os_last_error_str(), LL_ERROR);
			return false;
		}
	}

	return migrate(conf_fn, settings.get());
}

std::string CloudFile::migration_info()
{
	IScopedLock lock(mutex.get());

	if (migrate_to_cf == nullptr)
		return "{\"migration\": false}";

	int done_pc = static_cast<int>((migration_copy_done * 100) / cloudfile_size);

	JSON::Object ret;

	ret.set("done_pc", done_pc);
	ret.set("migration", true);
	ret.set("num_dirty_items", migrate_to_cf->getNumDirtyItems());
	ret.set("num_memfile_items", migrate_to_cf->getNumMemfileItems());
	ret.set("submitted_bytes", migrate_to_cf->getSubmittedBytes());
	ret.set("used_bytes", migrate_to_cf->getSubmittedBytes());
	ret.set("cache_size", migrate_to_cf->getCacheSize());
	ret.set("comp_bytes", migrate_to_cf->getCompBytes());
	ret.set("congested", migrate_to_cf->Congested());
	ret.set("raid_groups", migrate_to_cf->get_raid_groups());
	ret.set("disk_error_info", migrate_to_cf->disk_error_info());
	ret.set("memfile_bytes", migrate_to_cf->get_memfile_bytes());
	ret.set("submitted_memfile_bytes", migrate_to_cf->get_submitted_memfile_bytes());
	ret.set("conf_fn", migration_conf_fn);

	return ret.stringify(false);
}

void CloudFile::preload_key(const std::string & key, int64 offset, int64 len, int tag, bool disable_memfiles, bool load_only)
{
	if (is_async)
		abort();

	IScopedLock lock(mutex.get());

	lock_extent(lock, offset, len, false);

	bool in_big_block = big_blocks_bitmap->get(offset / big_block_size);

	int64 curr_block_size;
	std::string curr_key;
	bool has_block;
	if (in_big_block)
	{
		curr_block_size = big_block_size;
		curr_key = big_block_key(offset / curr_block_size);
		has_block = bitmap_has_big_block(offset / curr_block_size);
	}
	else
	{
		curr_block_size = small_block_size;
		curr_key = small_block_key(offset / curr_block_size);
		has_block = bitmap_has_small_block(offset / curr_block_size);
	}

	if (key == curr_key
		&& has_block)
	{
		lock.relock(nullptr);

		unsigned int flags;
		if (disable_memfiles
			&& load_only)
		{
			flags = TransactionalKvStore::Flag::read_only | TransactionalKvStore::Flag::prioritize_read | 
				TransactionalKvStore::Flag::disable_memfiles;
		}
		else
		{
			flags = TransactionalKvStore::Flag::read_only | TransactionalKvStore::Flag::prioritize_read |
				TransactionalKvStore::Flag::preload_once;

			if(disable_memfiles)
				flags |= TransactionalKvStore::Flag::disable_memfiles;
		}

		IFile* block = kv_store.get(key, TransactionalKvStore::BitmapInfo::Present,
			flags, curr_block_size, tag);

		if (block != nullptr)
		{
			kv_store.release(key);
		}

		lock.relock(mutex.get());
	}

	unlock_extent(lock, offset, len, false);
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::preload_key_async(fuse_io_context& io, const std::string& key,
	int64 offset, int64 len, int tag, bool disable_memfiles, bool load_only)
{
	size_t lock_idx = co_await lock_extent_async(offset, len, false);

	bool in_big_block = big_blocks_bitmap->get(offset / big_block_size);

	int64 curr_block_size;
	std::string curr_key;
	bool has_block;
	if (in_big_block)
	{
		curr_block_size = big_block_size;
		curr_key = big_block_key(offset / curr_block_size);
		has_block = co_await bitmap_has_big_block_async(io, offset / curr_block_size);
	}
	else
	{
		curr_block_size = small_block_size;
		curr_key = small_block_key(offset / curr_block_size);
		has_block = co_await bitmap_has_small_block_async(io, offset / curr_block_size);
	}

	if (key == curr_key
		&& has_block)
	{
		unsigned int flags;
		if (disable_memfiles
			&& load_only)
		{
			flags = TransactionalKvStore::Flag::read_only | TransactionalKvStore::Flag::prioritize_read |
				TransactionalKvStore::Flag::disable_memfiles;
		}
		else
		{
			flags = TransactionalKvStore::Flag::read_only | TransactionalKvStore::Flag::prioritize_read |
				TransactionalKvStore::Flag::preload_once;

			if (disable_memfiles)
				flags |= TransactionalKvStore::Flag::disable_memfiles;
		}

		IFile* block = co_await kv_store.get_async(io, key, TransactionalKvStore::BitmapInfo::Present,
			flags, curr_block_size, tag);

		if (block != nullptr)
		{
			co_await kv_store.release_async(io, key);
		}
	}

	unlock_extent_async(offset, len, false, lock_idx);

	co_return 0;
}
#endif //HAS_ASYNC

int64 CloudFile::get_uploaded_bytes()
{
	return online_kv_store->get_uploaded_bytes();
}

int64 CloudFile::get_downloaded_bytes()
{
	return online_kv_store->get_downloaded_bytes();
}

int64 CloudFile::get_written_bytes()
{
	if (is_async)
	{
		return written_bytes;
	}

	IScopedLock lock(mutex.get());
	return written_bytes;
}

int64 CloudFile::get_read_bytes()
{
	if (is_async)
	{
		read_bytes;
	}

	IScopedLock lock(mutex.get());
	return read_bytes;
}


std::string CloudFile::get_raid_io_stats()
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->io_stats();
		}
	}
#endif

	return std::string();
}

namespace
{
#pragma pack(1)
	struct SLogHeader
	{
		_u32 size;
		int64 offset;
		_u32 crc;
	};
#pragma pack()

	const std::string slog_magic = "TCDSLOG#1.0";
}

bool CloudFile::replay_slog()
{
#ifdef WITH_SLOG
	if (slog_path.empty())
		return true;

	if (!FileExists(slog_path))
	{
		slog.reset(Server->openFile(slog_path, MODE_WRITE));

		if (slog.get() == nullptr)
		{
			Server->Log("Error creating slog file at \"" + slog_path + "\". " + os_last_error_str(), LL_ERROR);
			return false;
		}

		if (slog->Write(slog_magic) != slog_magic.size())
		{
			Server->Log("Error writing magic to slog file at \"" + slog_path + "\". " + os_last_error_str(), LL_ERROR);
			return false;
		}

		int64 transid = kv_store.get_basetransid();

		if (slog->Write(reinterpret_cast<char*>(&transid), sizeof(transid)) != sizeof(transid))
		{
			Server->Log("Error writing transid to slog at \""+slog_path+"\". " + os_last_error_str(), LL_ERROR);
			return false;
		}

		slog->Sync();

		slog_size = slog_magic.size() + sizeof(transid);
		slog_last_sync = slog_magic.size() + sizeof(transid);

		return true;
	}

	slog.reset(Server->openFile(slog_path, MODE_READ_SEQUENTIAL));

	if (slog.get() == nullptr)
	{
		Server->Log("Error opening slog file at \"" + slog_path + "\". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	std::string magic = slog->Read(static_cast<_u32>(slog_magic.size()));

	if (magic != slog_magic)
	{
		Server->Log("Slog magic wrong", LL_ERROR);
		return false;
	}

	slog_size = slog_magic.size();


	int64 transid = 0;

	if (slog->Read(reinterpret_cast<char*>(&transid), sizeof(transid)) != sizeof(transid))
	{
		Server->Log("Error reading transid", LL_ERROR);
		return false;
	}

	if (kv_store.get_basetransid() != transid)
	{
		Server->Log("Transid in slog wrong expected " + convert(kv_store.get_basetransid()) + " got " + convert(transid), LL_ERROR);
		return false;
	}

	slog_size += sizeof(transid);
	
	std::vector<char> buf;

	Server->Log("Replaying slog size " + PrettyPrintBytes(slog->Size()), LL_INFO);

	do
	{
		bool has_read_error = false;
		SLogHeader slog_header = {};
		if (slog->Read(reinterpret_cast<char*>(&slog_header), sizeof(slog_header), &has_read_error) != sizeof(slog_header))
		{
			if (has_read_error)
			{
				Server->Log("Error reading slog header. " + os_last_error_str(), LL_ERROR);
				return false;
			}

			break;
		}

		if (buf.size() < slog_header.size)
		{
			buf.resize(slog_header.size);
		}

		if (slog->Read(buf.data(), slog_header.size, &has_read_error) != slog_header.size)
		{
			if (has_read_error)
			{
				Server->Log("Error reading slog data. " + os_last_error_str(), LL_ERROR);
				return false;
			}

			Server->Log("Error reading enough slog data", LL_ERROR);
			return false;
		}

		_u32 crc = slog_header.crc;
		slog_header.crc = 0;

		cryptopp_crc::CRC32C crc2;
		crc2.Update(reinterpret_cast<char*>(&slog_header), sizeof(slog_header));
		crc2.Update(buf.data(), slog_header.size);
		_u32 r2 = crc2.Final();

		if (r2 != crc)
		{
			Server->Log("slog crc32c wrong. " + convert(r2) + "!=" + convert(crc), LL_ERROR);
			return false;
		}

		if (Write(slog_header.offset, buf.data(), slog_header.size) != slog_header.size)
		{
			Server->Log("Error writing slog at offset " + convert(slog_header.offset) + " size " + convert(slog_header.size), LL_ERROR);
			return false;
		}

		slog_size += sizeof(slog_header) + slog_header.size;
	} while (true);

	slog.reset(Server->openFile(slog_path, MODE_RW_READNONE));

	if (slog.get() == nullptr)
	{
		Server->Log("Error opening slog file at \"" + slog_path + "\" (2). " + os_last_error_str(), LL_ERROR);
		return false;
	}

	slog_last_sync = slog_size.load();

#endif //WITH_SLOG

	return true;
}

std::string CloudFile::get_mirror_stats()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->mirror_stats();
	}

	return std::string();
}

std::string CloudFile::get_raid_freespace_stats()
{
#ifdef HAS_LOCAL_BACKEND
	{
		IScopedLock lock(mutex.get());

		if (!enable_raid_freespace_stats)
			return std::string();
	}

	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->freespace_stats();
		}
	}
#endif

	return std::string();
}

bool CloudFile::slog_write(int64 pos, const char * buffer, _u32 bsize, bool& needs_reset)
{
#ifdef WITH_SLOG
	needs_reset = false;

	if (slog.get() == nullptr)
		return true;

	SLogHeader slog_header = {};
	slog_header.offset = pos;
	slog_header.size = bsize;

	cryptopp_crc::CRC32C crc2;
	crc2.Update(reinterpret_cast<char*>(&slog_header), sizeof(slog_header));
	crc2.Update(buffer, bsize);
	slog_header.crc = crc2.Final();

	int64 slog_pos = slog_size.fetch_add(sizeof(slog_header) + bsize);

	if (slog->Write(slog_pos, reinterpret_cast<char*>(&slog_header), sizeof(slog_header)) != sizeof(slog_header))
	{
		Server->Log("Error writing to slog file " + slog_path + " (1). " + os_last_error_str(), LL_ERROR);
		return false;
	}

	if (slog->Write(slog_pos + sizeof(slog_header), buffer, bsize) != bsize)
	{
		Server->Log("Error writing to slog file " + slog_path + " (2). " + os_last_error_str(), LL_ERROR);
		return false;
	}

	int64 slog_pos_end = slog_pos + static_cast<int>(sizeof(slog_header)) + bsize;

	if (slog_pos < slog_max_size 
		&& slog_pos_end >= slog_max_size)
	{
		needs_reset = true;
	}
	else if (slog_last_sync + slog_kernel_buffer_size < slog_pos
		&& slog_pos_end >= slog_last_sync + slog_kernel_buffer_size)
	{
#ifndef _WIN32
		sync_file_range(slog->getOsHandle(), 0, slog_pos_end - slog_kernel_buffer_size,
			SYNC_FILE_RANGE_WRITE);
		posix_fadvise64(slog->getOsHandle(), 0, slog_pos_end - slog_kernel_buffer_size, POSIX_FADV_DONTNEED);
#endif
		slog_last_sync = slog_pos_end;
	}

#endif //WITH_SLOG

	return true;
}

bool CloudFile::migrate(const std::string& conf_fn, ISettingsReader * settings)
{
#ifdef WITH_MIGRATION
	if (!os_directory_exists(cache_path + "/migration"))
	{
		Server->Log(cache_path + "/migration does not exist", LL_ERROR);
		return false;
	}

	std::string key;
	if (!settings->getValue("key", &key))
	{
		Server->Log("Migration setting missing", LL_ERROR);
		return false;
	}

	std::string aes_key = pbkdf2_sha256(key);

	IScopedLock lock(mutex.get());

	int64 orig_cloudfile_size = cloudfile_size;
	++wait_for_exclusive;
	lock_extent(lock, 0, orig_cloudfile_size, true);
	--wait_for_exclusive;

	migration_conf_fn = conf_fn;

	IBackupFileSystem* migrate_cachefs = new PassThroughFileSystem(cache_path + "/migration");

	IOnlineKvStore* migrate_online_kv_store = create_migration_endpoint(conf_fn, settings,
		migrate_cachefs);

	int64 reserved_cache_device_space = 1LL * 1024 * 1024 * 1024;
	int64 min_metadata_cache_free = 1LL * 1024 * 1024 * 1024;

	int64 total_ram = get_total_ram_kb();

	if (total_ram > 0)
	{
		total_ram *= 1024LL;
	}

	int64 cache_size = 2LL * 1024 * 1024 * 1024;
	if (total_ram < 4LL * 1024 * 1024 * 1024)
	{
		cache_size = total_ram / 4;
	}

	std::string background_compression_method = settings->getValue("background_compression_method", "zstd_19");

	migrate_to_cf.reset(new CloudFile(cache_path + "/migration",
		migrate_cachefs, cloudfile_size, cloudfile_size, migrate_online_kv_store,
		aes_key, get_compress_encrypt_factory(), false, 1, false, 1,
		reserved_cache_device_space, min_metadata_cache_free, false, true, true, 0.8f, cache_size, std::string(), 1, true,
		std::string(), std::string(), CompressionMethodFromString(background_compression_method),
		std::string(), 0, is_async));

	migration_has_error = false;
	migration_copy_max = 0;
	migration_copy_done = 0;

	std::string last_transid = settings->getValue("transid");
	if (!last_transid.empty()
		&& watoi64(last_transid) != migrate_to_cf->kv_store.get_basetransid())
	{
		Server->Log("Last transid " + last_transid + " does not equal current base transid " + convert(migrate_to_cf->kv_store.get_basetransid()) + ". Not continuing migration", LL_ERROR);
		migration_has_error = true;
	}

	size_t num_threads = os_get_num_cpus();

	migration_copy_done_lag = (num_threads + 2)*big_block_size;

	migration_thread_pool.reset(Server->createThreadPool(num_threads, 2, "idle migration thread"));

	IPipe* migration_pipe = Server->createMemoryPipe();

	for (size_t i = 0; i < num_threads; ++i)
	{
		migration_thread_pool->execute(new MigrationThread(migration_pipe, this, migration_has_error), "migration");
	}

	std::string settings_data;
	std::vector<std::string> settings_keys = settings->getKeys();

	for (std::string& key : settings_keys)
	{
		if (key != "transid")
		{
			settings_data += key + "=" + settings->getValue(key) + "\n";
		}
	}

	settings_data += "transid=" + convert(migrate_to_cf->kv_store.get_transid()) + "\n";

	IFsFile* settings_f = kv_store.get("clouddrive_migration_settings", TransactionalKvStore::BitmapInfo::Unknown,
		TransactionalKvStore::Flag::disable_fd_cache | TransactionalKvStore::Flag::disable_throttling, -1);

	if (settings_f != nullptr)
	{
		if (settings_f->Write(0, settings_data) != settings_data.size())
		{
			migration_has_error = true;
		}

		if (!settings_f->Resize(settings_data.size(), false))
		{
			migration_has_error = true;
		}

		kv_store.release("clouddrive_migration_settings");
	}
	else
	{
		migration_has_error = true;
	}

	unlock_extent(lock, 0, orig_cloudfile_size, true);

	if (migration_has_error)
	{
		migration_pipe->Write(std::string());
		return false;
	}

	int64 pos = settings->getValue("done", 0LL);
	
	migration_copy_done = pos;

	for (; pos < cloudfile_size; pos += big_block_size)
	{
		CWData data;
		data.addVarInt(pos);
		data.addVarInt(big_block_size);

		migration_copy_max = pos + big_block_size;

		migration_pipe->Write(data.getDataPtr(), data.getDataSize());

		if (migration_has_error)
		{
			break;
		}

		while (migration_pipe->getNumElements() > 100)
		{
			Server->wait(100);
		}
	}

	migration_pipe->Write(std::string());

	return !migration_has_error;
#else //WITH_MIGRATION
	return false;
#endif //WITH_MIGRATION
}

bool CloudFile::update_migration_settings()
{
	IFsFile* settings_f = kv_store.get("clouddrive_migration_settings", TransactionalKvStore::BitmapInfo::Unknown,
		TransactionalKvStore::Flag::disable_fd_cache | TransactionalKvStore::Flag::disable_throttling, -1);

	if (settings_f != nullptr)
	{
		std::string curr_settings = settings_f->Read(static_cast<_u32>(settings_f->Size()));

		std::unique_ptr<ISettingsReader> settings(Server->createMemorySettingsReader(curr_settings));

		std::string settings_data;
		std::vector<std::string> settings_keys = settings->getKeys();

		for (std::string& key : settings_keys)
		{
			if (key != "transid"
				&& key!="done")
			{
				settings_data += key + "=" + settings->getValue(key) + "\n";
			}
		}

		settings_data += "transid=" + convert(migrate_to_cf->kv_store.get_transid()) + "\n";
		settings_data += "done=" + convert(migration_copy_done) + "\n";

		if (settings_f->Write(0, settings_data) != settings_data.size())
		{
			kv_store.release("clouddrive_migration_settings");
			return false;
		}

		if (!settings_f->Resize(settings_data.size(), false))
		{
			kv_store.release("clouddrive_migration_settings");
			return false;
		}

		kv_store.release("clouddrive_migration_settings");
	}
	else
	{
		return false;
	}

	return true;
}

IOnlineKvStore * CloudFile::create_migration_endpoint(const std::string& conf_fn, ISettingsReader* settings,
	IBackupFileSystem* migrate_cachefs)
{
#ifdef WITH_MIGRATION
	std::string endpoint;
	std::string key;
	if (!settings->getValue("endpoint", &endpoint)
		|| !settings->getValue("key", &key))
	{
		Server->Log("Migration setting missing", LL_ERROR);
		return nullptr;
	}

	std::string aes_key = pbkdf2_sha256(key);

	if (endpoint == "urbackup")
	{
		std::string auth_token;
		std::string api;
		if (!settings->getValue("auth_token", &auth_token)
			|| !settings->getValue("api", &api))
		{
			Server->Log("Migration setting missing (1)", LL_ERROR);
			return nullptr;
		}

		return new OnlineKvStore(api, auth_token, aes_key,
			cache_path + "/migration/generation", new TokenRefreshCallback(conf_fn),
			get_compress_encrypt_factory(),
			CompressionMethodFromString(settings->getValue("compression_method", "zstd_9")),
			migrate_cachefs);
	}
	else if (endpoint == "s3")
	{
		std::string access_key;
		std::string secret_access_key;

		if (!settings->getValue("access_key", &access_key) ||
			!settings->getValue("secret_access_key", &secret_access_key))
		{
			Server->Log("access_key and secret_access_key not set correctly", LL_ERROR);
			return nullptr;
		}

		std::string bucket_name;
		if (!settings->getValue("bucket_name", &bucket_name))
		{
			Server->Log("S3 bucket name not set correctly", LL_ERROR);
			return nullptr;
		}
		std::string s3_endpoint;
		settings->getValue("s3_endpoint", &s3_endpoint);
		std::string s3_region;
		settings->getValue("s3_region", &s3_region);
		std::string storage_class;
		settings->getValue("storage_class", &storage_class);

#ifndef _WIN32
		IKvStoreBackend* backend = new KvStoreBackendS3(aes_key, access_key,
			secret_access_key, bucket_name, get_compress_encrypt_factory(), s3_endpoint, 
			s3_region, storage_class,
			CompressionMethodFromString(settings->getValue("compression_method", "zstd_9")),
			CompressionMethodFromString(settings->getValue("metadata_compression_method", "zstd_9"))
			migrate_cachefs);

		return new KvStoreFrontend(cache_path + "/migration/objects.db",
				backend, true, std::string(), std::string(), nullptr, std::string(), false,
				false, migrate_cachefs);
#else
		return nullptr;
#endif
	}
	else if (endpoint == "azure")
	{
		std::string account_name;
		std::string account_key;

		if (!settings->getValue("account_name", &account_name) ||
			!settings->getValue("account_key", &account_key))
		{
			Server->Log("account_name and account_key not set correctly", LL_ERROR);
			return nullptr;
		}

		std::string container_name;
		if (!settings->getValue("container_name", &container_name))
		{
			Server->Log("Azure container name not set correctly", LL_ERROR);
			return nullptr;
		}

#ifndef _WIN32
		IKvStoreBackend* backend = new KvStoreBackendAzure(aes_key, account_name,
			account_key, container_name, get_compress_encrypt_factory(),
			CompressionMethodFromString(settings->getValue("compression_method", "zstd_9")),
			CompressionMethodFromString(settings->getValue("metadata_compression_method", "zstd_9"))
			migrate_cachefs);

		return new KvStoreFrontend(cache_path + "/migration/objects.db",
			backend, true, std::string(), std::string(), nullptr, std::string(), false,
			false, migrate_cachefs);
#else
		return nullptr;
#endif
	}

	Server->Log("Endpoint " + endpoint + " not implemented", LL_ERROR);
#endif //WITH_MIGRATION
	return nullptr;
}

bool CloudFile::is_metadata(int64 offset, const std::string& key)
{
	assert(fuse_io_context::is_sync_thread());

	if (offset <= 5*1024*1024)
	{
		return true;
	}

	{
		IScopedReadLock lock(chunks_mutex.get());

		if (mount_path.empty())
			return false;	

		int64 timems = Server->getTimeMS();

		if (timems - last_fs_chunks_update>60*60*1000
			&& !updating_fs_chunks)
		{
			lock.relock(nullptr);
			{
				IScopedWriteLock lock(nullptr);
				update_fs_chunks(true, lock);
			}
			lock.relock(chunks_mutex.get());
		}

		auto it = std::upper_bound(fs_chunks.begin(), fs_chunks.end(), IBackupFileSystem::SChunk(offset, 0, false));

		if (it != fs_chunks.begin())
			--it;

		if (it != fs_chunks.end()
			&& it->offset <= offset && it->offset + it->len >= offset)
			return it->metadata;

		if (timems - last_fs_chunks_update < 1000)
		{
			lock.relock(nullptr);

			IScopedWriteLock wlock(chunks_mutex.get());
			if (!key.empty())
			{
				missing_chunk_keys.push_back(std::make_pair(key, timems));
				update_missing_chunks_thread->notify();
			}
			return false;
		}

		while (updating_fs_chunks
			&& Server->getTimeMS() - timems < 60000)
		{
			lock.relock(nullptr);
			Server->wait(100);
			lock.relock(chunks_mutex.get());
		}

		if (updating_fs_chunks)
		{
			Server->Log("Cannot find chunk for offset " + convert(offset) + " in fs chunk data. Fs chunk data currently updating.", LL_INFO);
			return false;
		}
	}
	
	IScopedWriteLock lock(nullptr);

	update_fs_chunks(false, lock);

	auto it = std::upper_bound(fs_chunks.begin(), fs_chunks.end(), IBackupFileSystem::SChunk(offset, 0, false));

	if (it != fs_chunks.begin())
		--it;

	if (it != fs_chunks.end()
		&& it->offset <= offset && it->offset + it->len >= offset)
		return it->metadata;
	
	last_fs_chunks_update = Server->getTimeMS();

	if (!key.empty())
	{
		assert(lock.getLock() != nullptr);
		missing_chunk_keys.push_back(std::make_pair(key, last_fs_chunks_update));
		update_missing_chunks_thread->notify();
	}
	Server->Log("Cannot find chunk for offset " + convert(offset) + " in fs chunk data. Fs chunk data size " + convert(fs_chunks.size()), LL_INFO);
	return false;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> CloudFile::is_metadata_async(fuse_io_context& io, int64 offset, const std::string& key)
{
	if (offset <= 5 * 1024 * 1024)
	{
		co_return true;
	}

	{
		IScopedReadLock lock(chunks_mutex.get());

		if (mount_path.empty())
			co_return false;

		int64 timems = Server->getTimeMS();

		if (timems - last_fs_chunks_update > 60 * 60 * 1000
			&& !updating_fs_chunks)
		{
			lock.relock(NULL);
			{
				co_await update_fs_chunks_async(io, true);
			}
			lock.relock(chunks_mutex.get());
		}

		auto it = std::upper_bound(fs_chunks.begin(), fs_chunks.end(), btrfs_chunks::SChunk(offset, 0, false));

		if (it != fs_chunks.begin())
			--it;

		if (it != fs_chunks.end()
			&& it->offset <= offset && it->offset + it->len >= offset)
			co_return it->metadata;

		if (timems - last_fs_chunks_update < 1000)
		{
			lock.relock(nullptr);

			IScopedWriteLock wlock(chunks_mutex.get());
			if (!key.empty())
			{
				missing_chunk_keys.push_back(std::make_pair(key, timems));
				update_missing_chunks_thread->notify();
			}
			co_return false;
		}

		while (updating_fs_chunks
			&& Server->getTimeMS() - timems < 60000)
		{
			lock.relock(nullptr);

			co_await io.sleep_ms(100);

			lock.relock(chunks_mutex.get());
		}

		if (updating_fs_chunks)
		{
			Server->Log("Cannot find chunk for offset " + convert(offset) + " in fs chunk data. Fs chunk data currently updating.", LL_INFO);
			co_return false;
		}
	}

	co_await update_fs_chunks_async(io, false);

	IScopedReadLock lock(chunks_mutex.get());

	auto it = std::upper_bound(fs_chunks.begin(), fs_chunks.end(), btrfs_chunks::SChunk(offset, 0, false));

	if (it != fs_chunks.begin())
		--it;

	if (it != fs_chunks.end()
		&& it->offset <= offset && it->offset + it->len >= offset)
		co_return it->metadata;

	last_fs_chunks_update = Server->getTimeMS();

	if (!key.empty())
	{
		assert(lock.getLock() != nullptr);
		missing_chunk_keys.push_back(std::make_pair(key, last_fs_chunks_update));
		update_missing_chunks_thread->notify();
	}
	Server->Log("Cannot find chunk for offset " + convert(offset) + " in fs chunk data. Fs chunk data size " + convert(fs_chunks.size()), LL_INFO);

	co_return false;
}
#endif //HAS_ASYNC

const unsigned int def_metadata_second_chances = 10;

unsigned int CloudFile::get_num_second_chances(const std::string & key)
{
	int64 offset = key_offset(key);

	if (offset < 0)
		return 0;

	if (is_metadata(offset, key))
	{
		return def_metadata_second_chances;
	}

	return 0;
}

bool CloudFile::is_metadata(const std::string & key)
{
	int64 offset = key_offset(key);

	if (offset < 0)
		return false;

	return is_metadata(offset, key);
}

bool CloudFile::update_missing_fs_chunks()
{
	IScopedWriteLock lock(nullptr);
	update_fs_chunks(false, lock);

	Server->Log("Retrying update missing chunks. Missing: " + convert(missing_chunk_keys.size()));

	int64 timems = Server->getTimeMS();
	bool first = true;

	std::vector<std::string> new_metadata_keys;
	assert(lock.getLock() != nullptr);
	std::vector<std::pair<std::string, int64> > new_missing_chunk_keys;
	for (std::pair<std::string, int64> key : missing_chunk_keys)
	{
		int64 offset = key_offset(key.first);

		if (offset < 0)
			continue;

		if (first)
		{
			Server->Log("First missing chunk offset: "+convert(offset));
			first = false;
		}

		auto it = std::upper_bound(fs_chunks.begin(), fs_chunks.end(), IBackupFileSystem::SChunk(offset, 0, false));

		if (it != fs_chunks.begin())
			--it;

		if (it != fs_chunks.end()
			&& it->offset <= offset && it->offset + it->len >= offset)
		{
			Server->Log("Chunk for offset " + convert(offset) + " found now.", LL_INFO);

			if (it->metadata)
			{
				new_metadata_keys.push_back(key.first);
			}
		}
		else if(timems - key.second<60*1000)
		{
			new_missing_chunk_keys.push_back(key);
		}
		else
		{
			Server->Log("Giving up on finding chunk for offset " + convert(offset) + ".", LL_INFO);
		}
	}

	missing_chunk_keys = new_missing_chunk_keys;

	lock.relock(nullptr);

	for (std::string& key : new_metadata_keys)
	{
		kv_store.set_second_chances(key, def_metadata_second_chances);
	}

	return new_missing_chunk_keys.empty();
}

bool CloudFile::migrate(std::vector<char>& buf, int64 offset, int64 len)
{
	IScopedLock lock(mutex.get());

	lock_extent(lock, offset, len, true);

	IScopedLock migrate_lock(migrate_to_cf->mutex.get());
	migrate_to_cf->lock_extent(migrate_lock, offset, len, true);

	bool ret = true;

	for (int64 curr_offset = offset; curr_offset < offset + len;)
	{
		bool has_block = bitmap->get(curr_offset / block_size);

		if (!has_block)
		{
			int64 tozero = (std::min)((block_size - curr_offset%block_size), offset + len - curr_offset);
			curr_offset += tozero;
			continue;
		}

		int64 toread = (std::min)(static_cast<int64>(buf.size()), offset + len - curr_offset);

		if (ReadInt(curr_offset, buf.data(), static_cast<_u32>(toread), &lock, 0, nullptr) != toread)
		{
			ret = false;
			break;
		}

		if (migrate_to_cf->WriteInt(curr_offset, buf.data(), static_cast<_u32>(toread), false, &migrate_lock, true, nullptr) != toread)
		{
			ret = false;
			break;
		}

		curr_offset += toread;
	}

	migrate_to_cf->unlock_extent(migrate_lock, offset, len, true);

	unlock_extent(lock, offset, len, true);

	if (ret
		&& offset+len- migration_copy_done_lag > migration_copy_done)
	{
		migration_copy_done = offset + len;
	}

	return ret;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::WriteAsync(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io,
	int64 pos, _u32 bsize, bool new_block, bool ext_lock, bool can_block)
{
	if (!fuse_io.write_fuse)
	{
		Server->Log("Write not from fuse");
	}

	fuse_in_header* fheader = reinterpret_cast<fuse_in_header*>(fuse_io.header_buf);

	if (pos > cloudfile_size)
	{
		Server->Log("Write position " + convert(pos) + " bigger than cloud drive file " + convert(cloudfile_size), LL_ERROR);
		co_return 0;
	}

	EXTENSIVE_DEBUG_LOG(int64 starttime = Server->getTimeMS();)

	int64 target = pos + bsize;

	if (target > cloudfile_size)
	{
		target = cloudfile_size;
		Server->Log("Write beyond target size. Reducing write size to " + convert(target - pos) + " from " + convert(bsize), LL_WARNING);
		bsize = static_cast<_u32>(target - pos);
	}

	int64 orig_pos = pos;
	size_t lock_idx;
	if (!ext_lock)
	{
		lock_idx = co_await lock_extent_async(orig_pos, bsize, false);
	}

	EXTENSIVE_DEBUG_LOG(Server->Log("Writing " + convert(bsize) + " bytes at pos " + convert(pos), LL_DEBUG);)

	size_t set_bits = 0;

	struct WriteTask
	{
		WriteTask(IFsFile* fd, std::string key, uint64_t off, size_t size)
			: fd(fd), key(std::move(key)), off(off), size(size) {}
		IFsFile* fd;
		std::string key;
		int64 off;
		_u32 size;
	};

	std::vector<WriteTask> write_tasks;

	while (pos < target)
	{
		int64 big_block_num = pos / big_block_size;
		bool has_big_block = co_await big_blocks_bitmap->get_async(io, big_block_num);

		int64 block_diff = big_block_num - active_big_block;
		if (has_big_block && active_big_block >= 0 && (block_diff < -1 || block_diff>1)
			&& old_big_blocks_bitmap->get(big_block_num))
		{
			add_fracture_big_block(big_block_num);
		}

		int64 curr_block_size;
		std::string key;
		bool has_block;
		if (has_big_block)
		{
			active_big_block = big_block_num;
			curr_block_size = big_block_size;
			key = big_block_key(pos / curr_block_size);
			has_block = co_await bitmap_has_big_block_async(io, pos / curr_block_size);

			new_big_blocks_bitmap->set(big_block_num, true);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(pos / curr_block_size);
			has_block = co_await bitmap_has_small_block_async(io, pos / curr_block_size);
		}

		int64 towrite = (std::min)((curr_block_size - pos % curr_block_size), target - pos);

		if (!new_block && !has_block)
		{
			bool waited = false;
			while (in_write_retrieval.find(key) != in_write_retrieval.end())
			{
				co_await WriteRetrievalAwaiter(*this);
				waited = true;
			}

			if (waited)
			{
				//Update bitmap info (has_block)
				continue;
			}

			in_write_retrieval.insert(key);
		}

		unsigned int flags = TransactionalKvStore::Flag::read_random;

		if (!can_block)
		{
			flags |= TransactionalKvStore::Flag::disable_throttling;
		}

		if (co_await is_metadata_async(io, pos, key))
		{
			flags |= TransactionalKvStore::Flag::disable_memfiles;
		}

		IFsFile* block = co_await kv_store.get_async(io, key,
			new_block ? TransactionalKvStore::BitmapInfo::NotPresent
			: (has_block ? TransactionalKvStore::BitmapInfo::Present
				: TransactionalKvStore::BitmapInfo::NotPresent),
			flags, curr_block_size);

		assert(block != NULL);

		if (fallocate_block_file)
		{
			int64 file_block_size = block->Size();
			if (file_block_size < pos % curr_block_size)
			{
				io_uring_sqe* sqe = io.get_sqe();

				io_uring_prep_fallocate(sqe, get_file_fd(block->getOsHandle()),
					0, 0, curr_block_size);

				int rc = co_await io.complete(sqe);

				if (rc == 0)
					block->increaseCachedSize(curr_block_size);
			}
		}

		int64 block_pos = pos % curr_block_size;
		write_tasks.push_back(WriteTask(block,
			key, block_pos, towrite));

		set_bits += co_await bitmap->set_range_async(io, pos / block_size, div_up(pos + towrite, block_size), true);

		if (!new_block && !has_block)
		{
			in_write_retrieval.erase(key);
			resume_write_retrieval_awaiters();
		}

		pos += towrite;
	}

	fuse_out_header* out_header = reinterpret_cast<fuse_out_header*>(fuse_io.scratch_buf);
    out_header->error = 0;
    out_header->len = sizeof(fuse_out_header) + sizeof(fuse_write_out);
    out_header->unique = fheader->unique;

    fuse_write_out* write_out = reinterpret_cast<fuse_write_out*>(fuse_io.scratch_buf + sizeof(fuse_out_header));
    write_out->size = bsize;
    write_out->padding = 0;

	std::vector<WriteTask> done_write_tasks;

	size_t ret = 0;
	size_t sqe_written = 0;
	size_t tries = 10;
	while (tries > 0)
	{
		size_t n_peek = write_tasks.size();

		if (fuse_io.write_fuse)
			n_peek += 2;

		std::vector<io_uring_sqe*> sqes;
		sqes.reserve(n_peek);
		for (WriteTask& task : write_tasks)
		{
			io_uring_sqe* write_sqe = io.get_sqe(n_peek);
			if (write_sqe == nullptr)
				co_return -1;
			--n_peek;

			io_uring_prep_splice(write_sqe, fuse_io.pipe[0],
				-1, get_file_fd(task.fd->getOsHandle()), task.off, task.size,
				SPLICE_F_MOVE | SPLICE_F_FD_IN_FIXED | SPLICE_F_NONBLOCK);
			write_sqe->flags |= IOSQE_IO_LINK;

			sqes.push_back(write_sqe);
		}

		int expected_last_rc =-1;

		if (fuse_io.write_fuse)
		{
			io_uring_sqe* sqe_write_response = io.get_sqe(n_peek);
			if (sqe_write_response == nullptr)
				co_return -1;
			--n_peek;

			io_uring_prep_write_fixed(sqe_write_response, fuse_io.pipe[1],
				fuse_io.scratch_buf, out_header->len,
				0, fuse_io.scratch_buf_idx);
			sqe_write_response->flags |= IOSQE_FIXED_FILE | IOSQE_IO_LINK;

			sqes.push_back(sqe_write_response);

			assert(n_peek == 1);
			io_uring_sqe* sqe_splice_response = io.get_sqe();
			if (sqe_splice_response == nullptr)
				co_return -1;

			io_uring_prep_splice(sqe_splice_response, fuse_io.pipe[0],
				-1, io.fuse_ring.fd, -1, out_header->len,
				SPLICE_F_MOVE | SPLICE_F_FD_IN_FIXED | SPLICE_F_NONBLOCK);
			sqe_splice_response->flags |= IOSQE_FIXED_FILE;

			sqes.push_back(sqe_splice_response);

			expected_last_rc = out_header->len;
		}
		else
		{
			assert(n_peek == 0);
			sqes.back()->flags &= ~IOSQE_IO_LINK;

			expected_last_rc = write_tasks.back().size;
		}

		std::vector<int> rcs = co_await io.complete(sqes);

		int last_rc = rcs[rcs.size() - 1];

		if ( (last_rc < 0 && last_rc == -ECANCELED) ||
			(expected_last_rc>=0 && last_rc!= expected_last_rc) )
		{
			std::string rcs_str;
			for (int lrc : rcs)
			{
				if (!rcs_str.empty())
					rcs_str += ", ";
				rcs_str += convert(lrc);
			}

			//Maybe needs short write splice handling here (last_rc>0 && last_rc<expected_last_rc)
			if(last_rc==-ECANCELED)
				Server->Log("Write error (ECANCELED) rcs=" + rcs_str, LL_ERROR);
			else if(last_rc!= expected_last_rc)
				Server->Log("Write error (last_rc="+convert(last_rc)+" expected="+convert(expected_last_rc)+") rcs="+ rcs_str, LL_ERROR);

			for (size_t i = 0; i < write_tasks.size();)
			{
				WriteTask& task = write_tasks[i];
				if (rcs[i] >= 0 && rcs[i] == task.size)
				{
					sqe_written += task.size;
					task.fd->increaseCachedSize(task.off + task.size);
					done_write_tasks.push_back(task);
					write_tasks.erase(write_tasks.begin() + i);
				}
				else if (rcs[i] > 0)
				{
					sqe_written += rcs[i];
					task.fd->increaseCachedSize(task.off + rcs[i]);

					Server->Log("Short write writing to block " + bytesToHex(reinterpret_cast<const unsigned char*>(task.key.c_str()),
						task.key.size()) + " at " + task.fd->getFilename()+" off "+convert(task.off)+" size "+convert(task.size)+
						". Rc " + convert(rcs[i]) + ". Retrying...", LL_WARNING);

					co_await io.sleep_ms(1000);

					write_tasks[i].off += rcs[i];
					write_tasks[i].size -= rcs[i];
					++tries;
					++i;
				}
				else if (rcs[i] <= 0 && rcs[i] != -ECANCELED)
				{
					struct stat stbuf;
					int rc = fstat(get_file_fd(task.fd->getOsHandle()), &stbuf);

					Server->Log("Error writing to block " + bytesToHex(reinterpret_cast<const unsigned char*>(task.key.c_str()),
						task.key.size()) + " at " + task.fd->getFilename() + " off " + convert(task.off) + " size " + convert(task.size) +
						". Rc " + convert(rcs[i]) + " file_size="+convert(rc!=0 ? (int64)-1 : (int64)stbuf.st_size)+" cached_file_size="+convert(task.fd->Size())+
						". Errno " + convert(rcs[i]) + "." + (tries>0 ? " Retrying..." : ""),
						tries>0 ? LL_WARNING:LL_ERROR);

					if (tries == 0)
					{
						addSystemEvent("cache_err_retry",
							"Error writing to block on cache",
							"Error writing to block " + bytesToHex(reinterpret_cast<const unsigned char*>(task.key.c_str()),
								task.key.size()) + " at " + task.fd->getFilename() + " off " + convert(task.off) + " size " + convert(task.size) +
							". Rc " + convert(rcs[i]) + ". Errno " + convert(rcs[i]), LL_ERROR);
					}
					else
					{
						co_await io.sleep_ms(1000);
					}
					++i;
				}
				else
				{
					++i;
				}
			}
			--tries;

			if (tries == 0)
			{
				if (fuse_io.write_fuse)
				{
					out_header->error = -EIO;
					out_header->len = sizeof(fuse_out_header) + sizeof(fuse_write_out);
					write_out->size = sqe_written;

					io_uring_sqe* sqe_write_response = io.get_sqe(2);
					if (sqe_write_response == nullptr)
						co_return -1;

					io_uring_prep_write_fixed(sqe_write_response, fuse_io.pipe[1],
						fuse_io.scratch_buf, out_header->len,
						0, fuse_io.scratch_buf_idx);
					sqe_write_response->flags |= IOSQE_FIXED_FILE | IOSQE_IO_LINK;

					io_uring_sqe* sqe_splice_response = io.get_sqe();
					if (sqe_splice_response == nullptr)
						co_return -1;

					io_uring_prep_splice(sqe_splice_response, fuse_io.pipe[0],
						-1, io.fuse_ring.fd, -1, out_header->len,
						SPLICE_F_MOVE | SPLICE_F_FD_IN_FIXED | SPLICE_F_NONBLOCK);
					sqe_splice_response->flags |= IOSQE_FIXED_FILE;

					auto [rc1, rc2] = co_await io.complete(std::make_pair(sqe_write_response, sqe_splice_response));

					ret = 0;
				}
				else
				{
					ret = -1;
				}
				break;
			}

			continue;
		}
		else if (last_rc != expected_last_rc)
		{
			Server->Log("Error sending response " + convert(last_rc) 
				+ " expected " + convert(expected_last_rc), LL_ERROR);
			ret = -1;
		}
		else
		{
			sqe_written = bsize;
			for (WriteTask& task : write_tasks)
			{
				task.fd->increaseCachedSize(task.off + task.size);
			}
		}

		break;
	}

	for (WriteTask& task : write_tasks)
	{
		co_await kv_store.release_async(io, task.key);
	}
	
	for (WriteTask& task : done_write_tasks)
	{
		co_await kv_store.release_async(io, task.key);
	}

	written_bytes += bsize;
	used_bytes += set_bits * block_size;

	EXTENSIVE_DEBUG_LOG(Server->Log("Done. " + convert(Server->getTimeMS() - starttime) + "ms", LL_DEBUG);)

	if (!ext_lock)
	{
		unlock_extent_async(orig_pos, bsize, false, lock_idx);
	}

	if (ret == 0)
		co_return sqe_written;

	co_return ret;
}
#endif //HAS_ASYNC

_u32 CloudFile::WriteInt( int64 pos, const char* buffer, _u32 bsize, bool new_block, IScopedLock* ext_lock, bool can_block, bool* has_error)
{
	if (is_async)
		abort();

	IScopedLock lock(nullptr);

	if(ext_lock==nullptr)
	{
		lock.relock(mutex.get());
	}

	if (pos > cloudfile_size)
	{
		Server->Log("Write position " + convert(pos) + " bigger than cloud drive file " + convert(cloudfile_size), LL_ERROR);
		return 0;
	}
	
	EXTENSIVE_DEBUG_LOG(int64 starttime=Server->getTimeMS();)

	int64 target = pos + bsize;

	if (target>cloudfile_size)
	{
		target = cloudfile_size;
		Server->Log("Write beyond target size. Reducing write size to " + convert(target - pos) + " from " + convert(bsize), LL_WARNING);
		bsize = static_cast<_u32>(target - pos);
	}
	
	const char* orig_buffer = buffer;
	int64 orig_pos = pos;
	if(ext_lock==nullptr)
	{
		lock_extent(lock, orig_pos, bsize, false);
	}

	IScopedLock migration_lock(nullptr);
	if (migrate_to_cf != nullptr)
	{
		migration_lock.relock(migrate_to_cf->mutex.get());
		migrate_to_cf->lock_extent(migration_lock, orig_pos, bsize, false);
	}

	bool slog_needs_reset = false;
	if (ext_lock == nullptr)
	{		
		if (!slog_write(pos, buffer, bsize, slog_needs_reset))
		{
			unlock_extent(lock, orig_pos, bsize, false);
			return 0;
		}
	}

	EXTENSIVE_DEBUG_LOG(Server->Log("Writing " + convert(bsize) + " bytes at pos " + convert(pos), LL_DEBUG);)

	size_t set_bits = 0;

	while(pos<target)
	{
		int64 big_block_num = pos/big_block_size;
		bool has_big_block = big_blocks_bitmap->get(big_block_num);

		int64 block_diff = big_block_num - active_big_block;
		if(has_big_block && active_big_block>=0 && ( block_diff<-1 || block_diff>1)
		    && old_big_blocks_bitmap->get(big_block_num) )
		{
			add_fracture_big_block(big_block_num);
		}

		int64 curr_block_size;
		std::string key;
		bool has_block;
		if(has_big_block)
		{
			active_big_block = big_block_num;
			curr_block_size = big_block_size;
			key = big_block_key(pos/curr_block_size);
			has_block = bitmap_has_big_block(pos/curr_block_size);

			new_big_blocks_bitmap->set(big_block_num, true);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(pos/curr_block_size);
			has_block = bitmap_has_small_block(pos/curr_block_size);
		}

		int64 towrite = (std::min)((curr_block_size - pos%curr_block_size), target-pos);

		if (!new_block && !has_block)
		{
			bool waited = false;
			while (in_write_retrieval.find(key) != in_write_retrieval.end())
			{
				if (ext_lock == nullptr)
				{
					in_write_retrieval_cond->wait(&lock);
				}
				else
				{
					in_write_retrieval_cond->wait(ext_lock);
				}
				waited = true;
			}

			if (waited)
			{
				//Update bitmap info (has_block)
				continue;
			}

			in_write_retrieval.insert(key);
		}

		if(ext_lock==nullptr)
		{
			lock.relock(nullptr);
		}
		else
		{
			ext_lock->relock(nullptr);
		}

		unsigned int flags = TransactionalKvStore::Flag::read_random;

		if (!can_block)
		{
			flags |= TransactionalKvStore::Flag::disable_throttling;
		}

		if (is_metadata(pos, key))
		{
			flags |= TransactionalKvStore::Flag::disable_memfiles;
		}

		IFsFile* block = kv_store.get(key, 
			new_block ? TransactionalKvStore::BitmapInfo::NotPresent 
						: (has_block ? TransactionalKvStore::BitmapInfo::Present
										: TransactionalKvStore::BitmapInfo::NotPresent) ,
			flags, curr_block_size);
		
		assert(block!=nullptr);

		int64 file_block_size = block->Size();
		if(file_block_size <pos%curr_block_size)
		{
			block->Resize(curr_block_size);
		}

		_u32 written = 0;
		size_t tries = 0;
		while(written<static_cast<_u32>(towrite))
		{
			int64 block_pos = pos%curr_block_size+written;
			_u32 curr_towrite = static_cast<_u32>(towrite)-written;
			
			
			/* Enable for Direct-IO TODO: buffer needs to be aligned
			if(block_pos%io_alignment==0
				&& curr_towrite%io_alignment==0)
			{
				written += block->Write(block_pos, buffer+written, curr_towrite);
			}
			else
			{
				written += WriteAligned(block, block_pos, buffer+written, curr_towrite);
			}*/
			written += block->Write(block_pos, buffer + written, curr_towrite);
			
			if(written<static_cast<_u32>(towrite))
			{
				std::string syserr = os_last_error_str();
				if (tries==4)
				{
					addSystemEvent("cache_err_retry",
						"Error writing to block on cache",
						"Error writing to block " + bytesToHex(reinterpret_cast<const unsigned char*>(key.c_str()),
							key.size()) + " at "+ block->getFilename()+". " + syserr, LL_ERROR);
				}
				Server->Log("Error writing to block "+bytesToHex(reinterpret_cast<const unsigned char*>(key.c_str()),
					key.size())+" at "+ block->getFilename()+". "+syserr+". Retrying...", LL_WARNING);
				Server->wait(1000);
				++tries;
			}
		}

		kv_store.release(key);

		if(written!=towrite)
		{
			if (has_error) *has_error = true;
			Server->Log("Error writing to block file", LL_ERROR);
			addSystemEvent("cache_err",
				"Error writing to block on cache",
				"Error writing to block " + bytesToHex(reinterpret_cast<const unsigned char*>(key.c_str()),
					key.size()) + " at "+cachefs->getName()+".", LL_ERROR);
			if (migrate_to_cf != nullptr)
			{
				migrate_to_cf->unlock_extent(migration_lock, orig_pos, bsize, false);
			}
			if(ext_lock==nullptr)
			{
				lock.relock(mutex.get());
				unlock_extent(lock, orig_pos, bsize, false);
			}
			else
			{
				ext_lock->relock(mutex.get());
			}

			if (!new_block && !has_block)
			{
				in_write_retrieval.erase(key);
				in_write_retrieval_cond->notify_all();
			}

			return 0;
		}

		if(ext_lock==nullptr)
		{
			lock.relock(mutex.get());
		}
		else
		{
			ext_lock->relock(mutex.get());
		}

		set_bits += bitmap->set_range(pos / block_size, div_up(pos + written, block_size), true);

		if (!new_block && !has_block)
		{
			in_write_retrieval.erase(key);
			in_write_retrieval_cond->notify_all();
		}

		buffer += written;
		pos += written;

		written_bytes += written;
	}

	used_bytes+=set_bits*block_size;
	
	EXTENSIVE_DEBUG_LOG(Server->Log("Done. " + convert(Server->getTimeMS()-starttime)+"ms", LL_DEBUG);)

	if (migrate_to_cf != nullptr)
	{
		if (orig_pos < migration_copy_max)
		{
			bool local_has_error = false;
			if (migrate_to_cf->WriteInt(orig_pos, orig_buffer, bsize, false, &migration_lock, can_block, &local_has_error) != bsize
				|| local_has_error)
			{
				migration_has_error = true;
			}
		}
		migrate_to_cf->unlock_extent(migration_lock, orig_pos, bsize, false);
	}
	
	if(ext_lock==nullptr)
	{
		unlock_extent(lock, orig_pos, bsize, false);
	}

	if (slog_needs_reset)
	{
		if (!Flush(false, true))
		{
			abort();
		}
	}

	return bsize;
}

_u32 CloudFile::WriteAligned(IFile* block, int64 pos, const char* buffer, _u32 towrite)
{
	EXTENSIVE_DEBUG_LOG(Server->Log("Fixing alignment for write at pos "+convert(pos)+" size "+convert(towrite), LL_DEBUG);)
	
	int64 startpos = (pos/io_alignment)*io_alignment;
	_u32 alignstartadd = static_cast<_u32>(pos-startpos);
	_u32 alignendadd = io_alignment - (towrite+alignstartadd)%io_alignment;
	_u32 alignwrite = towrite + alignstartadd + alignendadd;
	char *buf = reinterpret_cast<char*>(aligned_alloc(io_alignment, alignwrite));
	if(buf==nullptr)
	{
		return 0;
	}
	_u32 r = block->Read(startpos, buf, alignwrite);
	
	if(r<alignstartadd)
	{
		free(buf);
		return 0;
	}
	
	if(r<alignwrite)
	{
		memset(buf+alignstartadd+towrite, 0, alignwrite-(std::max)(alignstartadd+towrite, r));
	}
	
	memcpy(buf+alignstartadd, buffer, towrite);
	
	_u32 w = block->Write(startpos, buf, alignwrite);
	
	free(buf);
	
	if(w<alignstartadd)
	{
		return 0;
	}	
	w-=alignstartadd;
	if(w>towrite)
	{
		w=towrite;
	}
	
	return w;
}

bool CloudFile::Seek( _i64 spos )
{
	if (is_async)
		abort();

	cf_pos = spos;
	return true;
}

_i64 CloudFile::Size( void )
{
#ifdef HAS_ASYNC
	if (is_async)
	{
		CWData wdata;
		wdata.addChar(queue_cmd_get_size);

		CRData rdata;
		get_async_msg(wdata, rdata);

		int64 ret;
		bool b = rdata.getVarInt(&ret);
		assert(b);
		return ret;
	}
#endif

	IScopedLock lock(mutex.get());

	lock_extent(lock, 0, 0, false);

	int64 ret = cloudfile_size;

	unlock_extent(lock, 0, 0, false);

	return ret;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<_i64> CloudFile::SizeAsync(void)
{
	size_t lock_idx = co_await lock_extent_async(0, 0, false);

	int64 ret = cloudfile_size;

	unlock_extent_async(0, 0, false, lock_idx);

	co_return ret;
}
#endif

_i64 CloudFile::RealSize(void)
{
	return getUsedBytes();
}

bool CloudFile::PunchHole( _i64 spos, _i64 size )
{
	if (is_async)
		abort();

	IScopedLock lock(mutex.get());

	EXTENSIVE_DEBUG_LOG(int64 starttime=Server->getTimeMS();)

	if (spos > cloudfile_size)
	{
		Server->Log("Punch start position " + convert(spos) + " bigger than cloud drive file " + convert(cloudfile_size), LL_ERROR);
		return false;
	}

	int64 target = spos + size;
	if (target>cloudfile_size)
	{
		target = cloudfile_size;
		size = target - spos;
	}

	int64 orig_spos = spos;
	lock_extent(lock, orig_spos, size, true);

	EXTENSIVE_DEBUG_LOG(Server->Log("Punching hole " + convert(size) + " bytes at pos " + convert(spos), LL_DEBUG);)

	std::vector<std::pair<int64, bool> > del_blocks;

	int64 lock_start = cloudfile_size;
	int64 lock_end = 0;
	while(spos<target)
	{
		int64 big_block_num = spos/big_block_size;
		bool has_big_block = big_blocks_bitmap->get(big_block_num);

		int64 curr_block_size;
		std::string key;
		bool has_block;
		if(has_big_block)
		{
			curr_block_size = big_block_size;
			key = big_block_key(spos/curr_block_size);
			has_block = bitmap_has_big_block(spos/curr_block_size);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(spos/curr_block_size);
			has_block = bitmap_has_small_block(spos/curr_block_size);
		}

		int64 towrite = (std::min)((curr_block_size - spos%curr_block_size), target-spos);

		if(has_block)
		{
			size_t set_bits = bitmap->set_range(div_up(spos, block_size), (spos+towrite)/block_size, false);

			used_bytes-=set_bits*block_size;

			if(has_big_block)
			{
				has_block = bitmap_has_big_block(spos/curr_block_size);
			}
			else
			{
				has_block = bitmap_has_small_block(spos/curr_block_size);
			}

			if(!has_block)
			{
				lock_start = (std::min)(lock_start, roundDown(spos, curr_block_size));
				lock_end = (std::max)(lock_end, spos%curr_block_size==0 ? spos+curr_block_size : roundUp(spos, curr_block_size));
				del_blocks.push_back(std::make_pair(spos / curr_block_size, has_big_block));
			}
		}

		spos+=towrite;
	}

	unlock_extent(lock, orig_spos, size, true);


	if (!del_blocks.empty())
	{
		//We need to lock the whole big/small block before deleting it
		assert(lock_end > lock_start);
		lock_extent(lock, lock_start, lock_end - lock_start, true);
		for (auto& it : del_blocks)
		{
			bool has_big_block = it.second;

			//If small/big block change it got fractured during unlock and has data
			if (has_big_block && !big_blocks_bitmap->get(it.first))
			{
				Server->Log("Block " + convert(it.first) + " changed to small block while trying to delete it");
				continue;
			}

			std::string key;
			bool has_block;
			int64 big_block_num;
			if (has_big_block)
			{
				key = big_block_key(it.first);
				has_block = bitmap_has_big_block(it.first);
				big_block_num = it.first;

				assert(lock_start <= it.first*big_block_size);
				assert(lock_end >= (it.first + 1)*big_block_size);
			}
			else
			{
				key = small_block_key(it.first);
				has_block = bitmap_has_small_block(it.first);
				big_block_num = (it.first*small_block_size) / big_block_size;

				assert(lock_start <= it.first*small_block_size);
				assert(lock_end >= (it.first + 1)*small_block_size);
			}

			if (!has_block)
			{
				{
					lock.relock(nullptr);

					kv_store.del(key);

					lock.relock(mutex.get());
				}
				if (!has_big_block &&
					!big_blocks_bitmap->get(big_block_num) )
				{
					consider_big_blocks.insert(big_block_num);
				}
			}
			else
			{
				Server->Log("Block " + convert(it.first) + " present when trying to delete it big_block=" + convert(has_big_block));
			}
		}
		for (int64 it : consider_big_blocks)
		{
			if (!bitmap_has_big_block(it))
			{
				old_big_blocks_bitmap->set(it, false);
				big_blocks_bitmap->set(it, true);
				new_big_blocks_bitmap->set(it, false);
			}
		}
		consider_big_blocks.clear();
		unlock_extent(lock, lock_start, lock_end - lock_start, true);
	}

	EXTENSIVE_DEBUG_LOG(Server->Log("Done. " + convert(Server->getTimeMS()-starttime)+"ms", LL_DEBUG);)

	return true;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::PunchHoleAsync(fuse_io_context& io, _i64 spos, _i64 size)
{
	EXTENSIVE_DEBUG_LOG(int64 starttime = Server->getTimeMS();)

	if (spos > cloudfile_size)
	{
		Server->Log("Punch start position " + convert(spos) + " bigger than cloud drive file " + convert(cloudfile_size), LL_ERROR);
		co_return 1;
	}

	int64 target = spos + size;
	if (target > cloudfile_size)
	{
		target = cloudfile_size;
		size = target - spos;
	}

	int64 orig_spos = spos;
	size_t lock_idx = co_await lock_extent_async(orig_spos, size, true);

	EXTENSIVE_DEBUG_LOG(Server->Log("Punching hole " + convert(size) + " bytes at pos " + convert(spos), LL_DEBUG);)

	std::vector<std::pair<int64, bool> > del_blocks;

	int64 lock_start = cloudfile_size;
	int64 lock_end = 0;
	while (spos < target)
	{
		int64 big_block_num = spos / big_block_size;
		bool has_big_block = co_await big_blocks_bitmap->get_async(io, big_block_num);

		int64 curr_block_size;
		std::string key;
		bool has_block;
		if (has_big_block)
		{
			curr_block_size = big_block_size;
			key = big_block_key(spos / curr_block_size);
			has_block = co_await bitmap_has_big_block_async(io, spos / curr_block_size);
		}
		else
		{
			curr_block_size = small_block_size;
			key = small_block_key(spos / curr_block_size);
			has_block = co_await bitmap_has_small_block_async(io, spos / curr_block_size);
		}

		int64 towrite = (std::min)((curr_block_size - spos % curr_block_size), target - spos);

		if (has_block)
		{
			size_t set_bits = co_await bitmap->set_range_async(io, div_up(spos, block_size), (spos + towrite) / block_size, false);

			used_bytes -= set_bits * block_size;

			if (has_big_block)
			{
				has_block = co_await bitmap_has_big_block_async(io, spos / curr_block_size);
			}
			else
			{
				has_block = co_await bitmap_has_small_block_async(io, spos / curr_block_size);
			}

			if (!has_block)
			{
				lock_start = (std::min)(lock_start, roundDown(spos, curr_block_size));
				lock_end = (std::max)(lock_end, spos % curr_block_size == 0 ? spos + curr_block_size : roundUp(spos, curr_block_size));
				del_blocks.push_back(std::make_pair(spos / curr_block_size, has_big_block));
			}
		}

		spos += towrite;
	}

	unlock_extent_async(orig_spos, size, true, lock_idx);


	if (!del_blocks.empty())
	{
		std::set<int64> consider_big_blocks;
		//We need to lock the whole big/small block before deleting it
		assert(lock_end > lock_start);
		lock_idx = co_await lock_extent_async(lock_start, lock_end - lock_start, true);
		for (auto it : del_blocks)
		{
			bool has_big_block = it.second;
			int64 block = it.first;

			//If small/big block change we didn't lock enough/wrong
			if (has_big_block && !(co_await big_blocks_bitmap->get_async(io, block)))
			{
				Server->Log("Block " + convert(block) + " changed to small block while trying to delete it");
				continue;
			}

			std::string key;
			bool has_block;
			int64 big_block_num;
			if (has_big_block)
			{
				key = big_block_key(block);
				has_block = co_await bitmap_has_big_block_async(io, block);
				big_block_num = block;

				assert(lock_start <= block * big_block_size);
				assert(lock_end >= (block + 1) * big_block_size);
			}
			else
			{
				key = small_block_key(block);
				has_block = co_await bitmap_has_small_block_async(io, block);
				big_block_num = (block * small_block_size) / big_block_size;

				assert(lock_start <= block * small_block_size);
				assert(lock_end >= (block + 1) * small_block_size);
			}

			if (!has_block)
			{
				{
					co_await kv_store.del_async(io, key);
				}

				if (!has_big_block &&
					!(co_await big_blocks_bitmap->get_async(io, big_block_num)) )
				{
					if (curr_consider_big_blocks.find(big_block_num)
						== curr_consider_big_blocks.end())
					{
						consider_big_blocks.insert(big_block_num);
					}
				}
			}
			else
			{
				Server->Log("Block " + convert(block) + " present when trying to delete it big_block=" + convert(has_big_block));
			}
		}

		if (!consider_big_blocks.empty())
		{
			curr_consider_big_blocks = std::move(consider_big_blocks);
			consider_big_blocks = std::set<int64>();
			for (int64 it : curr_consider_big_blocks)
			{
				if (!(co_await bitmap_has_big_block_async(io, it)))
				{
					old_big_blocks_bitmap->set(it, false);
					co_await big_blocks_bitmap->set_async(io, it, true);
					new_big_blocks_bitmap->set(it, false);
				}
			}
			curr_consider_big_blocks.clear();
		}

		unlock_extent_async(lock_start, lock_end - lock_start, true, lock_idx);
	}

	EXTENSIVE_DEBUG_LOG(Server->Log("Done. " + convert(Server->getTimeMS() - starttime) + "ms", LL_DEBUG);)

	co_return 0;
}
#endif //HAS_ASYNC

std::string CloudFile::getFilename( void )
{
	return "CloudFile";
}

std::string CloudFile::block_key(int64 blocknum)
{
	std::string key;
	if(blocknum<UCHAR_MAX)
	{
		key.resize(sizeof(unsigned char)+1);
		unsigned char bnum = static_cast<unsigned char>(blocknum);
		memcpy(&key[1], &bnum, sizeof(bnum));
	}
	else if(blocknum<USHRT_MAX)
	{
		key.resize(sizeof(unsigned short)+1);
		unsigned short bnum = static_cast<unsigned short>(blocknum);
		memcpy(&key[1], &bnum, sizeof(bnum));
	}
	else if(blocknum<UINT_MAX)
	{
		key.resize(sizeof(unsigned int)+1);
		unsigned int bnum = static_cast<unsigned int>(blocknum);
		memcpy(&key[1], &bnum, sizeof(bnum));
	}
	else
	{
		key.resize(sizeof(int64)+1);	
		memcpy(&key[1], &blocknum, sizeof(blocknum));
	}
	return key;
}

int64 CloudFile::block_key_rev(const std::string & data)
{
	if (data.size() == sizeof(unsigned char) + 1)
	{
		return data[1];
	}
	else if (data.size() == sizeof(unsigned short) + 1)
	{
		unsigned short ret;
		memcpy(&ret, &data[1], sizeof(ret));
		return ret;
	}
	else if (data.size() == sizeof(unsigned int) + 1)
	{
		unsigned int ret;
		memcpy(&ret, &data[1], sizeof(ret));
		return ret;
	}
	else if (data.size() == sizeof(int64) + 1)
	{
		int64 ret;
		memcpy(&ret, &data[1], sizeof(ret));
		return ret;
	}
	else
	{
		return -1;
	}
}

std::string CloudFile::big_block_key( int64 blocknum )
{
	std::string key = block_key(blocknum);
	key[0]='b';
	return key;
}

std::string CloudFile::small_block_key( int64 blocknum )
{
	std::string key = block_key(blocknum);
	key[0]='s';
	return key;
}

std::string CloudFile::hex_big_block_key(int64 bytenum)
{
	return bytesToHex(big_block_key(bytenum/big_block_size));
}
	
std::string CloudFile::hex_small_block_key(int64 bytenum)
{
	return bytesToHex(small_block_key(bytenum/small_block_size));
}

int CloudFile::Congested()
{
	int ret = 0;
	if (kv_store.is_congested())
		ret |= 1;

	IScopedLock lock(mutex.get());
	if (is_flushing)
		ret |= 2;
	return ret;
}

#ifdef HAS_ASYNC
int CloudFile::CongestedAsync()
{
	int ret = 0;
	if (kv_store.is_congested_async())
		ret |= 1;
	if (is_flushing)
		ret |= 2;
	return ret;
}
#endif

std::string CloudFile::get_raid_groups()
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->get_raid_groups();
		}
	}
#endif

	return std::string();
}

std::string CloudFile::scrub_stats()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->scrub_stats();
	}

	return std::string();
}

void CloudFile::start_scrub(ScrubAction action)
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		frontend->start_scrub(action, std::string());
	}
}

void CloudFile::stop_scrub()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		frontend->stop_scrub();
	}
}

bool CloudFile::add_disk(const std::string & path)
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->add_location(path);
		}
	}
#endif

	return false;
}

bool CloudFile::remove_disk(const std::string & path, bool completely)
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->remove_location(path, completely);
		}
	}
#endif

	return false;
}

std::string CloudFile::disk_error_info()
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->disk_error_info();
		}
	}
#endif

	return std::string();
}

int64 CloudFile::current_total_size()
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			int64 total_space, free_space;
			if (backend->get_space_info(total_space, free_space))
			{
				return total_space;
			}
		}
	}
#endif

	return -1;
}

int64 CloudFile::current_free_space()
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			int64 total_space, free_space;
			if (backend->get_space_info(total_space, free_space))
			{
				return free_space;
			}
		}
	}
#endif

	return -1;
}

bool CloudFile::set_target_failure_probability(double t)
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->set_target_failure_probability(t);
		}
	}
#endif

	return false;
}

bool CloudFile::set_target_overhead(double t)
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != NULL)
	{
		KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
		if (backend != NULL)
		{
			return backend->set_target_overhead(t);
		}
	}
#endif

	return false;
}

std::string CloudFile::get_scrub_position()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->scrub_position();
	}
	return std::string();
}

int64 CloudFile::get_memfile_bytes()
{
	return kv_store.get_memfile_bytes();
}

int64 CloudFile::get_submitted_memfile_bytes()
{
	return kv_store.get_submitted_memfile_bytes();
}

void CloudFile::operator()()
{
	if (is_async)
		abort();

	std::vector<char> buf;
	buf.resize(small_block_size);

	IScopedLock lock(mutex.get());
	while (!exit_thread)
	{
		int64 wtime = 1000;
		int64 ctime = Server->getTimeMS();
		for (auto it = fracture_big_blogs.begin(); it != fracture_big_blogs.end();)
		{
			if (it->second <= ctime)
			{
				int64 big_block_num = it->first;
				Server->Log("Fracturing big block " + convert(big_block_num) + "...", LL_INFO);

				int64 orig_start = big_block_num*big_block_size;
				int64 stop = big_block_num*big_block_size + big_block_size;

				lock_extent(lock, orig_start, stop - orig_start, true);

				bool has_big_block = big_blocks_bitmap->get(big_block_num);
				if (has_big_block
					&& bitmap_has_big_block(big_block_num)
					&& old_big_blocks_bitmap->get(big_block_num))
				{
					bool success = true;
					for (int64 start = orig_start; start < stop; start += small_block_size)
					{
						_u32 toread = static_cast<_u32>((std::min)(small_block_size, cloudfile_size - start));

						if (!bitmap_has_small_block(start / small_block_size))
						{
							continue;
						}

						bool has_error;
						big_blocks_bitmap->set(big_block_num, true);
						if (ReadInt(start, buf.data(), toread, &lock, 0, &has_error) != toread)
						{
							Server->Log("Error reading while fracturing big block", LL_ERROR);
							success = false;
							break;
						}

						big_blocks_bitmap->set(big_block_num, false);
						if (WriteInt(start, buf.data(), toread, true, &lock, true, &has_error) != toread)
						{
							Server->Log("Error writing while fracturing big block", LL_ERROR);
							success = false;
							break;
						}

						if (toread < small_block_size)
						{
							break;
						}
					}
					
					big_blocks_bitmap->set(big_block_num, !success);
					
					if (success)
					{
						lock.relock(nullptr);

						kv_store.del(big_block_key(big_block_num));

						lock.relock(mutex.get());
					}

					unlock_extent(lock, orig_start, stop - orig_start, true);
				}
				else
				{
					unlock_extent(lock, orig_start, stop - orig_start, true);					
				}

				auto it_curr = it;
				++it;
				fracture_big_blogs.erase(it_curr);

				ctime = Server->getTimeMS();
			}
			else
			{
				wtime = (std::min)(wtime, it->second - ctime);
				++it;
			}
		}

		thread_cond->wait(&lock, static_cast<int>(wtime));
	}
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task_discard<int> CloudFile::run_async(fuse_io_context& io)
{
	run_async_queue(io);

	if (!io.has_fuse_io())
		co_return -1;

	fuse_io_context::FuseIoVal fuse_io = io.get_fuse_io();
	fuse_io->write_fuse = false;

	{
		fuse_io_context::ScratchBufInit* scratch_init = reinterpret_cast<fuse_io_context::ScratchBufInit*>(fuse_io->scratch_buf);

		Server->Log("Setting pipe fd " + convert(scratch_init->pipe_out) + " size " + PrettyPrintBytes(small_block_size));
		int rc = fcntl(scratch_init->pipe_out, F_SETPIPE_SZ, small_block_size);
		if (rc < 0)
		{
			Server->Log("Error setting pipe size to " + std::to_string(small_block_size) + ". " + os_last_error_str(), LL_ERROR);
			co_return -1;
		}
	}

	while (!exit_thread)
	{
		int64 wtime = 1000;
		int64 ctime = Server->getTimeMS();
		for (auto it = fracture_big_blogs.begin(); it != fracture_big_blogs.end();)
		{
			if (it->second <= ctime)
			{
				int64 big_block_num = it->first;
				Server->Log("Fracturing big block " + convert(big_block_num) + "...", LL_INFO);

				int64 orig_start = big_block_num * big_block_size;
				int64 stop = big_block_num * big_block_size + big_block_size;

				size_t lock_idx = co_await lock_extent_async(orig_start, stop - orig_start, true);

				bool has_big_block = co_await big_blocks_bitmap->get_async(io, big_block_num);
				if (has_big_block
					&& (co_await bitmap_has_big_block_async(io, big_block_num))
					&& old_big_blocks_bitmap->get(big_block_num))
				{
					bool success = true;
					for (int64 start = orig_start; start < stop; start += small_block_size)
					{
						_u32 toread = static_cast<_u32>((std::min)(small_block_size, cloudfile_size - start));

						if (!(co_await bitmap_has_small_block_async(io, start / small_block_size)))
						{
							continue;
						}

						co_await big_blocks_bitmap->set_async(io, big_block_num, true);
						int rc = co_await ReadAsync(io, fuse_io.get(), start, toread, 0, true);
						if (rc<0 ||  rc != toread)
						{
							Server->Log("Error reading while fracturing big block. Rc=" + convert(rc) + " Expected=" + convert(toread), LL_ERROR);
							co_await empty_pipe(io, fuse_io.get());
							success = false;
							break;
						}

						co_await big_blocks_bitmap->set_async(io, big_block_num, false);
						rc = co_await WriteAsync(io, fuse_io.get(), start, toread, true, true, true);
						if (rc<0 || rc != toread)
						{
							co_await empty_pipe(io, fuse_io.get());
							Server->Log("Error writing while fracturing big block. Rc="+convert(rc)+" Expected="+convert(toread), LL_ERROR);
							success = false;
							break;
						}

						if (toread < small_block_size)
						{
							break;
						}
					}

					big_blocks_bitmap->set(big_block_num, !success);

					if (success)
					{
						co_await kv_store.del_async(io, big_block_key(big_block_num));
					}

					unlock_extent_async(orig_start, stop - orig_start, true, lock_idx);
				}
				else
				{
					unlock_extent_async(orig_start, stop - orig_start, true, lock_idx);
				}

				auto it_curr = it;
				++it;
				fracture_big_blogs.erase(it_curr);

				ctime = Server->getTimeMS();
			}
			else
			{
				wtime = (std::min)(wtime, it->second - ctime);
				++it;
			}
		}

		co_await io.sleep_ms(wtime);
	}

	co_return 0;
}
#endif //HAS_ASYNC

void CloudFile::run_cd_fracture()
{
	fracture_big_blogs_ticket = Server->getThreadPool()->execute(this, "cd fracture");
}

bool CloudFile::start_raid_defrag(const std::string& settings)
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->start_defrag(settings);
	}

	return false;
}

bool CloudFile::is_background_worker_enabled()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->is_background_worker_enabled();
	}
	return false;
}

bool CloudFile::is_background_worker_running()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->is_background_worker_running();
	}
	return false;
}

void CloudFile::enable_background_worker(bool b)
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->enable_background_worker(b);
	}
}

bool CloudFile::start_background_worker()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->start_background_worker();
	}
	return false;
}

void CloudFile::set_background_worker_result_fn(const std::string& result_fn)
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->set_background_worker_result_fn(result_fn);
	}
}

bool CloudFile::has_background_task()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->has_background_task();
	}
	return false;
}

void CloudFile::preload(int64 start, int64 stop, size_t n_threads)
{
	if (is_async)
		abort();

	std::unique_ptr<IPipe> preload_pipe(Server->createMemoryPipe());
	std::vector<THREADPOOL_TICKET> preload_tickets;
	for (size_t i = 0; i < n_threads; ++i)
	{
		preload_tickets.push_back(Server->getThreadPool()->execute(new PreloadThread(preload_pipe.get(), this), "preload"));
	}

	{
		IScopedLock lock(mutex.get());
		while (start < stop
			&& start<cloudfile_size)
		{
			bool has_block = bitmap->get(start / block_size);

			if (!has_block)
			{
				start += block_size;
				continue;
			}

			bool in_big_block = big_blocks_bitmap->get(start / big_block_size);

			int64 curr_block_size;
			if (in_big_block)
			{
				curr_block_size = big_block_size;
			}
			else
			{
				curr_block_size = small_block_size;
			}

			int64 toread = (std::min)((curr_block_size - start%curr_block_size), stop - start);

			CWData data;
			data.addInt64(start);

			preload_pipe->Write(std::string(data.getDataPtr(), data.getDataSize()));

			while (preload_pipe->getNumElements() > 1000)
			{
				lock.relock(nullptr);
				Server->wait(1000);
				lock.relock(mutex.get());
			}

			start += toread;
		}
	}

	preload_pipe->Write(std::string());

	Server->getThreadPool()->waitFor(preload_tickets);
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task_discard<int> CloudFile::preload_async_read(fuse_io_context& io, 
	std::vector<std::unique_ptr<fuse_io_context::FuseIo> >& ios, AwaiterCoList*& ios_waiters_head, int64 start, _u32 size)
{
	std::unique_ptr<fuse_io_context::FuseIo> fuse_io = co_await IosAwaiter(ios, ios_waiters_head);

	co_await ReadAsync(io, *fuse_io.get(), start, size, 0, false);

	co_await empty_pipe(io, *fuse_io.get());

	ios.push_back(std::move(fuse_io));

	iosWaitersResume(ios, ios_waiters_head);

	co_return 0;
}
#endif //HAS_ASYNC

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task_discard<int> CloudFile::preload_items_single_async(fuse_io_context& io,
	size_t& available_workers, AwaiterCoList*& worker_waiters_head, std::string key, int64 offset, _u32 len,
	int tag, bool disable_memfiles, bool load_only)
{
	co_await WorkerAwaiter(available_workers, worker_waiters_head);

	co_await preload_key_async(io, key, offset, len, tag, disable_memfiles, load_only);

	++available_workers;

	workerWaitersResume(available_workers, worker_waiters_head);

	co_return 0;
}
#endif

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::complete_read_tasks(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io,
	const std::vector<ReadTask>& read_tasks, const size_t flush_mem_n_tasks, const int64 orig_pos,
	const _u32 bsize, const unsigned int flags)
{
	fuse_out_header* out_header = reinterpret_cast<fuse_out_header*>(fuse_io.scratch_buf);

	_u32 n_peek = static_cast<_u32>(read_tasks.size()) + flush_mem_n_tasks;

	if (fuse_io.write_fuse)
		n_peek += 2;

	std::vector<io_uring_sqe*> sqes;
	sqes.reserve(n_peek);

	if (fuse_io.write_fuse)
	{
		assert(co_await verify_pipe_empty(io, fuse_io));

		io_uring_sqe* header_sqe = io.get_sqe(n_peek);
		--n_peek;
		sqes.push_back(header_sqe);

		io_uring_prep_write_fixed(header_sqe, fuse_io.pipe[1],
			fuse_io.scratch_buf, sizeof(fuse_out_header),
			-1, fuse_io.scratch_buf_idx);
		header_sqe->flags |= IOSQE_FIXED_FILE | IOSQE_IO_LINK;
	}

	for (const ReadTask& task : read_tasks)
	{
		io_uring_sqe* sqe = io.get_sqe(n_peek);
		--n_peek;
		sqes.push_back(sqe);

		io_uring_prep_splice(sqe, task.fd,
			task.off, fuse_io.pipe[1], -1, task.size,
			SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
		sqe->flags |= IOSQE_FIXED_FILE | IOSQE_IO_LINK;

		if (flags & TransactionalKvStore::Flag::prioritize_read)
		{
			sqe->ioprio = io_prio_val;
		}
	}

	if (fuse_io.write_fuse)
	{
		io_uring_sqe* splice_sqe = io.get_sqe(n_peek);
		--n_peek;
		io_uring_prep_splice(splice_sqe, fuse_io.pipe[0],
			-1, io.fuse_ring.fd, -1, out_header->len,
			SPLICE_F_MOVE | SPLICE_F_FD_IN_FIXED | SPLICE_F_NONBLOCK);
		splice_sqe->flags |= IOSQE_FIXED_FILE;
		splice_sqe->ioprio = io_prio_val;
		sqes.push_back(splice_sqe);
	}
	else
	{
		sqes.back()->flags &= ~IOSQE_IO_LINK;
	}

	io_uring_sqe* sqe_rm_mem = nullptr;
	for (const ReadTask& task : read_tasks)
	{
		if (task.flush_mem)
		{
			if (sqe_rm_mem == nullptr)
			{
				sqes.back()->flags |= IOSQE_IO_LINK;
			}

			sqe_rm_mem = io.get_sqe(n_peek);
			--n_peek;

			io_uring_prep_fadvise(sqe_rm_mem,
				task.fd, task.off, task.size, POSIX_FADV_DONTNEED);
			sqe_rm_mem->flags |= IOSQE_IO_LINK;

			io.submit(sqe_rm_mem);
		}
	}

	if (sqe_rm_mem != nullptr)
	{
		sqe_rm_mem->flags &= ~IOSQE_IO_LINK;
	}

	assert(n_peek==0);

	std::vector<int> rcs = co_await io.complete(sqes);

	int last_rc = rcs[sqes.size() - 1];

	size_t need_fuse_write = 0;
	int ret = bsize;
	if ((last_rc == -ECANCELED &&
		(rcs[0] == sizeof(fuse_out_header) || !fuse_io.write_fuse)) ||
		(!fuse_io.write_fuse && last_rc >= 0 && last_rc < read_tasks.back().size))
	{
		Server->Log("Last rc=" + convert(last_rc) + " Retrying (short) reads... write_fuse=" + convert(fuse_io.write_fuse), LL_WARNING);
		need_fuse_write += sizeof(fuse_out_header);

		bool submit = false;
		for (size_t i = 0; i < read_tasks.size();)
		{
			const ReadTask& task = read_tasks[i];
			size_t task_rc_idx = fuse_io.write_fuse ? i + 1 : i;
			const int rc = rcs[task_rc_idx];

			const std::string src_fn = task.file->getFilename() == task.key ? ("mem:" + bytesToHex(task.key)) : task.file->getFilename();

			if (rc < 0 && rc != -ECANCELED)
			{
				addSystemEvent("cache_err",
					"Error reading from block file on cache",
					"Read error while reading from file " + src_fn + ""
					" at positition " + convert(task.off) + " block device pos " + convert(orig_pos) + " len " + convert(task.size) + " "
					+ " write_fuse=" + convert(fuse_io.write_fuse) + ". Errno "
					+ convert(rc * -1), LL_ERROR);
				ret = -1;
				break;
			}
			else if (rc > 0 && rc == task.size)
			{
				need_fuse_write += task.size;
				++i;
			}
			else if (rc >= 0 && rc < task.size)
			{
				struct stat stbuf;
				fstat(task.fd, &stbuf);

				Server->Log("Short read " + convert(i) + " (" + convert(rc) + "/" + convert(task.size) + ") while reading from file " + src_fn +
					" at positition " + convert(task.off) + " block file size " + convert(task.file->Size()) + " non cached " + convert((int64)stbuf.st_size) + ". Continuing with zeros...", LL_WARNING);

				if (task.file->Size() >= task.off + task.size)
				{
					Server->Log("Short read offset below (cached) file size (see warning)", LL_ERROR);
					abort();
				}

				submit = true;

				need_fuse_write += rc;

				size_t towrite = task.size - rc;

				while (towrite > 0)
				{
					io_uring_sqe* zero_sqe = io.get_sqe();
					io_uring_prep_splice(zero_sqe, get_file_fd(zero_file->getOsHandle()),
						0, fuse_io.pipe[1], -1, towrite,
						SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
					zero_sqe->flags |= IOSQE_FIXED_FILE;

					int zero_rc = co_await io.complete(zero_sqe);

					if (zero_rc <= 0)
					{
						Server->Log("Error reading zeroes. Rc=" + convert(zero_rc), LL_ERROR);
						ret = -1;
						break;
					}
					else
					{
						towrite -= zero_rc;
						need_fuse_write += zero_rc;
					}
				}

				if (ret == -1)
					break;

				++i;
				continue;
			}
			else if (submit)
			{
				io_uring_sqe* sqe = io.get_sqe();

				io_uring_prep_splice(sqe, task.fd,
					task.off, fuse_io.pipe[1], -1, task.size,
					SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
				sqe->flags |= IOSQE_FIXED_FILE;

				int submit_rc = co_await io.complete(sqe);

				if (submit_rc < 0)
				{
					Server->Log("Error submitting to pipe after short read rc=" + convert(submit_rc), LL_ERROR);
					ret = -1;
					break;
				}

				need_fuse_write += submit_rc;

				if (submit_rc < task.size)
				{
					Server->Log("Short read " + convert(i) + " while recovering from previous short read (" + convert(submit_rc) + "/" + convert(task.size) + ") while reading from file " + src_fn +
						" at positition " + convert(task.off) + " block file size " + convert(task.file->Size()) + ". Looping...", LL_WARNING);
					rcs[task_rc_idx] = submit_rc;
					continue;
				}

				++i;
			}
			else
			{
				assert(false);
				++i;
			}
		}
	}
	else if (last_rc < 0)
	{
		Server->Log("Read error errno=" + convert(last_rc * -1) + " write_fuse=" + convert(fuse_io.write_fuse), LL_ERROR);
		ret = -1;
	}
	else if (fuse_io.write_fuse &&
		last_rc < out_header->len)
	{
		Server->Log("Last_rc smaller than size in header (" + convert(out_header->len) + "). Last_rc=" + convert(last_rc) + " write_fuse=" + convert(fuse_io.write_fuse), LL_ERROR);
		ret = -1;
	}

	if (need_fuse_write > 0 &&
		fuse_io.write_fuse)
	{
		io_uring_sqe* splice_sqe = io.get_sqe();
		io_uring_prep_splice(splice_sqe, fuse_io.pipe[0],
			-1, io.fuse_ring.fd, -1, need_fuse_write,
			SPLICE_F_FD_IN_FIXED | SPLICE_F_NONBLOCK);
		splice_sqe->flags |= IOSQE_FIXED_FILE;

		int rc = co_await io.complete(splice_sqe);

		if (rc < 0 || rc != need_fuse_write)
		{
			Server->Log("Fuse write failed in ReadAsync error recovery. rc=" + convert(rc), LL_ERROR);
			ret = -1;
		}
	}

	co_return ret;
}
#endif //HAS_ASYNC

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task_discard<int> CloudFile::run_async_queue(fuse_io_context& io)
{
	IScopedLock lock(msg_queue_mutex.get());
	std::vector<char> buf(sizeof(int64));

	while (true)
	{
		if (msg_queue.empty())
		{
			lock.relock(nullptr);

			io_uring_sqe* sqe = io.get_sqe();
			io_uring_prep_read(sqe, queue_in_eventfd, buf.data(), buf.size(), 0);

			int rc = co_await io.complete(sqe);

			if (rc != buf.size())
			{
				Server->Log("Error reading from queue_in_eventfd. Rc=" + convert(rc), LL_ERROR);
			}

			lock.relock(msg_queue_mutex.get());
		}

		if (msg_queue.empty())
			continue;

		std::vector<char> msg = msg_queue.front();
		msg_queue.pop();

		lock.relock(nullptr);

		CRData rdata(msg.data(), msg.size());

		int64 id;
		bool b = rdata.getVarInt(&id);
		assert(b);
		char cmd;
		b = rdata.getChar(&cmd);
		assert(b);

		CWData resp;

		if (cmd == queue_cmd_resize)
		{
			int64 new_size;;
			b = rdata.getVarInt(&new_size);
			assert(b);

			b = co_await ResizeAsync(io, new_size);
			resp.addChar(b ? 1 : 0);
		}
		else if (cmd == queue_cmd_get_size)
		{
			int64 size = co_await SizeAsync();
			resp.addVarInt(size);
		}
		else if (cmd == queue_cmd_meminfo)
		{
			size_t lock_idx = co_await lock_extent_async(0, 0, false);
			
			resp.addVarInt(async_locked_extents.capacity());
			resp.addVarInt(bitmap->meminfo());
			resp.addVarInt(bitmap->get_max_cache_items());
			resp.addVarInt(big_blocks_bitmap->meminfo());
			resp.addVarInt(big_blocks_bitmap->get_max_cache_items());
			resp.addVarInt(old_big_blocks_bitmap->meminfo());
			resp.addVarInt(new_big_blocks_bitmap->meminfo());
			resp.addVarInt(fracture_big_blogs.size());
			resp.addVarInt(in_write_retrieval.size());

			unlock_extent_async(0, 0, false, lock_idx);
		}
		else
		{
			assert(false);
		}

		lock.relock(msg_queue_mutex.get());

		msg_queue_responses[id].assign(resp.getDataPtr(), resp.getDataPtr()+resp.getDataSize());

		msg_queue_cond->notify_all();
	}

	co_return 0;
}
#endif //HAS_ASYNC

#ifdef HAS_ASYNC
bool CloudFile::get_async_msg(CWData& msg, CRData& resp)
{
	CWData int_msg;

	IScopedLock lock(msg_queue_mutex.get());

	int64 id = msg_queue_id++;

	int_msg.addVarInt(id);
	int_msg.addBuffer(msg.getDataPtr(), msg.getDataSize());

	std::vector<char> data;
	data.assign(int_msg.getDataPtr(), int_msg.getDataPtr()+int_msg.getDataSize());
	
	msg_queue.push(data);

	eventfd_write(queue_in_eventfd, 1);

	std::map<size_t, std::vector<char> >::iterator it;
	while ((it=msg_queue_responses.find(id)) == msg_queue_responses.end())
	{
		msg_queue_cond->wait(&lock);
	}

	resp.set(it->second.data(), it->second.size(), true);

	msg_queue_responses.erase(it);

	return true;
}
#endif

bool CloudFile::hasKey(const std::string& key)
{
	bool big_block;
	int64 offset = key_offset(key, big_block);
	if (offset < 0)
	{
		Server->Log("Cannot get key offset for key " + key, LL_INFO);
		return true;
	}

	bool is_big_block = big_blocks_bitmap->get(offset / big_block_size);

	if (big_block != is_big_block)
	{
		Server->Log("Key " + bytesToHex(key) + " big block info differs. Bitmap is big block: "
			+ convert(is_big_block) + " key is big block: " + convert(big_block));
		return false;
	}

	int64 blocknum = big_block ? (offset / big_block_size) : (offset / small_block_size);

	bool ret;
	if (big_block)
		ret = bitmap_has_big_block(blocknum);
	else
		ret = bitmap_has_small_block(blocknum);

	if (!ret)
	{
		Server->Log("Key " + bytesToHex(key) + " not set in bitmap", LL_INFO);
	}

	return ret;
}

int64 CloudFile::get_transid()
{
	return kv_store.get_transid();
}


#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::preload_async(fuse_io_context& io, int64 start, int64 stop, size_t n_threads)
{
	const _u32 rsize = 4096;
	auto ios = std::make_unique< std::vector<std::unique_ptr<fuse_io_context::FuseIo> > >();

	for (size_t i = 0; i < n_threads; ++i)
	{
		//needs special handling for non-fixed pipe
		abort();
		std::unique_ptr<fuse_io_context::FuseIo> fuse_io = std::make_unique<fuse_io_context::FuseIo>();
		int rc = pipe2(fuse_io->pipe, O_CLOEXEC | O_NONBLOCK);
		if (rc != 0)
		{
			Server->Log("Error creating pipe for preload. " + os_last_error_str(), LL_ERROR);
			co_return -1;
		}
		rc = fcntl(fuse_io->pipe[0], F_SETPIPE_SZ, rsize);
		if (rc < 0)
		{
			Server->Log("Error setting pipe size to " + std::to_string(rsize) + " in preload_async. " + os_last_error_str(), LL_ERROR);
			co_return -1;
		}
		fuse_io->write_fuse = false;
		ios->push_back(std::move(fuse_io));
	}

	auto ios_waiters_head = std::make_unique<AwaiterCoList*>(nullptr);

	while (start < stop
		&& start < cloudfile_size)
	{
		size_t lock_idx = co_await lock_extent_async(0, 0, false);

		bool has_block = co_await bitmap->get_async(io, start / block_size);

		if (!has_block)
		{
			start += block_size;
			unlock_extent_async(0, 0, false, lock_idx);
			continue;
		}

		bool in_big_block = co_await big_blocks_bitmap->get_async(io, start / big_block_size);

		unlock_extent_async(0, 0, false, lock_idx);

		int64 curr_block_size;
		if (in_big_block)
		{
			curr_block_size = big_block_size;
		}
		else
		{
			curr_block_size = small_block_size;
		}

		int64 toread = (std::min)((curr_block_size - start % curr_block_size), stop - start);

		preload_async_read(io, *ios, *ios_waiters_head, start, rsize);

		start += toread;
	}

	while (ios->size() != n_threads)
	{
		co_await io.sleep_ms(100);
	}

	for (auto& it : *ios)
	{
		io_uring_sqe* sqe = io.get_sqe();
		io_uring_prep_close(sqe, it->pipe[0]);
		io.submit(sqe);

		io_uring_sqe* sqe2 = io.get_sqe();
		io_uring_prep_close(sqe2, it->pipe[1]);
		io.submit(sqe2);
	}
}
#endif //HAS_ASYNC

void CloudFile::cmd(const std::string & c)
{
	if (is_async)
		abort();

	str_map params;
	ParseParamStrHttp(c, &params);

	std::string action = params["a"];
	if (action == "preload")
	{
		auto it_start = params.find("start");
		auto it_stop = params.find("stop");
		auto it_n_threads = params.find("n_threads");

		if (it_start == params.end())
		{
			Server->Log("Insufficient arguments for preload. 'start' missing.", LL_WARNING);
			return;
		}

		if (it_stop == params.end())
		{
			Server->Log("Insufficient arguments for preload. 'stop' missing.", LL_WARNING);
			return;
		}

		size_t n_threads = 8;
		if (it_n_threads!=params.end())
		{
			n_threads = watoi(it_n_threads->second);
		}

		preload(watoi64(it_start->second), watoi64(it_stop->second), n_threads);
	}
	else if (action == "scrub_sync_test1")
	{
		KvStoreFrontend::start_scrub_sync_test1();
	}
	else if (action == "scrub_sync_test2")
	{
		KvStoreFrontend::start_scrub_sync_test2();
	}
	else if (action == "reset_disk_errors")
	{
		auto frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
#ifdef HAS_LOCAL_BACKEND
			auto local = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
			if (local != nullptr)
			{
				if (!local->reset_disk_errors())
				{
					Server->Log("Resetting disk error info failed", LL_WARNING);
				}
			}
			else
#endif //HAS_LOCAL_BACKEND
			{
				Server->Log("Wrong kv store backend type (not KvStoreBackendLocal)", LL_WARNING);
			}
		}
		else
		{
			Server->Log("Wrong kv store type (not KvStoreFrontend)", LL_WARNING);
		}
	}
	else if (action == "dirty_all")
	{
		kv_store.dirty_all();
	}
	else if (action == "retry_all_deletion")
	{
		auto frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
			frontend->retry_all_deletion();
		}
	}
	else if (action == "disable_compression")
	{
		kv_store.disable_compression(watoi64(params["time"]));
	}
	else if (action == "disable_flush")
	{
		IScopedLock lock(mutex.get());
		++flush_enabled;
	}
	else if (action == "enable_flush")
	{
		IScopedLock lock(mutex.get());
		if (flush_enabled > 0)
			--flush_enabled;
	}
	else if (action == "preload_items")
	{
		auto it_n_threads = params.find("n_threads");

		size_t n_threads = os_get_num_cpus()*3;
		if (it_n_threads != params.end())
		{
			n_threads = watoi(it_n_threads->second);
		}

		int tag = watoi(params["tag"]);
		bool enable_memfiles = params["enable_memfiles"] == "1";
		bool load_only = params["load_only"] == "1";

		preload_items(params["fn"], n_threads, tag, !enable_memfiles, load_only);
	}
	else if (action == "preload_remove")
	{
		int tag = watoi(params["tag"]);
		kv_store.remove_preload_items(tag);
	}
	else if (action == "set_loglevel")
	{
		int loglevel = LL_INFO;
		if (params["loglevel"] == "debug")
			loglevel = LL_DEBUG;
		else if (params["loglevel"] == "warn")
			loglevel = LL_WARNING;
		else if (params["loglevel"] == "error")
			loglevel = LL_ERROR;
		else if (params["loglevel"] == "info")
			loglevel = LL_INFO;
		else
			loglevel = watoi(params["loglevel"]);

		Server->setLogLevel(loglevel);
	}
	else if (action == "migrate")
	{
		if (migration_ticket != ILLEGAL_THREADPOOL_TICKET)
		{
			Server->Log("Waiting for existing migration to finish", LL_WARNING);
			Server->getThreadPool()->waitFor(migration_ticket);
		}

		std::string conf_fn = params["conf"];
		migration_ticket = Server->getThreadPool()->execute(new MigrationCoordThread(this, conf_fn, false), "migrate coord");
	}
	else if (action == "stop_defrag")
	{
		auto frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
			frontend->stop_defrag();
		}
	}
	else if (action == "disable_read_memfiles")
	{
		kv_store.set_disable_read_memfiles(true);
	}
	else if (action == "disable_write_memfiles")
	{
		kv_store.set_disable_write_memfiles(true);
	}
	else if (action == "enable_read_memfiles")
	{
		kv_store.set_disable_read_memfiles(false);
	}
	else if (action == "enable_write_memfiles")
	{
		kv_store.set_disable_write_memfiles(false);
	}
	else if (action == "set_background_throttle_limit")
	{
		KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
#ifdef HAS_LOCAL_BACKEND
			KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
			if (backend != NULL)
			{
				backend->set_background_throttle_limit(atof(params["limit"].c_str()));
			}
#endif
		}
	}
	else if (action == "set_all_mirrored")
	{
		KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
			frontend->set_all_mirrored(true);
		}
	}
	else if (action == "set_all_unmirrored")
	{
		KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
			frontend->set_all_mirrored(false);
		}
	}
	else if (action == "enable_raid_freespace_stats")
	{
		IScopedLock lock(mutex.get());	
		enable_raid_freespace_stats = true;
	}
	else if (action == "disable_raid_freespace_stats")
	{
		IScopedLock lock(mutex.get());
		enable_raid_freespace_stats = false;
	}
	else if (action == "purge_jemalloc")
	{
		purge_jemalloc();
	}
	else if (action == "set_jemalloc_dirty_decay_ms")
	{
		set_jemalloc_dirty_decay(watoi64(params["timems"]));
	}
	else
	{
		Server->Log("Unknown cloud file action '" + action + "'", LL_WARNING);
	}
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::cmd_async(fuse_io_context& io, const std::string& c)
{
	str_map params;
	ParseParamStrHttp(c, &params);

	std::string action = params["a"];
	if (action == "preload")
	{
		auto it_start = params.find("start");
		auto it_stop = params.find("stop");
		auto it_n_threads = params.find("n_threads");

		if (it_start == params.end())
		{
			Server->Log("Insufficient arguments for preload. 'start' missing.", LL_WARNING);
			co_return 0;
		}

		if (it_stop == params.end())
		{
			Server->Log("Insufficient arguments for preload. 'stop' missing.", LL_WARNING);
			co_return 0;
		}

		size_t n_threads = 8;
		if (it_n_threads != params.end())
		{
			n_threads = watoi(it_n_threads->second);
		}

		co_await preload_async(io, watoi64(it_start->second), watoi64(it_stop->second), n_threads);

		co_return 0;
	}
	else if (action == "scrub_sync_test1")
	{
		co_return co_await io.run_in_threadpool([]() {
			KvStoreFrontend::start_scrub_sync_test1();
			return 0;
			}, "test1");
	}
	else if (action == "scrub_sync_test2")
	{
		co_return co_await io.run_in_threadpool([]() {
			KvStoreFrontend::start_scrub_sync_test2();
			return 0;
			}, "test2");
	}
	else if (action == "reset_disk_errors")
	{
		auto frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
			auto local = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
			if (local != nullptr)
			{
				co_return co_await io.run_in_threadpool([local]() {
					if (!local->reset_disk_errors())
					{
						Server->Log("Resetting disk error info failed", LL_WARNING);
					}
					return 0;
					}, "rest disk errors");
			}
			else
			{
				Server->Log("Wrong kv store backend type (not KvStoreBackendLocal)", LL_WARNING);
			}
		}
		else
		{
			Server->Log("Wrong kv store type (not KvStoreFrontend)", LL_WARNING);
		}
	}
	else if (action == "dirty_all")
	{
		co_return co_await io.run_in_threadpool([this]() {
			this->kv_store.dirty_all();
			return 0;
			}, "dirty all");
	}
	else if (action == "retry_all_deletion")
	{
		auto frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
			co_return co_await io.run_in_threadpool([frontend]() {
				frontend->retry_all_deletion();
				return 0;
				}, "del retry all");
		}
	}
	else if (action == "disable_compression")
	{
		int64 timems = watoi64(params["time"]);
		co_return co_await io.run_in_threadpool([this, timems]() {
			this->kv_store.disable_compression(timems);
			return 0;
			}, "disable compression");
	}
	else if (action == "disable_flush")
	{
		IScopedLock lock(mutex.get());
		++flush_enabled;
	}
	else if (action == "enable_flush")
	{
		IScopedLock lock(mutex.get());
		if (flush_enabled > 0)
			--flush_enabled;
	}
	else if (action == "preload_items")
	{
		auto it_n_threads = params.find("n_threads");

		size_t n_threads = os_get_num_cpus() * 3;
		if (it_n_threads != params.end())
		{
			n_threads = watoi(it_n_threads->second);
		}

		int tag = watoi(params["tag"]);
		bool enable_memfiles = params["enable_memfiles"] == "1";
		bool load_only = params["load_only"] == "1";

		co_return co_await preload_items_async(io, params["fn"], n_threads, tag, !enable_memfiles, load_only);
	}
	else if (action == "preload_remove")
	{
		int tag = watoi(params["tag"]);
		co_return co_await io.run_in_threadpool([this, tag]() {
			this->kv_store.remove_preload_items(tag);
			return 0;
			}, "remove preload items");
	}
	else if (action == "set_loglevel")
	{
		int loglevel = LL_INFO;
		if (params["loglevel"] == "debug")
			loglevel = LL_DEBUG;
		else if (params["loglevel"] == "warn")
			loglevel = LL_WARNING;
		else if (params["loglevel"] == "error")
			loglevel = LL_ERROR;
		else if (params["loglevel"] == "info")
			loglevel = LL_INFO;
		else
			loglevel = watoi(params["loglevel"]);

		Server->setLogLevel(loglevel);
	}
	else if (action == "migrate")
	{
		abort();

		if (migration_ticket != ILLEGAL_THREADPOOL_TICKET)
		{
			Server->Log("Waiting for existing migration to finish", LL_WARNING);
			Server->getThreadPool()->waitFor(migration_ticket);
		}

		std::string conf_fn = params["conf"];
		migration_ticket = Server->getThreadPool()->execute(new MigrationCoordThread(this, conf_fn, false), "migrate coord");
	}
	else if (action == "stop_defrag")
	{
		auto frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != nullptr)
		{
			co_return co_await io.run_in_threadpool([frontend]() {
				frontend->stop_defrag();
				return 0;
				}, "stop defrag");
		}
	}
	else if (action == "disable_read_memfiles")
	{
		co_return co_await io.run_in_threadpool([this]() {
			kv_store.set_disable_read_memfiles(true);
			return 0;
		}, "disable read memfiles");
	}
	else if (action == "disable_write_memfiles")
	{
		co_return co_await io.run_in_threadpool([this]() {
			kv_store.set_disable_write_memfiles(true);
			return 0;
		}, "disable_write_memfiles");
	}
	else if (action == "enable_read_memfiles")
	{
		co_return co_await io.run_in_threadpool([this]() {
			kv_store.set_disable_read_memfiles(false);
			return 0;
		}, "enable_read_memfiles");
	}
	else if (action == "enable_write_memfiles")
	{
		co_return co_await io.run_in_threadpool([this]() {
			kv_store.set_disable_write_memfiles(false);
			return 0;
		}, "enable_write_memfiles");
	}
	else if (action == "set_background_throttle_limit")
	{
		KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != NULL)
		{
			KvStoreBackendLocal* backend = dynamic_cast<KvStoreBackendLocal*>(frontend->getBackend());
			if (backend != NULL)
			{
				double limit = atof(params["limit"].c_str());
				co_return co_await io.run_in_threadpool([backend, limit]() {
					backend->set_background_throttle_limit(limit);
					return 0;
					}, "set bg throttle limit");
			}
		}
	}
	else if (action == "set_all_mirrored")
	{
		KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != NULL)
		{
			co_return co_await io.run_in_threadpool([frontend]() {
				frontend->set_all_mirrored(true);
				return 0;
				}, "set mirrored");
		}
	}
	else if (action == "set_all_unmirrored")
	{
		KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
		if (frontend != NULL)
		{
			co_return co_await io.run_in_threadpool([frontend]() {
				frontend->set_all_mirrored(false);
				return 0;
				}, "set unmirrored");
		}
	}
	else if (action == "enable_raid_freespace_stats")
	{
		enable_raid_freespace_stats = true;
	}
	else if (action == "disable_raid_freespace_stats")
	{
		enable_raid_freespace_stats = false;
	}
	else if (action == "purge_jemalloc")
	{
		io.run_in_threadpool_nowait([this]() {
			this->purge_jemalloc();
			});
	}
	else if (action == "set_jemalloc_dirty_decay_ms")
	{
		int64 timems = watoi64(params["timems"]);
		io.run_in_threadpool_nowait([this, timems]() {
			this->set_jemalloc_dirty_decay(timems);
		});
	}
	else
	{
		Server->Log("Unknown cloud file action '" + action + "'", LL_WARNING);
	}

	co_return 0;
}
#endif //HAS_ASYNC

int64 CloudFile::key_offset_hex(const std::string & key)
{
	std::string bkey = hexToBytes(key);

#ifndef NDEBUG
	if (strlower(bytesToHex(bkey))!=strlower(key))
	{
		Server->Log("Key is not hex", LL_ERROR);
		return -1;
	}
#endif

	return key_offset(bkey);
}

int64 CloudFile::key_offset(const std::string& key)
{
	bool big_block;
	return key_offset(key, big_block);
}

int64 CloudFile::key_offset(const std::string & key, bool& big_block)
{
	if (key.empty())
		return -1;

	if (key[0] == 's')
	{
		big_block = false;
		return block_key_rev(key)*small_block_size;
	}
	else if (key[0] == 'b')
	{
		big_block = true;
		return block_key_rev(key)*big_block_size;
	}
	else
	{
		return -1;
	}
}

std::string CloudFile::meminfo()
{
	std::string ret;

	if (is_async)
	{
#ifdef HAS_ASYNC
		CWData wdata;
		wdata.addChar(queue_cmd_meminfo);
		CRData rdata;
		get_async_msg(wdata, rdata);

		int64 locked_extents_info;
		rdata.getVarInt(&locked_extents_info);
		int64 bitmap_info;
		rdata.getVarInt(&bitmap_info);
		int64 bitmap_max;
		rdata.getVarInt(&bitmap_max);
		int64 big_blocks_bitmap_info;
		rdata.getVarInt(&big_blocks_bitmap_info);
		int64 big_blocks_bitmap_max;
		rdata.getVarInt(&big_blocks_bitmap_max);
		int64 old_big_blocks_bitmap_info;
		rdata.getVarInt(&old_big_blocks_bitmap_info);
		int64 new_big_blocks_bitmap_info;
		rdata.getVarInt(&new_big_blocks_bitmap_info);
		int64 fracture_big_blogs_info;
		rdata.getVarInt(&fracture_big_blogs_info);
		int64 in_write_retrieval_info;
		rdata.getVarInt(&in_write_retrieval_info);

		ret += "##CloudFile:\n";
		ret += "  locked_extents: " + convert(locked_extents_info) + " * " + PrettyPrintBytes(sizeof(SExtent)) + "\n";
		ret += "  bitmap: " + PrettyPrintBytes(bitmap_info) + "/" + PrettyPrintBytes(bitmap_max * 4096) + "\n";
		ret += "  big_blocks_bitmap: " + PrettyPrintBytes(big_blocks_bitmap_info) + "/" + PrettyPrintBytes(big_blocks_bitmap_max * 4096) + "\n";
		if (old_big_blocks_bitmap.get() != nullptr)
		{
			ret += "  old_big_blocks_bitmap: " + PrettyPrintBytes(old_big_blocks_bitmap_info) + "\n";
		}
		if (new_big_blocks_bitmap.get() != nullptr)
		{
			ret += "  new_big_blocks_bitmap: " + PrettyPrintBytes(new_big_blocks_bitmap_info) + "\n";
		}
		ret += "  fracture_big_blogs: " + convert(fracture_big_blogs_info) + " * " + PrettyPrintBytes(sizeof(int64) * 2) + "\n";
		ret += "  in_write_retrieval: " + convert(in_write_retrieval_info) + " * " + PrettyPrintBytes(sizeof(std::string)) + "\n";
#else
		assert(false);
#endif
	}
	else
	{
		IScopedLock lock(mutex.get());

		ret += "##CloudFile:\n";
		ret += "  locked_extents: " + convert(locked_extents.capacity()) + " * " + PrettyPrintBytes(sizeof(SExtent))+"\n";
		ret += "  bitmap: " + PrettyPrintBytes(bitmap->meminfo())+"/"+PrettyPrintBytes(bitmap->get_max_cache_items()*4096)+ "\n";
		ret += "  big_blocks_bitmap: " + PrettyPrintBytes(big_blocks_bitmap->meminfo()) +"/"+PrettyPrintBytes(big_blocks_bitmap->get_max_cache_items()*4096)+ "\n";
		if (old_big_blocks_bitmap.get() != nullptr)
		{
			ret += "  old_big_blocks_bitmap: " + PrettyPrintBytes(old_big_blocks_bitmap->meminfo()) + "\n";
		}
		if (new_big_blocks_bitmap.get() != nullptr)
		{
			ret += "  new_big_blocks_bitmap: " + PrettyPrintBytes(new_big_blocks_bitmap->meminfo()) + "\n";
		}
		ret += "  fracture_big_blogs: " + convert(fracture_big_blogs.size()) + " * " + PrettyPrintBytes(sizeof(int64) * 2) + "\n";
		ret += "  in_write_retrieval: " + convert(in_write_retrieval.size()) + " * " + PrettyPrintBytes(sizeof(std::string)) + "\n";
	}

	{
		IScopedReadLock lock(chunks_mutex.get());
		ret += "  fs_chunks: " + convert(fs_chunks.capacity()) + " * " + PrettyPrintBytes(sizeof(IBackupFileSystem::SChunk)) + " = "+PrettyPrintBytes(sizeof(IBackupFileSystem::SChunk)*fs_chunks.capacity())+"\n";
	}

	ret += kv_store.meminfo();

	return ret;
}

void CloudFile::shrink_mem()
{
	if (is_async)
		abort();

	{
		IScopedLock lock(mutex.get());
		if(bitmap.get()!=nullptr)
			bitmap->flush();
		if (big_blocks_bitmap.get() != nullptr)
			big_blocks_bitmap->flush();
	}

	kv_store.shrink_mem();
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::shrink_mem_async(fuse_io_context& io)
{
	Server->Log("Shrinking memory (async)", LL_WARNING);

	size_t lock_idx = co_await lock_extent_async(0, 0, false);
	
	if (bitmap.get() != nullptr)
		co_await bitmap->flush_async(io);
	if (big_blocks_bitmap.get() != nullptr)
		co_await big_blocks_bitmap->flush_async(io);

	unlock_extent_async(0, 0, false, lock_idx);

	co_await io.run_in_threadpool([this]() {
		this->kv_store.shrink_mem();
		return 0;
		}, "shrink kvstore mem");

	co_return 0;
}
#endif

int64 CloudFile::get_total_hits()
{
	return kv_store.get_total_hits();
}

int64 CloudFile::get_total_hits_async()
{
	return kv_store.get_total_hits_async();
}

int64 CloudFile::get_total_memory_hits()
{
	return kv_store.get_total_memory_hits();
}

int64 CloudFile::get_total_memory_hits_async()
{
	return kv_store.get_total_memory_hits_async();
}

int64 CloudFile::get_total_cache_miss_backend()
{
	return kv_store.get_total_cache_miss_backend();
}

int64 CloudFile::get_total_cache_miss_decompress()
{
	return kv_store.get_total_cache_miss_decompress();
}

int64 CloudFile::get_total_dirty_ops()
{
	return kv_store.get_total_dirty_ops();
}

int64 CloudFile::get_total_balance_ops()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->get_total_balance_ops();
	}
	return -1;
}

int64 CloudFile::get_total_del_ops()
{
	KvStoreFrontend* frontend = dynamic_cast<KvStoreFrontend*>(online_kv_store.get());
	if (frontend != nullptr)
	{
		return frontend->get_total_del_ops();
	}
	return -1;
}

int64 CloudFile::get_total_put_ops()
{
	return kv_store.get_total_put_ops();
}

int64 CloudFile::get_total_compress_ops()
{
	return kv_store.get_total_compress_ops();
}

bool CloudFile::Flush(bool do_submit, bool for_slog)
{
	if (is_async)
		abort();

	if (!bdev_name.empty())
	{
		Server->Log("Switching bcache device " + bdev_name + " to writethrough mode...", LL_INFO);

		{
			IScopedLock lock(mutex.get());

			if (writeback_count == 0)
			{
				std::unique_ptr<IFile> writeback_percent_f(Server->openFile("/sys/block/" + bdev_name + "/bcache/writeback_percent", MODE_READ));
				bcache_writeback_percent = trim(writeback_percent_f->Read(512));
			}

			++writeback_count;
		}

		doublefork_writestring("writethrough", "/sys/block/" + bdev_name + "/bcache/cache_mode");

		//disables writeback throttling
		writestring("0", "/sys/block/" + bdev_name + "/bcache/writeback_percent");

		while (true)
		{
			Server->wait(100);
			std::unique_ptr<IFile> state_f(Server->openFile("/sys/block/" + bdev_name + "/bcache/state", MODE_READ));
			std::string state = trim(state_f->Read(512));

			if (!state.empty() && state != "dirty")
			{
				Server->Log("Bcache device is in state '"+state+"'", LL_INFO);
				break;
			}
		}

#ifndef _WIN32
		/*
		This makes Linux deadlock in some cases. Not sure if even necessary...
		if (!bdev_name.empty())
		{
			Server->Log("Flushing /dev/" + bdev_name + "...");
			flush_dev("/dev/" + bdev_name);
		}
		

		if (!ldev_name.empty())
		{
			Server->Log("Flushing /dev/" + ldev_name + "...");
			flush_dev("/dev/" + ldev_name);
		}*/
#endif
	}

	IScopedLock lock(mutex.get());

	is_flushing = true;

	Auto(is_flushing = false);

	while (flush_enabled>0)
	{
		lock.relock(nullptr);
		Server->wait(1000);
		lock.relock(mutex.get());
	}
	
	int64 orig_cloudfile_size = cloudfile_size;
	++wait_for_exclusive;
	lock_extent(lock, 0, orig_cloudfile_size, true);
	--wait_for_exclusive;

	if (for_slog
		&& slog.get() != nullptr
		&& slog->Size() < slog_max_size)
	{
		return true;
	}

	bool ret = true;

	IScopedLock migrate_lock(nullptr);
	if (migrate_to_cf != nullptr)
	{
		migrate_lock.relock(migrate_to_cf->mutex.get());
		++migrate_to_cf->wait_for_exclusive;
		migrate_to_cf->lock_extent(migrate_lock, 0, orig_cloudfile_size, true);
		--migrate_to_cf->wait_for_exclusive;

		if (!update_migration_settings())
		{
			Server->Log("Error updating migration settings", LL_ERROR);
			migrate_to_cf->unlock_extent(migrate_lock, 0, orig_cloudfile_size, true);
			unlock_extent(lock, 0, orig_cloudfile_size, true);
			return false;
		}

		if (!migrate_to_cf->close_bitmaps())
		{
			migrate_to_cf->unlock_extent(migrate_lock, 0, orig_cloudfile_size, true);
			unlock_extent(lock, 0, orig_cloudfile_size, true);	
			return false;
		}

		size_t retry_n = 0;
		while (migrate_to_cf->kv_store.checkpoint(do_submit, retry_n))
		{
			Server->Log("Error checkpointing migrate_to KVStore. Retrying...", LL_WARNING);
			retryWait(retry_n++);
		}

		try
		{
			migrate_to_cf->open_bitmaps();
		}
		catch (const std::runtime_error& e)
		{
			Server->Log(std::string("Error while opening migrate_to bitmaps: ") + e.what(), LL_ERROR);
			ret = false;
		}
	}

	if(!close_bitmaps())
	{
		if (migrate_to_cf != nullptr)
		{
			migrate_to_cf->unlock_extent(migrate_lock, 0, orig_cloudfile_size, true);
		}
		unlock_extent(lock, 0, orig_cloudfile_size, true);
		if (!bdev_name.empty())
		{
			Server->Log("Switching bcache device " + bdev_name + " to writeback mode...", LL_INFO);

			--writeback_count;

			if (writeback_count == 0)
			{
				doublefork_writestring("writeback", "/sys/block/" + bdev_name + "/bcache/cache_mode");
			}
		}
		return false;
	}

	size_t retry_n=0;
	while(!kv_store.checkpoint(do_submit, retry_n))
	{
		Server->Log("Error checkpointing KVStore. Retrying...", LL_WARNING);
		retryWait(retry_n++);
	}
	
	try
	{
		open_bitmaps();
	}
	catch(const std::runtime_error& e)
	{
		Server->Log(std::string("Error while opening bitmaps: ") + e.what(), LL_ERROR);
		ret = false;
	}

	if (migrate_to_cf != nullptr)
	{
		migrate_to_cf->unlock_extent(migrate_lock, 0, orig_cloudfile_size, true);
	}

	unlock_extent(lock, 0, orig_cloudfile_size, true);

	if (!bdev_name.empty())
	{
		Server->Log("Switching bcache device " + bdev_name + " to writeback mode...", LL_INFO);

		--writeback_count;

		if (writeback_count == 0)
		{
			lock.relock(nullptr);

			doublefork_writestring("writeback", "/sys/block/" + bdev_name + "/bcache/cache_mode");

			writestring(bcache_writeback_percent, "/sys/block/" + bdev_name + "/bcache/writeback_percent");
		}
	}

	if (!slog_open())
	{
		ret = false;
	}

	return ret;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> CloudFile::FlushAsync(fuse_io_context& io, bool do_submit)
{
	if (!bdev_name.empty())
	{
		Server->Log("Switching bcache device " + bdev_name + " to writethrough mode...", LL_INFO);

		{
			IScopedLock lock(mutex.get());

			if (writeback_count == 0)
			{
				std::unique_ptr<IFile> writeback_percent_f(Server->openFile("/sys/block/" + bdev_name + "/bcache/writeback_percent", MODE_READ));
				bcache_writeback_percent = trim(writeback_percent_f->Read(512));
			}

			++writeback_count;
		}

		doublefork_writestring("writethrough", "/sys/block/" + bdev_name + "/bcache/cache_mode");

		//disables writeback throttling
		writestring("0", "/sys/block/" + bdev_name + "/bcache/writeback_percent");

		while (true)
		{
			Server->wait(100);
			std::unique_ptr<IFile> state_f(Server->openFile("/sys/block/" + bdev_name + "/bcache/state", MODE_READ));
			std::string state = trim(state_f->Read(512));

			if (!state.empty() && state != "dirty")
			{
				Server->Log("Bcache device is in state '" + state + "'", LL_INFO);
				break;
			}
		}

#ifndef _WIN32
		/*
		This makes Linux deadlock in some cases. Not sure if even necessary...
		if (!bdev_name.empty())
		{
			Server->Log("Flushing /dev/" + bdev_name + "...");
			flush_dev("/dev/" + bdev_name);
		}


		if (!ldev_name.empty())
		{
			Server->Log("Flushing /dev/" + ldev_name + "...");
			flush_dev("/dev/" + ldev_name);
		}*/
#endif
	}

	is_flushing = true;
	Auto(is_flushing = false);

	while (flush_enabled > 0)
	{
		co_await io.sleep_ms(1000);
	}

	int64 orig_cloudfile_size = cloudfile_size;
	++wait_for_exclusive;
	size_t lock_idx = co_await lock_extent_async(0, orig_cloudfile_size, true);
	--wait_for_exclusive;
	resume_exclusive_awaiters();

	bool ret = true;

	size_t migrate_lock_idx;
	if (migrate_to_cf != nullptr)
	{
		++migrate_to_cf->wait_for_exclusive;
		migrate_lock_idx = co_await migrate_to_cf->lock_extent_async(0, orig_cloudfile_size, true);
		--migrate_to_cf->wait_for_exclusive;
		migrate_to_cf->resume_exclusive_awaiters();

		if (!update_migration_settings())
		{
			Server->Log("Error updating migration settings", LL_ERROR);
			migrate_to_cf->unlock_extent_async(0, orig_cloudfile_size, true, migrate_lock_idx);
			unlock_extent_async(0, orig_cloudfile_size, true, lock_idx);
			co_return false;
		}

		if (!migrate_to_cf->close_bitmaps())
		{
			migrate_to_cf->unlock_extent_async(0, orig_cloudfile_size, true, migrate_lock_idx);
			unlock_extent_async(0, orig_cloudfile_size, true, lock_idx);
			co_return false;
		}

		size_t retry_n = 0;
		while (migrate_to_cf->kv_store.checkpoint(do_submit, retry_n))
		{
			Server->Log("Error checkpointing migrate_to KVStore. Retrying...", LL_WARNING);
			retryWait(retry_n++);
		}

		try
		{
			migrate_to_cf->open_bitmaps();
		}
		catch (const std::runtime_error& e)
		{
			Server->Log(std::string("Error while opening migrate_to bitmaps: ") + e.what(), LL_ERROR);
			ret = false;
		}
	}

	if (!close_bitmaps())
	{
		unlock_extent_async(0, orig_cloudfile_size, true, lock_idx);
		if (!bdev_name.empty())
		{
			Server->Log("Switching bcache device " + bdev_name + " to writeback mode...", LL_INFO);

			--writeback_count;

			if (writeback_count == 0)
			{
				doublefork_writestring("writeback", "/sys/block/" + bdev_name + "/bcache/cache_mode");
			}
		}
		co_return false;
	}

	bool b = co_await io.run_in_threadpool([this, do_submit]() {
		size_t retry_n = 0;
		while (!this->kv_store.checkpoint(do_submit, retry_n))
		{
			Server->Log("Error checkpointing KVStore. Retrying...", LL_WARNING);
			retryWait(retry_n++);
		}

		try
		{
			this->open_bitmaps();
		}
		catch (const std::runtime_error& e)
		{
			Server->Log(std::string("Error while opening bitmaps: ") + e.what(), LL_ERROR);
			return false;
		}
		return true;

		}, "kv checkpoint");

	if (!b)
		ret = false;
	

	if (migrate_to_cf != nullptr)
	{
		migrate_to_cf->unlock_extent_async(0, orig_cloudfile_size, true, migrate_lock_idx);
	}

	unlock_extent_async(0, orig_cloudfile_size, true, lock_idx);

	if (!bdev_name.empty())
	{
		Server->Log("Switching bcache device " + bdev_name + " to writeback mode...", LL_INFO);

		--writeback_count;

		if (writeback_count == 0)
		{
			doublefork_writestring("writeback", "/sys/block/" + bdev_name + "/bcache/cache_mode");

			writestring(bcache_writeback_percent, "/sys/block/" + bdev_name + "/bcache/writeback_percent");
		}
	}

	if (!slog_open())
	{
		ret = false;
	}

	co_return ret;
}
#endif //HAS_ASYNC

bool CloudFile::slog_open()
{
	if (slog.get() == nullptr)
		return true;

	slog.reset();
	Server->deleteFile(slog_path);

	syncDir(ExtractFilePath(slog_path, os_file_sep()));

	slog.reset(Server->openFile(slog_path, MODE_WRITE));

	if (slog.get() == nullptr)
	{
		Server->Log("Error opening slog file at " + slog_path + ". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	if (slog->Size() != 0)
		return false;

	if (slog->Write(slog_magic) != slog_magic.size())
	{
		Server->Log("Error writing slog magic. " + os_last_error_str(), LL_ERROR);
		return false;
	}

	int64 transid = kv_store.get_basetransid();

	if (slog->Write(reinterpret_cast<char*>(&transid), sizeof(transid)) != sizeof(transid))
	{
		Server->Log("Error writing transid to slog. " + os_last_error_str(), LL_ERROR);
		return false;
	}

	slog->Sync();

	slog_size = slog_magic.size() + sizeof(transid);
	slog_last_sync = slog_magic.size() + sizeof(transid);;

	return true;
}

void CloudFile::purge_jemalloc()
{
#if !defined(NO_JEMALLOC) && !defined(_WIN32)
	unsigned narenas = 0;
	size_t sz = sizeof(unsigned);
	if (!mallctl("arenas.narenas", &narenas, &sz, NULL, 0))
	{
		std::string purge_cmd = "arena."+convert(narenas)+".purge";
		if(mallctl(purge_cmd.c_str(), NULL, 0, NULL, 0))
		{
			Server->Log("Purging jemalloc arenas failed", LL_WARNING);
		}
	}
#endif
}

void CloudFile::set_jemalloc_dirty_decay(int64 timems)
{
#if !defined(NO_JEMALLOC) && !defined(_WIN32)
	ssize_t dirty_decay_old;
	ssize_t dirty_decay_new = timems;
	size_t sz = sizeof(dirty_decay_old);
	if (!mallctl("arenas.dirty_decay_ms", &dirty_decay_old, &sz, &dirty_decay_new, sizeof(dirty_decay_new)))
	{
		Server->Log("Setting dirty decay ms failed", LL_WARNING);
	}
#endif
}

bool CloudFile::Flush()
{
	return Flush(true);
}


void CloudFile::open_bitmaps()
{
	bitmaps_file_size = 0;

	big_blocks_bitmap_file = kv_store.get("big_blocks_bitmap", TransactionalKvStore::BitmapInfo::Unknown,
		TransactionalKvStore::Flag::disable_fd_cache|TransactionalKvStore::Flag::disable_throttling|
		TransactionalKvStore::Flag::disable_memfiles, -1);

	if(big_blocks_bitmap_file==nullptr)
	{
		throw std::runtime_error("Cannot open big_blocks_bitmap");
	}

	bitmaps_file_size += big_blocks_bitmap_file->Size();	
	
	if(new_big_blocks_bitmap_file!=nullptr)
	{
		std::string fn = new_big_blocks_bitmap_file->getFilename();
		Server->destroy(new_big_blocks_bitmap_file);
		Server->deleteFile(fn);
	}
	
	new_big_blocks_bitmap_file = Server->openTemporaryFile();

	if(new_big_blocks_bitmap_file==nullptr)
	{
		throw std::runtime_error("Cannot open new_big_blocks_bitmap");
	}
	
	old_big_blocks_bitmap_file = kv_store.get("old_big_blocks_bitmap", TransactionalKvStore::BitmapInfo::Unknown, 
		TransactionalKvStore::Flag::disable_fd_cache | TransactionalKvStore::Flag::disable_throttling |
		TransactionalKvStore::Flag::disable_memfiles, -1);

	if(old_big_blocks_bitmap_file==nullptr)
	{
		throw std::runtime_error("Cannot open old_big_blocks_bitmap");
	}

	bitmaps_file_size += old_big_blocks_bitmap_file->Size();

	int64 big_blocks = div_up(cloudfile_size, big_block_size);

	size_t bitmap_cache_items = static_cast<size_t>((70 * 1024 * 1024*memory_usage_factor) / bitmap_block_size);

	size_t big_blocks_bitmap_cache_items = (std::max)(static_cast<size_t>(bitmap_cache_items / (big_block_size / block_size)),
		static_cast<size_t>(100));

	big_blocks_bitmap.reset(new SparseFileBitmap(big_blocks_bitmap_file, big_blocks, true, big_blocks_bitmap_cache_items));
	
	new_big_blocks_bitmap.reset(new FileBitmap(new_big_blocks_bitmap_file, big_blocks, false));
	old_big_blocks_bitmap.reset(new FileBitmap(old_big_blocks_bitmap_file, big_blocks, false));

	bitmap_file = kv_store.get("bitmap", TransactionalKvStore::BitmapInfo::Unknown,
		TransactionalKvStore::Flag::disable_fd_cache | TransactionalKvStore::Flag::disable_throttling|
		TransactionalKvStore::Flag::disable_memfiles, -1);

	if(bitmap_file==nullptr)
	{
		throw std::runtime_error("Cannot open bitmap");
	}

	bitmaps_file_size += bitmap_file->Size();

	int64 bitmap_blocks = div_up(div_up(cloudfile_size, big_block_size)*big_block_size, block_size);

	bitmap_cache_items = (std::max)(bitmap_cache_items, static_cast<size_t>(100));

	bitmap.reset(new SparseFileBitmap(bitmap_file, bitmap_blocks, false, bitmap_cache_items));

	if (used_bytes == 0)
	{
		used_bytes = bitmap->count_bits()*block_size;
		bitmap->clear_cache_no_flush();
	}

	Server->Log("Cloud drive size: " + PrettyPrintBytes(used_bytes));
}

bool CloudFile::bitmap_has_big_block( int64 blocknum )
{
	return bitmap->get_range((blocknum*big_block_size)/block_size, ((blocknum+1)*big_block_size)/block_size);
}

bool CloudFile::bitmap_has_small_block( int64 blocknum )
{
	return bitmap->get_range((blocknum*small_block_size)/block_size, ((blocknum+1)*small_block_size)/block_size);
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> CloudFile::bitmap_has_big_block_async(fuse_io_context& io, int64 blocknum)
{
	co_return co_await bitmap->get_range_async(io, (blocknum * big_block_size) / block_size, ((blocknum + 1) * big_block_size) / block_size);
}

fuse_io_context::io_uring_task<bool> CloudFile::bitmap_has_small_block_async(fuse_io_context& io, int64 blocknum)
{
	co_return co_await bitmap->get_range_async(io, (blocknum * small_block_size) / block_size, ((blocknum + 1) * small_block_size) / block_size);
}
#endif

bool CloudFile::Sync()
{
	if (!slog_path.empty())
	{
		IScopedLock lock(mutex.get());
		lock_extent(lock, 1, 0, false);
		bool ret;
		if (slog.get() == nullptr)
		{
			ret = false;
		}
		else
		{
			ret = slog->Sync();
		}
		unlock_extent(lock, 1, 0, false);
		return ret;
	}
	return true;
}

int64 CloudFile::getDirtyBytes()
{
	return kv_store.get_dirty_bytes() - bitmaps_file_size;
}

int64 CloudFile::getSubmittedBytes()
{
	return kv_store.get_submitted_bytes();
}

int64 CloudFile::getTotalSubmittedBytes()
{
	return kv_store.get_total_submitted_bytes();
}

int64 CloudFile::getUsedBytes()
{
	IScopedLock lock(mutex.get());

	return used_bytes;
}

std::string CloudFile::getNumDirtyItems()
{
	std::string dirty_items;
	std::map<int64, size_t> di = kv_store.get_num_dirty_items();
	for(std::map<int64, size_t>::iterator it=di.begin();
		it!=di.end();++it)
	{
		dirty_items+=convert(it->first)+": "+convert(it->second)+"\n";
	}

	return dirty_items;
}

std::string CloudFile::getNumMemfileItems()
{
	std::string memfile_items;
	std::map<int64, size_t> di = kv_store.get_num_memfile_items();
	for (std::map<int64, size_t>::iterator it = di.begin();
		it != di.end(); ++it)
	{
		memfile_items += convert(it->first) + ": " + convert(it->second) + "\n";
	}

	return memfile_items;
}

int64 CloudFile::getCacheSize()
{
	return kv_store.get_cache_size();
}

void CloudFile::Reset()
{
	if (is_async)
		abort();

	IScopedLock lock(mutex.get());

	close_bitmaps();

	kv_store.reset();

	open_bitmaps();
}

bool CloudFile::close_bitmaps()
{
	IScopedLock lock(mutex.get());

	if(!big_blocks_bitmap->flush())
	{
		return false;
	}

	if(!bitmap->flush())
	{
		return false;
	}

	for(int64 i=0;i<new_big_blocks_bitmap->size();++i)
	{
		if(new_big_blocks_bitmap->get(i))
		{
			old_big_blocks_bitmap->set(i, true);
		}
	}

	if(!old_big_blocks_bitmap->flush())
	{
		return false;
	}

	big_blocks_bitmap.reset();
	bitmap.reset();
	old_big_blocks_bitmap.reset();

	kv_store.release("big_blocks_bitmap");
	kv_store.release("bitmap");
	kv_store.release("old_big_blocks_bitmap");

	return true;
}

void CloudFile::lock_extent(IScopedLock& lock, int64 start, int64 length, bool exclusive)
{
	if (is_async)
		abort();

	while (!exclusive
		&& wait_for_exclusive>0)
	{
		lock.relock(nullptr);
		Server->wait(1);
		lock.relock(mutex.get());
	}

	size_t first_dead;
    bool retry=true;
    while(retry)
    {
		retry=false;
		first_dead = std::string::npos;
		for(size_t i=0;i<locked_extents.size()
			&& i<=locked_extents_max_alive;++i)
		{
			SExtent& extent = locked_extents[i];

			if (!extent.alive)
			{
				if (extent.refcount <= 0
					&& first_dead==std::string::npos)
				{
					first_dead = i;
				}
				continue;
			}

			if(!exclusive && extent.cond==nullptr && extent.start==start
				&& extent.length==length)
			{
				++extent.refcount;
				return;
			}

			if( (start<=extent.start && start+length>=extent.start+extent.length)
				|| (start>=extent.start && start<extent.start+extent.length)
				|| (start+length>extent.start && start+length<=extent.start+extent.length) )
			{
				if(extent.cond!=nullptr)
				{
					retry=true;
					++extent.refcount;
					ICondition* cond = extent.cond;
					cond->wait(&lock);

					for (size_t j = 0; j < locked_extents.size(); ++j)
					{
						if (locked_extents[j].cond == cond)
						{
							--locked_extents[j].refcount;
							if (locked_extents[j].refcount <= 0)
							{
								assert(!locked_extents[j].alive);
								Server->destroy(locked_extents[j].cond);
								locked_extents[j].cond = nullptr;
								if (j == locked_extents_max_alive
									&& locked_extents_max_alive>0)
								{
									--locked_extents_max_alive;
									while (locked_extents_max_alive > 0
										&& !locked_extents[locked_extents_max_alive].alive
										&& locked_extents[locked_extents_max_alive].refcount <= 0)
									{
										--locked_extents_max_alive;
									}
								}
							}
							break;
						}
					}
					break;
				}
				else if(exclusive)
				{
					retry=true;
					lock.relock(nullptr);
					Server->wait(1);
					lock.relock(mutex.get());
					break;
				}
			}
		}
    }

	if (first_dead == std::string::npos)
	{
		for (size_t i = locked_extents_max_alive+1;
			i < locked_extents.size(); ++i)
		{
			if (!locked_extents[i].alive
				&& locked_extents[i].refcount <= 0)
			{
				first_dead = i;
				break;
			}
		}
	}

	if (first_dead != std::string::npos)
	{
		SExtent& extent = locked_extents[first_dead];
		extent.start = start;
		extent.length = length;
		if (exclusive)
		{
			extent.cond = Server->createCondition();
		}
		else
		{
			extent.cond = nullptr;
		}
		extent.refcount = 1;
		extent.alive = true;

		if (first_dead > locked_extents_max_alive)
		{
			locked_extents_max_alive = first_dead;
		}
		return;
	}
    
    SExtent new_extent;
    new_extent.start = start;
    new_extent.length = length;
    if(exclusive)
    {
		new_extent.cond = Server->createCondition();
    }
	else
	{
		new_extent.cond = nullptr;
	}
    new_extent.refcount = 1;
	new_extent.alive = true;
    
    locked_extents.push_back(new_extent);
	locked_extents_max_alive = locked_extents.size() - 1;
}

void CloudFile::unlock_extent(IScopedLock& lock, int64 start, int64 length, bool exclusive)
{
	if (is_async)
		abort();

    for(size_t i=0;i<locked_extents.size();++i)
    {
		SExtent& extent = locked_extents[i];

		if(extent.start==start && extent.length==length)
		{
			assert(!exclusive || extent.cond!=nullptr);

			--extent.refcount;
			if (exclusive)
			{
				extent.alive = false;
			}

			if(extent.refcount<=0)
			{
				Server->destroy(extent.cond);
				extent.cond = nullptr;
				extent.alive = false;
				if (i == locked_extents_max_alive
					&& locked_extents_max_alive>0)
				{
					--locked_extents_max_alive;
					while (locked_extents_max_alive > 0
						&& !locked_extents[locked_extents_max_alive].alive
						&& locked_extents[locked_extents_max_alive].refcount <= 0)
					{
						--locked_extents_max_alive;
					}
				}
			}
			else if (extent.cond != nullptr)
			{
				extent.cond->notify_all();
			}

			break;
		}
    }
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<size_t> CloudFile::lock_extent_async(int64 start, int64 length, bool exclusive)
{
	while (!exclusive
		&& wait_for_exclusive>0)
	{
		co_await ExclusiveAwaiter(*this);
	}

	size_t first_dead;
    bool retry=true;
    while(retry)
    {
		retry=false;
		first_dead = std::string::npos;
		for(size_t i=0;i<async_locked_extents.size()
			&& i<=locked_extents_max_alive;++i)
		{
			SExtentAsync& extent = async_locked_extents[i];

			if(extent.refcount<=0)
			{
				if(first_dead==std::string::npos)
				{
					first_dead = i;
				}
				continue;
			}

			if(!exclusive && !extent.exclusive && extent.start==start
				&& extent.length==length)
			{
				++extent.refcount;
				co_return i;
			}

			if( (start<=extent.start && start+length>=extent.start+extent.length)
				|| (start>=extent.start && start<extent.start+extent.length)
				|| (start+length>extent.start && start+length<=extent.start+extent.length) )
			{
				if(extent.exclusive || exclusive)
				{
					retry=true;
					co_await ExtentAwaiter(extent);
					break;
				}
			}
		}
    }

	if (first_dead == std::string::npos)
	{
		for (size_t i = locked_extents_max_alive+1;
			i < async_locked_extents.size(); ++i)
		{
			if (async_locked_extents[i].refcount <= 0)
			{
				first_dead = i;
				break;
			}
		}
	}

	if (first_dead != std::string::npos)
	{
		SExtentAsync& extent = async_locked_extents[first_dead];
		assert(extent.awaiters == nullptr
			&& extent.refcount<=0);
		extent.start = start;
		extent.length = length;
		extent.exclusive = exclusive;
		extent.refcount = 1;

		if (first_dead > locked_extents_max_alive)
		{
			locked_extents_max_alive = first_dead;
		}
		co_return first_dead;
	}
    
    SExtentAsync new_extent;
    new_extent.start = start;
    new_extent.length = length;
    new_extent.exclusive = exclusive;
    new_extent.refcount = 1;
	new_extent.awaiters = nullptr;
    
    async_locked_extents.push_back(new_extent);
	locked_extents_max_alive = async_locked_extents.size() - 1;
	co_return async_locked_extents.size() - 1;
}
#endif //HAS_ASYNC

#ifdef HAS_ASYNC
void CloudFile::unlock_extent_async(int64 start, int64 length, bool exclusive, size_t lock_idx)
{
	SExtentAsync& extent = async_locked_extents[lock_idx];
	assert(extent.start == start);
	assert(extent.length == length);
	assert(!exclusive || extent.exclusive);

	--extent.refcount;
	assert(extent.refcount >= 0);
	if(extent.refcount<=0)
	{
		extent.exclusive = false;
		if (lock_idx == locked_extents_max_alive
			&& locked_extents_max_alive>0)
		{
			--locked_extents_max_alive;
			while (locked_extents_max_alive > 0
				&& async_locked_extents[locked_extents_max_alive].refcount <= 0)
			{
				--locked_extents_max_alive;
			}
		}

		AwaiterCoList* curr_awaiters = extent.awaiters;
		extent.awaiters = nullptr;
		while (curr_awaiters != nullptr)
		{
			AwaiterCoList* next = curr_awaiters->next;
			curr_awaiters->awaiter.resume();
			curr_awaiters = next;
		}
	}
}
#endif //HAS_ASYNC

void CloudFile::add_fracture_big_block(int64 b)
{
	if (fracture_big_blogs.size() > 100)
	{
		return;
	}

	auto it = fracture_big_blogs.find(b);
	if (it != fracture_big_blogs.end())
	{
		return;
	}

	fracture_big_blogs[b] = Server->getTimeMS()+60*1000;
}

bool CloudFile::Resize(int64 nsize)
{
	if (is_async)
	{
#ifdef HAS_ASYNC
		CWData wdata;
		wdata.addChar(queue_cmd_resize);
		wdata.addVarInt(nsize);

		CRData rdata;
		get_async_msg(wdata, rdata);

		char ch;
		bool b = rdata.getChar(&ch);
		assert(b);
		return ch==1;
#else
		assert(false);
#endif
	}

	IScopedLock lock(mutex.get());

	int64 orig_cloudfile_size = cloudfile_size;
	++wait_for_exclusive;
	lock_extent(lock, 0, cloudfile_size, true);
	--wait_for_exclusive;

	if (!close_bitmaps())
	{
		unlock_extent(lock, 0, cloudfile_size, true);
		return false;
	}	
	bool ret = true;

	IFile* f_cloudfile_size = kv_store.get("cloudfile_size", TransactionalKvStore::BitmapInfo::Unknown, 
		TransactionalKvStore::Flag::disable_fd_cache | TransactionalKvStore::Flag::disable_throttling, -1);

	if (f_cloudfile_size == nullptr)
	{
		Server->Log("Cannot open cloudfile_size", LL_ERROR);
		unlock_extent(lock, 0, orig_cloudfile_size, true);
		return false;
	}

	cloudfile_size = nsize;
		
	if (f_cloudfile_size->Write(0, reinterpret_cast<const char*>(&cloudfile_size), sizeof(cloudfile_size)) != sizeof(cloudfile_size))
	{
		Server->Log("Error writing new cloudfile size. " + os_last_error_str(), LL_ERROR);
		ret = false;
	}

	kv_store.release("cloudfile_size");

	try
	{
		open_bitmaps();
	}
	catch (const std::runtime_error&)
	{
		ret = false;
	}

	unlock_extent(lock, 0, orig_cloudfile_size, true);

	return ret;
}

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> CloudFile::ResizeAsync(fuse_io_context& io, int64 nsize)
{
	int64 orig_cloudfile_size = cloudfile_size;
	++wait_for_exclusive;
	size_t lock_idx = co_await lock_extent_async(0, cloudfile_size, true);
	--wait_for_exclusive;
	resume_exclusive_awaiters();

	if (!close_bitmaps())
	{
		unlock_extent_async(0, cloudfile_size, true, lock_idx);
		co_return false;
	}
	bool ret = true;

	std::string cloudfile_size_str("cloudfile_size");

	IFile* f_cloudfile_size = co_await kv_store.get_async(io, cloudfile_size_str, TransactionalKvStore::BitmapInfo::Unknown,
		TransactionalKvStore::Flag::disable_fd_cache | TransactionalKvStore::Flag::disable_throttling, -1);

	if (f_cloudfile_size == NULL)
	{
		Server->Log("Cannot open cloudfile_size", LL_ERROR);
		unlock_extent_async(0, orig_cloudfile_size, true, lock_idx);
		co_return false;
	}

	cloudfile_size = nsize;

	if (f_cloudfile_size->Write(0, reinterpret_cast<const char*>(&cloudfile_size), sizeof(cloudfile_size)) != sizeof(cloudfile_size))
	{
		Server->Log("Error writing new cloudfile size. " + os_last_error_str(), LL_ERROR);
		ret = false;
	}

	co_await kv_store.release_async(io, cloudfile_size_str);

	bool b = co_await io.run_in_threadpool([this]() {
		try
		{
			this->open_bitmaps();
		}
		catch (const std::runtime_error&)
		{
			return false;
		}
		return true;
		}, "resize open bm");

	if (!b)
		ret = false;

	unlock_extent_async(0, orig_cloudfile_size, true, lock_idx);

	co_return ret;
}
#endif //HAS_ASYNC

std::string CloudFile::getStats()
{
	return online_kv_store->get_stats();
}

int64 CloudFile::getCompBytes()
{
	return kv_store.get_comp_bytes();
}

void CloudFile::setDevNames(std::string bdev, std::string ldev)
{
	bdev_name = bdev;
	ldev_name = ldev;
}

void task_set_less_throttle()
{
#ifndef _WIN32
	static bool warned = false;
	if (prctl(PR_SET_IO_FLUSHER, 1, 0, 0, 0) != 0)
	{
		if (!warned)
		{
			warned = true;
			Server->Log("Setting PR_SET_IO_FLUSHER failed. " + os_last_error_str(), LL_WARNING);
		}
	}
#endif
}

void task_unset_less_throttle()
{
#ifndef _WIN32
	prctl(PR_SET_IO_FLUSHER, 0, 0, 0, 0);
#endif
}

/*_u32 CloudFile::prepare_zero_n_sqes(_u32 towrite)
{
	return towrite /zero_memfd_size + (towrite%zero_memfd_size ==0 ? 0 : 1);
}

std::vector<io_uring_sqe*> CloudFile::prepare_zeros(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io, _u32 towrite, _u32 peek_add)
{
	_u32 nwrite = prepare_zero_n_sqes(towrite);
	std::vector<io_uring_sqe*> sqes;	
	sqes.reserve(nwrite);
	while(nwrite>0)
	{
		io_uring_sqe* sqe = io.get_sqe(nwrite + peek_add);
		--nwrite;
		
		_u32 c_to_write = (std::min)(towrite, zero_memfd_size);
		io_uring_prep_splice(sqe, zero_memfd,
				0, fuse_io.pipe[1], -1, c_to_write,
				SPLICE_F_MOVE| SPLICE_F_NONBLOCK);
		sqe->flags |= IOSQE_FIXED_FILE | IOSQE_IO_LINK;
		sqes.push_back(sqe);
		towrite-=c_to_write;
	}
	assert(towrite==0);
	return sqes;
}*/

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<int> CloudFile::empty_pipe(fuse_io_context& io, fuse_io_context::FuseIo& fuse_io)
{
	int rc;
	do
	{
		io_uring_sqe* write_sqe = io.get_sqe();
		if (write_sqe == nullptr)
			co_return -1;

		io_uring_prep_splice(write_sqe, fuse_io.pipe[0],
			-1, get_file_fd(null_file->getOsHandle()), 0, 512,
			SPLICE_F_MOVE | SPLICE_F_FD_IN_FIXED | SPLICE_F_NONBLOCK);

		rc = co_await io.complete(write_sqe);
	} while (rc > 0);
	co_return 0;
}
#endif //HAS_ASYNC

#ifdef HAS_ASYNC
fuse_io_context::io_uring_task<bool> CloudFile::verify_pipe_empty(fuse_io_context& io, 
			fuse_io_context::FuseIo& fuse_io)
{
	struct io_uring_sqe *sqe;
    sqe = io.get_sqe();
    if(sqe==nullptr)
        co_return false;

    char ch;
    io_uring_prep_read(sqe, fuse_io.pipe[0],
            &ch, 1, 0);
    sqe->flags |= IOSQE_FIXED_FILE;

    int rc = co_await io.complete(sqe);

    co_return rc!=1;
}
#endif //HAS_ASYNC
