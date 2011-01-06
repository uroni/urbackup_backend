#ifndef FILE_H
#define FILE_H

#include "Interface/File.h"

const int MODE_TEMP=4;

//#define MODE_STL //No 64bit
#ifdef _WIN32
#	define MODE_WIN
#elif LINUX
#	define MODE_LIN
#else
#	define MODE_STL
#	warning "using STL file access files>4GB are not supported"
#endif

#ifdef MODE_STL
#	include <fstream>
#endif
#ifdef MODE_WIN
#	include <windows.h>
#endif
#ifdef MODE_LIN
#ifndef _LARGEFILE64_SOURCE
#	define _LARGEFILE64_SOURCE
#endif
#	include <fcntl.h>
#	include <sys/stat.h>
#	include <stdlib.h>
#	include <unistd.h>
#	define _unlink unlink
#endif

class File : public IFile
{
public:
	~File();
	bool Open(std::wstring pfn, int mode=MODE_READ);
	bool OpenTemporaryFile(void);
	std::string Read(_u32 tr);
	_u32 Read(char* buffer, _u32 bsize);
	_u32 Write(const std::string &tw);
	_u32 Write(const char* buffer, _u32 bsize);
	bool Seek(_i64 spos);
	_i64 Size(void);
	void Close();
	
	std::string getFilename(void);
	std::wstring getFilenameW(void);
private:
#ifdef MODE_STL
	std::fstream fi;
#endif
#ifdef MODE_WIN
	HANDLE hfile;
#endif
#ifdef MODE_LIN
	int fd;
#endif
	std::wstring fn;
};

bool DeleteFileInt(std::string pFilename);
bool DeleteFileInt(std::wstring pFilename);

#endif //FILE_H

