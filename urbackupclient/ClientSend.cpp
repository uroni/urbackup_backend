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

#include "ClientSend.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Server.h"

ClientSend::ClientSend(IPipe *pPipe, unsigned int bsize, unsigned int nbufs)
{
	pipe=pPipe;
	bufmgr=new CBufMgr2(nbufs, bsize);
	mutex=Server->createMutex();
	cond=Server->createCondition();
	exit=false;
	has_error=false;
}

ClientSend::~ClientSend(void)
{
	delete bufmgr;
	Server->destroy(mutex);
}

void ClientSend::operator()(void)
{
	while(true)
	{
		BufferItem item;
		bool has_item=false;
		bool do_exit;
		{
			IScopedLock lock(mutex);
			do_exit=exit;
			if(tqueue.empty() && exit==false)
				cond->wait(&lock);
			if(!tqueue.empty())
			{
				item=tqueue.front();
				tqueue.pop();
				has_item=true;
			}
		}
		
		if(has_item)
		{
			bool b=pipe->Write(item.buf, item.bsize);
			if(!b)
				has_error=true;

			bufmgr->releaseBuffer(item.buf);
		}
		else if(do_exit)
		{
			break;
		}
	}
}

char *ClientSend::getBuffer(void)
{
	return bufmgr->getBuffer();
}

std::vector<char*> ClientSend::getBuffers(unsigned int nbufs)
{
	return bufmgr->getBuffers(nbufs);
}

void ClientSend::sendBuffer(char* buf, size_t bsize)
{
	IScopedLock lock(mutex);
	BufferItem item;
	item.buf=buf;
	item.bsize=bsize;
	tqueue.push(item);
	cond->notify_all();
}

void ClientSend::freeBuffer(char *buf)
{
	bufmgr->releaseBuffer(buf);
}

void ClientSend::doExit(void)
{
	IScopedLock lock(mutex);
	exit=true;
	cond->notify_all();
}

size_t ClientSend::getQueueSize(void)
{
	IScopedLock lock(mutex);
	return tqueue.size();
}

bool ClientSend::hasError(void)
{
	return has_error;
}