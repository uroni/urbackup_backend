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

#include "../vld.h"
#include "CriticalSection.h"
#include "../Interface/Server.h"


    CriticalSection::CriticalSection()
    {
		if(Server!=NULL)
			mutex=Server->createMutex();
		else
			mutex=NULL;
        //::InitializeCriticalSection(&cs);
    }

    CriticalSection::~CriticalSection()
    {
        //::DeleteCriticalSection(&cs);
    	if(mutex!=NULL)
    		mutex->Remove();
    }

    void CriticalSection::Enter()
    {
		if(mutex==NULL)
			mutex=Server->createMutex();
        //::EnterCriticalSection(&cs);
		mutex->Lock();
    }

    /*bool CriticalSection::TryEnter()
    {
		BOOL ret=TryEnterCriticalSection(&cs);
		if(ret!=0)
			return true;
		else
			return false;
    }*/

    void CriticalSection::Leave()
    {
        //LeaveCriticalSection(&cs);
		mutex->Unlock();
    }
