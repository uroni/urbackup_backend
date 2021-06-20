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
#ifdef _WIN32
	//Must be at least 64, otherwise it might get stuck
	const size_t readahead_num_blocks = 80;
#else
	const size_t readahead_num_blocks = 5120;
#endif
	const size_t max_idle_buffers = readahead_num_blocks;
	const size_t readahead_low_level_blocks = readahead_num_blocks/2;
	const size_t slow_read_warning_seconds = 5 * 60;
	const size_t max_read_wait_seconds = 60 * 60;

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

class Filesystem_ReadaheadThread : public IThread
{
public:
	Filesystem_ReadaheadThread(Filesystem& fs, bool background_priority)
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
	~Filesystem_ReadaheadThread()
	{
		for (std::map<int64, IFilesystem::IFsBuffer*>::iterator it = read_blocks.begin();
			it != read_blocks.end(); ++it)
		{
			fs.releaseBuffer(it->second);
		}
	}


	void operator()()
	{
		ScopedBackgroundPrio background_prio(false);
		if (background_priority)
		{
#ifndef _DEBUG
			background_prio.enable();
#endif
		}

		IScopedLock lock(mutex.get());
		while (!do_stop)
		{
			if (read_blocks.size() >= readahead_num_blocks)
			{
				while (read_blocks.size()>readahead_low_level_blocks
					&& !readahead_miss)
				{
					start_readahead_cond->wait(&lock);

					if (do_stop) break;
				}
			}

			if (do_stop) break;

			while (current_block == -1)
			{
				start_readahead_cond->wait(&lock);

				if (do_stop) break;
			}

			if (do_stop) break;

			std::map<int64, IFilesystem::IFsBuffer*>::iterator it;
			do
			{
				it = read_blocks.find(current_block);
				if (it != read_blocks.end())
				{
					current_block = fs.nextBlockInt(current_block);
				}
			} while (it != read_blocks.end());

			if (current_block != -1)
			{
				int64 l_current_block = current_block;
				lock.relock(nullptr);
				IFilesystem::IFsBuffer* buf = fs.readBlockInt(l_current_block, false, nullptr);
				lock.relock(mutex.get());

				read_blocks[l_current_block] = buf;

				current_block = fs.nextBlockInt(l_current_block);

				if (readahead_miss)
				{
					read_block_cond->notify_all();
					readahead_miss = false;
				}
			}
		}
	}

	void setMaxReadaheadNBuffers(int64 n_max_buffers)
	{
		curr_fs_readahead_n_max_buffers = n_max_buffers;
	}

	IFilesystem::IFsBuffer* getBlock(int64 block)
	{
		IScopedLock lock(mutex.get());

		clearUnusedReadahead(block);

		IFilesystem::IFsBuffer* ret = nullptr;
		while (ret == nullptr)
		{
			std::map<int64, IFilesystem::IFsBuffer*>::iterator it = read_blocks.find(block);

			if (it != read_blocks.end())
			{
				ret = it->second;
				read_blocks.erase(it);
			}
			else
			{
				readaheadFromInt(block);
				readahead_miss = true;
				read_block_cond->wait(&lock);
			}
		}

		return ret;
	}

	void stop()
	{
		IScopedLock lock(mutex.get());
		do_stop = true;
		start_readahead_cond->notify_all();
	}

private:

	void readaheadFromInt(int64 pBlock)
	{
		current_block = pBlock;
		start_readahead_cond->notify_all();
	}

	void clearUnusedReadahead(int64 pBlock)
	{
		for (std::map<int64, IFilesystem::IFsBuffer*>::iterator it = read_blocks.begin();
			it != read_blocks.end();)
		{
			if (it->first<pBlock)
			{
				std::map<int64, IFilesystem::IFsBuffer*>::iterator todel = it;
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

	std::unique_ptr<IMutex> mutex;
	std::unique_ptr<ICondition> start_readahead_cond;
	std::unique_ptr<ICondition> read_block_cond;
	Filesystem& fs;

	std::map<int64, IFilesystem::IFsBuffer*> read_blocks;

	bool readahead_miss;

	int64 current_block;

	bool do_stop;

	bool background_priority;

	int64 curr_fs_readahead_n_max_buffers;
};

Filesystem::Filesystem(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, IFsNextBlockCallback* next_block_callback)
	: buffer_mutex(Server->createMutex()), next_block_callback(next_block_callback), overlapped_next_block(-1),
	num_uncompleted_blocks(0), errcode(0), curr_fs_readahead_n_max_buffers(fs_readahead_n_max_buffers)
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
	if(dev==nullptr)
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
	assert(readahead_thread.get()==nullptr);

	if(dev!=nullptr && own_dev)
	{
		Server->destroy(dev);
	}
	
	for(size_t i=0;i<buffers.size();++i)
	{
		delete[] buffers[i]->buf;
		delete buffers[i];
	}

	if (read_ahead_mode == IFSImageFactory::EReadaheadMode_Overlapped)
	{
		for (size_t i = 0; i < next_blocks.size(); ++i)
		{
#ifdef _WIN32
			VirtualFree(next_blocks[i].buffers[0].buffer, 0, MEM_RELEASE);
#else
			delete[] next_blocks[i].buffers[0].buffer;
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

IFilesystem::IFsBuffer* Filesystem::readBlock(int64 pBlock, bool* p_has_error)
{
	return readBlockInt(pBlock, readahead_thread.get()!=nullptr, p_has_error);
}

IFilesystem::IFsBuffer* Filesystem::readBlockInt(int64 pBlock, bool use_readahead, bool* p_has_error)
{
	const unsigned char *bitmap=getBitmap();
	int64 blocksize=getBlocksize();

	size_t bitmap_byte=(size_t)(pBlock/8);
	size_t bitmap_bit=pBlock%8;

	unsigned char b=bitmap[bitmap_byte];

	bool has_bit=((b & (1<<bitmap_bit))>0);

	if(!has_bit)
		return nullptr;
	
	if (read_ahead_mode == IFSImageFactory::EReadaheadMode_Overlapped)
	{
		SBlockBuffer* block_buf = completionGetBlock(pBlock, p_has_error);
		if (block_buf == nullptr)
			return nullptr;
		return block_buf;
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
			return nullptr;
		}
		IFsBuffer* buf = getBuffer();
		if(!readFromDev(buf->getBuf(), (_u32)blocksize) )
		{
			Server->Log("Reading from device failed -1", LL_ERROR);
			has_error=true;
			return nullptr;
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
		for(size_t i=0;i<block->n_buffers;++i)
			block->buffers[i].state = ENextBlockState_Error;
	}
	else if (dwNumberOfBytesTransfered != block->n_buffers*getBlocksize())
	{
		Server->Log("Reading from device at position " + convert(offset) + " failed. OS returned only "+convert((int64)dwNumberOfBytesTransfered)+" bytes"
			". Expected "+convert(block->n_buffers*getBlocksize())+" bytes", LL_ERROR);
		has_error = true;
		for(size_t i=0;i<block->n_buffers;++i)
			block->buffers[i].state = ENextBlockState_Error;
	}
	else
	{
		for(size_t i=0;i<block->n_buffers;++i)
			block->buffers[i].state = ENextBlockState_Ready;
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

void Filesystem::readaheadSetMaxNBuffers(int64 n_max_buffers)
{
	curr_fs_readahead_n_max_buffers = n_max_buffers;
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
				SBlockBuffer* block_buf = completionGetBlock(i, nullptr);
				if (block_buf != nullptr)
				{
					memcpy(buffers[currbuf] + buffer_offset, block_buf->buffer, blocksize);
					++currbuf;
					ret.push_back(i);
					
					completionFreeBuffer(block_buf);
				}
				else if (has_error)
				{
					break;
				}
			}
		}
		else
		{
			IFsBuffer* buf = readBlock(i);
			if (buf != nullptr)
			{
				memcpy(buffers[currbuf] + buffer_offset, buf->getBuf(), blocksize);
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

#ifdef _WIN32
	IFsFile* fs_dev = dynamic_cast<IFsFile*>(dev);
	if (fs_dev != NULL)
	{
		hVol = fs_dev->getOsHandle();

		DWORD lpBytesReturned = 0;
		OVERLAPPED ovl = {};
		ovl.hEvent = CreateEvent(NULL, false, false, NULL);
		if (ovl.hEvent != NULL)
		{
			BOOL b = DeviceIoControl(hVol, FSCTL_ALLOW_EXTENDED_DASD_IO,
				NULL, 0, NULL, 0, &lpBytesReturned, &ovl);
			if (!b
				&& GetLastError() == ERROR_IO_PENDING)
			{
				if (!GetOverlappedResult(hVol, &ovl, &lpBytesReturned, TRUE))
				{
					Server->Log("Setting FSCTL_ALLOW_EXTENDED_DASD_IO failed -1. Err: " + convert(getLastSystemError()), LL_ERROR);
				}
			}
			else if(!b)
			{
				Server->Log("Setting FSCTL_ALLOW_EXTENDED_DASD_IO failed -2. Err: " + convert(getLastSystemError()), LL_ERROR);
			}

			CloseHandle(ovl.hEvent);
		}
		else
		{
			Server->Log("Error creating event. Err: " + convert(getLastSystemError()), LL_ERROR);
		}
	}
	else
	{
		hVol = INVALID_HANDLE_VALUE;
	}
#endif

	if (read_ahead== IFSImageFactory::EReadaheadMode_Overlapped)
	{
		next_blocks.resize(readahead_num_blocks);

		for (size_t i = 0; i < next_blocks.size(); ++i)
		{
#ifdef _WIN32
			next_blocks[i].buffers[0].buffer = reinterpret_cast<char*>(VirtualAlloc(NULL, getBlocksize()*fs_readahead_n_max_buffers, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
			next_blocks[i].buffers[0].buffer = new char[getBlocksize()*fs_readahead_n_max_buffers];
#endif
			if (next_blocks[i].buffers[0].buffer == nullptr)
			{
				has_error = true;
			}
			next_blocks[i].buffers[0].state = ENextBlockState_Queued;
			next_blocks[i].fs = this;
			next_blocks[i].buffers[0].block = &next_blocks[i];
			
			for(size_t j=1;j<fs_readahead_n_max_buffers;++j)
			{
				SBlockBuffer& block_buf = next_blocks[i].buffers[j];
				block_buf.state = ENextBlockState_Queued;
				block_buf.block = &next_blocks[i];
				block_buf.buffer = next_blocks[i].buffers[0].buffer+j*getBlocksize();
			}

			free_next_blocks.push(&next_blocks[i]);
		}
	}
	else if (read_ahead == IFSImageFactory::EReadaheadMode_Thread)
	{
		readahead_thread.reset(new Filesystem_ReadaheadThread(*this, background_priority));
		readahead_thread_ticket = Server->getThreadPool()->execute(readahead_thread.get(), "device readahead");
	}

	if (read_ahead != IFSImageFactory::EReadaheadMode_None
		&& next_block_callback==nullptr)
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
			
			block->n_buffers=1;
			int64 overlapped_start_block = overlapped_next_block;
			int64 overlapped_last_block = overlapped_next_block;
			
			block->buffers[0].state = ENextBlockState_Queued;
			queued_next_blocks[overlapped_next_block] = &block->buffers[0];
			overlapped_next_block = next_block_callback->nextBlock(overlapped_next_block);
			
			while(block->n_buffers<curr_fs_readahead_n_max_buffers
				&& overlapped_last_block==overlapped_next_block-1)
			{
				block->buffers[block->n_buffers].state = ENextBlockState_Queued;
				queued_next_blocks[overlapped_next_block] = &block->buffers[block->n_buffers];
				overlapped_last_block = overlapped_next_block;
				overlapped_next_block = next_block_callback->nextBlock(overlapped_next_block);
				++block->n_buffers;
			}
			
			++num_uncompleted_blocks;
#ifdef _WIN32
			memset(&block->ovl, 0, sizeof(block->ovl));
			LARGE_INTEGER offset;
			offset.QuadPart = overlapped_start_block*getBlocksize();
			block->ovl.Offset = offset.LowPart;
			block->ovl.OffsetHigh = offset.HighPart;
			block->ovl.hEvent = block;

			BOOL b = ReadFileEx(hVol, block->buffers[0].buffer, 
				static_cast<DWORD>(block->n_buffers*blocksize), &block->ovl,
				FileIOCompletionRoutine);
			if (!b)
			{
				--num_uncompleted_blocks;
				Server->Log("Error starting overlapped read operation. System error code " + convert(getLastSystemError()), LL_ERROR);
				has_error = true;
				return false;
			}
#endif	
			ret = true;

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

SBlockBuffer* Filesystem::completionGetBlock(int64 pBlock, bool* p_has_error)
{
	std::map<int64, SBlockBuffer*>::iterator it = queued_next_blocks.find(pBlock);
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

	SBlockBuffer* next_block = it->second;
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
			if (p_has_error != nullptr) *p_has_error = true;
			errcode = fs_error_read_timeout;
			has_error = true;
			return nullptr;
		}
	}

	if (next_block->state != ENextBlockState_Ready)
	{
		if (p_has_error != nullptr) *p_has_error = true;
		completionFreeBuffer(next_block);
		return nullptr;
	}

	return next_block;
}

void Filesystem::completionFreeBuffer(SBlockBuffer* block_buf)
{
	--block_buf->block->n_buffers;
	if(block_buf->block->n_buffers==0)
	{
		free_next_blocks.push(block_buf->block);
	}
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

IFilesystem::IFsBuffer* Filesystem::getBuffer()
{
	assert(read_ahead_mode != IFSImageFactory::EReadaheadMode_Overlapped);

	{
		IScopedLock lock(buffer_mutex.get());

		if(!buffers.empty())
		{
			SSimpleBuffer* ret = buffers[buffers.size()-1];
			buffers.erase(buffers.begin()+buffers.size()-1);
			return ret;
		}
	}

	SSimpleBuffer* nb= new SSimpleBuffer;
	nb->buf = new char[getBlocksize()];
	return nb;
}

void Filesystem::releaseBuffer(IFsBuffer* buf)
{
	if(read_ahead_mode == IFSImageFactory::EReadaheadMode_Overlapped)
	{
		SBlockBuffer* block_buf = static_cast<SBlockBuffer*>(buf);
		completionFreeBuffer(block_buf);
		return;
	}

	{
		IScopedLock lock(buffer_mutex.get());
		
		if(buffers.size()<max_idle_buffers)
		{
			buffers.push_back(static_cast<SSimpleBuffer*>(buf));
			return;
		}
	}

	SSimpleBuffer* simple_buf = static_cast<SSimpleBuffer*>(buf);
	delete[] simple_buf->buf;
	delete simple_buf;
}

void Filesystem::shutdownReadahead()
{
	if(readahead_thread.get()!=nullptr)
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

bool Filesystem::excludeFile(const std::string& path)
{
	Server->Log("Trying to exclude contents of file " + path + " from backup...", LL_DEBUG);

#ifdef _WIN32
	HANDLE hFile = CreateFileW(Server->ConvertToWchar(path).c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		STARTING_VCN_INPUT_BUFFER start_vcn = {};
		std::vector<char> ret_buf;
		ret_buf.resize(32768);

		while (true)
		{
			RETRIEVAL_POINTERS_BUFFER* ret_ptrs = reinterpret_cast<RETRIEVAL_POINTERS_BUFFER*>(ret_buf.data());

			DWORD bytesRet = 0;

			BOOL b = DeviceIoControl(hFile, FSCTL_GET_RETRIEVAL_POINTERS,
				&start_vcn, sizeof(start_vcn), ret_ptrs, static_cast<DWORD>(ret_buf.size()), &bytesRet, NULL);

			DWORD err = GetLastError();

			if (b || err == ERROR_MORE_DATA)
			{
				LARGE_INTEGER last_vcn = ret_ptrs->StartingVcn;
				for (DWORD i = 0; i < ret_ptrs->ExtentCount; ++i)
				{
					//Sparse entries have Lcn -1
					if (ret_ptrs->Extents[i].Lcn.QuadPart != -1)
					{
						int64 count = ret_ptrs->Extents[i].NextVcn.QuadPart - last_vcn.QuadPart;

						if (!excludeSectors(ret_ptrs->Extents[i].Lcn.QuadPart, count))
						{
							Server->Log("Error excluding sectors of file " + path, LL_WARNING);
						}
					}

					last_vcn = ret_ptrs->Extents[i].NextVcn;
				}
			}

			if (!b)
			{
				if (err == ERROR_MORE_DATA)
				{
					start_vcn.StartingVcn = ret_ptrs->Extents[ret_ptrs->ExtentCount - 1].NextVcn;
				}
				else
				{
					Server->Log("Error " + convert((int)GetLastError()) + " while accessing retrieval points", LL_WARNING);
					CloseHandle(hFile);
					break;
				}
			}
			else
			{
				CloseHandle(hFile);
				break;
			}
		}
		return true;
	}
	else
	{
		Server->Log("Error opening file handle to " + path, LL_DEBUG);
		return false;
	}
#else
	std::unique_ptr<IFsFile> f(Server->openFile(path, MODE_READ));
	if (f.get() == nullptr)
	{
		Server->Log("Error opening file " + path + ". " + os_last_error_str(), LL_DEBUG);
		return false;
	}

	bool has_more_extents = true;
	int64 offset = 0;
	int64 block_size = getBlocksize();
	while (has_more_extents)
	{
		std::vector<IFsFile::SFileExtent> exts = f->getFileExtents(offset, block_size, has_more_extents);
		for (size_t i = 0; i < exts.size(); ++i)
		{
			if (exts[i].volume_offset != -1)
			{
				int64 vol_block=exts[i].volume_offset/block_size;

				if(exts[i].volume_offset%block_size!=0)
					vol_block++;

				if (!excludeSectors(vol_block, exts[i].size / block_size))
				{
					Server->Log("Error excluding sectors of file " + path, LL_WARNING);
				}
			}
			offset = (std::max)(exts[i].offset + exts[i].size, offset);
		}
	}
	return true;
#endif
}

bool Filesystem::excludeFiles(const std::string& path, const std::string& fn_contains)
{
#ifdef _WIN32
	HANDLE fHandle;
	WIN32_FIND_DATAW wfd;
	std::wstring tpath = Server->ConvertToWchar(path);
	if (!tpath.empty() && tpath[tpath.size() - 1] == '\\') tpath.erase(path.size() - 1, 1);
	fHandle = FindFirstFileW((tpath + L"\\*" + Server->ConvertToWchar(fn_contains) + L"*").c_str(), &wfd);

	if (fHandle == INVALID_HANDLE_VALUE)
	{
		Server->Log("Error opening find handle to " + path + " err: " + convert((int)GetLastError()), LL_DEBUG);
		return false;
	}

	bool ret = true;
	do
	{
		std::string name = Server->ConvertFromWchar(wfd.cFileName);
		if (name == "." || name == "..")
			continue;

		if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			if (!excludeFile(Server->ConvertFromWchar(tpath + L"\\" + wfd.cFileName)))
			{
				ret = false;
			}
		}
	} while (FindNextFileW(fHandle, &wfd));
	FindClose(fHandle);

	return ret;
#else
	std::vector<SFile> files = getFiles(path);
	bool ret = true;
	for (size_t i = 0; i < files.size(); ++i)
	{
		const SFile& f = files[i];
		if (f.isdir)
			continue;

		if (f.name.find(fn_contains) != std::string::npos)
		{
			if (!excludeFile(path + os_file_sep() + f.name))
			{
				ret = false;
			}
		}
	}
	return ret;
#endif
}

bool Filesystem::excludeSectors(int64 start, int64 count)
{
	for (int64 block = start; block < start + count; ++block)
	{
		if (!excludeBlock(block))
		{
			return false;
		}
	}

	return true;
}

bool Filesystem::excludeBlock(int64 block)
{
	size_t bitmap_byte = (size_t)(block / 8);
	size_t bitmap_bit = block % 8;

	unsigned char* bitmap = const_cast<unsigned char*>(getBitmap());
	unsigned char b = bitmap[bitmap_byte];

	b = b & (~(1 << bitmap_bit));

	bitmap[bitmap_byte] = b;

	return true;
}
