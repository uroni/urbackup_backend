#include <string>

#include "../Interface/Types.h"
#include "../Interface/Plugin.h"

class IFilesystem;
class IVHDFile;
class IFile;
class IReadOnlyBitmap;

class IFSImageFactory : public IPlugin
{
public:
	virtual IFilesystem *createFilesystem(const std::string &pDev, bool read_ahead, bool background_priority, std::string orig_letter)=0;

	enum ImageFormat
	{
		ImageFormat_VHD=0,
		ImageFormat_CompressedVHD=1,
		ImageFormat_RawCowFile=2
	};

	virtual IVHDFile *createVHDFile(const std::string &fn, bool pRead_only, uint64 pDstsize,
		unsigned int pBlocksize=2*1024*1024, bool fast_mode=false, ImageFormat compress=ImageFormat_VHD)=0;

	virtual IVHDFile *createVHDFile(const std::string &fn, const std::string &parent_fn,
		bool pRead_only, bool fast_mode=false, ImageFormat compress=ImageFormat_VHD)=0;

	virtual void destroyVHDFile(IVHDFile *vhd)=0;

	virtual IReadOnlyBitmap* createClientBitmap(const std::string& fn)=0;

	virtual IReadOnlyBitmap* createClientBitmap(IFile* bitmap_file)=0;
};
