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
#include "piped_process.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

void CPipedProcess::operator()(void)
{
	Server->Log("CPipedProcess - Thread", LL_DEBUG);
	std::string r;
	do
	{
		r=Read();
		if( r.size()>0 )
			output->Write(r);
	}
	while(r.size()>0 && stop_thread==false);
	Server->Log("Pipe broken", LL_DEBUG);


	
	output->Write("end");	
	Server->Log("CPipedProcess - Thread shutdown", LL_DEBUG);
	
#ifndef _WIN32
	IScopedLock lock(mutex);
	output->Write(nconvert(return_code));
#endif
	thread_stopped=true;
	is_open=false;
}

IPipe *CPipedProcess::getOutputPipe()
{
	return output;
}

bool CPipedProcess::isOpen()
{
	return is_open;
}

bool CPipedProcess::isOpenInt()
{
	return is_open;
}
