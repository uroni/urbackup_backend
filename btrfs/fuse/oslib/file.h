#ifndef FILE_H
#define FILE_H

#include "../../../Interface/File.h"

const int MODE_TEMP = 4;

//#define MODE_STL //No 64bit
#ifdef _WIN32
#	define MODE_WIN
#elif LINUX
#	define MODE_LIN
#else
#	define MODE_STL
#	warning "using STL file access files>4GB are not supported"
#endif

#ifdef MODE_STL
#	include <fstream>
#endif
#ifdef MODE_WIN
#	include <Windows.h>
#endif
#ifdef MODE_LIN
#if !defined(_LARGEFILE64_SOURCE) && !defined(__APPLE__)
#	define _LARGEFILE64_SOURCE
#endif
#	include <fcntl.h>
#	include <sys/stat.h>
#	include <stdlib.h>
#	include <unistd.h>
#	define _unlink unlink
#if defined(__FreeBSD__) || defined(__APPLE__)
#define off64_t off_t
#endif
#endif

#include <vector>
#include <string>

class alignas(16) File
{
public:
#pragma pack(push)
	struct SSparseExtent
	{
		SSparseExtent()
			: offset(-1), size(-1)
		{

		}

		SSparseExtent(int64 offset, int64 size)
			: offset(offset), size(size)
		{

		}

		bool operator<(const SSparseExtent& other) const
		{
			return offset < other.offset;
		}

		int64 offset;
		int64 size;
	};
#pragma pack(pop)

	struct SFileExtent
	{
		enum class Flags
		{
			None = 0,
			Shared = 1
		};

		SFileExtent()
			: offset(-1), size(-1), volume_offset(-1), flags(Flags::None)
		{}

		int64 offset;
		int64 size;
		int64 volume_offset;
		Flags flags;
	};

	enum class FiemapFlag
	{
		None = 0,
		Physical = 8,
		Noshared = 16
	};

#ifdef _WIN32
	typedef void* os_file_handle;
#else
	typedef int os_file_handle;
#endif

	File();
	~File();
	bool Open(std::string pfn, int mode=MODE_READ);
	bool Open(void *handle, const std::string& pFilename);
	std::string Read(_u32 tr, bool *has_error=NULL);
	std::string Read(int64 spos, _u32 tr, bool *has_error = NULL);
	_u32 Read(char* buffer, _u32 bsize, bool *has_error=NULL);
	_u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error = NULL);
	_u32 Write(const std::string &tw, bool *has_error=NULL);
	_u32 Write(int64 spos, const std::string &tw, bool *has_error = NULL);
	_u32 Write(const char* buffer, _u32 bsize, bool *has_error=NULL);
	_u32 Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error = NULL);
	bool Seek(_i64 spos);
	_i64 Size(void);
	_i64 RealSize();
	void Close();
	bool PunchHole( _i64 spos, _i64 size );
	bool Sync();
	bool Resize(int64 new_size, bool set_sparse=true);
	void resetSparseExtentIter();
	SSparseExtent nextSparseExtent();
	std::vector<SFileExtent> getFileExtents(int64 starting_offset, int64 block_size, bool& more_data, unsigned int flags);
	os_file_handle getOsHandle(bool release_handle = false);

	std::string getFilename(void);

private:
#ifdef MODE_STL
	std::fstream fi;
#endif
#ifdef MODE_WIN
	HANDLE hfile;
	bool is_sparse;
#endif
#ifdef MODE_LIN
	int fd;
#endif
	std::string fn;

#ifdef _WIN32
	bool setSparse();

	bool more_extents;
	std::vector<FILE_ALLOCATED_RANGE_BUFFER> res_extent_buffer;
	size_t curr_extent;
	int64 last_sparse_pos;
#else
	off64_t last_sparse_pos;
#endif
	
};


bool DeleteFileInt(std::string pFilename);

#endif //FILE_H

