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

void RestoreFiles::operator()()
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	FileClient fc(false, client_token, 3,
		true, this, NULL);

	

	log("Starting restore...", LL_INFO);

	ClientConnector::updateRestorePc(status_id, -1, server_token);
	
	if(!connectFileClient(fc))
	{
		log("Connecting for restore failed", LL_ERROR);
		ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
		return;
	}

	log("Loading file list...", LL_INFO);

	if(!downloadFilelist(fc))
	{
		ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
		return;
	}

	log("Calculating download size...", LL_INFO);

	int64 total_size = calculateDownloadSize();
	if(total_size==-1)
	{
		ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
		return;
	}

	FileClient fc_metadata(false, client_token, 3,
		true, this, NULL);

	if(!connectFileClient(fc_metadata))
	{
		log("Connecting for file metadata failed", LL_ERROR);
		ClientConnector::restoreDone(log_id, status_id, restore_id, false, server_token);
		return;
	}

	std::auto_ptr<FileMetadataDownloadThread> metadata_thread(new FileMetadataDownloadThread(*this, fc_metadata, client_token ));
	THREADPOOL_TICKET metadata_dl = Server->getThreadPool()->execute(metadata_thread.get());

	log("Downloading necessary file data...", LL_INFO);

	if(!downloadFiles(fc, total_size))
	{
		restore_failed(fc, metadata_dl);
		return;
	}

	ClientConnector::updateRestorePc(status_id, 101, server_token);

	fc.InformMetadataStreamEnd(client_token);
	Server->getThreadPool()->waitFor(metadata_dl);

	if(!metadata_thread->applyMetadata())
	{
		restore_failed(fc, metadata_dl);
		return;
	}

	log("Restore finished successfully.", LL_INFO);

	ClientConnector::restoreDone(log_id, status_id, restore_id, true, server_token);
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

	_u32 rc = fc.GetFile("clientdl_filelist", filelist, true, false);

	if(rc!=ERR_SUCCESS)
	{
		log("Error getting file list. Errorcode: "+FileClient::getErrorString(rc)+" ("+nconvert(rc)+")", LL_ERROR);
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
	std::map<std::wstring, std::wstring> extra;

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

bool RestoreFiles::downloadFiles(FileClient& fc, int64 total_size)
{
	std::vector<char> buffer;
	buffer.resize(32768);

	FileListParser filelist_parser;

	std::auto_ptr<FileClientChunked> fc_chunked = createFcChunked();

	_u32 read;
	SFile data;
	std::map<std::wstring, std::wstring> extra;

	size_t depth = 0;

	std::wstring restore_path;
	std::wstring server_path = L"clientdl";

	RestoreDownloadThread* restore_download = new RestoreDownloadThread(fc, *fc_chunked, client_token);
	Server->getThreadPool()->execute(restore_download);

	std::wstring curr_files_dir;
	std::vector<SFileAndHash> curr_files;

	ClientDAO client_dao(db, false);

	size_t line=0;

	bool has_error=false;

	filelist->Seek(0);

	int64 laststatsupdate=Server->getTimeMS();
	int64 skipped_bytes = 0;

	do 
	{
		read = filelist->Read(buffer.data(), static_cast<_u32>(buffer.size()));

		for(_u32 i=0;i<read;++i)
		{
			if(filelist_parser.nextEntry(buffer[i], data, &extra))
			{
				FileMetadata metadata;
				metadata.read(extra);

				int64 ctime=Server->getTimeMS();
				if(ctime-laststatsupdate>1000)
				{
					laststatsupdate=ctime;
					if(total_size==0)
					{
						ClientConnector::updateRestorePc(status_id, 100, server_token);
					}
					else
					{
						int pcdone = (std::min)(100,(int)(((float)fc.getReceivedDataBytes() + (float)fc_chunked->getReceivedDataBytes() + skipped_bytes)/((float)total_size/100.f)+0.5f));;
						ClientConnector::updateRestorePc(status_id, pcdone, server_token);
					}
				}

				if(data.isdir)
				{
					if(data.name!=L"..")
					{
						bool set_orig_path = false;
						str_map::iterator it_orig_path = extra.find(L"orig_path");
						if(it_orig_path!=extra.end())
						{
							restore_path = Server->ConvertToUnicode(base64_decode_dash(wnarrow(it_orig_path->second)));
							set_orig_path=true;
						}

						if(depth==0)
						{
							if(!os_directory_exists(os_file_prefix(restore_path)))
							{
								if(!os_create_dir_recursive(os_file_prefix(restore_path)))
								{
									log(L"Error recursively creating directory \""+restore_path+L"\"", LL_ERROR);
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
									log(L"Error creating directory \""+restore_path+L"\"", LL_ERROR);
									has_error=true;
								}
							}							
						}

						server_path+=L"/"+data.name;

						restore_download->addToQueueFull(line, server_path, restore_path, 0,
							metadata, false, true, false, false);

						++depth;
					}
					else
					{
						--depth;

						server_path=ExtractFilePath(server_path, L"/");
						restore_path=ExtractFilePath(restore_path, os_file_sep());						
					}
				}
				else
				{
					std::wstring local_fn = restore_path + os_file_sep() + data.name;
#ifdef _WIN32
					std::wstring name_lower = strlower(data.name);
#else
					std::wstring name_lower = data.name;
#endif
					std::wstring server_fn = server_path + L"/" + data.name;

					str_map::iterator it_orig_path = extra.find(L"orig_path");
					if(it_orig_path!=extra.end())
					{
						local_fn = Server->ConvertToUnicode(base64_decode_dash(wnarrow(it_orig_path->second)));
						restore_path = ExtractFilePath(local_fn, os_file_sep());
					}

				
					if(Server->fileExists(os_file_prefix(local_fn)))
					{
						if(restore_path!=curr_files_dir)
						{
							curr_files_dir = restore_path;

#ifndef _WIN32
							std::wstring restore_path_lower = restore_path;
#else
							std::wstring restore_path_lower = strlower(restore_path);
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

						IFile* orig_file = Server->openFile(os_file_prefix(local_fn), MODE_RW);

						if(orig_file==NULL)
						{
							log(L"Cannot open file \""+local_fn+L"\" for writing. Not restoring file.", LL_ERROR);
							has_error=true;
						}
						else
						{
							IFile* chunkhashes = Server->openTemporaryFile();

							if(chunkhashes==NULL)
							{
								log(L"Cannot open temporary file for chunk hashes of file \""+local_fn+L"\". Not restoring file.", LL_ERROR);
								has_error=true;
								delete orig_file;
							}
							else
							{
								if(shahash.empty())
								{
									log(L"Calculating hashes of file \""+local_fn+L"\"...", LL_DEBUG);
									shahash = build_chunk_hashs(orig_file, chunkhashes, NULL, true, NULL, false, NULL);
								}
								
								if(shahash!=base64_decode_dash(wnarrow(extra[L"shahash"])))
								{
									restore_download->addToQueueChunked(line, server_fn, local_fn, 
										data.size, metadata, false, orig_file, chunkhashes);
								}
								else
								{
									restore_download->addToQueueFull(line, server_fn, local_fn, 
										data.size, metadata, false, false, false, true);

									std::wstring tmpfn = chunkhashes->getFilenameW();
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
							data.size, metadata, false, false, false, false);
					}
				}
				++line;
			}
		}

	} while (read>0);

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

void RestoreFiles::log( const std::wstring& msg, int loglevel )
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

