#pragma once

#include "Object.h"
#include "File.h"
#include "../urbackupcommon/os_functions.h"

#ifdef HAS_ASYNC
#include "../clouddrive/fuse_io_context.h"
#endif


class IBackupFileSystem : public IObject
{
public:
	virtual bool hasError() = 0;

	virtual IFsFile* openFile(const std::string& path, int mode) = 0;

#ifdef HAS_ASYNC
	virtual fuse_io_context::io_uring_task<IFsFile*> openFileAsync(fuse_io_context& io, const std::string& path, int mode) = 0;

	virtual fuse_io_context::io_uring_task<int> getFileTypeAsync(fuse_io_context& io, const std::string& path) = 0;

	virtual void closeAsync(fuse_io_context& io, IFsFile* fd) = 0;

	virtual fuse_io_context::io_uring_task<bool> deleteFileAsync(fuse_io_context& io, const std::string& path) = 0;
#endif

	virtual bool reflinkFile(const std::string& source, const std::string& dest) = 0;

	virtual bool createDir(const std::string& path) = 0;

	virtual bool deleteFile(const std::string& path) = 0;

	virtual int getFileType(const std::string& path) = 0;

	virtual bool sync(const std::string& path) = 0;

	virtual std::vector<SFile> listFiles(const std::string& path) = 0;

	virtual bool createSubvol(const std::string& path) = 0;

	virtual bool createSnapshot(const std::string& src_path,
		const std::string& dest_path) = 0;

	virtual bool deleteSubvol(const std::string& path) = 0;

	virtual bool rename(const std::string& src_name,
		const std::string& dest_name) = 0;

	virtual bool removeDirRecursive(const std::string& path) = 0;

	virtual bool directoryExists(const std::string& path) = 0;

	virtual bool linkSymbolic(const std::string& target, const std::string& lname) = 0;

	virtual bool copyFile(const std::string& src, const std::string& dst,
		bool flush=false, std::string* error_str=nullptr) = 0;

	virtual int64 totalSpace() = 0;

	virtual int64 freeSpace() = 0;

	virtual int64 freeMetadataSpace() = 0;

	virtual int64 unallocatedSpace() = 0;

	virtual bool forceAllocMetadata() = 0;

	virtual bool balance(int usage, size_t limit, bool metadata, bool& enospc, size_t& relocated) = 0;

	virtual std::string fileSep() = 0;

	virtual std::string filePath(IFile* f) = 0;

	virtual bool getXAttr(const std::string& path, const std::string& key, std::string& value) = 0;

	virtual bool setXAttr(const std::string& path, const std::string& key, const std::string& val) = 0;

	virtual std::string getName() = 0;

	virtual IFile* getBackingFile() = 0;

	virtual std::string lastError() = 0;

	struct SChunk
	{
		SChunk()
			: offset(-1), len(0), metadata(false) {}

		SChunk(int64 offset, int64 len, bool metadata)
			: offset(offset), len(len), metadata(metadata) {}

		bool operator<(const SChunk& other) const
		{
			return offset < other.offset;
		}

		int64 offset;
		int64 len;
		int metadata;
	};

	virtual std::vector<SChunk> getChunks() = 0;
};
