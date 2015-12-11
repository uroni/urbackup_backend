/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

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
	Seek(0);
}

bool FileWrapper::PunchHole( _i64 spos, _i64 size )
{
	return false;
}

bool FileWrapper::Sync()
{
	return false;
}

