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

#include "../vld.h"
#include "FileServFactory.h"
#include "pluginmgr.h"
#include "../stringtools.h"

IPlugin *CFileServPluginMgr::createPluginInstance(str_map &params)
{
	return new FileServFactory();
}

void CFileServPluginMgr::destroyPluginInstance(IPlugin *plugin)
{
	FileServFactory *s=(FileServFactory*)plugin;
	delete s;
}

