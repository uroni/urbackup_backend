#include "../Interface/Types.h"

class IVHDFile
{
public:
	virtual void Seek(uint64 offset)=0;
	virtual bool Read(char* buffer, size_t bsize, size_t &read)=0;
	virtual bool Write(char *buffer, size_t bsize)=0;
	virtual bool isOpen(void)=0;
	virtual uint64 getSize(void)=0;
	virtual std::wstring getFilename(void)=0;
	virtual bool has_sector(void)=0;
	virtual unsigned int getBlocksize()=0;

};