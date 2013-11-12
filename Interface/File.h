#ifndef IFILE_H
#define IFILE_H

#include <string>

#include "Types.h"
#include "Object.h"
#include "Server.h"

const int MODE_READ=0;
const int MODE_WRITE=1;
const int MODE_APPEND=2;
const int MODE_RW=3;
const int MODE_RW_CREATE=5;
const int MODE_READ_DEVICE=6;
const int MODE_READ_SEQUENTIAL=7;
const int MODE_READ_SEQUENTIAL_BACKUP=8;
const int MODE_RW_SEQUENTIAL=9;
//Linux only
const int MODE_RW_READNONE=10;

class IFile : public IObject
{
public:
	virtual std::string Read(_u32 tr)=0;
	virtual _u32 Read(char* buffer, _u32 bsize)=0;
	virtual _u32 Write(const std::string &tw)=0;
	virtual _u32 Write(const char* buffer, _u32 bsize)=0;
	virtual bool Seek(_i64 spos)=0;
	virtual _i64 Size(void)=0;
	
	virtual std::string getFilename(void)=0;
	virtual std::wstring getFilenameW(void)=0;
};

class ScopedDeleteFile
{
public:
	ScopedDeleteFile(IFile *file)
		: file(file) {}
	~ScopedDeleteFile(void){
		del();
	}
	void clear() {
		del();
	}
	void reset(IFile *pfile) {
		del();
		file=pfile;
	}
	void release() {
		file=NULL;
	}
private:
	void del() {
		if(file!=NULL) {
			std::wstring tmpfn=file->getFilenameW();
			file->Remove();
			Server->deleteFile(tmpfn);
		}
		file=NULL;
	}
	IFile *file;
};

#endif //IFILE_H