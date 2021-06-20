#include <string>
#include <algorithm>
#include <memory>
#include "Interface/File.h"
#include "Interface/SharedMutex.h"
#include <atomic>
#ifndef _WIN32
#include <sys/mman.h>
#endif

namespace
{
	template <class T>
	struct aligned_allocator {
		typedef T value_type;
		aligned_allocator() noexcept {}
		template <class U> aligned_allocator(const aligned_allocator<U>&) noexcept {}
		T* allocate(std::size_t n);
		void deallocate(T* p, std::size_t n);
	};

	template <class T, class U>
	constexpr bool operator== (const aligned_allocator<T>&, const aligned_allocator<U>&) noexcept
	{
		return true;
	}

	template <class T, class U>
	constexpr bool operator!= (const aligned_allocator<T>&, const aligned_allocator<U>&) noexcept
	{
		return false;
	}
}

class CMemoryFile : public IMemFile
{
public:
	CMemoryFile(const std::string& name, bool mlock_mem);
	~CMemoryFile();

	virtual std::string Read(_u32 tr, bool *has_error = NULL);
	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error = NULL);
	virtual _u32 Write(const std::string &tw, bool *has_error = NULL);
	virtual _u32 Write(const char* buffer, _u32 bsize, bool *has_error = NULL);
	virtual bool Seek(_i64 spos);
	virtual _i64 Size(void);
	virtual _i64 RealSize();
	virtual bool PunchHole( _i64 spos, _i64 size );
	virtual bool Sync();

	virtual std::string Read(int64 spos, _u32 tr, bool * has_error = NULL);
	virtual _u32 Read(int64 spos, char * buffer, _u32 bsize, bool * has_error = NULL);
	virtual _u32 Write(int64 spos, const std::string & tw, bool * has_error = NULL);
	virtual _u32 Write(int64 spos, const char * buffer, _u32 bsiz, bool * has_error = NULL);
	
	virtual std::string getFilename(void);

	virtual void resetSparseExtentIter();
	virtual SSparseExtent nextSparseExtent();
	virtual bool Resize(int64 new_size, bool set_sparse = true);
	virtual std::vector<SFileExtent> getFileExtents(int64 starting_offset, int64 block_size, bool& more_data, unsigned int flags);
	
	virtual os_file_handle getOsHandle(bool release_handle = false);

	virtual char* getDataPtr();

	void protect_mem();
	void unprotect_mem();

	static int get_page_size()
	{
		return page_size;
	}

	virtual IVdlVolCache* createVdlVolCache() override;
	virtual int64 getValidDataLength(IVdlVolCache* vol_cache) override;

private:

	std::vector<char, aligned_allocator<char> > data;
	std::unique_ptr<ISharedMutex> mutex;
	size_t pos;
	bool mlock_mem;
	std::string name;
	static int page_size;
	int memory_protected;
	std::unique_ptr<IMutex> mprotect_mutex;
};

namespace
{
	template <class T>
	T* aligned_allocator<T>::allocate(std::size_t n)
	{
#ifdef _WIN32
		return static_cast<T*>(malloc(n * sizeof(T)));
#else
		int page_size = CMemoryFile::get_page_size();
		size_t toalloc = n * sizeof(T);
		toalloc = ((toalloc + page_size - 1) / page_size) * page_size;

		T* ret = static_cast<T*>(mmap(NULL, toalloc, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		madvise(ret, toalloc, MADV_DONTDUMP);
		return ret;
#endif
	}

	template <class T>
	void aligned_allocator<T>::deallocate(T* p, std::size_t n) {
#ifdef _WIN32
		free(p);
#else
		int page_size = CMemoryFile::get_page_size();
		size_t toalloc = n * sizeof(T);
		toalloc = ((toalloc + page_size - 1) / page_size) * page_size;
		munmap(p, toalloc);
#endif
	}
}