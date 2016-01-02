/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#include "OutputStream.h"

#ifndef _WIN32
#include <memory.h>
#endif

void CStringOutputStream::write(const std::string &tw)
{
	data+=tw;
}

std::string CStringOutputStream::getData(void)
{
	return data;
}

void CStringOutputStream::write(const char* buf, size_t count, ostream_type_t stream)
{
	size_t osize=data.size();
	data.resize(osize+count);
	memcpy(&data[osize], buf, count);
}