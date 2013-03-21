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
#include "../urbackupcommon/os_functions.h"
#include "server_log.h"
#include "server_cleanup.h"

extern IFSImageFactory *image_fak;
const size_t free_space_lim=1000*1024*1024; //1000MB
const uint64 filebuf_lim=1000*1024*1024; //1000MB

ServerVHDWriter::ServerVHDWriter(IVHDFile *pVHD, unsigned int blocksize, unsigned int nbufs, int pClientid, bool use_tmpfiles)
{
	filebuffer=use_tmpfiles;

	clientid=pClientid;
	vhd=pVHD;
	if(filebuffer)
	{
		bufmgr=new CBufMgr2(nbufs, sizeof(FileBufferVHDItem)+blocksize);
	}
	else
	{
		bufmgr=new CBufMgr2(nbufs, blocksize);
	}

	if(filebuffer)
	{
		filebuf=new CFileBufMgr(false);
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
				if(tqueue.empty() && exit==false)
				{
					cond->wait(&lock);
				}
				do_exit=exit;
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
						FileBufferVHDItem *fbi=(FileBufferVHDItem*)(item.buf-sizeof(FileBufferVHDItem));
						fbi->pos=item.pos;
						fbi->bsize=item.bsize;
						writeRetry(currfile, (char*)fbi, sizeof(FileBufferVHDItem)+item.bsize);
						currfile_size+=item.bsize+sizeof(FileBufferVHDItem);

						if(currfile_size>filebuf_lim)
						{
							filebuf_writer->writeBuffer(currfile);
							currfile=filebuf->getBuffer();
							currfile_size=0;
						}
					}
				}

				freeBuffer(item.buf);
			}
			else if(do_exit)
			{
				break;
			}

			if(!filebuffer && written>=free_space_lim/2)
			{
				written=0;
				checkFreeSpaceAndCleanup();
			}
		}
	}
	if(filebuffer)
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

void ServerVHDWriter::checkFreeSpaceAndCleanup(void)
{
	std::wstring p;
	{
		IScopedLock lock(vhd_mutex);
		p=ExtractFilePath(vhd->getFilenameW());
	}
	int64 fs=os_free_space(os_file_prefix(p));
	if(fs!=-1 && fs <= free_space_lim )
	{
		Server->Log("Not enough free space. Waiting for cleanup...");
		if(!cleanupSpace())
		{
			Server->Log("Not enough free space.", LL_WARNING);
		}
	}
}

void ServerVHDWriter::writeVHD(uint64 pos, char *buf, unsigned int bsize)
{
	IScopedLock lock(vhd_mutex);
	vhd->Seek(pos);
	bool b=vhd->Write(buf, bsize)!=0;
	written+=bsize;
	if(!b)
	{
		int retry=3;
		for(int i=0;i<retry;++i)
		{
			Server->wait(100);
			Server->Log("Retrying writing to VHD file...");
			vhd->Seek(pos);
			if(vhd->Write(buf, bsize)==0)
			{
				Server->Log("Writing to VHD file failed");
			}
			else
			{
				return;
			}
		}

		std::wstring p=ExtractFilePath(vhd->getFilenameW());
		int64 fs=os_free_space(os_file_prefix(p));
		if(fs!=-1 && fs <= free_space_lim )
		{
			Server->Log("Not enough free space. Waiting for cleanup...");
			if(cleanupSpace())
			{
				vhd->Seek(pos);
				if(vhd->Write(buf, bsize)==0)
				{
					retry=3;
					for(int i=0;i<retry;++i)
					{
						Server->wait(100);
						Server->Log("Retrying writing to VHD file...");
						vhd->Seek(pos);
						if(vhd->Write(buf, bsize)==0)
						{
							Server->Log("Writing to VHD file failed");
						}
						else
						{
							return;
						}
					}

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
	if(filebuffer)
	{
		char *buf=bufmgr->getBuffer();
		if(buf!=NULL)
		{
			return buf+sizeof(FileBufferVHDItem);
		}
		else
		{
			return buf;
		}
	}
	else
	{
		return bufmgr->getBuffer();
	}
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
	if(filebuffer)
		bufmgr->releaseBuffer(buf-sizeof(FileBufferVHDItem));
	else
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

void ServerVHDWriter::setHasError(bool b)
{
	has_error=b;
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
	if(ServerCleanupThread::cleanupSpace(free_space_lim) )
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
	while( off<bsize && !has_error)
	{
		unsigned int r=f->Write(buf+off, bsize-off);
		off+=r;
		if(off<bsize)
		{
			Server->Log("Error writing to file \""+f->getFilename()+"\". Retrying", LL_WARNING);
			Server->wait(10000);
		}
	}
}

//-------------FilebufferWriter-----------------

ServerFileBufferWriter::ServerFileBufferWriter(ServerVHDWriter *pParent, unsigned int pBlocksize) : parent(pParent), blocksize(pBlocksize)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();
	exit=false;
	exit_now=false;
	written=free_space_lim;
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
	char *blockbuf=new char[blocksize+sizeof(FileBufferVHDItem)];
	unsigned int blockbuf_size=blocksize+sizeof(FileBufferVHDItem);

	while(!exit_now)
	{
		IFile* tmp;
		bool do_exit;
		bool has_item=false;
		{
			IScopedLock lock(mutex);
			while(fb_queue.empty() && exit==false)
			{
				cond->wait(&lock);
			}
			do_exit=exit;
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
				if(exit_now)
					break;

				if(!parent->hasError())
				{
					unsigned int tw=blockbuf_size;
					bool old_method=false;
					if(tw<sizeof(FileBufferVHDItem) || tmp->Read(blockbuf, tw)!=tw)
					{
						old_method=true;
					}
					else
					{
						FileBufferVHDItem *item=(FileBufferVHDItem*)blockbuf;
						if(tw==item->bsize+sizeof(FileBufferVHDItem) )
						{
							parent->writeVHD(item->pos, blockbuf+sizeof(FileBufferVHDItem), item->bsize);
							written+=item->bsize;
							tpos+=item->bsize+sizeof(FileBufferVHDItem);
						}
						else
						{
							old_method=true;
						}
					}

					if(old_method==true)
					{
						tmp->Seek(tpos);
						FileBufferVHDItem item;
						if(tmp->Read((char*)&item, sizeof(FileBufferVHDItem))!=sizeof(FileBufferVHDItem))
						{
							Server->Log("Error reading FileBufferVHDItem", LL_ERROR);
							exit_now=true;
							parent->setHasError(true);
							break;
						}
						tpos+=sizeof(FileBufferVHDItem);
						unsigned int tw=item.bsize;
						if(tpos+tw>tsize)
						{
							Server->Log("Size field is wrong", LL_ERROR);
							exit_now=true;
							parent->setHasError(true);
							break;
						}
						if(tw>blockbuf_size)
						{
							delete []blockbuf;
							blockbuf=new char[tw+sizeof(FileBufferVHDItem)];
							blockbuf_size=tw+sizeof(FileBufferVHDItem);
						}
						if(tmp->Read(blockbuf, tw)!=tw)
						{
							Server->Log("Error reading from tmp.f", LL_ERROR);
							exit_now=true;
							parent->setHasError(true);
							break;
						}
						parent->writeVHD(item.pos, blockbuf, tw);
						written+=tw;
						tpos+=item.bsize;
					}

					if( written>=free_space_lim/2)
					{
						written=0;
						parent->checkFreeSpaceAndCleanup();
					}
				}
				else
				{
					break;
				}
			}
			parent->freeFile(tmp);
		}
		else if(do_exit)
		{
			break;
		}
	}

	delete []blockbuf;
}

void ServerFileBufferWriter::doExit(void)
{
	IScopedLock lock(mutex);
	exit=true;
	cond->notify_all();
}

void ServerFileBufferWriter::doExitNow(void)
{
	IScopedLock lock(mutex);
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