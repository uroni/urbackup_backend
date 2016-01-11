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

#include "RestoreFiles.h"
#include "ClientService.h"
#include "../stringtools.h"
#include "../urbackupcommon/filelist_utils.h"
#include "RestoreDownloadThread.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "clientdao.h"
#include "../Interface/Server.h"
#include <algorithm>
#include "client.h"
#include <stack>
#include "../urbackupcommon/chunk_hasher.h"
#include "database.h"
#include "FileMetadataDownloadThread.h"

namespace
{
	class RestoreUpdaterThread : public IThread
	{
	public:
		RestoreUpdaterThread(int64 restore_id, int64 status_id, std::string server_token)
			: update_mutex(Server->createMutex()), stopped(false),
			update_cond(Server->createCondition()), curr_pc(-1),
			restore_id(restore_id), status_id(status_id), server_token(server_token)
		{}

		void operator()()
		{
			{
				IScopedLock lock(update_mutex.get());
				while (!stopped)
				{
					ClientConnector::updateRestorePc(restore_id, status_id, curr_pc, server_token);
					update_cond->wait(&lock, 60000);
				}
			}
			delete this;
		}

		void stop()
		{
			IScopedLock lock(update_mutex.get());
			stopped = true;
			update_cond->notify_all();
		}

		void update_pc(int new_pc)
		{
			IScopedLock lock(update_mutex.get());
			curr_pc = new_pc;
			update_cond->notify_all();
		}

	private:
		std::auto_ptr<IMutex> update_mutex;
		std::auto_ptr<ICondition> update_cond;
		bool stopped;
		int curr_pc;
		int64 restore_id;
		int64 status_id;
		std::string server_token;
	};

	class ScopedRestoreUpdater
	{
	public:
		ScopedRestoreUpdater(int64 restore_id, int64 status_id, std::string server_token)
			: restore_updater(new RestoreUpdaterThread(restore_id, status_id, server_token))
		{
			restore_updater_ticket = Server->getThreadPool()->execute(restore_updater);
		}

		void update_pc(int new_pc)
		{
			restore_updater->update_pc(new_pc);
		}

		~ScopedRestoreUpdater()
		{
			restore_updater->stop();
			Server->getThreadPool()->waitFor(restore_updater_ticket);
		}

	private:
		RestoreUpdaterThread* restore_updater;
		THREADPOOL_TICKET restore_updater_ticket;
	};
}


void RestoreFiles::operator()()
{
	if (restore_declined)
	{
		log("Restore was declined by client", LL_ERROR);
		ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
		delete this;
		return;
	}


	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	FileClient fc(false, client_token, 3,
		true, this, NULL);

	log("Starting restore...", LL_INFO);

	{
		ScopedRestoreUpdater restore_updater(restore_id, status_id, server_token);

		if (!connectFileClient(fc))
		{
			log("Connecting for restore failed", LL_ERROR);
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			return;
		}

		log("Loading file list...", LL_INFO);

		if (!downloadFilelist(fc))
		{
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			return;
		}

		log("Calculating download size...", LL_INFO);

		int64 total_size = calculateDownloadSize();
		if (total_size == -1)
		{
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			return;
		}

		FileClient fc_metadata(false, client_token, 3,
			true, this, NULL);

		if (!connectFileClient(fc_metadata))
		{
			log("Connecting for file metadata failed", LL_ERROR);
			ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
			return;
		}

		std::auto_ptr<client::FileMetadataDownloadThread> metadata_thread(new client::FileMetadataDownloadThread(*this, fc_metadata, client_token));
		THREADPOOL_TICKET metadata_dl = Server->getThreadPool()->execute(metadata_thread.get());

		int64 starttime = Server->getTimeMS();

		while (Server->getTimeMS() - starttime < 10000)
		{
			if (fc_metadata.isDownloading())
			{
				break;
			}
		}

		if (!fc_metadata.isDownloading())
		{
			log("Error starting metadata download", LL_INFO);
			restore_failed(fc, metadata_dl);
			return;
		}

		log("Downloading necessary file data...", LL_INFO);

		if (!downloadFiles(fc, total_size, restore_updater))
		{
			restore_failed(fc, metadata_dl);
			return;
		}

		restore_updater.update_pc(101);

		do
		{
			if (fc.InformMetadataStreamEnd(client_token) == ERR_TIMEOUT)
			{
				fc.Reconnect();

				fc.InformMetadataStreamEnd(client_token);
			}

			log("Waiting for metadata download stream to finish", LL_DEBUG);

			Server->wait(1000);

			metadata_thread->shutdown();
		} while (!Server->getThreadPool()->waitFor(metadata_dl, 10000));

		if (!metadata_thread->applyMetadata())
		{
			restore_failed(fc, metadata_dl);
			return;
		}
	}

	log("Restore finished successfully.", LL_INFO);

	ClientConnector::restoreDone(log_id, status_id, restore_id, true, server_token);

	delete this;
}

IPipe * RestoreFiles::new_fileclient_connection( )
{
	return ClientConnector::getFileServConnection(server_token, 10000);
}

bool RestoreFiles::connectFileClient( FileClient& fc )
{
	IPipe* np = new_fileclient_connection();

	if(np!=NULL)
	{
		fc.Connect(np);
		return true;
	}
	else
	{
		return false;
	}
}

bool RestoreFiles::downloadFilelist( FileClient& fc )
{
	filelist = Server->openTemporaryFile();
	filelist_del.reset(filelist);

	if(filelist==NULL)
	{
		return false;
	}

	_u32 rc = fc.GetFile("clientdl_filelist", filelist, true, false, 0, false, 0);

	if(rc!=ERR_SUCCESS)
	{
		log("Error getting file list. Errorcode: "+FileClient::getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		return false;
	}

	return true;
}

int64 RestoreFiles::calculateDownloadSize()
{
	std::vector<char> buffer;
	buffer.resize(32768);

	FileListParser filelist_parser;

	_u32 read;
	SFile data;
	std::map<std::string, std::string> extra;

	int64 total_size = 0;

	filelist->Seek(0);

	do 
	{
		read = filelist->Read(buffer.data(), static_cast<_u32>(buffer.size()));

		for(_u32 i=0;i<read;++i)
		{
			if(filelist_parser.nextEntry(buffer[i], data, &extra))
			{
				if(data.size>0)
				{
					total_size+=data.size;
				}				
			}
		}

	} while (read>0);

	return total_size;
}

bool RestoreFiles::downloadFiles(FileClient& fc, int64 total_size, ScopedRestoreUpdater& restore_updater)
{
	std::vector<char> buffer;
	buffer.resize(32768);

	FileListParser filelist_parser;

	std::auto_ptr<FileClientChunked> fc_chunked = createFcChunked();

	if(fc_chunked.get()==NULL)
	{
		return false;
	}

	_u32 read;
	SFile data;
	std::map<std::string, std::string> extra;

	size_t depth = 0;

	std::string restore_path;
	std::string server_path = "clientdl";

	RestoreDownloadThread* restore_download = new RestoreDownloadThread(fc, *fc_chunked, client_token);
    THREADPOOL_TICKET restore_download_ticket = Server->getThreadPool()->execute(restore_download);

	std::string curr_files_dir;
	std::vector<SFileAndHash> curr_files;

	ClientDAO client_dao(db);

	size_t line=0;

	bool has_error=false;

	filelist->Seek(0);

	int64 laststatsupdate=Server->getTimeMS();
	int64 skipped_bytes = 0;

	std::vector<size_t> folder_items;
	folder_items.push_back(0);

	std::stack<std::vector<std::string> > folder_files;
	folder_files.push(std::vector<std::string>());

	std::vector<std::string> deletion_queue;
	std::vector<std::pair<std::string, std::string> > rename_queue;

	bool single_item=false;

	do 
	{
		read = filelist->Read(buffer.data(), static_cast<_u32>(buffer.size()));

		for(_u32 i=0;i<read && !has_error;++i)
		{
			if(filelist_parser.nextEntry(buffer[i], data, &extra))
			{
				FileMetadata metadata;
				metadata.read(extra);

				if(depth==0 && extra.find("single_item")!=extra.end())
				{
					single_item=true;
				}

				int64 ctime=Server->getTimeMS();
				if(ctime-laststatsupdate>1000)
				{
					laststatsupdate=ctime;
					if(total_size==0)
					{
						restore_updater.update_pc(100);
					}
					else
					{
						int pcdone = (std::min)(100,(int)(((float)fc.getReceivedDataBytes(true) + (float)fc_chunked->getReceivedDataBytes(true) + skipped_bytes)/((float)total_size/100.f)+0.5f));;
						restore_updater.update_pc(pcdone);
					}
				}

				if(!data.isdir || data.name!="..")
				{
					for(size_t j=0;j<folder_items.size();++j)
					{
						++folder_items[j];
					}
				}

				if(data.isdir)
				{
					if(data.name!="..")
					{
						bool set_orig_path = false;
                        if(!metadata.orig_path.empty())
						{
                            restore_path = (metadata.orig_path);
							set_orig_path=true;
						}

						if(depth==0)
						{
							if(!os_directory_exists(os_file_prefix(restore_path)))
							{
								if(!os_create_dir_recursive(os_file_prefix(restore_path)))
								{
									log("Error recursively creating directory \""+restore_path+"\"", LL_ERROR);
									has_error=true;
								}
							}
						}
						else
						{
							if(!set_orig_path)
							{
								restore_path+=os_file_sep()+data.name;
							}

							if(!os_directory_exists(os_file_prefix(restore_path)))
							{
								if(!os_create_dir(os_file_prefix(restore_path)))
								{
									log("Error creating directory \""+restore_path+"\"", LL_ERROR);
									has_error=true;
								}
							}							
						}

#ifdef _WIN32
						std::string name_lower = strlower(data.name);
#else
						std::string name_lower = data.name;
#endif
						folder_files.top().push_back(name_lower);

						folder_items.push_back(0);
						folder_files.push(std::vector<std::string>());

						server_path+="/"+data.name;

						++depth;
					}
					else
					{
						--depth;

						if(!removeFiles(restore_path, folder_files, deletion_queue))
						{
							has_error=true;
						}

						server_path=ExtractFilePath(server_path, "/");
						restore_path=ExtractFilePath(restore_path, os_file_sep());						

                        restore_download->addToQueueFull(line, server_path, restore_path, 0,
                            metadata, false, true, folder_items.back());

						folder_items.pop_back();
						folder_files.pop();
					}
				}
				else
				{
					std::string local_fn = restore_path + os_file_sep() + data.name;
#ifdef _WIN32
					std::string name_lower = strlower(data.name);
#else
					std::string name_lower = data.name;
#endif
					std::string server_fn = server_path + "/" + data.name;

					folder_files.top().push_back(name_lower);

					str_map::iterator it_orig_path = extra.find("orig_path");
					if(it_orig_path!=extra.end())
					{
                        local_fn = it_orig_path->second;
						restore_path = ExtractFilePath(local_fn, os_file_sep());
					}
				
					if(Server->fileExists(os_file_prefix(local_fn)))
					{
						if(restore_path!=curr_files_dir)
						{
							curr_files_dir = restore_path;

#ifndef _WIN32
							std::string restore_path_lower = restore_path;
#else
							std::string restore_path_lower = strlower(restore_path);
#endif

							if(!client_dao.getFiles(restore_path_lower + os_file_sep(), curr_files))
							{
								curr_files.clear();
							}
						}

						std::string shahash;

						SFileAndHash search_file = {};
						search_file.name = data.name;

						std::vector<SFileAndHash>::iterator it_file = std::lower_bound(curr_files.begin(), curr_files.end(), search_file);
						if(it_file!=curr_files.end() && it_file->name == data.name)
						{
							SFile metadata = getFileMetadataWin(local_fn, true);
							if(!metadata.name.empty())
							{
								int64 change_indicator = metadata.last_modified;
								if(metadata.usn!=0)
								{
									change_indicator = metadata.usn;
								}
								if(!metadata.isdir
									&& metadata.size==it_file->size
									&& change_indicator==it_file->change_indicator
									&& !it_file->hash.empty())
								{
									shahash = it_file->hash;
								}
							}							
						}

						IFsFile* orig_file = Server->openFile(os_file_prefix(local_fn), MODE_RW);

#ifdef _WIN32
						if(orig_file==NULL)
						{
							size_t idx=0;
							std::string old_local_fn=local_fn;
							while(orig_file==NULL && idx<100)
							{
								local_fn=old_local_fn+"_"+convert(idx);
								++idx;
								orig_file = Server->openFile(os_file_prefix(local_fn), MODE_RW);
							}
							rename_queue.push_back(std::make_pair(local_fn, old_local_fn));
						}
#endif

						if(orig_file==NULL)
						{
							log("Cannot open file \""+local_fn+"\" for writing. Not restoring file.", LL_ERROR);
							has_error=true;
						}
						else
						{
							IFile* chunkhashes = Server->openTemporaryFile();

							if(chunkhashes==NULL)
							{
								log("Cannot open temporary file for chunk hashes of file \""+local_fn+"\". Not restoring file.", LL_ERROR);
								has_error=true;
								delete orig_file;
							}
							else
							{
                                bool calc_hashes=false;
								if(shahash.empty())
								{
									log("Calculating hashes of file \""+local_fn+"\"...", LL_DEBUG);
									FsExtentIterator extent_iterator(orig_file, 32768);
									shahash = build_chunk_hashs(orig_file, chunkhashes, NULL, true, NULL, false, NULL,
										NULL, false, &extent_iterator);
                                    calc_hashes = true;
								}
								
								if(shahash!=base64_decode_dash(extra["shahash"]))
								{
                                    if(!calc_hashes)
                                    {
                                        log("Calculating hashes of file \""+local_fn+"\"...", LL_DEBUG);
										FsExtentIterator extent_iterator(orig_file);
                                        build_chunk_hashs(orig_file, chunkhashes, NULL, false, NULL, false, NULL,
											NULL, false, &extent_iterator);
                                    }

									restore_download->addToQueueChunked(line, server_fn, local_fn, 
										data.size, metadata, false, orig_file, chunkhashes);
								}
								else
								{
									restore_download->addToQueueFull(line, server_fn, local_fn, 
                                        data.size, metadata, false, true, 0);

									std::string tmpfn = chunkhashes->getFilename();
									delete chunkhashes;
									Server->deleteFile(tmpfn);
									delete orig_file;
								}
							}
						}
					}
					else
					{
						restore_download->addToQueueFull(line, server_fn, local_fn, 
                            data.size, metadata, false, false, 0);
					}
				}
				++line;
			}
		}

	} while (read>0 && !has_error);

	if(!single_item)
	{
		if(!removeFiles(restore_path, folder_files, deletion_queue))
		{
			has_error=true;
		}
	}

    restore_download->queueStop();

    while(!Server->getThreadPool()->waitFor(restore_download_ticket, 1000))
    {
        if(total_size==0)
        {
			restore_updater.update_pc(100);
        }
        else
        {
            int pcdone = (std::min)(100,(int)(((float)fc.getReceivedDataBytes(true) + (float)fc_chunked->getReceivedDataBytes(true) + skipped_bytes)/((float)total_size/100.f)+0.5f));
			restore_updater.update_pc(pcdone);
        }
    }

#ifdef _WIN32
	bool request_restart=false;

	if(!has_error && !restore_download->hasError())
	{
		if(!deletion_queue.empty())
		{
			request_restart = true;

			if(!deleteFilesOnRestart(deletion_queue))
			{
				has_error=true;
			}
		}

		if(!rename_queue.empty())
		{
			request_restart = true;
			if(!renameFilesOnRestart(rename_queue))
			{
				has_error=true;
			}
		}

		rename_queue = restore_download->getRenameQueue();
		if(!rename_queue.empty())
		{
			request_restart = true;
			if(!renameFilesOnRestart(rename_queue))
			{
				has_error=true;
			}
		}
	}	

	if(request_restart)
	{
		ClientConnector::requestRestoreRestart();
	}
#endif

    if(restore_download->hasError())
    {
        log("Error while downloading files during restore", LL_ERROR);
        return false;
    }

    return !has_error;
}

void RestoreFiles::log( const std::string& msg, int loglevel )
{
	Server->Log(msg, loglevel);
	if(loglevel>=LL_INFO)
	{
		ClientConnector::tochannelLog(log_id, msg, loglevel, server_token);
	}
}

std::auto_ptr<FileClientChunked> RestoreFiles::createFcChunked()
{
	IPipe* conn = new_fileclient_connection();

	if(conn==NULL)
	{
		return std::auto_ptr<FileClientChunked>();
	}

	return std::auto_ptr<FileClientChunked>(new FileClientChunked(conn, true, &tcpstack, this,
		NULL, client_token, NULL));
}

void RestoreFiles::restore_failed(FileClient& fc, THREADPOOL_TICKET metadata_dl)
{
	log("Restore failed.", LL_INFO);

	fc.InformMetadataStreamEnd(client_token);

	Server->getThreadPool()->waitFor(metadata_dl);

	ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
}

bool RestoreFiles::removeFiles( std::string restore_path,
	std::stack<std::vector<std::string> > &folder_files, std::vector<std::string> &deletion_queue )
{
	bool ret=true;

	bool get_files_error;
	std::vector<SFile> files = getFiles(os_file_prefix(restore_path), &get_files_error);

	if(get_files_error)
	{
		log("Error enumerating files in \""+restore_path+"\"", LL_ERROR);
		ret=false;
	}
	else
	{
		for(size_t j=0;j<files.size();++j)
		{
			std::string fn_lower = strlower(files[j].name);

			if(std::find(folder_files.top().begin(), folder_files.top().end(), fn_lower)==folder_files.top().end())
			{
				std::string cpath = restore_path+os_file_sep()+files[j].name;
				if(files[j].isdir)
				{
					if(!os_remove_nonempty_dir(os_file_prefix(cpath)))
					{
						log("Error deleting directory \""+restore_path+"\"", LL_WARNING);
#ifndef _WIN32
						ret=false;
#else
						deletion_queue.push_back(cpath);
#endif
					}
				}
				else
				{
					if(!Server->deleteFile(os_file_prefix(cpath)))
					{
						log("Error deleting file \""+restore_path+"\"", LL_WARNING);

#ifndef _WIN32
						ret=false;
#else
						deletion_queue.push_back(cpath);
#endif
					}
				}
			}
		}
	}

	return ret;
}

bool RestoreFiles::deleteFilesOnRestart( std::vector<std::string> &deletion_queue_dirs )
{
	for(size_t i=0;i<deletion_queue_dirs.size();++i)
	{
		int ftype = os_get_file_type(deletion_queue_dirs[i]);

		if(ftype & EFileType_Directory)
		{
			if(!deleteFolderOnRestart(deletion_queue_dirs[i]))
			{
				log("Error deleting folder "+deletion_queue_dirs[i]+" on restart", LL_ERROR);
				return false;
			}
		}
		else
		{
			if(!deleteFileOnRestart(deletion_queue_dirs[i]))
			{
				log("Error deleting file "+deletion_queue_dirs[i]+" on restart", LL_ERROR);
				return false;
			}
		}
	}

	return true;
}

bool RestoreFiles::deleteFileOnRestart( const std::string& fpath )
{
#ifndef _WIN32
	return false;
#else
	BOOL b = MoveFileExW(Server->ConvertToWchar(os_file_prefix(fpath)).c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	if(b!=TRUE)
	{
		log("Error deleting file "+fpath+" on restart", LL_ERROR);
	}
	return b==TRUE;
#endif
}

bool RestoreFiles::deleteFolderOnRestart( const std::string& fpath )
{
	std::vector<SFile> files = getFiles(os_file_prefix(fpath));

	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir)
		{
			if(!deleteFolderOnRestart(fpath + os_file_sep() + files[i].name))
			{
				return false;
			}
		}
		else
		{
			if(!deleteFileOnRestart(fpath + os_file_sep() + files[i].name))
			{
				return false;
			}
		}
	}

#ifndef _WIN32
	return false;
#else
	BOOL b = MoveFileExW(Server->ConvertToWchar(os_file_prefix(fpath)).c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	if(b!=TRUE)
	{
		log("Error deleting folder "+fpath+" on restart", LL_ERROR);
	}
	return b==TRUE;
#endif
}

bool RestoreFiles::renameFilesOnRestart( std::vector<std::pair<std::string, std::string> >& rename_queue )
{
#ifndef _WIN32
	return false;
#else
	for(size_t i=0;i<rename_queue.size();++i)
	{
		BOOL b = MoveFileExW(Server->ConvertToWchar(os_file_prefix(rename_queue[i].first)).c_str(), 
			Server->ConvertToWchar(os_file_prefix(rename_queue[i].second)).c_str(), MOVEFILE_DELAY_UNTIL_REBOOT);
		if(b!=TRUE)
		{
			log("Error renaming "+rename_queue[i].first+" to "+rename_queue[i].second+" on Windows restart", LL_ERROR);
			return false;
		}
	}
	return true;
#endif
}

