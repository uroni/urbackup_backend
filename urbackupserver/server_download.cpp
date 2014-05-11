#include <algorithm>

#include "server_download.h"
#include "../Interface/Server.h"
#include "server_log.h"
#include "server_get.h"
#include "../stringtools.h"
#include "../common/data.h"

namespace
{
	const unsigned int shadow_copy_timeout=30*60*1000;
	const size_t max_queue_size = 5000;
}

ServerDownloadThread::ServerDownloadThread( FileClient& fc, FileClientChunked* fc_chunked, bool with_hashes, const std::wstring& backuppath, const std::wstring& backuppath_hashes, const std::wstring& last_backuppath, const std::wstring& last_backuppath_complete, bool hashed_transfer, bool save_incomplete_file, int clientid,
	const std::wstring& clientname, bool use_tmpfiles, const std::wstring& tmpfile_path, const std::string& server_token, bool use_reflink, int backupid, bool r_incremental, IPipe* hashpipe_prepare, BackupServerGet* server_get,
	int filesrv_protocol_version)
	: fc(fc), fc_chunked(fc_chunked), with_hashes(with_hashes), backuppath(backuppath), backuppath_hashes(backuppath_hashes), 
	last_backuppath(last_backuppath), last_backuppath_complete(last_backuppath_complete), hashed_transfer(hashed_transfer), save_incomplete_file(save_incomplete_file), clientid(clientid),
	clientname(clientname),
	use_tmpfiles(use_tmpfiles), tmpfile_path(tmpfile_path), server_token(server_token), use_reflink(use_reflink), backupid(backupid), r_incremental(r_incremental), hashpipe_prepare(hashpipe_prepare), max_ok_id(0),
	is_offline(false), server_get(server_get), filesrv_protocol_version(filesrv_protocol_version), skipping(false)
{
	mutex = Server->createMutex();
	cond = Server->createCondition();
}

ServerDownloadThread::~ServerDownloadThread()
{
	Server->destroy(mutex);
	Server->destroy(cond);
}

void ServerDownloadThread::operator()( void )
{
	fc.setQueueCallback(this);
	if(fc_chunked!=NULL && filesrv_protocol_version>2)
	{
		fc_chunked->setQueueCallback(this);
	}

	while(true)
	{
		SQueueItem curr;
		{
			IScopedLock lock(mutex);
			while(dl_queue.empty())
			{
				cond->wait(&lock);
			}
			curr = dl_queue.front();
			dl_queue.pop_front();
		}

		if(curr.action==EQueueAction_Quit)
		{
			break;
		}
		else if(curr.action=EQueueAction_Skip)
		{
			skipping = true;
		}

		if(is_offline || skipping)
		{
			download_nok_ids.push_back(curr.id);

			if(curr.patch_dl_files.prepared)
			{
				delete curr.patch_dl_files.orig_file;
				ScopedDeleteFile del_1(curr.patch_dl_files.patchfile);
				ScopedDeleteFile del_2(curr.patch_dl_files.hashoutput);
				if(curr.patch_dl_files.delete_chunkhashes)
				{
					ScopedDeleteFile del_3(curr.patch_dl_files.chunkhashes);
				}
				else
				{
					delete curr.patch_dl_files.chunkhashes;
				}
			}

			continue;
		}

		if(curr.action==EQueueAction_StartShadowcopy)
		{
			start_shadowcopy(Server->ConvertToUTF8(curr.fn));
			continue;
		}
		else if(curr.action==EQueueAction_StopShadowcopy)
		{
			stop_shadowcopy(Server->ConvertToUTF8(curr.fn));
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
			IScopedLock lock(mutex);
			is_offline=true;
		}
	}

	std::sort(download_nok_ids.begin(), download_nok_ids.end());
}

void ServerDownloadThread::addToQueueFull(size_t id, const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path, const std::wstring &os_path, bool at_front )
{
	SQueueItem ni;
	ni.id = id;
	ni.fn = fn;
	ni.short_fn = short_fn;
	ni.curr_path = curr_path;
	ni.os_path = os_path;
	ni.fileclient = EFileClient_Full;
	ni.patch_dl_files.prepared=false;
	ni.patch_dl_files.prepare_error=false;
	ni.action = EQueueAction_Fileclient;

	IScopedLock lock(mutex);
	if(!at_front)
	{
		dl_queue.push_back(ni);
	}
	else
	{
		dl_queue.push_front(ni);
	}
	cond->notify_one();

	if(!at_front)
	{
		sleepQueue(lock);
	}
}


void ServerDownloadThread::addToQueueChunked(size_t id, const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path, const std::wstring &os_path, _i64 predicted_filesize )
{
	SQueueItem ni;
	ni.id = id;
	ni.fn = fn;
	ni.short_fn = short_fn;
	ni.curr_path = curr_path;
	ni.os_path = os_path;
	ni.fileclient = EFileClient_Chunked;
	ni.patch_dl_files.prepared=false;
	ni.patch_dl_files.prepare_error=false;
	ni.action = EQueueAction_Fileclient;
	ni.predicted_filesize= predicted_filesize;

	IScopedLock lock(mutex);
	dl_queue.push_back(ni);
	cond->notify_one();

	sleepQueue(lock);
}

void ServerDownloadThread::addToQueueStartShadowcopy(const std::wstring& fn)
{
	SQueueItem ni;
	ni.action = EQueueAction_StartShadowcopy;
	ni.fn=fn;
	ni.id=std::string::npos;
	ni.patch_dl_files.prepared=false;
	ni.patch_dl_files.prepare_error=false;

	IScopedLock lock(mutex);
	dl_queue.push_back(ni);
	cond->notify_one();

	sleepQueue(lock);
}

void ServerDownloadThread::addToQueueStopShadowcopy(const std::wstring& fn)
{
	SQueueItem ni;
	ni.action = EQueueAction_StopShadowcopy;
	ni.fn=fn;
	ni.id=std::string::npos;
	ni.patch_dl_files.prepared=false;
	ni.patch_dl_files.prepare_error=false;

	IScopedLock lock(mutex);
	dl_queue.push_back(ni);
	cond->notify_one();

	sleepQueue(lock);
}


bool ServerDownloadThread::load_file(SQueueItem todl)
{
	ServerLogger::Log(clientid, L"Loading file \""+todl.fn+L"\"", LL_DEBUG);
	IFile *fd=BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid);
	if(fd==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'fd' in load_file", LL_ERROR);
		return false;
	}

	std::wstring cfn=getDLPath(todl);

	_u32 rc=fc.GetFile(Server->ConvertToUTF8(cfn), fd, hashed_transfer);

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		fd->Seek(0);
		rc=fc.GetFile(Server->ConvertToUTF8(cfn), fd, hashed_transfer);
		--hash_retries;
	}

	bool ret = true;
	bool hash_file = false;

	if(rc!=ERR_SUCCESS)
	{
		download_nok_ids.push_back(todl.id);
		ServerLogger::Log(clientid, L"Error getting file \""+cfn+L"\" from "+clientname+L". Errorcode: "+widen(fc.getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);

		if( (rc==ERR_TIMEOUT || rc==ERR_ERROR)
			&& save_incomplete_file
			&& fd->Size()>0 )
		{
			ServerLogger::Log(clientid, L"Saving incomplete file.", LL_DEBUG);
			hash_file = true;
		}
		else
		{
			BackupServerGet::destroyTemporaryFile(fd);					
		}

		if(rc==ERR_TIMEOUT || rc==ERR_ERROR || rc==ERR_BASE_DIR_LOST)
		{
			ret=false;
		}
	}
	else
	{
		if(todl.id>max_ok_id)
		{
			max_ok_id=todl.id;
		}
		hash_file=true;
	}

	if(hash_file)
	{
		std::wstring os_curr_path=BackupServerGet::convertToOSPathFromFileClient(todl.os_path+L"/"+todl.short_fn);		
		std::wstring dstpath=backuppath+os_curr_path;
		std::wstring hashpath;
		std::wstring filepath_old;
		if(with_hashes)
		{
			hashpath=backuppath_hashes+os_curr_path;
		}
		if( use_reflink && (!last_backuppath.empty() || !last_backuppath_complete.empty() ) )
		{
			std::wstring cfn_short=todl.os_path+L"/"+todl.short_fn;
			if(cfn_short[0]=='/')
				cfn_short.erase(0,1);

			filepath_old=last_backuppath+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);

			IFile *file_old=Server->openFile(os_file_prefix(filepath_old), MODE_READ);

			if(file_old==NULL)
			{
				if(!last_backuppath_complete.empty())
				{
					filepath_old=last_backuppath_complete+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
					file_old=Server->openFile(os_file_prefix(filepath_old), MODE_READ);
				}
				if(file_old==NULL)
				{
					ServerLogger::Log(clientid, L"No old file for \""+todl.fn+L"\"", LL_DEBUG);
					filepath_old.clear();
				}
			}

			if(file_old!=NULL)
			{
				Server->destroy(file_old);
			}
		}

		hashFile(dstpath, hashpath, fd, NULL, Server->ConvertToUTF8(filepath_old));
	}
	return ret;
}

bool ServerDownloadThread::load_file_patch(SQueueItem todl)
{
	std::wstring cfn=todl.curr_path+L"/"+todl.fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	bool full_dl=false;
	SPatchDownloadFiles dlfiles = todl.patch_dl_files;
	if(!dlfiles.prepared && !dlfiles.prepare_error)
	{
		dlfiles = preparePatchDownloadFiles(todl, full_dl);

		if(dlfiles.orig_file==NULL && full_dl)
		{
			addToQueueFull(todl.id, todl.fn, todl.short_fn, todl.curr_path, todl.os_path, true);
			return true;
		}
	}

	if(dlfiles.prepare_error)
	{
		return false;
	}

	/*std::wstring cfn_short=todl.os_path+L"/"+todl.short_fn;
	if(cfn_short[0]=='/')
		cfn_short.erase(0,1);

	std::wstring dstpath=backuppath+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	std::wstring hashpath=backuppath_hashes+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	std::wstring hashpath_old=last_backuppath+os_file_sep()+L".hashes"+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	std::wstring filepath_old=last_backuppath+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);

	std::auto_ptr<IFile> file_old(Server->openFile(os_file_prefix(filepath_old), MODE_READ));

	if(file_old.get()==NULL)
	{
		if(!last_backuppath_complete.empty())
		{
			filepath_old=last_backuppath_complete+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
			file_old.reset(Server->openFile(os_file_prefix(filepath_old), MODE_READ));
		}
		if(file_old.get()==NULL)
		{
			ServerLogger::Log(clientid, L"No old file for \""+todl.fn+L"\"", LL_DEBUG);
			return load_file(todl);
		}
		hashpath_old=last_backuppath_complete+os_file_sep()+L".hashes"+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	}

	ServerLogger::Log(clientid, L"Loading file patch for \""+todl.fn+L"\"", LL_DEBUG);
	IFile *pfd=BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid);
	if(pfd==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'pfd' in load_file_patch", LL_ERROR);
		return false;
	}
	ScopedDeleteFile pfd_destroy(pfd);
	IFile *hash_tmp=BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid);
	if(hash_tmp==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'hash_tmp' in load_file_patch", LL_ERROR);
		return false;
	}
	ScopedDeleteFile hash_tmp_destroy(hash_tmp);

	if(!server_token.empty())
	{
		cfn=widen(server_token)+L"|"+cfn;
	}

	std::auto_ptr<IFile> hashfile_old(Server->openFile(os_file_prefix(hashpath_old), MODE_READ));

	bool delete_hashfile=false;

	ScopedDeleteFile hashfile_old_destroy(NULL);

	if( (hashfile_old.get()==NULL || hashfile_old->Size()==0 ) && file_old.get()!=NULL )
	{
		ServerLogger::Log(clientid, L"Hashes for file \""+filepath_old+L"\" not available. Calulating hashes...", LL_DEBUG);
		hashfile_old.reset(BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid));
		if(hashfile_old.get()==NULL)
		{
			ServerLogger::Log(clientid, L"Error creating temporary file 'hashfile_old' in load_file_patch", LL_ERROR);
			return false;
		}
		hashfile_old_destroy.reset(hashfile_old.get());
		delete_hashfile=true;
		BackupServerPrepareHash::build_chunk_hashs(file_old.get(), hashfile_old.get(), NULL, false, NULL, false);
		hashfile_old->Seek(0);
	} */

	ServerLogger::Log(clientid, L"Loading file patch for \""+todl.fn+L"\"", LL_DEBUG);

	ScopedDeleteFile pfd_destroy(dlfiles.patchfile);
	ScopedDeleteFile hash_tmp_destroy(dlfiles.hashoutput);
	ScopedDeleteFile hashfile_old_destroy(NULL);
	ObjectScope file_old_destroy(dlfiles.orig_file);
	ObjectScope hashfile_old_delete(dlfiles.chunkhashes);

	if(dlfiles.delete_chunkhashes)
	{
		hashfile_old_destroy.reset(dlfiles.chunkhashes);
		hashfile_old_delete.release();
	}

	if(!server_token.empty())
	{
		cfn=widen(server_token)+L"|"+cfn;
	}

	_u32 rc=fc_chunked->GetFilePatch(Server->ConvertToUTF8(cfn), dlfiles.orig_file, dlfiles.patchfile, dlfiles.chunkhashes, dlfiles.hashoutput, todl.predicted_filesize);

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		dlfiles.orig_file->Seek(0);
		dlfiles.patchfile=BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid);
		if(dlfiles.patchfile==NULL)
		{
			ServerLogger::Log(clientid, L"Error creating temporary file 'pfd' in load_file_patch", LL_ERROR);
			return false;
		}
		pfd_destroy.reset(dlfiles.patchfile);
		dlfiles.hashoutput=BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid);
		if(dlfiles.hashoutput==NULL)
		{
			ServerLogger::Log(clientid, L"Error creating temporary file 'hash_tmp' in load_file_patch -2", LL_ERROR);
			return false;
		}
		hash_tmp_destroy.reset(dlfiles.hashoutput);
		dlfiles.chunkhashes->Seek(0);
		rc=fc_chunked->GetFilePatch(Server->ConvertToUTF8(cfn), dlfiles.orig_file, dlfiles.patchfile, dlfiles.chunkhashes, dlfiles.hashoutput, todl.predicted_filesize);
		--hash_retries;
	} 

	bool hash_file;

	if(rc!=ERR_SUCCESS)
	{
		download_nok_ids.push_back(todl.id);

		ServerLogger::Log(clientid, L"Error getting file \""+cfn+L"\" from "+clientname+L". Errorcode: "+widen(FileClient::getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);

		if( (rc==ERR_TIMEOUT || rc==ERR_CONN_LOST || rc==ERR_SOCKET_ERROR)
			&& dlfiles.patchfile->Size()>0
			&& save_incomplete_file)
		{
			ServerLogger::Log(clientid, L"Saving incomplete file.", LL_DEBUG);
			hash_file=true;
		}
		else
		{
			hash_file=false;
		}
	}
	else
	{
		if(todl.id>max_ok_id)
		{
			max_ok_id=todl.id;
		}
		hash_file=true;
	}

	if(hash_file)
	{
		std::wstring os_curr_path=BackupServerGet::convertToOSPathFromFileClient(todl.os_path+L"/"+todl.short_fn);		
		std::wstring dstpath=backuppath+os_curr_path;

		pfd_destroy.release();
		hash_tmp_destroy.release();
		hashFile(dstpath, dlfiles.hashpath, dlfiles.patchfile, dlfiles.hashoutput, Server->ConvertToUTF8(dlfiles.filepath_old));
	}

	if(rc==ERR_TIMEOUT || rc==ERR_ERROR || rc==ERR_SOCKET_ERROR
		|| rc==ERR_INT_ERROR || rc==ERR_BASE_DIR_LOST || rc==ERR_CONN_LOST )
		return false;
	else
		return true;
}

void ServerDownloadThread::hashFile(std::wstring dstpath, std::wstring hashpath, IFile *fd, IFile *hashoutput, std::string old_file)
{
	int l_backup_id=backupid;

	CWData data;
	data.addString(Server->ConvertToUTF8(fd->getFilenameW()));
	data.addInt(l_backup_id);
	data.addChar(r_incremental==true?1:0);
	data.addString(Server->ConvertToUTF8(dstpath));
	data.addString(Server->ConvertToUTF8(hashpath));
	if(hashoutput!=NULL)
	{
		data.addString(Server->ConvertToUTF8(hashoutput->getFilenameW()));
	}
	else
	{
		data.addString("");
	}

	data.addString(old_file);

	ServerLogger::Log(clientid, "GT: Loaded file \""+ExtractFileName(Server->ConvertToUTF8(dstpath))+"\"", LL_DEBUG);

	Server->destroy(fd);
	if(hashoutput!=NULL)
	{
		Server->destroy(hashoutput);
	}
	hashpipe_prepare->Write(data.getDataPtr(), data.getDataSize() );
}

bool ServerDownloadThread::isOffline()
{
	IScopedLock lock(mutex);
	return is_offline;
}

void ServerDownloadThread::queueStop(bool immediately)
{
	SQueueItem ni;
	ni.action = EQueueAction_Quit;

	IScopedLock lock(mutex);
	if(immediately)
	{
		dl_queue.push_front(ni);
	}
	else
	{
		dl_queue.push_back(ni);
	}
	cond->notify_one();
}

bool ServerDownloadThread::isDownloadOk( size_t id )
{
	return !std::binary_search(download_nok_ids.begin(), download_nok_ids.end(),
		id);
}

size_t ServerDownloadThread::getMaxOkId()
{
	return max_ok_id;
}

std::string ServerDownloadThread::getQueuedFileFull()
{
	IScopedLock lock(mutex);
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			!it->queued && it->fileclient==EFileClient_Full)
		{
			it->queued=true;
			return Server->ConvertToUTF8(getDLPath(*it));
		}
	}

	return std::string();
}

std::wstring ServerDownloadThread::getDLPath( SQueueItem todl )
{
	std::wstring cfn=todl.curr_path+L"/"+todl.fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	if(!server_token.empty())
	{
		cfn=widen(server_token)+L"|"+cfn;
	}

	return cfn;
}

void ServerDownloadThread::resetQueueFull()
{
	IScopedLock lock(mutex);
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

bool ServerDownloadThread::getQueuedFileChunked( std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFile*& hashoutput, _i64& predicted_filesize )
{
	IScopedLock lock(mutex);
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			!it->queued && it->fileclient==EFileClient_Chunked)
		{
			if(it->patch_dl_files.prepare_error)
			{
				continue;
			}
			
			remotefn = Server->ConvertToUTF8(getDLPath(*it));


			if(!it->patch_dl_files.prepared)
			{
				bool full_dl;
				it->patch_dl_files = preparePatchDownloadFiles(*it, full_dl);

				if(it->patch_dl_files.orig_file==NULL &&
					full_dl)
				{
					it->fileclient=EFileClient_Full;
					continue;
				}
			}

			if(it->patch_dl_files.prepared)
			{
				it->queued=true;
				orig_file = it->patch_dl_files.orig_file;
				patchfile = it->patch_dl_files.patchfile;
				chunkhashes = it->patch_dl_files.chunkhashes;
				hashoutput = it->patch_dl_files.hashoutput;
				predicted_filesize = it->predicted_filesize;
				return true;
			}
		}
	}

	return false;
}

void ServerDownloadThread::resetQueueChunked()
{
	IScopedLock lock(mutex);
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && it->fileclient==EFileClient_Chunked)
		{
			it->queued=false;
		}
	}
}

SPatchDownloadFiles ServerDownloadThread::preparePatchDownloadFiles( SQueueItem todl, bool& full_dl )
{
	SPatchDownloadFiles dlfiles = {};
	dlfiles.prepare_error=true;
	full_dl=false;

	std::wstring cfn=todl.curr_path+L"/"+todl.fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	std::wstring cfn_short=todl.os_path+L"/"+todl.short_fn;
	if(cfn_short[0]=='/')
		cfn_short.erase(0,1);

	std::wstring dstpath=backuppath+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	std::wstring hashpath=backuppath_hashes+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	std::wstring hashpath_old=last_backuppath+os_file_sep()+L".hashes"+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	std::wstring filepath_old=last_backuppath+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);

	std::auto_ptr<IFile> file_old(Server->openFile(os_file_prefix(filepath_old), MODE_READ));

	if(file_old.get()==NULL)
	{
		if(!last_backuppath_complete.empty())
		{
			filepath_old=last_backuppath_complete+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
			file_old.reset(Server->openFile(os_file_prefix(filepath_old), MODE_READ));
		}
		if(file_old.get()==NULL)
		{
			ServerLogger::Log(clientid, L"No old file for \""+todl.fn+L"\"", LL_DEBUG);
			full_dl=true;
			return dlfiles;
		}
		hashpath_old=last_backuppath_complete+os_file_sep()+L".hashes"+os_file_sep()+BackupServerGet::convertToOSPathFromFileClient(cfn_short);
	}

	IFile *pfd=BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid);
	if(pfd==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'pfd' in load_file_patch", LL_ERROR);
		return dlfiles;
	}
	ScopedDeleteFile pfd_delete(pfd);
	IFile *hash_tmp=BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid);
	if(hash_tmp==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'hash_tmp' in load_file_patch", LL_ERROR);
		return dlfiles;
	}
	ScopedDeleteFile hash_tmp_delete(pfd);

	if(!server_token.empty())
	{
		cfn=widen(server_token)+L"|"+cfn;
	}

	std::auto_ptr<IFile> hashfile_old(Server->openFile(os_file_prefix(hashpath_old), MODE_READ));

	dlfiles.delete_chunkhashes=false;
	if( (hashfile_old.get()==NULL || hashfile_old->Size()==0 ) && file_old.get()!=NULL )
	{
		ServerLogger::Log(clientid, L"Hashes for file \""+filepath_old+L"\" not available. Calulating hashes...", LL_DEBUG);
		hashfile_old.reset(BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid));
		if(hashfile_old.get()==NULL)
		{
			ServerLogger::Log(clientid, L"Error creating temporary file 'hashfile_old' in load_file_patch", LL_ERROR);
			return dlfiles;
		}
		dlfiles.delete_chunkhashes=true;
		BackupServerPrepareHash::build_chunk_hashs(file_old.get(), hashfile_old.get(), NULL, false, NULL, false);
		hashfile_old->Seek(0);
	}

	dlfiles.orig_file=file_old.release();
	dlfiles.patchfile=pfd;
	pfd_delete.release();
	dlfiles.chunkhashes=hashfile_old.release();
	dlfiles.hashoutput=hash_tmp;
	hash_tmp_delete.release();
	dlfiles.hashpath = hashpath;
	dlfiles.filepath_old = filepath_old;
	dlfiles.prepared=true;
	dlfiles.prepare_error=false;

	return dlfiles;
}

void ServerDownloadThread::start_shadowcopy(const std::string &path)
{
	server_get->sendClientMessage("START SC \""+path+"\"#token="+server_token, "DONE", L"Activating shadow copy on \""+clientname+L"\" for path \""+Server->ConvertToUnicode(path)+L"\" failed", shadow_copy_timeout);
}

void ServerDownloadThread::stop_shadowcopy(const std::string &path)
{
	server_get->sendClientMessage("STOP SC \""+path+"\"#token="+server_token, "DONE", L"Removing shadow copy on \""+clientname+L"\" for path \""+Server->ConvertToUnicode(path)+L"\" failed", shadow_copy_timeout);
}

void ServerDownloadThread::sleepQueue(IScopedLock& lock)
{
	while(dl_queue.size()>max_queue_size)
	{
		lock.relock(NULL);
		Server->wait(1000);
		lock.relock(mutex);
	}
}

void ServerDownloadThread::queueSkip()
{
	SQueueItem ni;
	ni.action = EQueueAction_Skip;

	IScopedLock lock(mutex);
	dl_queue.push_front(ni);
	cond->notify_one();
}
