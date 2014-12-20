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
	return static_cast<_i64>(wfile->getSize());
}

bool FileWrapper::Seek( _i64 spos )
{
	return wfile->Seek(offset+spos);
}

_u32 FileWrapper::Write( const char* buffer, _u32 bsize, bool *has_error )
{
	return wfile->Write(buffer, bsize, has_error);
}

_u32 FileWrapper::Write( const std::string &tw, bool *has_error )
{
	return Write( tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 FileWrapper::Read( char* buffer, _u32 bsize, bool *has_error )
{
	size_t read;
	bool rc = wfile->Read(buffer, bsize, read);
	if(!rc)
	{
		if(has_error) *has_error=true;
		read=0;
	}
	return static_cast<_u32>(read);
}

std::string FileWrapper::Read( _u32 tr, bool *has_error )
{
	std::string ret;
	ret.resize(tr);
	_u32 gc=Read((char*)ret.c_str(), tr, has_error);
	if( gc<tr )
		ret.resize( gc );

	return ret;
}

FileWrapper::FileWrapper( IVHDFile* wfile, int64 offset )
	: wfile(wfile), offset(offset)
{

}

