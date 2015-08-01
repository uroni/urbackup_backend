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

#include "IndexFiles.h"

std::vector<std::string> IndexFiles::files;

const std::vector<std::string>& IndexFiles::getIndexFiles(void)
{
	return files;
}

void IndexFiles::addIndexFile( const std::string &fn)
{
	files.push_back(fn);
}

void add_default_indexfiles(void)
{
	IndexFiles::addIndexFile("index.htm");
	IndexFiles::addIndexFile("index.html");
}


