#pragma once
#include <string>

#include "../Interface/Types.h"
#include "../Interface/Plugin.h"

class IFilesystem;
class IVHDFile;
class IFile;
class IReadOnlyBitmap;
class IFsNextBlockCallback;

class IFSImageFactory : public IPlugin
{
public:
	enum EReadaheadMode
	{
		EReadaheadMode_None = 0,
		EReadaheadMode_Thread = 1,
		EReadaheadMode_Overlapped = 2
	};

	struct SPartition
	{
		SPartition()
			: offset(-1), length(0) {}
		int64 offset;
		int64 length;
	};

	virtual IFilesystem *createFilesystem(const std::string &pDev, EReadaheadMode read_ahead,
		bool background_priority, std::string orig_letter, IFsNextBlockCallback* next_block_callback)=0;

	enum ImageFormat
	{
		ImageFormat_VHD=0,
		ImageFormat_CompressedVHD=1,
		ImageFormat_RawCowFile=2
	};

	virtual IVHDFile *createVHDFile(const std::string &fn, bool pRead_only, uint64 pDstsize,
		unsigned int pBlocksize=2*1024*1024, bool fast_mode=false, ImageFormat compress=ImageFormat_VHD)=0;

	virtual IVHDFile *createVHDFile(const std::string &fn, const std::string &parent_fn,
		bool pRead_only, bool fast_mode=false, ImageFormat compress=ImageFormat_VHD, uint64 pDstsize=0)=0;

	virtual void destroyVHDFile(IVHDFile *vhd)=0;

	virtual IReadOnlyBitmap* createClientBitmap(const std::string& fn)=0;

	virtual IReadOnlyBitmap* createClientBitmap(IFile* bitmap_file)=0;

	virtual bool initializeImageMounting() = 0;

	virtual std::vector<SPartition> readPartitions(IVHDFile *vhd, int64 offset, bool& gpt_style) = 0;
};
