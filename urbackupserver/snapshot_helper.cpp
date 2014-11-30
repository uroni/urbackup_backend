/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include "snapshot_helper.h"
#include "../Interface/Server.h"
#include <stdlib.h>

std::string SnapshotHelper::helper_name="urbackup_snapshot_helper";

bool SnapshotHelper::isAvailable(void)
{
	int rc=system((helper_name+" test").c_str());
	return rc==0;
}

bool SnapshotHelper::createEmptyFilesystem(std::wstring clientname, std::wstring name)
{
	int rc=system((helper_name+" create \""+Server->ConvertToUTF8(clientname)+"\" \""+Server->ConvertToUTF8(name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::snapshotFileSystem(std::wstring clientname, std::wstring old_name, std::wstring snapshot_name)
{
	int rc=system((helper_name+" snapshot \""+Server->ConvertToUTF8(clientname)+"\" \""+Server->ConvertToUTF8(old_name)+"\" \""+Server->ConvertToUTF8(snapshot_name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::removeFilesystem(std::wstring clientname, std::wstring name)
{
	int rc=system((helper_name+" remove \""+Server->ConvertToUTF8(clientname)+"\" \""+Server->ConvertToUTF8(name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::isSubvolume(std::wstring clientname, std::wstring name)
{
	int rc=system((helper_name+" issubvolume \""+Server->ConvertToUTF8(clientname)+"\" \""+Server->ConvertToUTF8(name)+"\"").c_str());
	return rc==0;
}

void SnapshotHelper::setSnapshotHelperCommand(std::string helper_command)
{
	helper_name=helper_command;
}