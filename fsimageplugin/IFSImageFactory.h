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

	virtual IVHDFile *createVHDFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize, unsigned int pBlocksize=2*1024*1024)=0;
	virtual IVHDFile *createVHDFile(const std::wstring &fn, const std::wstring &parent_fn, bool pRead_only)=0;
	virtual void destroyVHDFile(IVHDFile *vhd)=0;
};