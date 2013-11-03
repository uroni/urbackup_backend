/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "Server.h"
#include "file.h"
#include "stringtools.h"

File::~File()
{
	Close();
}

std::string File::getFilename(void)
{
	return Server->ConvertToUTF8(getFilenameW());
}

std::wstring File::getFilenameW(void)
{
	return fn;
}

bool DeleteFileInt(std::string pFilename)
{
	return _unlink(pFilename.c_str())==0?true:false;
}

bool DeleteFileInt(std::wstring pFilename)
{
#ifndef _WIN32
	return _unlink(Server->ConvertToUTF8(pFilename).c_str())==0?true:false;
#else
	return DeleteFileW(pFilename.c_str())!=0;
#endif
}
