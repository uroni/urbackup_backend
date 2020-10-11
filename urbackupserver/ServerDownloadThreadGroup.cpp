#include "ServerDownloadThreadGroup.h"

ServerDownloadThreadGroup::ServerDownloadThreadGroup(FileClient& fc, FileClientChunked* fc_chunked, const std::string& backuppath,
	const std::string& backuppath_hashes, const std::string& last_backuppath, const std::string& last_backuppath_complete,
	bool hashed_transfer, bool save_incomplete_file, int clientid, const std::string& clientname, const std::string& clientsubname,
	bool use_tmpfiles, const std::string& tmpfile_path, const std::string& server_token, bool use_reflink, int backupid, bool r_incremental,
	IPipe* hashpipe_prepare, ClientMain* client_main, int filesrv_protocol_version, int incremental_num, logid_t logid, bool with_hashes,
	const std::vector<std::string>& shares_without_snapshot, bool with_sparse_hashing, server::FileMetadataDownloadThread* file_metadata_download,
	bool sc_failure_fatal, size_t n_threads, ServerSettings* server_settings, bool intra_file_diffs, FilePathCorrections& filepath_corrections, MaxFileId& max_file_id)
{
	for (size_t i = 0; i < n_threads; ++i)
	{
		ServerDlThread dl_thread;

		FileClient* curr_fc;
		FileClientChunked* curr_fc_chunked;
		if (i == 0)
		{
			curr_fc = &fc;
			curr_fc_chunked = fc_chunked;
		}
		else
		{
			curr_fc = new FileClient(false, client_main->getIdentity(), 
				client_main->getProtocolVersions().filesrv_protocol_version,
				client_main->isOnInternetConnection(), client_main, use_tmpfiles ? NULL : client_main);
			_u32 rc = client_main->getClientFilesrvConnection(curr_fc, server_settings, 60000);
			if (rc != ERR_CONNECTED)
			{
				ServerLogger::Log(logid, "Failed to connect FileClient "+convert(i), LL_WARNING);
				delete curr_fc;
				continue;
			}
			else
			{
				curr_fc->setProgressLogCallback(client_main);
			}

			if (incremental_num > 0 &&
				intra_file_diffs)
			{
				std::auto_ptr<FileClientChunked> new_fc;
				if (client_main->getClientChunkedFilesrvConnection(new_fc, server_settings, client_main, 60000))
				{
					new_fc->setProgressLogCallback(client_main);
					new_fc->setDestroyPipe(true);
					if (new_fc->hasError())
					{
						ServerLogger::Log(logid, "Failed to connect chunked FileClient "+convert(i)+ " -1", LL_WARNING);
						continue;
					}
				}
				else
				{
					ServerLogger::Log(logid, "Failed to connect chunked FileClient " + convert(i) + " -2", LL_WARNING);
					continue;
				}

				curr_fc_chunked = new_fc.release();
			}

			dl_thread.fc = curr_fc;
			dl_thread.fc_chunked = curr_fc_chunked;
		}

		dl_thread.dl_thread = new ServerDownloadThread(*curr_fc, curr_fc_chunked,
			backuppath, backuppath_hashes, last_backuppath, last_backuppath_complete,
			hashed_transfer, save_incomplete_file, clientid, clientname, clientsubname,
			use_tmpfiles, tmpfile_path, server_token, use_reflink, backupid, r_incremental,
			hashpipe_prepare, client_main, filesrv_protocol_version, incremental_num, logid,
			with_hashes, shares_without_snapshot, with_sparse_hashing, file_metadata_download,
			sc_failure_fatal, i, filepath_corrections, max_file_id, active_dls_ids);

		tickets.push_back(Server->getThreadPool()->execute(dl_thread.dl_thread, "fbackup load" + convert(i)));

		dl_threads.push_back(dl_thread);
	}
}

ServerDownloadThreadGroup::~ServerDownloadThreadGroup()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		ServerDlThread& dl_thread = dl_threads[i];
		if (i > 0)
		{
			delete dl_thread.fc;
			delete dl_thread.fc_chunked;
		}
		delete dl_thread.dl_thread;
	}
}

void ServerDownloadThreadGroup::queueSkip()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		dl_threads[i].dl_thread->queueSkip();
	}
}

bool ServerDownloadThreadGroup::sleepQueue()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		if (!dl_threads[i].dl_thread->queueFull())
			return false;
	}
	Server->wait(100);
	return true;
}

bool ServerDownloadThreadGroup::isOffline()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		if (dl_threads[i].dl_thread->isOffline())
			return true;
	}
	return false;
}

void ServerDownloadThreadGroup::addToQueueStartShadowcopy(const std::string& fn)
{
	getMinQueued()->addToQueueStartShadowcopy(fn);
}

void ServerDownloadThreadGroup::addToQueueStopShadowcopy(const std::string& fn)
{
	getMinQueued()->addToQueueStopShadowcopy(fn);
}

void ServerDownloadThreadGroup::queueStop()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		dl_threads[i].dl_thread->queueStop();
	}
}

void ServerDownloadThreadGroup::addToQueueFull(size_t id, const std::string& fn, const std::string& short_fn, const std::string& curr_path, const std::string& os_path, _i64 predicted_filesize, const FileMetadata& metadata, bool is_script, bool metadata_only, size_t folder_items, const std::string& sha_dig, bool at_front_postpone_quitstop, unsigned int p_script_random, std::string display_fn, bool write_metadata)
{
	getMinQueued()->addToQueueFull(id, fn, short_fn, curr_path,
		os_path, predicted_filesize, metadata, is_script, metadata_only, folder_items, sha_dig,
		at_front_postpone_quitstop, p_script_random, display_fn, write_metadata);
}

void ServerDownloadThreadGroup::addToQueueChunked(size_t id, const std::string& fn, const std::string& short_fn, const std::string& curr_path, const std::string& os_path, _i64 predicted_filesize, const FileMetadata& metadata, bool is_script, const std::string& sha_dig, unsigned int p_script_random, std::string display_fn)
{
	getMinQueued()->addToQueueChunked(id, fn, short_fn,
		curr_path, os_path, predicted_filesize, metadata, is_script,
		sha_dig, p_script_random, display_fn);
}

size_t ServerDownloadThreadGroup::getNumIssues()
{
	size_t ret = 0;
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		ret += dl_threads[i].dl_thread->getNumIssues();
	}
	return ret;
}

bool ServerDownloadThreadGroup::getHasDiskError()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		if (dl_threads[i].dl_thread->getHasDiskError())
			return true;
	}

	return false;
}

bool ServerDownloadThreadGroup::hasTimeout()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		if (dl_threads[i].dl_thread->hasTimeout())
			return true;
	}
	return false;
}

bool ServerDownloadThreadGroup::shouldBackoff()
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		if (dl_threads[i].dl_thread->shouldBackoff())
			return true;
	}
	return false;
}

size_t ServerDownloadThreadGroup::getNumEmbeddedMetadataFiles()
{
	size_t ret = 0;
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		ret += dl_threads[i].dl_thread->getNumEmbeddedMetadataFiles();
	}
	return ret;
}

bool ServerDownloadThreadGroup::deleteTempFolder()
{
	return dl_threads[0].dl_thread->deleteTempFolder();
}

bool ServerDownloadThreadGroup::isDownloadOk(size_t id)
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		if (!dl_threads[i].dl_thread->isDownloadOk(id))
			return false;
	}
	return true;
}

bool ServerDownloadThreadGroup::isDownloadPartial(size_t id)
{
	for (size_t i = 0; i < dl_threads.size(); ++i)
	{
		if (dl_threads[i].dl_thread->isDownloadPartial(id))
			return true;
	}
	return false;
}

size_t ServerDownloadThreadGroup::getMaxOkId()
{
	size_t max_ok_id = dl_threads[0].dl_thread->getMaxOkId();
	for (size_t i = 1; i < dl_threads.size(); ++i)
	{
		max_ok_id = (std::max)(max_ok_id, dl_threads[i].dl_thread->getMaxOkId());
	}
	return max_ok_id;
}

bool ServerDownloadThreadGroup::join(int waitms)
{
	return Server->getThreadPool()->waitFor(tickets, waitms);
}

ServerDownloadThread* ServerDownloadThreadGroup::getMinQueued()
{
	ServerDownloadThread* ret = dl_threads[0].dl_thread;
	size_t queue_size = dl_threads[0].dl_thread->queueSize();
	for (size_t i = 1; i < dl_threads.size(); ++i)
	{
		size_t curr_queue_size = dl_threads[i].dl_thread->queueSize();
		if (curr_queue_size < queue_size)
		{
			queue_size = curr_queue_size;
			ret = dl_threads[i].dl_thread;
		}
	}
	return ret;
}
