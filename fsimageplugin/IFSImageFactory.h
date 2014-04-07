#include <string>

#include "../Interface/Types.h"
#include "../Interface/Plugin.h"

class IFilesystem;
class IVHDFile;

class IFSImageFactory : public IPlugin
{
public:
	virtual IFilesystem *createFilesystem(const std::wstring &pDev)=0;
	virtual void destroyFilesystem(IFilesystem *fs)=0;

	enum CompressionSetting
	{
		CompressionSetting_None=0,
		CompressionSetting_Zlib=1
	};

	virtual IVHDFile *createVHDFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize,
		unsigned int pBlocksize=2*1024*1024, bool fast_mode=false, CompressionSetting compress=CompressionSetting_None)=0;

	virtual IVHDFile *createVHDFile(const std::wstring &fn, const std::wstring &parent_fn,
		bool pRead_only, bool fast_mode=false, CompressionSetting compress=CompressionSetting_None)=0;

	virtual void destroyVHDFile(IVHDFile *vhd)=0;
};