#include <string>

#include "../Interface/Types.h"
#include "../Interface/Plugin.h"

class IFilesystem;
class IVHDFile;

class IFSImageFactory : public IPlugin
{
public:
	virtual IFilesystem *createFilesystem(const std::wstring &pDev, bool read_ahead, bool background_priority, bool exclude_shadow_storage)=0;

	enum ImageFormat
	{
		ImageFormat_VHD=0,
		ImageFormat_CompressedVHD=1,
		ImageFormat_RawCowFile=2
	};

	virtual IVHDFile *createVHDFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize,
		unsigned int pBlocksize=2*1024*1024, bool fast_mode=false, ImageFormat compress=ImageFormat_VHD)=0;

	virtual IVHDFile *createVHDFile(const std::wstring &fn, const std::wstring &parent_fn,
		bool pRead_only, bool fast_mode=false, ImageFormat compress=ImageFormat_VHD)=0;

	virtual void destroyVHDFile(IVHDFile *vhd)=0;
};
