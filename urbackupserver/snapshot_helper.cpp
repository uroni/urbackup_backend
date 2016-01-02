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

#include "snapshot_helper.h"
#include "../Interface/Server.h"
#include <stdlib.h>

std::string SnapshotHelper::helper_name="urbackup_snapshot_helper";

bool SnapshotHelper::isAvailable(void)
{
	int rc=system((helper_name+" test").c_str());
	return rc==0;
}

bool SnapshotHelper::createEmptyFilesystem(std::string clientname, std::string name)
{
	int rc=system((helper_name+" create \""+(clientname)+"\" \""+(name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::snapshotFileSystem(std::string clientname, std::string old_name, std::string snapshot_name)
{
	int rc=system((helper_name+" snapshot \""+(clientname)+"\" \""+(old_name)+"\" \""+(snapshot_name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::removeFilesystem(std::string clientname, std::string name)
{
	int rc=system((helper_name+" remove \""+(clientname)+"\" \""+(name)+"\"").c_str());
	return rc==0;
}

bool SnapshotHelper::isSubvolume(std::string clientname, std::string name)
{
	int rc=system((helper_name+" issubvolume \""+(clientname)+"\" \""+(name)+"\"").c_str());
	return rc==0;
}

void SnapshotHelper::setSnapshotHelperCommand(std::string helper_command)
{
	helper_name=helper_command;
}