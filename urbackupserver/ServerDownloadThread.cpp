
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

#include <algorithm>

#include "ServerDownloadThread.h"
#include "../Interface/Server.h"
#include "server_log.h"
#include "ClientMain.h"
#include "../stringtools.h"
#include "../common/data.h"
#include "../urbackupcommon/file_metadata.h"
#include "server_settings.h"
#include "server_cleanup.h"
#include "FileBackup.h"
#include "../urbackupcommon/os_functions.h"
#include "server.h"
#include "FileMetadataDownloadThread.h"

namespace
{
	const size_t max_queue_size = 500;
	const size_t queue_items_full = 1;
	const size_t queue_items_chunked = 4;
}

ServerDownloadThread::ServerDownloadThread( FileClient& fc, FileClientChunked* fc_chunked, const std::string& backuppath, const std::string& backuppath_hashes, const std::string& last_backuppath, const std::string& last_backuppath_complete, bool hashed_transfer, bool save_incomplete_file, int clientid,
	const std::string& clientname, const std::string& clientsubname, bool use_tmpfiles, const std::string& tmpfile_path, const std::string& server_token, bool use_reflink, int backupid, bool r_incremental, IPipe* hashpipe_prepare, ClientMain* client_main,
	int filesrv_protocol_version, int incremental_num, logid_t logid, bool with_hashes, const std::vector<std::string>& shares_without_snapshot, bool with_sparse_hashing, server::FileMetadataDownloadThread* file_metadata_download)
	: fc(fc), fc_chunked(fc_chunked), backuppath(backuppath), backuppath_hashes(backuppath_hashes), 
	last_backuppath(last_backuppath), last_backuppath_complete(last_backuppath_complete), hashed_transfer(hashed_transfer), save_incomplete_file(save_incomplete_file), clientid(clientid),
	clientname(clientname), clientsubname(clientsubname),
	use_tmpfiles(use_tmpfiles), tmpfile_path(tmpfile_path), server_token(server_token), use_reflink(use_reflink), backupid(backupid), r_incremental(r_incremental), hashpipe_prepare(hashpipe_prepare), max_ok_id(0),
	is_offline(false), client_main(client_main), filesrv_protocol_version(filesrv_protocol_version), skipping(false), queue_size(0),
	all_downloads_ok(true), incremental_num(incremental_num), logid(logid), has_timeout(false), with_hashes(with_hashes), with_metadata(client_main->getProtocolVersions().file_meta>0), shares_without_snapshot(shares_without_snapshot),
	with_sparse_hashing(with_sparse_hashing), exp_backoff(false), num_embedded_metadata_files(0), file_metadata_download(file_metadata_download), num_issues(0)
{
	mutex = Server->createMutex();
	cond = Server->createCondition();

	if (BackupServer::useTreeHashing())
	{
		default_hashing_method = HASH_FUNC_TREE;
	}
	else
	{
		default_hashing_method = HASH_FUNC_SHA512;
	}
}

ServerDownloadThread::~ServerDownloadThread()
{
	Server->destroy(mutex);
	Server->destroy(cond);
}

void ServerDownloadThread::operator()( void )
{
	if(fc_chunked!=NULL && filesrv_protocol_version>2)
	{
		fc_chunked->setQueueCallback(this);
	}

	if(filesrv_protocol_version>2)
	{
		fc.setQueueCallback(this);
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
			IScopedLock lock(mutex);
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
			if(curr.fileclient== EFileClient_Chunked)
			{
				ServerLogger::Log(logid, "Copying incomplete file \"" + curr.fn+ "\"", LL_DEBUG);								
				bool full_dl = false;
				
				if(!curr.patch_dl_files.prepared)
				{
					curr.patch_dl_files = preparePatchDownloadFiles(curr, full_dl);
				}				

				if(!full_dl && curr.patch_dl_files.prepared 
				    && !curr.patch_dl_files.prepare_error && curr.patch_dl_files.orig_file!=NULL)
				{
					if(link_or_copy_file(curr))
					{
						download_partial_ids.add(curr.id);
						max_ok_id = (std::max)(max_ok_id, curr.id);
					}
					else
		    		{
						ServerLogger::Log(logid, "Copying incomplete file \""+curr.fn+"\" failed", LL_WARNING);
						download_nok_ids.add(curr.id);					
						
						IScopedLock lock(mutex);
						all_downloads_ok=false;
					}
				
					continue;
				}
			}
			
			if (!curr.metadata_only)
			{
				download_nok_ids.add(curr.id);

				{
					IScopedLock lock(mutex);
					all_downloads_ok = false;
				}
			}

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
			start_shadowcopy(curr.fn);
			continue;
		}
		else if(curr.action==EQueueAction_StopShadowcopy)
		{
			stop_shadowcopy(curr.fn);
			continue;
		}		

		bool ret = true;

		if(curr.fileclient == EFileClient_Full)
		{
			if(curr.script_end)
			{
				fc.FinishScript(curr.fn);
			}
			else
			{
				ret = load_file(curr);
			}
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

	if(!is_offline && !skipping && client_main->getProtocolVersions().file_meta>0)
	{
		_u32 rc = fc.InformMetadataStreamEnd(server_token, 3);

		if(rc!=ERR_SUCCESS)
		{
			ServerLogger::Log(logid, "Error informing client about metadata stream end. Errorcode: "+fc.getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		}
	}

	download_nok_ids.finalize();
	download_partial_ids.finalize();
}

void ServerDownloadThread::addToQueueFull(size_t id, const std::string &fn, const std::string &short_fn, const std::string &curr_path,
	const std::string &os_path, _i64 predicted_filesize, const FileMetadata& metadata,
    bool is_script, bool metadata_only, size_t folder_items, const std::string& sha_dig, bool at_front_postpone_quitstop, unsigned int p_script_random)
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
	ni.predicted_filesize = predicted_filesize;
	ni.metadata = metadata;
	ni.is_script = is_script;
    ni.metadata_only = metadata_only;
	ni.folder_items = folder_items;
	ni.sha_dig = sha_dig;

	if(is_script)
	{
		if (p_script_random != 0)
		{
			ni.script_random = p_script_random;
		}
		else
		{
			ni.script_random = Server->getRandomNumber();
		}
	}

	IScopedLock lock(mutex);

	if(!at_front_postpone_quitstop)
	{
		dl_queue.push_back(ni);
	}
	else
	{
		ni.switched = true;
		size_t idx = insertFullQueueEarliest(ni, true);
		postponeQuitStop(idx);
	}

	cond->notify_one();

	queue_size+=queue_items_full;
}


void ServerDownloadThread::addToQueueChunked(size_t id, const std::string &fn, const std::string &short_fn,
	const std::string &curr_path, const std::string &os_path, _i64 predicted_filesize, const FileMetadata& metadata,
	bool is_script, const std::string& sha_dig, unsigned int p_script_random)
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
	ni.metadata = metadata;
	ni.is_script = is_script;
    ni.metadata_only = false;
	ni.sha_dig=sha_dig;

	if(is_script)
	{
		if (p_script_random != 0)
		{
			ni.script_random = p_script_random;
		}
		else
		{
			ni.script_random = Server->getRandomNumber();
		}
	}

	IScopedLock lock(mutex);
	dl_queue.push_back(ni);
	cond->notify_one();

	queue_size+=queue_items_chunked;
}

void ServerDownloadThread::addToQueueStartShadowcopy(const std::string& fn)
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
}

void ServerDownloadThread::addToQueueStopShadowcopy(const std::string& fn)
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
}

void ServerDownloadThread::queueScriptEnd(const std::string &fn)
{
	SQueueItem ni;
	ni.action = EQueueAction_Fileclient;
	ni.fn=fn;
	ni.id=std::string::npos;
	ni.patch_dl_files.prepared=false;
	ni.patch_dl_files.prepare_error=false;
	ni.is_script=true;
	ni.metadata_only=false;
	ni.script_end=true;

	IScopedLock lock(mutex);

	insertFullQueueEarliest(ni, false);

	cond->notify_one();
}


size_t ServerDownloadThread::insertFullQueueEarliest( SQueueItem ni, bool after_switched)
{
	size_t idx=0;
	size_t earlies_other_idx;
	bool no_queued=true;
	std::deque<SQueueItem>::iterator earliest_other=dl_queue.end();
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();it!=dl_queue.end();++it)
	{
		if(it->action == EQueueAction_Fileclient
			&& it->fileclient == EFileClient_Full)
		{
			if(!it->queued )
			{
				if (!after_switched || !it->switched)
				{
					if (earliest_other != dl_queue.end())
					{
						dl_queue.insert(earliest_other, ni);
						return earlies_other_idx;
					}
					else
					{
						dl_queue.insert(it, ni);
						return idx;
					}
				}
				else
				{
					earliest_other = it + 1;
					earlies_other_idx = idx+1;
				}
			}
			else
			{
				no_queued=false;
				earliest_other=dl_queue.end();
			}
		}
		else
		{
			if(earliest_other==dl_queue.end())
			{
				earliest_other = it;
				earlies_other_idx = idx;
			}
		}
		++idx;
	}
	if(no_queued)
	{
		dl_queue.push_front(ni);
		return 0;
	}
	else
	{
		dl_queue.push_back(ni);
		return dl_queue.size();
	}
}

bool ServerDownloadThread::hasFullQueuedAfter(std::deque<SQueueItem>::iterator it)
{
	for (; it != dl_queue.end(); ++it)
	{
		if (it->action == EQueueAction_Fileclient
			&& it->fileclient == EFileClient_Full)
		{
			if (it->queued)
			{
				return true;
			}
		}
	}
	return false;
}



bool ServerDownloadThread::load_file(SQueueItem todl)
{
	ServerLogger::Log(logid, "Loading file \""+todl.fn+"\"" + (todl.metadata_only ? " (metadata only)" : ""), LL_DEBUG);
	IFsFile *fd=NULL;
    if(!todl.metadata_only)
	{
		fd = ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
		if(fd==NULL)
		{
			ServerLogger::Log(logid, "Error creating temporary file 'fd' in load_file. " + os_last_error_str(), LL_ERROR);
			return false;
		}
	}
	

	std::string cfn=getDLPath(todl);

	int64 script_start_time = Server->getTimeSeconds()-60;

    _u32 rc=fc.GetFile(cfn, fd, hashed_transfer, todl.metadata_only, todl.folder_items, todl.is_script, with_metadata ? (todl.id+1) : 0);

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		fd->Seek(0);
        rc=fc.GetFile(cfn, fd, hashed_transfer, todl.metadata_only, todl.folder_items, todl.is_script, with_metadata ? (todl.id+1) : 0);
		--hash_retries;
	}

	bool ret = true;
	bool hash_file = false;
	bool script_ok = true;

	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error getting complete file \""+cfn+"\" from "+clientname+". Errorcode: "+fc.getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		{
			IScopedLock lock(mutex);
			all_downloads_ok=false;
		}

		if( (rc==ERR_TIMEOUT || rc==ERR_ERROR)
			&& save_incomplete_file
            && fd!=NULL && fd->Size()>0
                && !todl.metadata_only)
		{
			ServerLogger::Log(logid, "Saving incomplete file.", LL_INFO);
			hash_file = true;
			todl.sha_dig.clear();

			max_ok_id = (std::max)(max_ok_id, todl.id);
			download_partial_ids.add(todl.id);
		}
        else if(!todl.metadata_only)
		{
			download_nok_ids.add(todl.id);
			if(fd!=NULL)
			{
				ClientMain::destroyTemporaryFile(fd);
			}			
		}

		if(rc==ERR_TIMEOUT || rc==ERR_ERROR || rc==ERR_BASE_DIR_LOST)
		{
			ret=false;
			has_timeout = true;

			if (rc == ERR_BASE_DIR_LOST)
			{
				exp_backoff = true;
			}
		}

		if (rc == ERR_FILE_DOESNT_EXIST)
		{
			++num_issues;
		}
	}
	else
	{
		hash_file = true;
		if(todl.is_script)
		{
			queueScriptEnd(cfn);

			if (!todl.metadata_only)
			{
				script_ok = logScriptOutput(cfn, todl, todl.sha_dig, script_start_time, hash_file);

				if (!hash_file)
				{
					std::string os_curr_hash_path = FileBackup::convertToOSPathFromFileClient(todl.os_path + "/" + escape_metadata_fn(todl.short_fn));
					std::string hashpath = backuppath_hashes + os_curr_hash_path;

					if (os_directory_exists(os_file_prefix(hashpath))
						|| os_create_dir(os_file_prefix(hashpath)))
					{
						write_file_metadata(hashpath + os_file_sep() + metadata_dir_fn, client_main, todl.metadata, false);
					}
				}
			}
		}

		max_ok_id = (std::max)(max_ok_id, todl.id);
	}

    if(hash_file && !todl.metadata_only)
	{
		std::string os_curr_path=FileBackup::convertToOSPathFromFileClient(todl.os_path+"/"+todl.short_fn);
		std::string os_curr_hash_path=FileBackup::convertToOSPathFromFileClient(todl.os_path+"/"+escape_metadata_fn(todl.short_fn));
		std::string dstpath=backuppath+os_curr_path;
		std::string hashpath =backuppath_hashes+os_curr_hash_path;
		std::string filepath_old;
		
		if( use_reflink && (!last_backuppath.empty() || !last_backuppath_complete.empty() ) )
		{
			std::string cfn_short=todl.os_path+"/"+todl.short_fn;
			if(cfn_short[0]=='/')
				cfn_short.erase(0,1);

			filepath_old=last_backuppath+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);

			IFile *file_old=Server->openFile(os_file_prefix(filepath_old), MODE_READ);

			if(file_old==NULL)
			{
				if(!last_backuppath_complete.empty())
				{
					filepath_old=last_backuppath_complete+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);
					file_old=Server->openFile(os_file_prefix(filepath_old), MODE_READ);
				}
				if(file_old==NULL)
				{
					ServerLogger::Log(logid, "No old file for \""+todl.fn+"\" (2)", LL_DEBUG);
					filepath_old.clear();
				}
			}

			Server->destroy(file_old);
		}

		hashFile(dstpath, hashpath, fd, NULL, filepath_old, fd->Size(), todl.metadata, todl.is_script, todl.sha_dig, fc.releaseSparseExtendsFile(),
			todl.is_script ? HASH_FUNC_SHA512_NO_SPARSE : default_hashing_method, fileHasSnapshot(todl));
	}
	else
	{
		fc.resetSparseExtentsFile();
	}

	if(todl.is_script && (rc!=ERR_SUCCESS || !script_ok) )
	{
		return false;
	}

	return ret;
}

bool ServerDownloadThread::link_or_copy_file(SQueueItem todl)
{
	SPatchDownloadFiles dlfiles = todl.patch_dl_files;
	
	std::string os_curr_path=FileBackup::convertToOSPathFromFileClient(todl.os_path+"/"+todl.short_fn);
	std::string dstpath=backuppath+os_curr_path;
	std::string dsthashpath = backuppath_hashes +os_curr_path;
	
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
	
	
	if( os_create_hardlink(os_file_prefix(dstpath), dlfiles.orig_file->getFilename(), use_reflink, NULL)
	    && os_create_hardlink(os_file_prefix(dsthashpath), dlfiles.chunkhashes->getFilename(), use_reflink, NULL) )
	{
		return true;
	}
	else
	{
		Server->deleteFile(os_file_prefix(dstpath));			
		
		bool ok = dlfiles.patchfile->Seek(0);
		int64 orig_filesize = dlfiles.orig_file->Size();
		int64 endian_filesize = little_endian(orig_filesize);
		ok = ok && (dlfiles.patchfile->Write(reinterpret_cast<char*>(&endian_filesize), sizeof(endian_filesize))==sizeof(endian_filesize));
			
		std::string hashfile_old_fn = dlfiles.chunkhashes->getFilename();
		std::string hashoutput_fn = dlfiles.hashoutput->getFilename();
		
		hash_tmp_destroy.release();
		delete dlfiles.hashoutput;
		dlfiles.hashoutput=NULL;
			
		if(ok && copy_file(hashfile_old_fn, hashoutput_fn) 
		    && (dlfiles.hashoutput=Server->openFile(hashoutput_fn, MODE_RW))!=NULL )
		{
			pfd_destroy.release();
			hashFile(dstpath, dlfiles.hashpath, dlfiles.patchfile, dlfiles.hashoutput,
			    (dlfiles.filepath_old), orig_filesize, todl.metadata, todl.is_script, todl.sha_dig, NULL,
				todl.is_script ? HASH_FUNC_SHA512_NO_SPARSE : default_hashing_method, fileHasSnapshot(todl));
			return true;
		}
		else
		{
			return false;
		}
	}
}


bool ServerDownloadThread::load_file_patch(SQueueItem todl)
{
	std::string cfn=todl.curr_path+"/"+todl.fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	if(todl.is_script)
	{
		cfn = "SCRIPT|" + cfn + "|" + convert(incremental_num) + "|" + convert(todl.script_random)+"|"+server_token;
	}

	bool full_dl=false;
	SPatchDownloadFiles& dlfiles = todl.patch_dl_files;
	if(!dlfiles.prepared && !dlfiles.prepare_error)
	{
		dlfiles = preparePatchDownloadFiles(todl, full_dl);

		if(dlfiles.orig_file==NULL && full_dl)
		{
            addToQueueFull(todl.id, todl.fn, todl.short_fn, todl.curr_path, todl.os_path,
				todl.predicted_filesize, todl.metadata, todl.is_script, todl.metadata_only, todl.folder_items, todl.sha_dig, true, todl.script_random);
			return true;
		}
	}

	if(dlfiles.prepare_error)
	{
		return false;
	}


	ServerLogger::Log(logid, "Loading file patch for \""+todl.fn+"\"", LL_DEBUG);

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

	if(!server_token.empty() && !todl.is_script)
	{
		cfn=server_token+"|"+cfn;
	}

	int64 script_start_time = Server->getTimeSeconds()-60;

	IFile* sparse_extents_f=NULL;
	_u32 rc=fc_chunked->GetFilePatch((cfn), dlfiles.orig_file, dlfiles.patchfile, dlfiles.chunkhashes, dlfiles.hashoutput,
		todl.predicted_filesize, with_metadata ? (todl.id+1) : 0, todl.is_script, &sparse_extents_f);

	int64 download_filesize = todl.predicted_filesize;

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		dlfiles.orig_file->Seek(0);
		dlfiles.patchfile=ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
		if(dlfiles.patchfile==NULL)
		{
			ServerLogger::Log(logid, "Error creating temporary file 'pfd' in load_file_patch", LL_ERROR);
			return false;
		}
		pfd_destroy.reset(dlfiles.patchfile);
		dlfiles.hashoutput=ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
		if(dlfiles.hashoutput==NULL)
		{
			ServerLogger::Log(logid, "Error creating temporary file 'hash_tmp' in load_file_patch -2", LL_ERROR);
			return false;
		}
		hash_tmp_destroy.reset(dlfiles.hashoutput);
		dlfiles.chunkhashes->Seek(0);
		download_filesize = todl.predicted_filesize;
		rc=fc_chunked->GetFilePatch((cfn), dlfiles.orig_file, dlfiles.patchfile, dlfiles.chunkhashes, dlfiles.hashoutput,
			download_filesize, with_metadata ? (todl.id+1) : 0, todl.is_script, &sparse_extents_f);
		--hash_retries;
	}

	ScopedDeleteFile sparse_extents_f_delete(sparse_extents_f);

	if(download_filesize<0)
	{
		Server->Log("download_filesize is smaller than zero", LL_DEBUG);
		download_filesize=todl.predicted_filesize;
	}

	bool hash_file;

	bool script_ok = true;

	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error getting file patch for \""+cfn+"\" from "+clientname+". Errorcode: "+FileClient::getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);

		if(rc==ERR_ERRORCODES)
		{
			ServerLogger::Log(logid, "Remote Error: "+fc_chunked->getErrorcodeString(), LL_ERROR);
		}

		if(rc==ERR_TIMEOUT || rc==ERR_CONN_LOST || rc==ERR_SOCKET_ERROR || rc==ERR_BASE_DIR_LOST)
		{
			has_timeout=true;

			if (rc == ERR_BASE_DIR_LOST)
			{
				exp_backoff = true;
			}
		}

		if (rc == ERR_FILE_DOESNT_EXIST)
		{
			++num_issues;
		}

		{
			IScopedLock lock(mutex);
			all_downloads_ok=false;
		}

		if( rc==ERR_BASE_DIR_LOST && save_incomplete_file)
		{
			ServerLogger::Log(logid, "Saving incomplete file. (2)", LL_INFO);
			
			pfd_destroy.release();
			hash_tmp_destroy.release();
			hashfile_old_destroy.release();
			file_old_destroy.release();
			hashfile_old_delete.release();
			
			if(link_or_copy_file(todl))
			{
				max_ok_id = (std::max)(max_ok_id, todl.id);
				download_partial_ids.add(todl.id);
			}
			else
			{
				download_nok_ids.add(todl.id);				
			}
			
			hash_file=false;
		}
		else if( (rc==ERR_TIMEOUT || rc==ERR_CONN_LOST || rc==ERR_SOCKET_ERROR)
			&& dlfiles.patchfile->Size()>0
			&& save_incomplete_file)
		{
			ServerLogger::Log(logid, "Saving incomplete file.", LL_INFO);
			hash_file=true;
			todl.sha_dig.clear();

			max_ok_id = (std::max)(max_ok_id, todl.id);
			download_partial_ids.add(todl.id);
		}
		else
		{
			hash_file=false;
			download_nok_ids.add(todl.id);
		}
	}
	else
	{
		hash_file = true;

		if(todl.is_script)
		{
			queueScriptEnd(cfn);

			if (!todl.metadata_only)
			{
				script_ok = logScriptOutput(cfn, todl, todl.sha_dig, script_start_time, hash_file);

				if (!hash_file)
				{
					std::string os_curr_hash_path = FileBackup::convertToOSPathFromFileClient(todl.os_path + "/" + escape_metadata_fn(todl.short_fn));
					std::string hashpath = backuppath_hashes + os_curr_hash_path;

					if (os_directory_exists(os_file_prefix(hashpath))
						|| os_create_dir(os_file_prefix(hashpath)))
					{
						write_file_metadata(hashpath + os_file_sep() + metadata_dir_fn, client_main, todl.metadata, false);
					}
				}
			}
		}

		max_ok_id = (std::max)(max_ok_id, todl.id);
	}

	if(hash_file)
	{
		std::string os_curr_path=FileBackup::convertToOSPathFromFileClient(todl.os_path+"/"+todl.short_fn);		
		std::string dstpath=backuppath+os_curr_path;

		pfd_destroy.release();
		hash_tmp_destroy.release();
		sparse_extents_f_delete.release();
		hashFile(dstpath, dlfiles.hashpath, dlfiles.patchfile, dlfiles.hashoutput,
			dlfiles.filepath_old, download_filesize, todl.metadata, todl.is_script, todl.sha_dig, sparse_extents_f,
			todl.is_script ? HASH_FUNC_SHA512_NO_SPARSE : default_hashing_method, fileHasSnapshot(todl));
	}

	if(todl.is_script && (rc!=ERR_SUCCESS || !script_ok) )
	{
		return false;
	}

	if(rc==ERR_TIMEOUT || rc==ERR_ERROR || rc==ERR_SOCKET_ERROR
		|| rc==ERR_INT_ERROR || rc==ERR_BASE_DIR_LOST || rc==ERR_CONN_LOST
		|| (rc == ERR_FILE_DOESNT_EXIST && fileHasSnapshot(todl) ) )
		return false;
	else
		return true;
}

void ServerDownloadThread::hashFile(std::string dstpath, std::string hashpath, IFile *fd, IFile *hashoutput, std::string old_file,
	int64 t_filesize, const FileMetadata& metadata, bool is_script, std::string sha_dig, IFile* sparse_extents_f, char hashing_method,
	bool has_snapshot)
{
	int l_backup_id=backupid;

	CWData data;
	data.addString(fd->getFilename());
	data.addInt(l_backup_id);
	data.addInt(r_incremental?1:0);
	data.addChar(with_hashes?1:0);
	data.addString(dstpath);
	data.addString(hashpath);
	if(hashoutput!=NULL)
	{
		data.addString(hashoutput->getFilename());
	}
	else
	{
		data.addString("");
	}

	data.addString(old_file);
	data.addInt64(t_filesize);
	data.addString(with_sparse_hashing ? sha_dig : std::string());
	data.addString(sparse_extents_f!=NULL ? sparse_extents_f->getFilename() : "");
	data.addChar(hashing_method);
	data.addChar(has_snapshot ? 1 : 0);
	metadata.serialize(data);

	ServerLogger::Log(logid, "GT: Loaded file \""+ExtractFileName((dstpath))+"\"", LL_DEBUG);

	Server->destroy(fd);
	Server->destroy(sparse_extents_f);
	if(hashoutput!=NULL)
	{
		if(!is_script)
		{
			int64 expected_hashoutput_size = get_hashdata_size(t_filesize);
			if(hashoutput->Size()>expected_hashoutput_size)
			{
				std::string hashoutput_fn = hashoutput->getFilename();
				Server->destroy(hashoutput);
				os_file_truncate(hashoutput_fn, expected_hashoutput_size);			
			}
			else
			{
				Server->destroy(hashoutput);
			}
		}
		else
		{
			Server->destroy(hashoutput);
		}
		
	}
	hashpipe_prepare->Write(data.getDataPtr(), data.getDataSize() );
}

bool ServerDownloadThread::isOffline()
{
	IScopedLock lock(mutex);
	return is_offline;
}

void ServerDownloadThread::queueStop()
{
	SQueueItem ni;
	ni.action = EQueueAction_Quit;

	IScopedLock lock(mutex);
	dl_queue.push_back(ni);
	cond->notify_one();
}

bool ServerDownloadThread::isDownloadOk( size_t id )
{
	return !download_nok_ids.hasId(id);
}


bool ServerDownloadThread::isDownloadPartial( size_t id )
{
	return download_partial_ids.hasId(id);
}


size_t ServerDownloadThread::getMaxOkId()
{
	return max_ok_id;
}

std::string ServerDownloadThread::getQueuedFileFull(FileClient::MetadataQueue& metadata, size_t& folder_items, bool& finish_script, int64& file_id)
{
	IScopedLock lock(mutex);
	int max_prepare = 1;
	bool retry = true;
	while (retry)
	{
		retry = false;
		for (std::deque<SQueueItem>::iterator it = dl_queue.begin();
				it != dl_queue.end(); ++it)
		{
			if (it->action == EQueueAction_Fileclient &&
				!it->queued && it->fileclient == EFileClient_Chunked
				&& max_prepare > 0)
			{
				if (it->patch_dl_files.prepare_error)
				{
					continue;
				}

				if (!it->patch_dl_files.prepared)
				{
					--max_prepare;

					bool full_dl;
					it->patch_dl_files = preparePatchDownloadFiles(*it, full_dl);

					if (it->patch_dl_files.orig_file == NULL &&
						full_dl)
					{
						if (hasFullQueuedAfter(it))
						{
							SQueueItem item = *it;
							dl_queue.erase(it);
							item.fileclient = EFileClient_Full;
							item.switched = true;
							size_t new_idx = insertFullQueueEarliest(item, true);
							postponeQuitStop(new_idx);
							queue_size -= queue_items_chunked - queue_items_full;
							retry = true;
							break;
						}
						else
						{
							it->fileclient = EFileClient_Full;
							it->switched = true;
							queue_size -= queue_items_chunked - queue_items_full;
						}						
					}
				}
			}

			if (it->action == EQueueAction_Fileclient &&
				!it->queued && it->fileclient == EFileClient_Full)
			{
				it->queued = true;
				file_id = with_metadata ? (it->id + 1) : 0;
				metadata = it->metadata_only ? FileClient::MetadataQueue_Metadata : FileClient::MetadataQueue_Data;
				folder_items = it->folder_items;
				finish_script = it->script_end;
				return (getDLPath(*it));
			}
		}
	}

	return std::string();
}

std::string ServerDownloadThread::getDLPath( SQueueItem todl )
{
	std::string cfn=todl.curr_path+"/"+todl.fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	if(todl.is_script)
	{
		cfn = "SCRIPT|" + cfn + "|" + convert(incremental_num) + "|" + convert(todl.script_random)+"|"+server_token;
	}
	else if(!server_token.empty())
	{
		cfn=server_token+"|"+cfn;
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

bool ServerDownloadThread::getQueuedFileChunked( std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFile*& hashoutput, _i64& predicted_filesize, int64& file_id, bool& is_script)
{
	IScopedLock lock(mutex);
	bool retry=true;
	while(retry)
	{
		retry=false;

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

				remotefn = (getDLPath(*it));

				if(!it->patch_dl_files.prepared)
				{
					bool full_dl;
					it->patch_dl_files = preparePatchDownloadFiles(*it, full_dl);

					if(it->patch_dl_files.orig_file==NULL &&
						full_dl)
					{
						if (hasFullQueuedAfter(it))
						{
							SQueueItem item = *it;
							dl_queue.erase(it);
							item.fileclient = EFileClient_Full;
							item.switched = true;
							size_t new_idx = insertFullQueueEarliest(item, true);
							postponeQuitStop(new_idx);
							queue_size -= queue_items_chunked - queue_items_full;
							retry = true;
							break;
						}
						else
						{
							it->fileclient = EFileClient_Full;
							it->switched = true;
							queue_size -= queue_items_chunked - queue_items_full;
						}
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
					file_id = with_metadata ? (it->id+1) : 0;
					is_script = it->is_script;
					return true;
				}
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

	std::string cfn=todl.curr_path+"/"+todl.fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	std::string cfn_short=todl.os_path+"/"+todl.short_fn;
	if(cfn_short[0]=='/')
		cfn_short.erase(0,1);

	std::string dstpath=backuppath+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);
	std::string hashpath=backuppath_hashes+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);
	std::string hashpath_old=last_backuppath+os_file_sep()+".hashes"+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);
	std::string filepath_old=last_backuppath+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);

	std::auto_ptr<IFsFile> file_old(Server->openFile(os_file_prefix(filepath_old), MODE_READ));

	if(file_old.get()==NULL)
	{
		if(!last_backuppath_complete.empty())
		{
			filepath_old=last_backuppath_complete+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);
			file_old.reset(Server->openFile(os_file_prefix(filepath_old), MODE_READ));
		}
		if(file_old.get()==NULL)
		{
			ServerLogger::Log(logid, "No old file for \""+todl.fn+"\" (1)", LL_DEBUG);
			full_dl=true;
			return dlfiles;
		}
		hashpath_old=last_backuppath_complete+os_file_sep()+".hashes"+os_file_sep()+FileBackup::convertToOSPathFromFileClient(cfn_short);
	}

	IFile *pfd=ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
	if(pfd==NULL)
	{
		ServerLogger::Log(logid, "Error creating temporary file 'pfd' in load_file_patch", LL_ERROR);
		return dlfiles;
	}
	ScopedDeleteFile pfd_delete(pfd);
	IFile *hash_tmp=ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
	if(hash_tmp==NULL)
	{
		ServerLogger::Log(logid, "Error creating temporary file 'hash_tmp' in load_file_patch", LL_ERROR);
		return dlfiles;
	}
	ScopedDeleteFile hash_tmp_delete(pfd);

	if(!server_token.empty())
	{
		cfn=server_token+"|"+cfn;
	}

	std::auto_ptr<IFile> hashfile_old(Server->openFile(os_file_prefix(hashpath_old), MODE_READ));

	dlfiles.delete_chunkhashes=false;
	if( (hashfile_old.get()==NULL ||
		hashfile_old->Size()==0  ||
		is_metadata_only(hashfile_old.get()) ) 
		  && file_old.get()!=NULL )
	{
		ServerLogger::Log(logid, "Hashes for file \""+filepath_old+"\" not available. Calulating hashes...", LL_DEBUG);
		hashfile_old.reset(ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid));
		if(hashfile_old.get()==NULL)
		{
			ServerLogger::Log(logid, "Error creating temporary file 'hashfile_old' in load_file_patch", LL_ERROR);
			return dlfiles;
		}
		dlfiles.delete_chunkhashes=true;
		FsExtentIterator extent_iterator(file_old.get());
		build_chunk_hashs(file_old.get(), hashfile_old.get(), NULL, NULL, false, NULL, NULL, false, NULL, &extent_iterator);
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

void ServerDownloadThread::start_shadowcopy(std::string path)
{
	if (!clientsubname.empty())
	{
		path += "/clientsubname=" + EscapeParamString(clientsubname);
	}

	client_main->sendClientMessageRetry("START SC \""+path+"\"#token="+server_token, "DONE", "Referencing snapshot on \""+clientname+"\" for path \""+path+"\" failed", 10000, 10);
}

void ServerDownloadThread::stop_shadowcopy(std::string path)
{
	if (!clientsubname.empty())
	{
		path += "/clientsubname=" + EscapeParamString(clientsubname);
	}

	if (fc_chunked != NULL)
	{
		fc_chunked->freeFile();
	}

	bool in_use_log = false;
	bool in_use = false;
	do
	{
		std::string ret = client_main->sendClientMessageRetry("STOP SC \"" + path + "\"#token=" + server_token, "Removing snapshot on \"" + clientname + "\" for path \"" + path+"\" failed", 10000, 10);

		in_use = false;

		if (ret == "IN USE")
		{
			in_use = true;

			if (!in_use_log)
			{
				in_use_log = true;
				ServerLogger::Log(logid, "Share \"" + path + "\" on \"" + clientname + "\" is still in use (meta-data transfer). Waiting before removing snapshot...", LL_DEBUG);

				if(file_metadata_download!=NULL)
					file_metadata_download->setProgressLogEnabled(true);
			}

			Server->wait(30000);
		}
		else if (ret != "DONE")
		{
			ServerLogger::Log(logid, "Removing snapshot on \"" + clientname + "\" for path \"" + path + "\" failed", LL_ERROR);
		}

	} while (in_use);


	if (in_use_log)
	{
		if (file_metadata_download != NULL)
			file_metadata_download->setProgressLogEnabled(false);
	}
}

bool ServerDownloadThread::sleepQueue()
{
	IScopedLock lock(mutex);
	if(queue_size>max_queue_size)
	{
		lock.relock(NULL);
		Server->wait(1000);
		return true;
	}
	return false;
}

std::map<std::string, std::string>& ServerDownloadThread::getFilePathCorrections()
{
	return filepath_corrections;
}

size_t ServerDownloadThread::getNumEmbeddedMetadataFiles()
{
	return num_embedded_metadata_files;
}

size_t ServerDownloadThread::getNumIssues()
{
	return num_issues;
}

void ServerDownloadThread::queueSkip()
{
	SQueueItem ni;
	ni.action = EQueueAction_Skip;

	IScopedLock lock(mutex);
	dl_queue.push_front(ni);
	cond->notify_one();
}

void ServerDownloadThread::unqueueFileFull( const std::string& fn, bool finish_script)
{
	IScopedLock lock(mutex);
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			it->queued && it->fileclient==EFileClient_Full
			&& it->script_end == finish_script
			&& (getDLPath(*it)) == fn)
		{
			it->queued=false;
			return;
		}
	}
}

void ServerDownloadThread::unqueueFileChunked( const std::string& remotefn )
{
	IScopedLock lock(mutex);
	for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
		it!=dl_queue.end();++it)
	{
		if(it->action==EQueueAction_Fileclient && 
			it->queued && it->fileclient==EFileClient_Chunked
			&& (getDLPath(*it)) == remotefn )
		{
			it->queued=false;
			return;
		}
	}
}

bool ServerDownloadThread::isAllDownloadsOk()
{
	IScopedLock lock(mutex);
	return all_downloads_ok;
}

bool ServerDownloadThread::logScriptOutput(std::string cfn, const SQueueItem &todl, std::string& sha_dig, int64 script_start_times, bool& hash_file)
{
	std::string script_output = client_main->sendClientMessageRetry("SCRIPT STDERR "+cfn,
		"Error getting script output for command \""+todl.fn+"\"", 120000, 10, true);

	if(script_output=="err")
	{
		ServerLogger::Log(logid, "Error getting script output for command \""+todl.fn+"\" (err response)", LL_ERROR);
		return false;
	}

	if(!script_output.empty())
	{
		size_t retval_stop=script_output.find(" ");
		size_t time_stop=std::string::npos;
		if(retval_stop!=std::string::npos)
		{
			time_stop=script_output.find(" ", retval_stop+1);
		}

		if(retval_stop!=std::string::npos
			&& time_stop!=std::string::npos)
		{
			int retval = atoi(script_output.substr(0, retval_stop).c_str());
			int64 last_timems = os_atoi64(script_output.substr(retval_stop+1, time_stop-retval_stop));


			script_output = script_output.substr(time_stop+1);


			std::string line;
			int64 curr_time=Server->getTimeSeconds();
			int64 timems;
			for(size_t i=0;i<script_output.size();)
			{
				if(script_output[i]==0)
				{
					if(i+sizeof(int64)+sizeof(unsigned int)>=script_output.size())
					{
						ServerLogger::Log(logid, "Error parsing script output for command \""+todl.fn+"\" -1", LL_ERROR);
						break;
					}

					memcpy(reinterpret_cast<char*>(&timems), &script_output[i+1], sizeof(timems));
					timems = little_endian(timems);

					unsigned int msgsize;
					memcpy(reinterpret_cast<char*>(&msgsize), &script_output[i+1+sizeof(timems)], sizeof(msgsize));
					msgsize = little_endian(msgsize);

					for(size_t j=i+1+sizeof(int64)+sizeof(unsigned int);
						j<i+1+sizeof(timems)+sizeof(msgsize)+msgsize && j<script_output.size();++j)
					{
						if(script_output[j]=='\n')
						{
							int64 times = curr_time - (last_timems - timems)/1000;

							if(times<script_start_times || times>curr_time)
							{
								times = curr_time;
							}

							ServerLogger::Log(times, logid, todl.fn + ": " + trim(line), retval!=0?LL_ERROR:LL_INFO);
							line.clear();
						}
						else
						{
							line+=script_output[j];
						}
					}

					i+=1+sizeof(timems)+sizeof(msgsize)+msgsize;
				}
				else if(script_output[i]==1)
				{					
					if(i+SHA_DEF_DIGEST_SIZE+1<=script_output.size())
					{
						if (sha_dig.empty())
						{
							sha_dig.assign(script_output.begin() + i + 1, script_output.begin() + i + 1 + SHA_DEF_DIGEST_SIZE);
						}						
					}
					else
					{
						ServerLogger::Log(logid, "Error parsing script output for command \"" + todl.fn + "\" -3", LL_ERROR);
						break;
					}

					i+=SHA_DEF_DIGEST_SIZE+1;
				}
				else if (script_output[i] == 2)
				{
					if (i + 1 + sizeof(_u32) <= script_output.size())
					{
						_u32 data_size;
						memcpy(&data_size, &script_output[i + 1], sizeof(data_size));
						data_size = little_endian(data_size);

						if (i + 1 + sizeof(_u32) + data_size <= script_output.size()
							&& data_size < 1 * 1024 * 1024)
						{
							if (!next(cfn, 0, "SCRIPT|urbackup/TAR"))
							{
								std::string tardirfn = ExtractFileName(getbetween("SCRIPT|", "|", cfn), "/");

								if (tardirfn != ".." && tardirfn != ".")
								{
									std::string tardirpath = backuppath + os_file_sep() + "urbackup_backup_scripts" + os_file_sep() + tardirfn;
									if (!os_directory_exists(os_file_prefix(tardirpath))
										&& !os_create_dir(os_file_prefix(tardirpath)))
									{
										ServerLogger::Log(logid, "Error creating TAR dir at \"" + tardirpath + "\"", LL_ERROR);
										break;
									}

									tardirpath = backuppath_hashes + os_file_sep() + "urbackup_backup_scripts" + os_file_sep() + tardirfn;

									if (!os_directory_exists(os_file_prefix(tardirpath))
										&& !os_create_dir(os_file_prefix(tardirpath)))
									{
										ServerLogger::Log(logid, "Error creating TAR dir at \"" + tardirpath + "\"", LL_ERROR);
										break;
									}
								}

								hash_file = false;
							}

							CRData fdata(&script_output[i + 1 + sizeof(_u32)], data_size);
							std::string fn;
							char is_dir;
							char is_symlink;
							char is_special;
							std::string symlink_target;
							int64 fsize;
							unsigned int script_random;
							bool b = fdata.getStr(&fn);
							b &= fdata.getChar(&is_dir);
							b &= fdata.getChar(&is_symlink);
							b &= fdata.getChar(&is_special);
							b &= fdata.getStr(&symlink_target);
							b &= fdata.getVarInt(&fsize);
							b &= fdata.getUInt(&script_random);

							if (!b)
							{
								ServerLogger::Log(logid, "Error parsing script output (tar file) for command \"" + todl.fn + "\"", LL_ERROR);
								break;
							}

							std::string os_path = os_file_sep() + tarFnToOsPath(fn);
							FileMetadata metadata;

							std::string remote_fn = "urbackup/TAR|" + server_token + "|" + fn;
							if (is_dir == 0 && is_symlink == 0 && is_special == 0)
							{
								if (fc_chunked != NULL)
								{
									addToQueueChunked(0, ExtractFileName(remote_fn, "/"),
										ExtractFileName(os_path, os_file_sep()),
										ExtractFilePath(remote_fn, "/"),
										ExtractFilePath(os_path, os_file_sep()),
										fsize, metadata, true, std::string(), script_random);
								}
								else
								{
									addToQueueFull(0, ExtractFileName(remote_fn, "/"),
										ExtractFileName(os_path, os_file_sep()),
										ExtractFilePath(remote_fn, "/"),
										ExtractFilePath(os_path, os_file_sep()),
										fsize, metadata, true, false, 0, std::string(), false, script_random);
								}
							}
							else
							{
								if (is_dir != 0)
								{
									if (!os_directory_exists(os_file_prefix(backuppath + os_path))
										&& !os_create_dir(os_file_prefix(backuppath + os_path)))
									{
										ServerLogger::Log(logid, "Error creating TAR dir at \"" + backuppath + os_path + "\"", LL_ERROR);
										break;
									}

									if (!os_directory_exists(os_file_prefix(backuppath_hashes + os_path))
										&& !os_create_dir(os_file_prefix(backuppath_hashes + os_path)))
									{
										ServerLogger::Log(logid, "Error creating TAR dir at \"" + backuppath_hashes + os_path + "\"", LL_ERROR);
										break;
									}
								}
								else
								{
									std::auto_ptr<IFile> touch_file(Server->openFile(os_file_prefix(backuppath + os_path), MODE_WRITE));
									if (touch_file.get() == NULL)
									{
										ServerLogger::Log(logid, "Error touching TAR special file at \"" + backuppath + os_path + "\"", LL_ERROR);
										break;
									}

									addToQueueFull(0, ExtractFileName(remote_fn, "/"),
										ExtractFileName(os_path, os_file_sep()),
										ExtractFilePath(remote_fn, "/"),
										ExtractFilePath(os_path, os_file_sep()), 0, metadata, true, true, 0, std::string(),
										false, script_random);
								}
							}

							i += 1 + sizeof(_u32) + data_size;
						}
						else
						{
							ServerLogger::Log(logid, "Error parsing script output for command \"" + todl.fn + "\" -5", LL_ERROR);
							break;
						}
					}
					else
					{
						ServerLogger::Log(logid, "Error parsing script output for command \"" + todl.fn + "\" -4", LL_ERROR);
						break;
					}
				}
				else if (script_output[i] == 3)
				{
					if (i + sizeof(_u32) + 1 <= script_output.size())
					{
						_u32 small_files;
						memcpy(&small_files, &script_output[i + 1], sizeof(small_files));
						small_files = little_endian(small_files);
						num_embedded_metadata_files += small_files;
					}
					else
					{
						ServerLogger::Log(logid, "Error parsing script output for command \"" + todl.fn + "\" -4", LL_ERROR);
						break;
					}

					i += sizeof(_u32) + 1;
				}
				else
				{
					ServerLogger::Log(logid, "Error parsing script output for command \""+todl.fn+"\" -2", LL_ERROR);
					break;
				}
			}
			
			if(!line.empty())
			{
				int64 times = curr_time - (last_timems - timems)/1000;
				ServerLogger::Log(times, logid, todl.fn + ": " + trim(line), retval!=0?LL_ERROR:LL_INFO);
			}

			if(retval!=0)
			{
				ServerLogger::Log(logid, "Script \""+todl.fn+"\" returned a nun-null value "+convert(retval)+". Failing backup.", LL_ERROR);
				return false;
			}
		}		
	}
	else
	{
		return false;
	}

	if (sha_dig.empty())
	{
		ServerLogger::Log(logid, "Missing checksum for script output for command \"" + todl.fn + "\"", LL_WARNING);
	}
	
	return true;
}

bool ServerDownloadThread::hasTimeout()
{
	return has_timeout;
}

bool ServerDownloadThread::shouldBackoff()
{
	return exp_backoff;
}

void ServerDownloadThread::postponeQuitStop( size_t idx )
{
	while(idx>0)
	{
		size_t curr_idx=0;
		SQueueItem postpone_item;
		bool has_postpone_item = false;
		for(std::deque<SQueueItem>::iterator it=dl_queue.begin();it!=dl_queue.end() && curr_idx<idx;++it)
		{
			if(it->action==EQueueAction_Quit || it->action==EQueueAction_StopShadowcopy)
			{
				postpone_item = *it;
				has_postpone_item = true;
				--idx;
				dl_queue.erase(it);
				break;
			}
			++curr_idx;
		}

		if(!has_postpone_item)
		{
			return;
		}

		curr_idx=0;
		bool inserted_item=false;
		for(std::deque<SQueueItem>::iterator it=dl_queue.begin();it!=dl_queue.end();++it)
		{
			if(curr_idx>idx)
			{
				dl_queue.insert(it, postpone_item);
				inserted_item=true;
				break;
			}
			++curr_idx;
		}

		if(!inserted_item)
		{
			dl_queue.push_back(postpone_item);
		}
	}
}

bool ServerDownloadThread::fileHasSnapshot(const SQueueItem& todl)
{
	std::string cfn = todl.curr_path + "/" + todl.fn;
	if (cfn[0] == '/')
		cfn.erase(0, 1);

	std::string share = getuntil("/", cfn);
	if (share.empty())
	{
		share = cfn;
	}

	return std::find(shares_without_snapshot.begin(), shares_without_snapshot.end(), share) == shares_without_snapshot.end();
}

std::string ServerDownloadThread::tarFnToOsPath(const std::string & tar_path)
{
	std::vector<std::string> toks;
	TokenizeMail(tar_path, toks, "/");

	std::string ret;
	for (size_t i = 0; i < toks.size(); ++i)
	{
		toks[i] = greplace(os_file_sep(), "", toks[i]);

		if (toks[i] == "." || toks[i] == "..")
		{
			continue;
		}

		if (toks[i].empty())
			continue;
		
		if (i + 1 >= toks.size())
		{
			std::set<std::string>& dir_tar_fns = tar_filenames[ret];

			std::string corr_fn = FileBackup::fixFilenameForOS(toks[i], dir_tar_fns, ret, true, logid, filepath_corrections);

			if (!ret.empty())
			{
				ret += os_file_sep();
			}
			ret += corr_fn;
		}
		else
		{
			if (!ret.empty())
			{
				ret += os_file_sep();
			}
			std::string tmp_ret = ret + toks[i];

			std::map<std::string, std::string>::iterator it = filepath_corrections.find(tmp_ret);
			if (it != filepath_corrections.end())
			{
				ret += it->second;
			}
			else
			{
				ret += toks[i];
			}
		}		
	}

	return ret;
}
