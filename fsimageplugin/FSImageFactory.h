#pragma once

#include "IFSImageFactory.h"

class FSImageFactory : public IFSImageFactory
{
public:
	virtual IFilesystem *createFilesystem(const std::wstring &pDev, bool read_ahead);

	virtual IVHDFile *createVHDFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize,
		unsigned int pBlocksize=2*1024*1024, bool fast_mode=false, IFSImageFactory::ImageFormat format=IFSImageFactory::ImageFormat_VHD);

	virtual IVHDFile *createVHDFile(const std::wstring &fn, const std::wstring &parent_fn,
		bool pRead_only, bool fast_mode=false, IFSImageFactory::ImageFormat format=IFSImageFactory::ImageFormat_VHD);

	virtual void destroyVHDFile(IVHDFile *vhd);

private:
	bool isNTFS(char *buffer);
};
