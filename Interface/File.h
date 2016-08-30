#ifndef IFILE_H
#define IFILE_H

#include <string>

#include "Types.h"
#include "Object.h"
#include "Server.h"

const int MODE_READ=0;
const int MODE_WRITE=1;
const int MODE_APPEND=2;
const int MODE_RW=3;
const int MODE_RW_CREATE=5;
const int MODE_READ_DEVICE=6;
const int MODE_READ_SEQUENTIAL=7;
const int MODE_READ_SEQUENTIAL_BACKUP=8;
const int MODE_RW_SEQUENTIAL=9;
const int MODE_RW_DEVICE = 11;
const int MODE_RW_RESTORE = 12;
const int MODE_RW_CREATE_RESTORE = 13;
const int MODE_RW_CREATE_DEVICE = 14;
const int MODE_READ_DEVICE_OVERLAPPED = 15;
//Linux only
const int MODE_RW_READNONE=10;

class IFile : public IObject
{
public:
	virtual std::string Read(_u32 tr, bool *has_error=NULL)=0;
	virtual std::string Read(int64 spos, _u32 tr, bool *has_error = NULL) = 0;
	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error=NULL)=0;
	virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error = NULL) = 0;
	virtual _u32 Write(const std::string &tw, bool *has_error=NULL)=0;
	virtual _u32 Write(int64 spos, const std::string &tw, bool *has_error = NULL) = 0;
	virtual _u32 Write(const char* buffer, _u32 bsiz, bool *has_error=NULL)=0;
	virtual _u32 Write(int64 spos, const char* buffer, _u32 bsiz, bool *has_error = NULL) = 0;
	virtual bool Seek(_i64 spos)=0;
	virtual _i64 Size(void)=0;
	virtual _i64 RealSize()=0;
	virtual bool PunchHole(_i64 spos, _i64 size) = 0;
	virtual bool Sync() = 0;
	
	virtual std::string getFilename(void)=0;
};

class IFsFile : public IFile
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
		SFileExtent()
			: offset(-1), size(-1), volume_offset(-1)
		{}

		int64 offset;
		int64 size;
		int64 volume_offset;
	};

	virtual void resetSparseExtentIter() = 0;
	virtual SSparseExtent nextSparseExtent() = 0;
	virtual bool Resize(int64 new_size, bool set_sparse=true) = 0;
	virtual std::vector<SFileExtent> getFileExtents(int64 starting_offset, int64 block_size, bool& more_data) = 0;
	virtual void* getOsHandle() = 0;
};

class ScopedDeleteFn
{
public:
	ScopedDeleteFn(std::string fn)
		: fn(fn) {}
	~ScopedDeleteFn(void) {
		del();
	}
	void clear() {
		del();
	}
	void reset(std::string pfn) {
		del();
		fn = pfn;
	}
	void release() {
		fn.clear();
	}
private:
	void del() {
		if (!fn.empty()) {
			Server->deleteFile(fn);
		}
		fn.clear();
	}
	std::string fn;
};

class ScopedDeleteFile
{
public:
	ScopedDeleteFile(IFile *file)
		: file(file) {}
	~ScopedDeleteFile(void){
		del();
	}
	void clear() {
		del();
	}
	void reset(IFile *pfile) {
		del();
		file=pfile;
	}
	void release() {
		file=NULL;
	}
private:
	void del() {
		if(file!=NULL) {
			std::string tmpfn=file->getFilename();
			file->Remove();
			Server->deleteFile(tmpfn);
		}
		file=NULL;
	}
	IFile *file;
};

namespace
{

std::string readToString(IFile* f)
{
	std::string ret;
	ret.resize(static_cast<size_t>(f->Size()));
	f->Seek(0);
	size_t pos = 0;
	while (pos < static_cast<size_t>(f->Size()) )
	{
		_u32 r = f->Read(&ret[pos], static_cast<_u32>(f->Size())-static_cast<_u32>(pos));
		if(r==0)
		{
			break;
		}
		pos += r;
	}
	if(pos!=f->Size())
	{
		ret.resize(pos);
	}
	return ret;
}

}

#endif //IFILE_H
