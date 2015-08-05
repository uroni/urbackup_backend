#include "FileWrapper.h"

std::wstring FileWrapper::getFilenameW( void )
{
	return wfile->getFilenameW();
}

std::string FileWrapper::getFilename( void )
{
	return wfile->getFilename();
}

_i64 FileWrapper::RealSize()
{
	return static_cast<_i64>(wfile->usedSize());
}

_i64 FileWrapper::Size( void )
{
	return static_cast<_i64>(wfile->getSize())-offset;
}

bool FileWrapper::Seek( _i64 spos )
{
	return wfile->Seek(offset+spos);
}

_u32 FileWrapper::Write( const char* buffer, _u32 bsize )
{
	return wfile->Write(buffer, bsize);
}

_u32 FileWrapper::Write( const std::string &tw )
{
	return Write( tw.c_str(), (_u32)tw.size());
}

_u32 FileWrapper::Read( char* buffer, _u32 bsize )
{
	size_t read;
	bool rc = wfile->Read(buffer, bsize, read);
	if(!rc)
	{
		read=0;
	}
	return static_cast<_u32>(read);
}

std::string FileWrapper::Read( _u32 tr )
{
	std::string ret;
	ret.resize(tr);
	_u32 gc=Read((char*)ret.c_str(), tr);
	if( gc<tr )
		ret.resize( gc );

	return ret;
}

FileWrapper::FileWrapper( IVHDFile* wfile, int64 offset )
	: wfile(wfile), offset(offset)
{

}