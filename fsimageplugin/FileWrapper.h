#pragma once

#include "../Interface/File.h"
#include "../Interface/Types.h"
#include <string>
#include "IVHDFile.h"

class FileWrapper : public IFile
{
public:
	FileWrapper(IVHDFile* wfile, int64 offset);

	virtual std::string Read(_u32 tr, bool *has_error);

	virtual std::string Read(int64 spos, _u32 tr, bool *has_error);

	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error);

	virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error);

	virtual _u32 Write(const std::string &tw, bool *has_error);

	virtual _u32 Write(int64 spos, const std::string &tw, bool *has_error);

	virtual _u32 Write(const char* buffer, _u32 bsize, bool *has_error);

	virtual _u32 Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error);

	virtual bool Seek(_i64 spos);

	virtual _i64 Size(void);

	virtual _i64 RealSize();

	virtual std::string getFilename(void);

	virtual bool PunchHole( _i64 spos, _i64 size );

	virtual bool Sync();

private:
	int64 offset;
	IVHDFile* wfile;
};