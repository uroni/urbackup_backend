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

#include "filesystem.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <memory.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#endif
#include "../Interface/Thread.h"
#include "../Interface/Condition.h"
#include "../Interface/ThreadPool.h"
#include <assert.h>


namespace
{
	const size_t readahead_num_blocks = 5120;
	const size_t max_idle_buffers = readahead_num_blocks;
	const size_t readahead_low_level_blocks = readahead_num_blocks/2;
	const size_t slow_read_warning_seconds = 5 * 60;
	const size_t max_read_wait_seconds = 60 * 60;


	class ReadaheadThread : public IThread
	{
	public:
		ReadaheadThread(Filesystem& fs, bool background_priority)
			: fs(fs), 
			  mutex(Server->createMutex()),
			  start_readahead_cond(Server->createCondition()),
			  read_block_cond(Server->createCondition()),
			  current_block(-1),
			  do_stop(false),
			  readahead_miss(false),
			  background_priority(background_priority)
		{

		}
		~ReadaheadThread()
		{
			for(std::map<int64, char*>::iterator it=read_blocks.begin();
				it!=read_blocks.end();++it)
			{
				fs.releaseBuffer(it->second);
			}
		}


		void operator()()
		{
			ScopedBackgroundPrio background_prio(false);
			if(background_priority)
			{
#ifndef _DEBUG
				background_prio.enable();
#endif
			}

			IScopedLock lock(mutex.get());
			while(!do_stop)
			{
				if(read_blocks.size()>=readahead_num_blocks)
				{
					while(read_blocks.size()>readahead_low_level_blocks
						&& !readahead_miss)
					{
						start_readahead_cond->wait(&lock);

						if(do_stop) break;
					}
				}

				if(do_stop) break;

				while(current_block==-1)
				{
					start_readahead_cond->wait(&lock);

					if(do_stop) break;
				}

				if(do_stop) break;

				std::map<int64, char*>::iterator it;
				do 
				{
					it = read_blocks.find(current_block);
					if(it!=read_blocks.end())
					{
						current_block = fs.nextBlockInt(current_block);
					}
				} while (it!=read_blocks.end());

				if(current_block!=-1)
				{
					int64 l_current_block = current_block;
					lock.relock(NULL);
					char* buf = fs.readBlockInt(l_current_block, false);
					lock.relock(mutex.get());

					read_blocks[l_current_block] = buf;

					current_block = fs.nextBlockInt(l_current_block);

					if(readahead_miss)
					{
						read_block_cond->notify_all();
						readahead_miss=false;
					}
				}
			}
		}

		char* getBlock(int64 block)
		{
			IScopedLock lock(mutex.get());

			clearUnusedReadahead(block);

			char* ret=NULL;
			while(ret==NULL)
			{
				std::map<int64, char*>::iterator it=read_blocks.find(block);

				if(it!=read_blocks.end())
				{
					ret = it->second;
					read_blocks.erase(it);
				}
				else
				{
					readaheadFromInt(block);
					readahead_miss=true;
					read_block_cond->wait(&lock);
				}
			}

			return ret;
		}

		void stop()
		{
			IScopedLock lock(mutex.get());
			do_stop=true;
			start_readahead_cond->notify_all();
		}

	private:

		void readaheadFromInt(int64 pBlock)
		{
			current_block=pBlock;
			start_readahead_cond->notify_all();
		}

		void clearUnusedReadahead(int64 pBlock)
		{
			for(std::map<int64, char*>::iterator it=read_blocks.begin();
				it!=read_blocks.end();)
			{
				if(it->first<pBlock)
				{
					std::map<int64, char*>::iterator todel = it;
					++it;
					fs.releaseBuffer(todel->second);
					read_blocks.erase(todel);
				}
				else
				{
					break;
				}
			}
		}

		std::auto_ptr<IMutex> mutex;
		std::auto_ptr<ICondition> start_readahead_cond;
		std::auto_ptr<ICondition> read_block_cond;
		Filesystem& fs;

		std::map<int64, char*> read_blocks;

		bool readahead_miss;

		int64 current_block;

		bool do_stop;

		bool background_priority;
	};

	int64 getLastSystemError()
	{
		int64 last_error;
#ifdef _WIN32
		last_error=GetLastError();
#else
		last_error=errno;
#endif
		return last_error;
	}

#ifdef _WIN32
	void WINAPI FileIOCompletionRoutine(__in DWORD dwErrorCode,
		__in    DWORD dwNumberOfBytesTransfered,
		__inout LPOVERLAPPED lpOverlapped)
	{
		LARGE_INTEGER li;
		li.LowPart = lpOverlapped->Offset;
		li.HighPart = lpOverlapped->OffsetHigh;

		SNextBlock* block = reinterpret_cast<SNextBlock*>(lpOverlapped->hEvent);

		block->fs->overlappedIoCompletion(block, dwErrorCode, dwNumberOfBytesTransfered, li.QuadPart);
	}
#endif
}

Filesystem::Filesystem(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, IFsNextBlockCallback* next_block_callback)
	: buffer_mutex(Server->createMutex()), next_block_callback(next_block_callback), overlapped_next_block(-1),
	num_uncompleted_blocks(0), errcode(0)
{
	has_error=false;

	if (read_ahead == IFSImageFactory::EReadaheadMode_Overlapped)
	{
		dev = Server->openFile(pDev, MODE_READ_DEVICE_OVERLAPPED);
	}
	else
	{
		dev = Server->openFile(pDev, MODE_READ_DEVICE);
	}
	if(dev==NULL)
	{
		errcode = getLastSystemError();
		Server->Log("Error opening device file. Errorcode: "+convert(errcode), LL_ERROR);
		has_error=true;
	}
	own_dev=true;
}

Filesystem::Filesystem(IFile *pDev, IFsNextBlockCallback* next_block_callback)
	: dev(pDev), next_block_callback(next_block_callback), overlapped_next_block(-1),
	num_uncompleted_blocks(0), errcode(0)
{
	has_error=false;
	own_dev=false;
}

Filesystem::~Filesystem()
{
	assert(readahead_thread.get()==NULL);

	if(dev!=NULL && own_dev)
	{
		Server->destroy(dev);
	}
	
	for(size_t i=0;i<buffers.size();++i)
	{
		delete[] buffers[i];
	}

	if (read_ahead_mode == IFSImageFactory::EReadaheadMode_Overlapped)
	{
		for (size_t i = 0; i < next_blocks.size(); ++i)
		{
#ifdef _WIN32
			VirtualFree(next_blocks[i].buffer, 0, MEM_RELEASE);
#else
			delete[] next_blocks[i].buffer;
#endif
		}
	}
}

bool Filesystem::hasBlock(int64 pBlock)
{
	const unsigned char *bitmap=getBitmap();
	int64 blocksize=getBlocksize();

	size_t bitmap_byte=(size_t)(pBlock/8);
	size_t bitmap_bit=pBlock%8;

	unsigned char b=bitmap[bitmap_byte];

	bool has_bit=((b & (1<<bitmap_bit))>0);

	return has_bit;
}

char* Filesystem::readBlock(int64 pBlock)
{
	return readBlockInt(pBlock, readahead_thread.get()!=NULL);
}

char* Filesystem::readBlockInt(int64 pBlock, bool use_readahead)
{
	const unsigned char *bitmap=getBitmap();
	int64 blocksize=getBlocksize();

	size_t bitmap_byte=(size_t)(pBlock/8);
	size_t bitmap_bit=pBlock%8;

	unsigned char b=bitmap[bitmap_byte];

	bool has_bit=((b & (1<<bitmap_bit))>0);

	if(!has_bit)
		return NULL;
	
	if (read_ahead_mode == IFSImageFactory::EReadaheadMode_Overlapped)
	{
		SNextBlock* next_block = completionGetBlock(pBlock);
		if (next_block == NULL)
			return NULL;
		used_next_blocks[next_block->buffer] = next_block;
		return next_block->buffer;
	}
	else if (read_ahead_mode == IFSImageFactory::EReadaheadMode_Thread)
	{
		return readahead_thread->getBlock(pBlock);
	}
	else
	{
		bool b=dev->Seek(pBlock*blocksize);
		if(!b)
		{
			Server->Log("Seeking in device failed -1", LL_ERROR);
			has_error=true;
			return NULL;
		}
		char* buf = getBuffer();
		if(!readFromDev(buf, (_u32)blocksize) )
		{
			Server->Log("Reading from device failed -1", LL_ERROR);
			has_error=true;
			return NULL;
		}

		return buf;
	}
}

int64 Filesystem::nextBlockInt(int64 curr_block)
{
	return next_block_callback->nextBlock(curr_block);
}

#ifdef _WIN32
void Filesystem::overlappedIoCompletion(SNextBlock * block, DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, int64 offset)
{
	--num_uncompleted_blocks;

	if (dwErrorCode != ERROR_SUCCESS)
	{
		errcode = dwErrorCode;
		Server->Log("Reading from device at position "+convert(offset)+" failed. System error code "+convert((int64)dwErrorCode), LL_ERROR);
		has_error = true;
		block->state = ENextBlockState_Error;
	}
	else if (dwNumberOfBytesTransfered != getBlocksize())
	{
		Server->Log("Reading from device at position " + convert(offset) + " failed. OS returned only "+convert((int64)dwNumberOfBytesTransfered)+" bytes", LL_ERROR);
		has_error = true;
		block->state = ENextBlockState_Error;
	}
	else
	{
		block->state = ENextBlockState_Ready;
	}
}
#endif

int64 Filesystem::nextBlock(int64 curr_block)
{
	int64 size_blocks = getSize() / getBlocksize();

	while (curr_block + 1<size_blocks)
	{
		++curr_block;

		if (hasBlock(curr_block))
		{
			return curr_block;
		}
	}

	return -1;
}

void Filesystem::slowReadWarning(int64 passed_time_ms, int64 curr_block)
{
	Server->Log("Waiting for block " + convert(curr_block) + " since " + PrettyPrintTime(passed_time_ms), LL_WARNING);
}

void Filesystem::waitingForBlockCallback(int64 curr_block)
{
}

int64 Filesystem::getOsErrorCode()
{
	return errcode;
}

std::vector<int64> Filesystem::readBlocks(int64 pStartBlock, unsigned int n,
	const std::vector<char*>& buffers, unsigned int buffer_offset)
{
	_u32 blocksize=(_u32)getBlocksize();
	std::vector<int64> ret;

	size_t currbuf = 0;

	for(int64 i=pStartBlock;i<pStartBlock+n;++i)
	{
		if (read_ahead_mode == IFSImageFactory::EReadaheadMode_Overlapped)
		{
			if (hasBlock(i))
			{
				SNextBlock* next_block = completionGetBlock(i);
				if (next_block != NULL)
				{
					memcpy(buffers[currbuf] + buffer_offset, next_block->buffer, blocksize);
					++currbuf;
					ret.push_back(i);

					free_next_blocks.push(next_block);
				}
				else if (has_error)
				{
					break;
				}
			}
		}
		else
		{
			char* buf = readBlock(i);
			if (buf != NULL)
			{
				memcpy(buffers[currbuf] + buffer_offset, buf, blocksize);
				++currbuf;
				ret.push_back(i);

				releaseBuffer(buf);
			}
			else if (has_error)
			{
				break;
			}
		}
	}

	return ret;
}

bool Filesystem::readFromDev(char *buf, _u32 bsize)
{
	assert(read_ahead_mode != IFSImageFactory::EReadaheadMode_Overlapped);

	int tries=20;
	_u32 rc=dev->Read(buf, bsize);
	while(rc<bsize)
	{
		Server->wait(200);
		errcode = getLastSystemError();
		Server->Log("Reading from device failed. Retrying. Errorcode: "+convert(errcode), LL_WARNING);
		rc+=dev->Read(buf+rc, bsize-rc);
		--tries;
		if(tries<0
			&& rc<bsize)
		{
			errcode = getLastSystemError();
			Server->Log("Reading from device failed. Errorcode: "+convert(errcode), LL_ERROR);
			return false;
		}
	}
	return true;
}

void Filesystem::initReadahead(IFSImageFactory::EReadaheadMode read_ahead, bool background_priority)
{
	read_ahead_mode = read_ahead;

	if (read_ahead== IFSImageFactory::EReadaheadMode_Overlapped)
	{
		next_blocks.resize(readahead_num_blocks);

		for (size_t i = 0; i < next_blocks.size(); ++i)
		{
#ifdef _WIN32
			next_blocks[i].buffer = reinterpret_cast<char*>(VirtualAlloc(NULL, getBlocksize(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
			next_blocks[i].buffer = new char[getBlocksize()];
#endif
			if (next_blocks[i].buffer == NULL)
			{
				has_error = true;
			}
			next_blocks[i].state = ENextBlockState_Queued;
			next_blocks[i].fs = this;

			free_next_blocks.push(&next_blocks[i]);
		}

#ifdef _WIN32
		IFsFile* fs_dev = dynamic_cast<IFsFile*>(dev);
		if (fs_dev != NULL)
		{
			hVol = fs_dev->getOsHandle();
		}
#endif
	}
	else if (read_ahead == IFSImageFactory::EReadaheadMode_Thread)
	{
		readahead_thread.reset(new ReadaheadThread(*this, background_priority));
		readahead_thread_ticket = Server->getThreadPool()->execute(readahead_thread.get(), "device readahead");
	}

	if (read_ahead != IFSImageFactory::EReadaheadMode_None
		&& next_block_callback==NULL)
	{
		next_block_callback = this;
	}
}

bool Filesystem::queueOverlappedReads(bool force_queue)
{
	bool ret = false;
	if (usedNextBlocks() < readahead_low_level_blocks
		|| force_queue)
	{
		int64 queue_starttime = Server->getTimeMS();
		unsigned int blocksize = static_cast<unsigned int>(getBlocksize());
		while (!free_next_blocks.empty()
			&& overlapped_next_block>=0)
		{
			SNextBlock* block = free_next_blocks.top();
			free_next_blocks.pop();
			block->state = ENextBlockState_Queued;
			++num_uncompleted_blocks;
			queued_next_blocks[overlapped_next_block] = block;
#ifdef _WIN32
			memset(&block->ovl, 0, sizeof(block->ovl));
			LARGE_INTEGER offset;
			offset.QuadPart = overlapped_next_block*getBlocksize();
			block->ovl.Offset = offset.LowPart;
			block->ovl.OffsetHigh = offset.HighPart;
			block->ovl.hEvent = block;

			BOOL b = ReadFileEx(hVol, block->buffer, blocksize, &block->ovl, FileIOCompletionRoutine);
			if (!b)
			{
				--num_uncompleted_blocks;
				Server->Log("Error starting overlapped read operation. System error code " + convert(getLastSystemError()), LL_ERROR);
				has_error = true;
				return false;
			}
#endif	
			ret = true;
			overlapped_next_block = next_block_callback->nextBlock(overlapped_next_block);

			if (Server->getTimeMS() - queue_starttime > 500)
			{
				return true;
			}
		}
	}

	return ret;
}

bool Filesystem::waitForCompletion(unsigned int wtimems)
{
#ifdef _WIN32
	return SleepEx(wtimems, TRUE)== WAIT_IO_COMPLETION;
#else
	return false;
#endif
}

size_t Filesystem::usedNextBlocks()
{
	return next_blocks.size() - free_next_blocks.size();
}

SNextBlock* Filesystem::completionGetBlock(int64 pBlock)
{
	std::map<int64, SNextBlock*>::iterator it = queued_next_blocks.find(pBlock);
	if (it == queued_next_blocks.end())
	{
		do
		{
			overlapped_next_block = pBlock;
			queueOverlappedReads(true);
			it = queued_next_blocks.find(pBlock);
			if (it == queued_next_blocks.end())
			{
				waitForCompletion(100);
				it = queued_next_blocks.find(pBlock);
			}
		} while (it == queued_next_blocks.end());
	}
	else
	{
		queueOverlappedReads(false);
	}

	SNextBlock* next_block = it->second;
	queued_next_blocks.erase(it);

	size_t nwait = 0;
	while (next_block->state == ENextBlockState_Queued)
	{
		if (nwait > 0)
		{
			next_block_callback->waitingForBlockCallback(pBlock);
		}

		if (!waitForCompletion(1000))
		{
			++nwait;
		}

		if (nwait == slow_read_warning_seconds)
		{
			next_block_callback->slowReadWarning(nwait * 1000, pBlock);
		}
		if (nwait >= max_read_wait_seconds)
		{
			errcode = fs_error_read_timeout;
			has_error = true;
			return NULL;
		}
	}

	if (next_block->state != ENextBlockState_Ready)
	{
		free_next_blocks.push(next_block);
		return NULL;
	}

	return next_block;
}

int64 Filesystem::calculateUsedSpace(void)
{
	const unsigned char *bm=getBitmap();
	uint64 blocks1=getSize()/getBlocksize();
	unsigned int tadd=(unsigned int)(blocks1/8);;
	if( blocks1%8>0)
		++tadd;

	const unsigned char *target=bm+tadd;
	int64 used_blocks=0;
	uint64 blocknum=0;
	while(bm!=target)
	{
		const unsigned char b=*bm;
		for(int i=0;i<8;++i)
		{
			if(blocknum>=blocks1)
				break;
			if( (b & (1<<i))>0 )
			{
				++used_blocks;
			}
			++blocknum;
		}
		++bm;
	}
	return used_blocks*getBlocksize();
}

bool Filesystem::hasError(void)
{
	return has_error;
}

char* Filesystem::getBuffer()
{
	assert(read_ahead_mode != IFSImageFactory::EReadaheadMode_Overlapped);

	{
		IScopedLock lock(buffer_mutex.get());

		if(!buffers.empty())
		{
			char* ret = buffers[buffers.size()-1];
			buffers.erase(buffers.begin()+buffers.size()-1);
			return ret;
		}
	}

	return new char[getBlocksize()];
}

void Filesystem::releaseBuffer(char* buf)
{
	if(read_ahead_mode == IFSImageFactory::EReadaheadMode_Overlapped)
	{
		std::map<char*, SNextBlock*>::iterator it = used_next_blocks.find(buf);

		if (it != used_next_blocks.end())
		{
			free_next_blocks.push(it->second);
			used_next_blocks.erase(it);
			return;
		}

		assert(false);
	}

	{
		IScopedLock lock(buffer_mutex.get());
		
		if(buffers.size()<max_idle_buffers)
		{
			buffers.push_back(buf);
			return;
		}
	}

	delete[] buf;
}

void Filesystem::shutdownReadahead()
{
	if(readahead_thread.get()!=NULL)
	{
		readahead_thread->stop();
		Server->getThreadPool()->waitFor(readahead_thread_ticket);
		readahead_thread.reset();
	}

	size_t num = 0;
	while (num_uncompleted_blocks > 0)
	{
		waitForCompletion(100);
		++num;
#ifdef _WIN32
		if (num>10
			&& num_uncompleted_blocks > 0)
		{
			CancelIo(hVol);
		}
#endif
	}
}

