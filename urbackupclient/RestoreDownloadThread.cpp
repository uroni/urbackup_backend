/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "RestoreDownloadThread.h"
#include "../urbackupcommon/file_metadata.h"
#include "../urbackupcommon/os_functions.h"


namespace
{
	const unsigned int shadow_copy_timeout=30*60*1000;
	const size_t max_queue_size = 500;
	const size_t queue_items_full = 1;
	const size_t queue_items_chunked = 4;
}

RestoreDownloadThread::RestoreDownloadThread( FileClient& fc, FileClientChunked& fc_chunked, const std::string& client_token )
	: fc(fc), fc_chunked(fc_chunked), queue_size(0), all_downloads_ok(true),
	mutex(Server->createMutex()), cond(Server->createCondition()), skipping(false), is_offline(false),
	client_token(client_token)
{

}


void RestoreDownloadThread::operator()()
{
	fc_chunked.setQueueCallback(this);
	fc.setQueueCallback(this);

	while(true)
	{
		SQueueItem curr;
		{
			IScopedLock lock(mutex.get());
			while(dl_queue.empty())
			{
				cond->wait(&lock);
			}
			curr = dl_queue.front();
			dl_queue.pop_front();

			if(curr.action == EQueueAction_Fileclient)
			{
				if(curr.fileclient == EFileClient_Full)
				{
					queue_size-=queue_items_full;
				}
				else if(curr.fileclient== EFileClient_Chunked)
				{
					queue_size-=queue_items_chunked;
				}
			}			
		}

		if(curr.action==EQueueAction_Quit)
		{
			IScopedLock lock(mutex.get());
			if(!dl_queue.empty())
			{
				dl_queue.push_back(curr);
				continue;
			}
			else
			{
				break;
			}
		}
		else if(curr.action==EQueueAction_Skip)
		{
			skipping = true;
			continue;
		}

		if(is_offline || skipping)
		{
			download_nok_ids.push_back(curr.id);

			{
				IScopedLock lock(mutex.get());
				all_downloads_ok=false;
			}

			delete curr.patch_dl_files.orig_file;
			ScopedDeleteFile del_3(curr.patch_dl_files.chunkhashes);

			continue;
		}

		bool ret = true;

		if(curr.fileclient == EFileClient_Full)
		{
			ret = load_file(curr);
		}
		else if(curr.fileclient== EFileClient_Chunked)
		{
			ret = load_file_patch(curr);
		}

		if(!ret)
		{
			IScopedLock lock(mutex.get());
			is_offline=true;
		}
	}

	if(!is_offline && !skipping)
	{
		_u32 rc = fc.InformMetadataStreamEnd(client_token);

		if(rc!=ERR_SUCCESS)
		{
			Server->Log("Error informing client about metadata stream end. Errorcode: "+fc.getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		}
	}
}

void RestoreDownloadThread::addToQueueFull( size_t id, const std::string &remotefn, const std::string &destfn,
    _i64 predicted_filesize, const FileMetadata& metadata, bool is_script, bool metadata_only, size_t folder_items)
{
	SQueueItem ni;
	ni.id = id;
	ni.remotefn = remotefn;
	ni.destfn = destfn;
	ni.fileclient = EFileClient_Full;
	ni.action = EQueueAction_Fileclient;
	ni.predicted_filesize = predicted_filesize;
	ni.metadata = metadata;
	ni.is_script = is_script;
	ni.patch_dl_files.chunkhashes=NULL;
	ni.patch_dl_files.orig_file=NULL;
	ni.metadata_only = metadata_only;
	ni.folder_items = folder_items;

	IScopedLock lock(mutex.get());
	dl_queue.push_back(ni);
	cond->notify_one();

	queue_size+=queue_items_full;
	sleepQueue(lock);
}

void RestoreDownloadThread::addToQueueChunked( size_t id, const std::string &remotefn, const std::string &destfn,
	_i64 predicted_filesize, const FileMetadata& metadata, bool is_script, IFile* orig_file, IFile* chunkhashes )
{
	SQueueItem ni;
	ni.id = id;
	ni.remotefn = remotefn;
	ni.destfn = destfn;
	ni.fileclient = EFileClient_Chunked;
	ni.action = EQueueAction_Fileclient;
	ni.predicted_filesize= predicted_filesize;
	ni.metadata = metadata;
	ni.is_script = is_script;
	ni.patch_dl_files.chunkhashes=chunkhashes;
	ni.patch_dl_files.orig_file=orig_file;
	ni.metadata_only=false;

	IScopedLock lock(mutex.get());
	dl_queue.push_back(ni);
	cond->notify_one();

	queue_size+=queue_items_chunked;
	sleepQueue(lock);
}

void RestoreDownloadThread::queueSkip()
{
	SQueueItem ni;
	ni.action = EQueueAction_Skip;

	IScopedLock lock(mutex.get());
	dl_queue.push_front(ni);
    cond->notify_one();
}

void RestoreDownloadThread::queueStop()
{
    SQueueItem ni;
    ni.action = EQueueAction_Quit;

    IScopedLock lock(mutex.get());
    dl_queue.push_back(ni);
    cond->notify_one();
}

bool RestoreDownloadThread::load_file( SQueueItem todl )
{
	std::auto_ptr<IFile> dest_f;
	
    if(!todl.metadata_only)
	{
		dest_f.reset(Server->openFile(os_file_prefix(todl.destfn), MODE_WRITE));

#ifdef _WIN32
		if(dest_f.get()==NULL)
		{
			size_t idx=0;
			std::string old_destfn=todl.destfn;
			while(dest_f.get()==NULL && idx<100)
			{
				todl.destfn=old_destfn+"_"+convert(idx);
				++idx;
				dest_f.reset(Server->openFile(os_file_prefix(todl.destfn), MODE_WRITE));
			}
			rename_queue.push_back(std::make_pair(todl.destfn, old_destfn));
		}
#endif

		if(dest_f.get()==NULL)
		{
			download_nok_ids.push_back(todl.id);
			return false;
		}
	}

	_u32 rc = fc.GetFile((todl.remotefn), dest_f.get(), true, todl.metadata_only, todl.folder_items, false);

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		dest_f->Seek(0);
		rc=fc.GetFile((todl.remotefn), dest_f.get(), true, todl.metadata_only, todl.folder_items, false);
		--hash_retries;
	}

	if(rc!=ERR_SUCCESS)
	{
		download_nok_ids.push_back(todl.id);
		return false;
	}

	return true;
}

bool RestoreDownloadThread::load_file_patch( SQueueItem todl )
{
	ScopedDeleteFile del_3(todl.patch_dl_files.chunkhashes);

	_u32 rc = fc_chunked.GetFileChunked((todl.remotefn), todl.patch_dl_files.orig_file, todl.patch_dl_files.chunkhashes, NULL, todl.predicted_filesize);

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		todl.patch_dl_files.orig_file->Seek(0);
		rc=fc_chunked.GetFileChunked((todl.remotefn), todl.patch_dl_files.orig_file, todl.patch_dl_files.chunkhashes, NULL, todl.predicted_filesize);
		--hash_retries;
	}

	int64 fsize = todl.patch_dl_files.orig_file->Size();

	delete todl.patch_dl_files.orig_file;
	todl.patch_dl_files.orig_file=NULL;

	if(rc==ERR_SUCCESS && fsize>todl.predicted_filesize)
	{
		os_file_truncate(os_file_prefix(todl.destfn), todl.predicted_filesize);
	}

	if(rc!=ERR_SUCCESS)
	{
		download_nok_ids.push_back(todl.id);
		return false;
	}

	return true;
}

std::string RestoreDownloadThread::getQueuedFileFull( FileClient::MetadataQueue& metadata, size_t& folder_items, bool& finish_script)
{
	IScopedLock lock(mutex.get());
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			!it->queued && it->fileclient==EFileClient_Full)
		{
			it->queued=true;
			if(it->metadata_only)
			{
				metadata = FileClient::MetadataQueue_Metadata;
			}
			else
			{
				metadata = FileClient::MetadataQueue_Data;
			}
			folder_items = it->folder_items;
			finish_script = false;
			return (it->remotefn);
		}
	}

	return std::string();
}

void RestoreDownloadThread::unqueueFileFull( const std::string& fn, bool finish_script)
{
	IScopedLock lock(mutex.get());
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			it->queued && it->fileclient==EFileClient_Full
			&& (it->remotefn) == fn)
		{
			it->queued=false;
			return;
		}
	}
}

void RestoreDownloadThread::resetQueueFull()
{
	IScopedLock lock(mutex.get());
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			it->fileclient==EFileClient_Full)
		{
			it->queued=false;
		}
	}
}

bool RestoreDownloadThread::getQueuedFileChunked( std::string& remotefn, IFile*& orig_file, IFile*& patchfile,
	IFile*& chunkhashes, IFile*& hashoutput, _i64& predicted_filesize )
{
	IScopedLock lock(mutex.get());
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			!it->queued && it->fileclient==EFileClient_Chunked
			&& it->predicted_filesize>0)
		{
			remotefn = (it->remotefn);

			it->queued=true;
			orig_file = it->patch_dl_files.orig_file;
			patchfile = NULL;
			chunkhashes = it->patch_dl_files.chunkhashes;
			hashoutput = NULL;
			predicted_filesize = it->predicted_filesize;
			return true;
		}
	}

	return false;
}

void RestoreDownloadThread::unqueueFileChunked( const std::string& remotefn )
{
	IScopedLock lock(mutex.get());
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			it->queued && it->fileclient==EFileClient_Chunked
			&& (it->remotefn) == remotefn )
		{
			it->queued=false;
			return;
		}
	}
}

void RestoreDownloadThread::resetQueueChunked()
{
	IScopedLock lock(mutex.get());
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && it->fileclient==EFileClient_Chunked)
		{
			it->queued=false;
		}
    }
}

bool RestoreDownloadThread::hasError()
{
    return !download_nok_ids.empty();
}

void RestoreDownloadThread::sleepQueue(IScopedLock& lock)
{
	while(queue_size>max_queue_size)
	{
		lock.relock(NULL);
		Server->wait(1000);
		lock.relock(mutex.get());
	}
}

std::vector<std::pair<std::string, std::string> > RestoreDownloadThread::getRenameQueue()
{
	return rename_queue;
}

