#include "IFSImageFactory.h"

class FSImageFactory : public IFSImageFactory
{
public:
	virtual IFilesystem *createFilesystem(const std::wstring &pDev);
	virtual void destroyFilesystem(IFilesystem *fs);

	virtual IVHDFile *createVHDFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize, unsigned int pBlocksize=2*1024*1024);
	virtual IVHDFile *createVHDFile(const std::wstring &fn, const std::wstring &parent_fn, bool pRead_only);
	virtual void destroyVHDFile(IVHDFile *vhd);

private:
	bool isNTFS(char *buffer);
};