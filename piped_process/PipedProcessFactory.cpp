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
#include "PipedProcessFactory.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

#include "piped_process.h"

IPipedProcess* PipedProcessFactory::createProcess(const std::string &cmdline)
{
	CPipedProcess *pp=new CPipedProcess(cmdline);
	if( pp->isOpenInt()==false )
	{
		Server->Log("Process not open", LL_DEBUG);
		delete pp;
		return NULL;
	}

	Server->Log("executing in tp", LL_DEBUG);
	Server->getThreadPool()->execute(pp);
	Server->Log("done...", LL_DEBUG);

	return pp;
}