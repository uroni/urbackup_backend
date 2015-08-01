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

#include "MIMEType.h"

std::map<std::string, std::string> MIMEType::types;

std::string MIMEType::getMIMEType(const std::string &extension)
{
	std::map<std::string, std::string>::iterator iter=types.find(extension);
	if( iter!=types.end() )
		return iter->second;
	else
		return "application/octet-stream";
}

void MIMEType::addMIMEType( const std::string &extension, const std::string &mimetype)
{
	types[extension]=mimetype;
}

void add_default_mimetypes(void)
{
	MIMEType::addMIMEType("htm", "text/html");
	MIMEType::addMIMEType("html", "text/html");
	MIMEType::addMIMEType("css", "text/css");
	MIMEType::addMIMEType("js", "text/javascript");
}