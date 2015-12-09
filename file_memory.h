#include <string>
#include <algorithm>
#include "Interface/File.h"

class CMemoryFile : public IFile
{
public:
	CMemoryFile();

	virtual std::string Read(_u32 tr);
	virtual _u32 Read(char* buffer, _u32 bsize);
	virtual _u32 Write(const std::string &tw);
	virtual _u32 Write(const char* buffer, _u32 bsize);
	virtual bool Seek(_i64 spos);
	virtual _i64 Size(void);
	virtual _i64 RealSize();
	
	virtual std::string getFilename(void);

private:
	std::string data;
	size_t pos;
};
