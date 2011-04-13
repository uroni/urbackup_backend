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

#ifndef CLIENT_ONLY

#include "server_writer.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Server.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../stringtools.h"
#include "os_functions.h"
#include "server_log.h"
#include "server_cleanup.h"

extern IFSImageFactory *image_fak;
const size_t free_space_lim=1000*1024*1024; //1000MB
const uint64 filebuf_lim=1000*1024*1024; //1000MB

ServerVHDWriter::ServerVHDWriter(IVHDFile *pVHD, unsigned int blocksize, unsigned int nbufs, int pClientid)
{
	filebuffer=true;

	clientid=pClientid;
	vhd=pVHD;
	bufmgr=new CBufMgr2(nbufs, blocksize);

	if(filebuffer)
	{
		filebuf=new CFileBufMgr(500, false);
		filebuf_writer=new ServerFileBufferWriter(this, blocksize);
		filebuf_writer_ticket=Server->getThreadPool()->execute(filebuf_writer);
		currfile=filebuf->getBuffer();
		currfile_size=0;
	}

	mutex=Server->createMutex();
	vhd_mutex=Server->createMutex();
	cond=Server->createCondition();
	exit=false;
	exit_now=false;
	has_error=false;
	written=free_space_lim;
}

ServerVHDWriter::~ServerVHDWriter(void)
{
	delete bufmgr;

	if(filebuffer)
	{
		delete filebuf_writer;
		delete filebuf;
	}

	Server->destroy(mutex);
	Server->destroy(vhd_mutex);
	Server->destroy(cond);
}

void ServerVHDWriter::operator()(void)
{
	{
		while(!exit_now)
		{
			BufferVHDItem item;
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
				if(!has_error)
				{
					if(!filebuffer)
					{
						writeVHD(item.pos, item.buf, item.bsize);
					}
					else
					{
						FileBufferVHDItem fbi;
						fbi.pos=item.pos;
						fbi.bsize=item.bsize;
						writeRetry(currfile, (char*)&fbi, sizeof(FileBufferVHDItem));
						currfile_size+=sizeof(FileBufferVHDItem);
						writeRetry(currfile, item.buf, item.bsize);
						currfile_size+=item.bsize;

						if(currfile_size>filebuf_lim)
						{
							filebuf_writer->writeBuffer(currfile);
							currfile=filebuf->getBuffer();
							currfile_size=0;
						}
					}
				}

				bufmgr->releaseBuffer(item.buf);
			}
			else if(do_exit)
			{
				break;
			}

			if(written>=free_space_lim/2)
			{
				written=0;
				std::wstring p=ExtractFilePath(vhd->getFilename());
				int64 fs=os_free_space(os_file_prefix()+p);
				if(fs!=-1 && fs <= free_space_lim )
				{
					Server->Log("Not enough free space. Waiting for cleanup...");
					if(!cleanupSpace())
					{
						Server->Log("Not enough free space.", LL_WARNING);
					}
				}
			}
		}
	}
	if(filebuf)
	{
		filebuf_writer->writeBuffer(currfile);

		if(!exit_now)
			filebuf_writer->doExit();
		else
			filebuf_writer->doExitNow();

		Server->getThreadPool()->waitFor(filebuf_writer_ticket);
	}

	image_fak->destroyVHDFile(vhd);
}

void ServerVHDWriter::writeVHD(uint64 pos, char *buf, unsigned int bsize)
{
	IScopedLock lock(vhd_mutex);
	vhd->Seek(pos);
	bool b=vhd->Write(buf, bsize);
	written+=bsize;
	if(!b)
	{
		std::wstring p=ExtractFilePath(vhd->getFilename());
		int64 fs=os_free_space(os_file_prefix()+p);
		if(fs!=-1 && fs <= free_space_lim )
		{
			Server->Log("Not enough free space. Waiting for cleanup...");
			if(cleanupSpace())
			{
				if(!vhd->Write(buf, bsize))
				{
					ServerLogger::Log(clientid, "FATAL: Writing failed after cleanup", LL_ERROR);
					has_error=true;
				}
			}
			else
			{
				has_error=true;
				Server->Log("FATAL: NOT ENOUGH free space. Cleanup failed.", LL_ERROR);
			}
		}
		else
		{
			has_error=true;
			ServerLogger::Log(clientid, "FATAL: Error writing to VHD-File.", LL_ERROR);
		}
	}
}

char *ServerVHDWriter::getBuffer(void)
{
	return bufmgr->getBuffer();
}

void ServerVHDWriter::writeBuffer(uint64 pos, char *buf, unsigned int bsize)
{
	IScopedLock lock(mutex);
	BufferVHDItem item;
	item.pos=pos;
	item.buf=buf;
	item.bsize=bsize;
	tqueue.push(item);
	cond->notify_all();
}

void ServerVHDWriter::freeBuffer(char *buf)
{
	bufmgr->releaseBuffer(buf);
}

void ServerVHDWriter::doExit(void)
{
	IScopedLock lock(mutex);
	exit=true;
	finish=true;
	cond->notify_all();
}

void ServerVHDWriter::doExitNow(void)
{
	IScopedLock lock(mutex);
	exit=true;
	exit_now=true;
	finish=true;
	cond->notify_all();
}

void ServerVHDWriter::doFinish(void)
{
	IScopedLock lock(mutex);
	finish=true;
	cond->notify_all();
}

size_t ServerVHDWriter::getQueueSize(void)
{
	IScopedLock lock(mutex);
	return tqueue.size();
}

bool ServerVHDWriter::hasError(void)
{
	return has_error;
}

IMutex * ServerVHDWriter::getVHDMutex(void)
{
	return vhd_mutex;
}

IVHDFile* ServerVHDWriter::getVHD(void)
{
	return vhd;
}

bool ServerVHDWriter::cleanupSpace(void)
{
	ServerLogger::Log(clientid, "Not enough free space. Cleaning up.", LL_INFO);
	ServerCleanupThread cleanup;
	if(!cleanup.do_cleanup(free_space_lim) )
	{
		ServerLogger::Log(clientid, "Could not free space for image. NOT ENOUGH FREE SPACE.", LL_ERROR);
		return false;
	}
	return true;
}

void ServerVHDWriter::freeFile(IFile *buf)
{
	filebuf->releaseBuffer(buf);
}

void ServerVHDWriter::writeRetry(IFile *f, char *buf, unsigned int bsize)
{
	unsigned int off=0;
	unsigned int r;
	while( (r=f->Write(buf+off, bsize-off))!=bsize-off)
	{
		off+=r;
		Server->Log("Error writing to file \""+f->getFilename()+"\". Retrying", LL_WARNING);
		Server->wait(10000);
	}
}

ServerFileBufferWriter::ServerFileBufferWriter(ServerVHDWriter *pParent, unsigned int pBlocksize) : parent(pParent), blocksize(pBlocksize)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();
	exit=false;
	exit_now=false;
}

ServerFileBufferWriter::~ServerFileBufferWriter(void)
{
	while(!fb_queue.empty())
	{
		parent->freeFile(fb_queue.front());
		fb_queue.pop();
	}
	Server->destroy(mutex);
	Server->destroy(cond);
}

void ServerFileBufferWriter::operator()(void)
{
	char *blockbuf=new char[blocksize];
	unsigned int blockbuf_size=blocksize;

	while(!exit_now)
	{
		IFile* tmp;
		bool has_item=false;
		{
			IScopedLock lock(mutex);
			while(fb_queue.empty() && exit==false)
			{
				cond->wait(&lock);
			}
			if(!fb_queue.empty())
			{
				has_item=true;
				tmp=fb_queue.front();
				fb_queue.pop();
			}
		}

		if(has_item)
		{
			tmp->Seek(0);
			uint64 tpos=0;
			uint64 tsize=tmp->Size();
			while(tpos<tsize)
			{
				if(!parent->hasError())
				{
					FileBufferVHDItem item;
					if(tmp->Read((char*)&item, sizeof(FileBufferVHDItem))!=sizeof(FileBufferVHDItem))
					{
						Server->Log("Error reading FileBufferVHDItem", LL_ERROR);
					}
					tpos+=sizeof(FileBufferVHDItem);
					unsigned int tw=item.bsize;
					if(tpos+tw>tsize)
					{
						Server->Log("Size field is wrong", LL_ERROR);
					}
					if(tw>blockbuf_size)
					{
						delete []blockbuf;
						blockbuf=new char[tw];
						blockbuf_size=tw;
					}
					if(tmp->Read(blockbuf, tw)!=tw)
					{
						Server->Log("Error reading from tmp.f", LL_ERROR);
					}
					parent->writeVHD(item.pos, blockbuf, tw);
					tpos+=item.bsize;
				}
				else
				{
					break;
				}
			}
			parent->freeFile(tmp);
		}
		else if(exit)
		{
			break;
		}
	}

	delete []blockbuf;
}

void ServerFileBufferWriter::doExit(void)
{
	exit=true;
	cond->notify_all();
}

void ServerFileBufferWriter::doExitNow(void)
{
	exit_now=true;
	exit=true;
	cond->notify_all();
}

void ServerFileBufferWriter::writeBuffer(IFile *buf)
{
	IScopedLock lock(mutex);
	fb_queue.push(buf);
	cond->notify_all();
}

#endif //CLIENT_ONLY