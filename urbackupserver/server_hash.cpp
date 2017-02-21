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
#include "server_hash.h"
#include "server_prepare_hash.h"
#include "server_settings.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../Interface/Mutex.h"
#include "../common/data.h"
#include "database.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../stringtools.h"
#include "server_log.h"
#include "server_cleanup.h"
#include "create_files_index.h"
#include <algorithm>
#include <memory.h>
#include "../urbackupcommon/file_metadata.h"
#include <assert.h>
#ifdef _WIN32
#include <Windows.h>
#endif

const size_t freespace_mod=50*1024*1024; //50 MB
const size_t BUFFER_SIZE=64*1024; //64KB

IMutex * delete_mutex=NULL;

void init_mutex1(void)
{
	delete_mutex=Server->createMutex();
}

void destroy_mutex1(void)
{
	Server->destroy(delete_mutex);
}

BackupServerHash::BackupServerHash(IPipe *pPipe, int pClientid, bool use_snapshots, bool use_reflink, bool use_tmpfiles, logid_t logid,
	bool snapshot_file_inplace)
	: use_snapshots(use_snapshots), use_reflink(use_reflink), use_tmpfiles(use_tmpfiles), filesdao(NULL), old_backupfolders_loaded(false),
	  logid(logid), snapshot_file_inplace(snapshot_file_inplace)
{
	pipe=pPipe;
	clientid=pClientid;
	link_logcnt=0;
	space_logcnt=0;
	working=false;
	has_error=false;
	chunk_patcher.setCallback(this);
	fileindex=NULL;

	if(use_reflink)
		ServerLogger::Log(logid, "Reflink copying is enabled", LL_DEBUG);
}

BackupServerHash::~BackupServerHash(void)
{
	if(pipe!=NULL)
	{
		Server->destroy(pipe);
	}

	delete fileindex;
}

void BackupServerHash::setupDatabase(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);

	filesdao = new ServerFilesDao(db);

	fileindex=create_lmdb_files_index(); 
}

void BackupServerHash::deinitDatabase(void)
{
	db->freeMemory();

	delete fileindex;
	fileindex=NULL;

	delete filesdao;
	filesdao =NULL;
}

void BackupServerHash::operator()(void)
{
	setupDatabase();

	while(true)
	{
		working=false;
		std::string data;
		size_t rc=pipe->Read(&data, static_cast<int>(60000) );
		if(rc==0)
		{
			link_logcnt=0;
			space_logcnt=0;
			continue;
		}
		
		working=true;
		if(data=="exit")
		{
			deinitDatabase();
			Server->Log("server_hash Thread finished - normal");
			Server->destroyDatabases(Server->getThreadID());
			delete this;
			return;
		}
		else if(data=="flush")
		{
			continue;
		}

		if(rc>0)
		{
			CRData rd(&data);

			int iaction;
			rd.getInt(&iaction);
			EAction action=static_cast<EAction>(iaction);

			if(action==EAction_LinkOrCopy)
			{
				std::string temp_fn;
				rd.getStr(&temp_fn);

				int backupid;
				rd.getInt(&backupid);

				int incremental;
				rd.getInt(&incremental);

				char with_hashes;
				rd.getChar(&with_hashes);

				std::string tfn;
				rd.getStr(&tfn);

				std::string hashpath;
				rd.getStr(&hashpath);

				std::string sha2;
				if(!rd.getStr(&sha2))
					ServerLogger::Log(logid, "Reading hash from pipe failed", LL_ERROR);

				if(sha2.size()!=SHA_DEF_DIGEST_SIZE)
					ServerLogger::Log(logid, "SHA length of file hash of \""+tfn+"\" wrong.", LL_ERROR);

				std::string hashoutput_fn;
				rd.getStr(&hashoutput_fn);

				std::string old_file_fn;
				rd.getStr(&old_file_fn);

				int64 t_filesize;
				rd.getInt64(&t_filesize);

				std::string sparse_extents_fn;
				rd.getStr(&sparse_extents_fn);

				FileMetadata metadata;
				metadata.read(rd);
				metadata.set_shahash(sha2);

				IFile *tf=Server->openFile(os_file_prefix((temp_fn)), MODE_READ_SEQUENTIAL);

				if(tf==NULL)
				{
					ServerLogger::Log(logid, "Error opening file \""+temp_fn+"\" from pipe for reading ec="+convert(os_last_error()), LL_ERROR);
					has_error=true;
				}
				else
				{
					std::auto_ptr<ExtentIterator> extent_iterator;
					if (!sparse_extents_fn.empty())
					{
						IFile* sparse_extents_f = Server->openFile(sparse_extents_fn, MODE_READ);

						if (sparse_extents_f != NULL)
						{
							extent_iterator.reset(new ExtentIterator(sparse_extents_f));
						}
					}

					addFile(backupid, incremental, tf, tfn, hashpath, sha2,
						old_file_fn, hashoutput_fn, t_filesize, metadata, with_hashes!=0, extent_iterator.get());
				}

				if(!hashoutput_fn.empty())
				{
					Server->deleteFile(hashoutput_fn);
				}
			}
			else if(action==EAction_Copy)
			{
				std::string source;
				bool b = rd.getStr(&source);
				assert(b);
				
				std::string dest;
				b = rd.getStr(&dest);
				assert(b);

				std::string hash_src;
				b = rd.getStr(&hash_src);
				assert(b);

				std::string hash_dest;
				b = rd.getStr(&hash_dest);
				assert(b);

				FileMetadata metadata;
				metadata.read(rd);
				
				FileMetadata src_metadata;
				if(read_metadata(os_file_prefix(hash_src),
					src_metadata))
				{
					metadata.set_shahash(src_metadata.shahash);
				}

				std::auto_ptr<IFile> tf(Server->openFile(os_file_prefix(source), MODE_READ_SEQUENTIAL));

				if(!tf.get())
				{
					ServerLogger::Log(logid, "Error opening file \""+source+"\" from pipe for reading ec="+convert(os_last_error()), LL_ERROR);
					has_error=true;
				}
				else
				{
					if(!copyFile(tf.get(), dest, NULL))
					{
						ServerLogger::Log(logid, "Error while copying file \""+source+"\" to \""+dest+"\"", LL_ERROR);
						has_error=true;
					}

					if(!hash_src.empty())
					{
						std::auto_ptr<IFile> hashf(Server->openFile(os_file_prefix(hash_src), MODE_READ_SEQUENTIAL));
						if(hashf.get())
						{
							copyFile(hashf.get(), hash_dest, NULL);
						}
					}
				}
			}
		}
	}
}

void BackupServerHash::addFileSQL(int backupid, int clientid, int incremental, const std::string &fp, const std::string &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid, int64 next_entry, bool update_fileindex)
{
	addFileSQL(*filesdao, *fileindex, backupid, clientid, incremental, fp, hash_path, shahash, filesize, rsize, prev_entry, prev_entry_clientid, next_entry, update_fileindex);
}

void BackupServerHash::addFileSQL(ServerFilesDao& filesdao, FileIndex& fileindex, int backupid, const int clientid, int incremental, const std::string &fp,
	const std::string &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid, int64 next_entry, bool update_fileindex)
{
	if (filesize < link_file_min_size)
	{
		assert(prev_entry_clientid == 0);
		assert(prev_entry == 0);
		assert(next_entry == 0);
		filesdao.addIncomingFile(filesize, clientid, backupid, std::string(), ServerFilesDao::c_direction_incoming, incremental);
		filesdao.addFileEntryExternal(backupid, fp, hash_path, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, 0);
		return;
	}

	bool new_for_client=false;

	if(prev_entry_clientid!=clientid || prev_entry==0)
	{
		new_for_client=true;

		//new file for this client (predicted)
		prev_entry=0;
		next_entry=0;

		std::string clients;

		if(prev_entry_clientid!=0)
		{
			//Other clients have this file

			std::map<int, int64> all_clients = fileindex.get_all_clients_with_cache(FileIndex::SIndexKey(shahash.c_str(), filesize), true);

			for(std::map<int, int64>::iterator it=all_clients.begin();it!=all_clients.end();++it)
			{
				if(it->second!=0)
				{
					if(it->first==clientid)
					{
						//client actually has this file, but it e.g. failed to link
						prev_entry=it->second;
					}
				
					if(!clients.empty())
					{
						clients+=",";
					}

					clients+=convert(it->first);
				}
			}
		}
		
		if(prev_entry==0)
		{
			filesdao.addIncomingFile(filesize, clientid, backupid, clients, ServerFilesDao::c_direction_incoming, incremental);
		}
		else
		{
			ServerFilesDao::SFindFileEntry fentry = filesdao.getFileEntry(prev_entry);
			
			if(fentry.exists)
			{
				next_entry = fentry.next_entry;
				if(fentry.pointed_to!=0)
				{
					update_fileindex = true;
				}
			}
			else
			{
				prev_entry=0;
			}
		}
	}

	if(update_fileindex)
	{
		//if prev_entry==0 the file is new for this client
		//and pointed_to does not need to be updated
		if(prev_entry!=0)
		{
			ServerFilesDao::CondInt64 fentry = filesdao.getPointedTo(prev_entry);

			if(fentry.exists && fentry.value!=0)
			{
				filesdao.setPointedTo(0, prev_entry);
			}
			else
			{
				int64 client_entryid = fileindex.get_with_cache_exact(FileIndex::SIndexKey(shahash.c_str(), filesize, clientid));
				if(client_entryid!=0)
				{
					filesdao.setPointedTo(0, client_entryid);
				}
			}
		}
	}

	int64 entryid = filesdao.addFileEntryExternal(backupid, fp, hash_path, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, (new_for_client || update_fileindex)?1:0);

	if(new_for_client || update_fileindex)
	{
		FILEENTRY_DEBUG(Server->Log("New fileindex entry for \"" + fp + "\""
			" id=" + convert(entryid)
			+" hash="+base64_encode(reinterpret_cast<const unsigned char*>(shahash.c_str()), bytes_in_index), LL_DEBUG));
		fileindex.put_delayed(FileIndex::SIndexKey(shahash.c_str(), filesize, clientid), entryid);
	}
}

void BackupServerHash::deleteFileSQL(ServerFilesDao& filesdao, FileIndex& fileindex, int64 id)
{
	ServerFilesDao::SFindFileEntry entry = filesdao.getFileEntry(id);
	
	if(entry.exists)
	{
		deleteFileSQL(filesdao, fileindex, reinterpret_cast<const char*>(entry.shahash.c_str()),
				entry.filesize, entry.rsize, entry.clientid, entry.backupid, entry.incremental,
				id, entry.prev_entry, entry.next_entry, entry.pointed_to, true, true, true, false, NULL);
	}
}

void BackupServerHash::deleteFileSQL(ServerFilesDao& filesdao, FileIndex& fileindex, const char* pHash, _i64 filesize, _i64 rsize, const int clientid, int backupid, int incremental, int64 id, int64 prev_id, int64 next_id, int pointed_to,
	bool use_transaction, bool del_entry, bool detach_dbs, bool with_backupstat, SInMemCorrection* correction)
{
	if(use_transaction)
	{
		filesdao.BeginWriteTransaction();
	}

	if(prev_id==0 && next_id==0)
	{
		if (filesize < link_file_min_size)
		{
			if (pointed_to != 0)
			{
				FILEENTRY_DEBUG(Server->Log("Small file entry with id " + convert(id) + " with filesize=" + convert(filesize)
					+ " hash=" + base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index)
					+ " has pointed_to!=0 but should be zero. The file entry index may be damaged.", LL_WARNING));
			}

			filesdao.addIncomingFile(filesize, clientid, backupid, convert(clientid),
				with_backupstat ? ServerFilesDao::c_direction_outgoing : ServerFilesDao::c_direction_outgoing_nobackupstat,
				incremental);

			if (del_entry)
			{
				filesdao.delFileEntry(id);
			}

			if (use_transaction)
			{
				filesdao.endTransaction();
			}
			return;
		}

		//client does not have this file anymore
		std::map<int, int64> all_clients = fileindex.get_all_clients_with_cache(FileIndex::SIndexKey(pHash, filesize), true);

		int64 target_entryid = 0;
		std::string clients;
		if(!all_clients.empty())
		{			
			for(std::map<int, int64>::iterator it=all_clients.begin();it!=all_clients.end();++it)
			{
				if(it->second!=0)
				{
					if(!clients.empty())
					{
						clients+=",";
					}

					clients+=convert(it->first);

					if (it->first == clientid)
					{
						target_entryid = it->second;
					}
				}
			}
		}
		else
		{
			FILEENTRY_DEBUG(Server->Log("File entry with id "+convert(id)+" with filesize="+convert(filesize)
				+ " hash="+base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index)
				+ " not found in entry index while deleting, but should be there. The file entry index may be damaged.", LL_WARNING));
			clients+=convert(clientid);
		}

		if (target_entryid == 0)
		{
			FILEENTRY_DEBUG(Server->Log("File entry with id " + convert(id) + " with filesize=" + convert(filesize)
				+ " hash=" + base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index)
				+ " not found for clientid "+convert(clientid)+" in file entry index while deleting, but should be there. The file entry index may be damaged.", LL_WARNING));

			if (!clients.empty())
			{
				clients += ",";
			}

			clients += convert(clientid);
		}
		else if (target_entryid != id)
		{
			FILEENTRY_DEBUG(Server->Log("File entry with id " + convert(id) + " with filesize=" + convert(filesize) +
				" hash=" + base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index) + " is the last file entry for this file and to be deleted. "
				"However, the file entry index points to entry id "+convert(target_entryid)+" which differs. "
				"The file entry index may be damaged or this was a small patch and the files are backed up with snapshots. Not deleting entry from file entry index", LL_WARNING));
		}

		if (!pointed_to)
		{
			FILEENTRY_DEBUG(Server->Log("File entry with id " + convert(id) + " with filesize=" + convert(filesize) 
				+ " hash=" + base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index) + " is the last file entry for this file and to be deleted. "
				"However, pointed_to is zero, so it won't be deleted from the file entry index. The file entry index may be damaged.", LL_WARNING));
		}
		

		filesdao.addIncomingFile(filesize, clientid, backupid, clients,
			with_backupstat? ServerFilesDao::c_direction_outgoing : ServerFilesDao::c_direction_outgoing_nobackupstat,
			incremental);

		if( pointed_to
			&& !all_clients.empty()
			&& (target_entryid==0 || target_entryid==id) )
		{
			FILEENTRY_DEBUG(Server->Log("Delete file index entry id=" + convert(id)+ " filesize="+convert(filesize)+" hash=" 
				+ base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index), LL_DEBUG));
			fileindex.del_delayed(FileIndex::SIndexKey(pHash, filesize, clientid));
		}
	}
	else if(pointed_to)
	{
		if(next_id!=0
			&& next_id!=id)
		{
			std::string str_correction;
			if (correction != NULL
				&& correction->needs_correction(next_id))
			{
				correction->pointed_to[next_id] = 1;
				str_correction = " (with correction)";
			}
			else
			{
				filesdao.setPointedTo(1, next_id);
			}

			fileindex.put_delayed(FileIndex::SIndexKey(pHash, filesize, clientid), next_id);

			FILEENTRY_DEBUG(Server->Log("Changed file index entry filesize="+convert(filesize)+" hash=" 
				+ base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index)
				+ " from " + convert(id) + " to " + convert(next_id) + " (next)"+str_correction, LL_DEBUG));
		}
		else if(prev_id!=id)
		{
			std::string str_correction;

			if (correction != NULL
				&& correction->needs_correction(prev_id))
			{
				correction->pointed_to[prev_id] = 1;
				str_correction = " (with correction)";
			}
			else
			{
				filesdao.setPointedTo(1, prev_id);
			}

			fileindex.put_delayed(FileIndex::SIndexKey(pHash, filesize, clientid), prev_id);

			FILEENTRY_DEBUG(Server->Log("Changed file index entry filesize="+convert(filesize)+" hash = " 
				+ base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index)
				+ " from " + convert(id) + " to " + convert(prev_id) + " (prev)"+str_correction, LL_DEBUG));
		}
		else
		{
			FILEENTRY_DEBUG(Server->Log("Could not change file entry index with pointed_to=1 filesize=" + convert(filesize) + " hash = "
				+ base64_encode(reinterpret_cast<const unsigned char*>(pHash), bytes_in_index)
				+ " from " + convert(id) + " to " + convert(prev_id) + " (prev) or "+convert(next_id)+" (next)", LL_WARNING));
		}
	}

	if(next_id!=0)
	{
		if (correction!=NULL
			&& correction->needs_correction(next_id))
		{
			correction->prev_entries[next_id] = prev_id;
		}
		else
		{
			filesdao.setPrevEntry(prev_id, next_id);
		}
	}

	if(prev_id!=0)
	{
		if (correction != NULL
			&& correction->needs_correction(prev_id))
		{
			correction->next_entries[prev_id] = next_id;
		}
		else
		{
			filesdao.setNextEntry(next_id, prev_id);
		}
	}

	if(del_entry)
	{
		filesdao.delFileEntry(id);
	}

	if(use_transaction)
	{
		filesdao.endTransaction();
	}
}

bool BackupServerHash::findFileAndLink(const std::string &tfn, IFile *tf, std::string hash_fn, const std::string &sha2,
	_i64 t_filesize, const std::string &hashoutput_fn, bool copy_from_hardlink_if_failed,
	bool &tries_once, std::string &ff_last, bool &hardlink_limit, bool &copied_file, int64& entryid, int& entryclientid
	, int64& rsize, int64& next_entry, FileMetadata& metadata, bool detach_dbs, ExtentIterator* extent_iterator)
{
	hardlink_limit=false;
	copied_file=false;


	tries_once=false;	
	bool first_logmsg=true;
	bool copy=true;

	SFindState find_state;
	ServerFilesDao::SFindFileEntry existing_file = findFileHash(sha2, t_filesize, clientid, find_state);

	while(existing_file.exists)
	{
		ff_last=existing_file.fullpath;
		tries_once=true;
		bool too_many_hardlinks;
		bool b = false;
		if (use_snapshots
			&& snapshot_file_inplace)
		{
			if (tf!=NULL
				&& tf->Size() <= 8
				&& os_get_file_type(os_file_prefix(tfn)) != 0)
			{
				//zero patch
				b = true;
			}
			else
			{
				Server->deleteFile(os_file_prefix(tfn));
			}
		}
		if (!b)
		{
			b = os_create_hardlink(os_file_prefix(tfn), os_file_prefix(existing_file.fullpath), use_snapshots, &too_many_hardlinks);
		}
		if(!b)
		{
			if(too_many_hardlinks)
			{
				ServerLogger::Log(logid, "HT: Hardlinking failed (Maximum hardlink count reached) Source=\""+existing_file.fullpath+"\" Destination=\""+tfn+"\"", LL_DEBUG);
				hardlink_limit = true;
				break;
			}
			else
			{
				hardlink_limit = false;

				std::string errmsg;
				int64 errcode = os_last_error(errmsg);

				IFile *ctf=Server->openFile(os_file_prefix(existing_file.fullpath), MODE_READ);
				if(ctf==NULL)
				{
					if(correctPath(existing_file.fullpath, existing_file.hashpath))
					{
						ServerLogger::Log(logid, "HT: Using new backupfolder for: \""+existing_file.fullpath+"\"", LL_DEBUG);
						continue;
					}

					if(first_logmsg)
					{
						ServerLogger::Log(logid, "HT: Hardlinking failed (File doesn't exist): \""+existing_file.fullpath+"\"", LL_DEBUG);
					}
					first_logmsg=false;

					deleteFileSQL(*filesdao, *fileindex, sha2.c_str(), t_filesize, existing_file.rsize, existing_file.clientid, existing_file.backupid, existing_file.incremental,
						existing_file.id, existing_file.prev_entry, existing_file.next_entry, existing_file.pointed_to, true, true, detach_dbs, false, NULL);

					existing_file = findFileHash(sha2, t_filesize, clientid, find_state);
				}
				else
				{
					ServerLogger::Log(logid, "HT: Hardlinking failed (unkown error) Source=\""+existing_file.fullpath+"\" Destination=\""+tfn+"\" -- "+errmsg+" (code: "+convert(errcode)+")", LL_DEBUG);

					if(copy_from_hardlink_if_failed)
					{
						ServerLogger::Log(logid, "HT: Copying from file \""+existing_file.fullpath+"\"", LL_DEBUG);

						if(!copyFile(ctf, tfn, extent_iterator))
						{
							ServerLogger::Log(logid, "Error copying file to destination -3", LL_ERROR);
							has_error=true;
						}
						else
						{
							copied_file=true;
							entryid = existing_file.id;
							entryclientid = existing_file.clientid;
							next_entry=existing_file.next_entry;
						}

						assert(!hash_fn.empty());

						if(!existing_file.hashpath.empty())
						{
							std::auto_ptr<IFile> ctf_hash(Server->openFile(os_file_prefix(existing_file.hashpath), MODE_READ));

							bool write_metadata = true;

							if(ctf_hash.get()!=NULL)
							{
								int64 hashfilesize = read_hashdata_size(ctf_hash.get());
								assert(hashfilesize == -1 || hashfilesize == t_filesize);
								if (hashfilesize != -1)
								{
									if (!copyFile(ctf_hash.get(), hash_fn, NULL))
									{
										ServerLogger::Log(logid, "Error copying hashfile to destination -3", LL_ERROR);
										has_error = true;
										write_metadata = false;
									}
									else
									{
										if (!os_file_truncate(os_file_prefix(hash_fn), get_hashdata_size(t_filesize)))
										{
											ServerLogger::Log(logid, "Error truncating hashdata file -2. " + os_last_error_str(), LL_ERROR);
										}
									}
								}
							}
							else
							{
								Server->Log("Error opening hash source file \""+existing_file.hashpath+"\". " + os_last_error_str(), LL_ERROR);
							}

							if (write_metadata && !write_file_metadata(hash_fn, this, metadata, false))
							{
								ServerLogger::Log(logid, "Error writing file metadata -2", LL_ERROR);
								has_error = true;
							}
						}

						copy=false;
					}

					Server->destroy(ctf);
					break;
				}		
			}
		}
		else //successfully linked file
		{
			entryid = existing_file.id;
			entryclientid = existing_file.clientid;
			next_entry=existing_file.next_entry;

			assert(!hash_fn.empty());
			
			if(existing_file.rsize!=0 &&
				existing_file.rsize!=existing_file.filesize)			
			{
				rsize=existing_file.rsize;
			}
			else
			{
				rsize=0;
			}

			metadata.rsize=rsize;

			bool write_metadata=true;

			if(!hashoutput_fn.empty())
			{
				std::auto_ptr<IFile> src(Server->openFile(os_file_prefix(hashoutput_fn), MODE_READ));
				if(src.get()!=NULL)
				{
					if(!copyFile(src.get(), hash_fn, NULL))
					{
						ServerLogger::Log(logid, "Error copying hashoutput to destination -1", LL_ERROR);
						has_error=true;
						hash_fn.clear();
						write_metadata = false;
					}
					else
					{
						if(!os_file_truncate(os_file_prefix(hash_fn), get_hashdata_size(t_filesize)))
						{
							ServerLogger::Log(logid, "Error truncating hashdata file -1", LL_ERROR);
						}
					}
				}
				else
				{
					ServerLogger::Log(logid, "HT: Error opening hashoutput", LL_ERROR);
					has_error=true;
					hash_fn.clear();
					write_metadata = false;
				}
			}
			else if(!existing_file.hashpath.empty())
			{
				std::auto_ptr<IFile> ctf(Server->openFile(os_file_prefix(existing_file.hashpath), MODE_READ));
				if(ctf.get()!=NULL)
				{
					int64 hashfilesize = read_hashdata_size(ctf.get());
					if(hashfilesize!=-1
						&& hashfilesize == t_filesize)
					{
						if(!copyFile(ctf.get(), hash_fn, NULL))
						{
							ServerLogger::Log(logid, "Error copying hashfile to destination -2", LL_ERROR);
							has_error=true;
							hash_fn.clear();
							write_metadata = false;
						}
						else
						{
							if(!os_file_truncate(os_file_prefix(hash_fn), get_hashdata_size(t_filesize)))
							{
								ServerLogger::Log(logid, "Error truncating hashdata file -2. " + os_last_error_str(), LL_ERROR);
							}
						}
					}
					else
					{
						if (hashfilesize != -1)
						{
							Server->Log("File size in meta-data file \"" + existing_file.hashpath + "\" does not match database. From database=" + convert(t_filesize) + " In meta-data=" + convert(hashfilesize), LL_WARNING);
						}
					}
				}
			}

			if(write_metadata && !write_file_metadata(hash_fn, this, metadata, false))
			{
				ServerLogger::Log(logid, "Error writing file metadata -1", LL_ERROR);
				has_error=true;
			}

			copy=false;
			break;
		}
	}

	return !copy;
}

void BackupServerHash::addFile(int backupid, int incremental, IFile *tf, const std::string &tfn,
	std::string hash_fn, const std::string &sha2, const std::string &orig_fn, const std::string &hashoutput_fn, int64 t_filesize,
	FileMetadata& metadata, bool with_hashes, ExtentIterator* extent_iterator)
{
	bool copy=true;
	bool tries_once = false;
	std::string ff_last;
	bool hardlink_limit = false;
	bool copied_file = false;
	int64 entryid = 0;
	int entryclientid = 0;
	int64 next_entryid = 0;
	int64 rsize = 0;
	if(t_filesize>= link_file_min_size
		&& (!snapshot_file_inplace || t_filesize<50*1024*1024 || tf->Size()>10*1024 || tf->Size()<=8)
		&& findFileAndLink(tfn, tf, hash_fn, sha2, t_filesize,hashoutput_fn,
		false, tries_once, ff_last, hardlink_limit, copied_file, entryid, entryclientid, rsize, next_entryid,
		metadata, false, extent_iterator))
	{
		ServerLogger::Log(logid, "HT: Linked file: \""+tfn+"\"", LL_DEBUG);
		copy=false;
		std::string temp_fn=tf->getFilename();
		Server->destroy(tf);
		tf=NULL;
		Server->deleteFile(temp_fn);
		addFileSQL(backupid, clientid, incremental, tfn, hash_fn, sha2, t_filesize, rsize, entryid, entryclientid, next_entryid, copied_file);
	}

	if(tries_once && copy && !hardlink_limit)
	{
		if(link_logcnt<5)
		{
			ServerLogger::Log(logid, "HT: Error creating hardlink from \""+ff_last+"\" to \""+tfn+"\"", LL_WARNING);
		}
		else if(link_logcnt==5)
		{
			ServerLogger::Log(logid, "HT: More hardlink errors. Skipping... ", LL_WARNING);
		}
		else
		{
			Server->Log("HT: Error creating hardlink from \""+ff_last+"\" to \""+tfn+"\"", LL_WARNING);
		}
		++link_logcnt;
	}
	
	if(copy)
	{
		ServerLogger::Log(logid, "HT: Copying file: \""+tfn+"\"", LL_DEBUG);
		int64 fs=tf->Size();
		if(!use_reflink)
		{
			fs=t_filesize;
		}
		int64 available_space=0;
		cow_filesize=0;
		if(fs>0)
		{
			available_space=os_free_space(os_file_prefix(ExtractFilePath(tfn, os_file_sep())));
		}
		if(available_space==-1)
		{
			if(space_logcnt==0)
			{
				ServerLogger::Log(logid, "HT: Error getting free space for path \""+tfn+"\"", LL_ERROR);
				++space_logcnt;
			}
			else
			{
				ServerLogger::Log(logid, "HT: Error getting free space for path \""+tfn+"\"", LL_ERROR);
			}
			std::string temp_fn=tf->getFilename();
			Server->destroy(tf);
			tf=NULL;
			Server->deleteFile(temp_fn);
		}
		else
		{
			available_space-=freespace_mod;

			bool free_ok=true;

			if( fs>0 && available_space<=fs )
			{
				if(space_logcnt==0)
				{
					ServerLogger::Log(logid, "HT: No free space available deleting backups...", LL_WARNING);
				}
				else
				{
					ServerLogger::Log(logid, "HT: No free space available deleting backups...", LL_WARNING);
				}
				free_ok=freeSpace(fs, os_file_prefix(tfn));
			}

			if(!free_ok)
			{
				if(space_logcnt==0)
				{
					ServerLogger::Log(logid, "HT: FATAL: Error freeing space", LL_ERROR);
					++space_logcnt;
				}
				else
				{
					ServerLogger::Log(logid, "HT: FATAL: Error freeing space", LL_ERROR);
				}
				has_error=true;
				std::string temp_fn=tf->getFilename();
				Server->destroy(tf);
				tf=NULL;
				Server->deleteFile(temp_fn);
			}
			else
			{
				bool r;
				if(hashoutput_fn.empty())
				{
					if(!use_reflink || orig_fn.empty())
					{
						if(with_hashes)
						{
							if(use_tmpfiles)
							{
								r=copyFileWithHashoutput(tf, tfn, hash_fn, extent_iterator);
							}
							else
							{
								r=renameFileWithHashoutput(tf, tfn, hash_fn, extent_iterator);
								tf=NULL;
							}
						}
						else
						{
							if(use_tmpfiles)
							{
								r=copyFile(tf, tfn, extent_iterator);
							}
							else
							{
								r=renameFile(tf, tfn);
								tf=NULL;
							}
						}
					}
					else
					{
						if(with_hashes)
						{
							r=replaceFileWithHashoutput(tf, tfn, hash_fn, orig_fn, extent_iterator);
						}
						else
						{
							r=replaceFile(tf, tfn, orig_fn, extent_iterator);
						}
					}
				}
				else
				{
					r=patchFile(tf, orig_fn, tfn, hashoutput_fn, hash_fn, t_filesize, extent_iterator);
				}
				
				if(!r)
				{
					has_error=true;
					ServerLogger::Log(logid, "Storing file to \""+tfn+"\" failed", LL_ERROR);
				}

				if(tf!=NULL)
				{
					metadata.rsize=tf->Size();

					std::string temp_fn=tf->getFilename();
					Server->destroy(tf);
					tf=NULL;
					Server->deleteFile(os_file_prefix(temp_fn));
				}

				if(r)
				{
					if(cow_filesize>0)
					{
						metadata.rsize=cow_filesize;
					}

					if(!write_file_metadata(hash_fn, this, metadata, false))
					{
						ServerLogger::Log(logid, "Writing metadata to "+hash_fn+" failed", LL_ERROR);
						has_error=true;
					}

					addFileSQL(backupid, clientid, incremental, tfn, hash_fn, sha2, t_filesize, cow_filesize>0?cow_filesize:t_filesize, 0, 0, 0, tries_once || hardlink_limit);
				}
			}
		}
	}
}

bool BackupServerHash::freeSpace(int64 fs, const std::string &fp)
{
	IScopedLock lock(delete_mutex);

	int64 available_space=os_free_space(ExtractFilePath(fp, os_file_sep()));
	if(available_space==-1)
	{
		if(space_logcnt==0)
		{
			ServerLogger::Log(logid, "Error getting free space for path \""+fp+"\"", LL_ERROR);
			++space_logcnt;
		}
		else
		{
			ServerLogger::Log(logid, "Error getting free space for path \""+fp+"\"", LL_ERROR);
		}
		return false;
	}
	else
	{
		available_space-=freespace_mod;
	}

	if(available_space>fs)
		return true;

	bool b =  ServerCleanupThread::cleanupSpace(freespace_mod+fs);

	return b;
}

ServerFilesDao::SFindFileEntry BackupServerHash::findFileHash(const std::string &pHash, _i64 filesize, int clientid, SFindState& state)
{
	int64 entryid;
	
	bool save_orig=false;
	bool switch_to_all_clients=false;
	bool switch_to_next_client=false;
	if(state.state==0)
	{
		entryid = fileindex->get_with_cache_prefer_client(FileIndex::SIndexKey(pHash.c_str(), filesize, clientid));
		state.state=1;
		save_orig=true;
	}
	else if(state.state==1)
	{
		if(state.prev.next_entry!=0)
		{
			entryid=state.prev.next_entry;
		}
		else
		{
			if(state.orig_prev!=0)
			{
				entryid = state.orig_prev;
				state.state=2;
			}
			else
			{
				switch_to_all_clients=true;
			}
		}
	}
	else if(state.state==2)
	{
		if(state.prev.prev_entry!=0)
		{
			entryid=state.prev.prev_entry;
		}
		else
		{
			switch_to_all_clients=true;
		}
	}
	else if(state.state==3)
	{
		if(state.prev.next_entry==0)
		{
			if(state.orig_prev!=0)
			{
				entryid = state.orig_prev;
				state.state=4;
			}
			else
			{
				switch_to_next_client=true;
			}
			
		}
		else
		{
			entryid=state.prev.next_entry;
		}
	}
	else if(state.state=4)
	{
		if(state.prev.prev_entry==0)
		{
			switch_to_next_client=true;
		}
		else
		{
			entryid=state.prev.prev_entry;
		}
	}

	if(switch_to_all_clients)
	{
		state.state=3;
		state.entryids = fileindex->get_all_clients_with_cache(FileIndex::SIndexKey(pHash.c_str(), filesize, 0), false);
		state.client = state.entryids.begin();
		if(state.client!=state.entryids.end())
		{
			entryid=state.client->second;
			save_orig=true;
		}
		else
		{
			entryid=0;
		}
	}

	if(switch_to_next_client)
	{
		++state.client;
		if(state.client==state.entryids.end())
		{
			entryid=0;
		}
		else
		{
			entryid=state.client->second;
		}
	}

	if(entryid==0)
	{
		ServerFilesDao::SFindFileEntry ret;
		ret.exists=false;
		return ret;
	}

	state.prev = filesdao->getFileEntry(entryid);

	if(!state.prev.exists)
	{
		ServerLogger::Log(logid, "Entry from file entry index not found. File entry index probably out of sync. (id="+convert(entryid)+")", LL_DEBUG);
		ServerFilesDao::SFindFileEntry ret;
		ret.exists=false;
		return ret;
	}

	if(memcmp(state.prev.shahash.data(), pHash.data(), pHash.size())!=0)
	{
		ServerLogger::Log(logid, "Hash of file entry differs from file entry index result. Something may be wrong with the file entry index or this is a hash collision. Ignoring existing file and downloading anew.", LL_DEBUG);
		ServerLogger::Log(logid, "While searching for file with size "+convert(filesize)+" and clientid "+convert(clientid)+". Resulting file path is \""+state.prev.fullpath+"\". (id="+convert(entryid)+")", LL_DEBUG);
		ServerFilesDao::SFindFileEntry ret;
		ret.exists=false;
		return ret;
	}

	assert(state.prev.filesize == filesize);

	if(save_orig && state.prev.exists)
	{
		state.orig_prev=state.prev.prev_entry;
	}

	return state.prev;
}

IFsFile* BackupServerHash::openFileRetry(const std::string &dest, int mode, std::string& errstr)
{
	IFsFile *dst=NULL;
	int count_t=0;
	while(dst==NULL)
	{
		dst=Server->openFile(os_file_prefix(dest), mode);
		if(dst==NULL)
		{
			errstr = os_last_error_str();
			ServerLogger::Log(logid, "Error opening file... \""+dest+"\" retrying... "+ errstr, LL_DEBUG);
			Server->wait(500);
			++count_t;
			if(count_t>=10)
			{
				ServerLogger::Log(logid, "Error opening file... \""+dest+"\". " + errstr, LL_ERROR);
				return NULL;
			}
		}
	}

	return dst;
}

bool BackupServerHash::copyFile(IFile *tf, const std::string &dest, ExtentIterator* extent_iterator)
{
	std::string errstr;
	std::auto_ptr<IFsFile> dst(openFileRetry(dest, MODE_WRITE, errstr));
	if (dst.get() == NULL)
	{
		ServerLogger::Log(logid, "Error opening dest file \"" + dest + "\". " + errstr, LL_ERROR);
		return false;
	}

	tf->Seek(0);
	_u32 read;
	char buf[BUFFER_SIZE];
	int64 fpos = 0;
	IFsFile::SSparseExtent curr_extent;

	if (extent_iterator != NULL)
	{
		curr_extent = extent_iterator->nextExtent();
	}

	int64 sparse_max = -1;

	do
	{
		while (curr_extent.offset != -1
			&& fpos >= curr_extent.offset
			&& fpos <= curr_extent.offset+ curr_extent.size)
		{
			fpos = curr_extent.offset + curr_extent.size;

			sparse_max = fpos;
				
			if (!tf->Seek(fpos))
			{
				ServerLogger::Log(logid, "Error seeking in source file for sparse extent \"" + tf->getFilename() + "\" -2. " + os_last_error_str(), LL_ERROR);
				return false;
			}

			if (!punchHoleOrZero(dst.get(), curr_extent.offset, curr_extent.size))
			{
				ServerLogger::Log(logid, "Error adding sparse extent to \"" + dest + "\"", LL_ERROR);
				return false;
			}

			if (!dst->Seek(fpos))
			{
				ServerLogger::Log(logid, "Error seeking in \"" + dest + "\" after adding sparse extent", LL_ERROR);
				return false;
			}

			curr_extent = extent_iterator->nextExtent();
		}

		_u32 toread = BUFFER_SIZE;

		if (curr_extent.offset != -1
			&& fpos + toread > curr_extent.offset)
		{
			toread = (std::min)(toread, static_cast<_u32>(curr_extent.offset - fpos));
		}

		bool has_read_error = false;
		read=tf->Read(buf, toread, &has_read_error);

		if (has_read_error)
		{
			ServerLogger::Log(logid, "Error while reading from \"" + tf->getFilename() + "\" while copying to \""+dest+"\"", LL_ERROR);
			return false;
		}

		bool b=writeRepeatFreeSpace(dst.get(), buf, read, this);
		if(!b)
		{
			ServerLogger::Log(logid, "Error writing to file \""+dest+"\" -2. "+os_last_error_str(), LL_ERROR);
			return false;
		}
		else
		{
			fpos += read;
		}
	}
	while(read>0);

	if (sparse_max!=-1
		&& sparse_max > dst->Size())
	{
		sparse_max = (std::min)(tf->Size(), sparse_max);
		if (!dst->Resize(sparse_max))
		{
			ServerLogger::Log(logid, "Error resizing file \"" + dest + "\" to max sparse size " + convert(sparse_max), LL_ERROR);
			return false;
		}
	}

	return true;
}

bool BackupServerHash::copyFileWithHashoutput(IFile *tf, const std::string &dest, const std::string hash_dest, ExtentIterator* extent_iterator)
{
	std::string errstr;
	IFsFile *dst=openFileRetry(dest, MODE_WRITE, errstr);
	if(dst==NULL) return false;
	ObjectScope dst_s(dst);

	if(tf->Size()>0)
	{
		IFile *dst_hash=openFileRetry(hash_dest, MODE_RW_CREATE, errstr);
		if(dst_hash==NULL)
		{
			return false;
		}
		ObjectScope dst_hash_s(dst_hash);

		return build_chunk_hashs(tf, dst_hash, this, dst, false, NULL, NULL, false, NULL, extent_iterator);
	}
	
	return true;
}

bool BackupServerHash::isWorking(void)
{
	return working;
}

bool BackupServerHash::hasError(void)
{
	volatile bool r=has_error;
	has_error=false;
	return r;
}

bool BackupServerHash::handle_not_enough_space(const std::string &path)
{
	int64 available_space=os_free_space(ExtractFilePath(path));
	if(available_space==-1)
	{
		if(space_logcnt==0)
		{
			ServerLogger::Log(logid, "Error writing to file \""+path+"\". "+os_last_error_str(), LL_ERROR);
			++space_logcnt;
		}
		else
		{
			ServerLogger::Log(logid, "Error writing to file \""+path+"\". "+os_last_error_str(), LL_ERROR);
		}
		return false;
	}
	else
	{
		if(available_space<=freespace_mod)
		{
			if(space_logcnt==0)
			{
				ServerLogger::Log(logid, "HT: No free space available deleting backups...", LL_WARNING);
			}
			else
			{
				ServerLogger::Log(logid, "HT: No free space available deleting backups...", LL_WARNING);
			}
			
			return freeSpace(0, path);
		}

		return true;
	}
}

void BackupServerHash::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed, bool* is_sparse)
{
	if(!has_reflink || changed )
	{
		if (buf != NULL) //buf is NULL for sparse extents
		{
			if (!chunk_output_fn->Seek(chunk_patch_pos))
			{
				ServerLogger::Log(logid, "Error seeking to offset "+convert(chunk_patch_pos)+" in \"" + chunk_output_fn->getFilename() + "\" -3", LL_ERROR);
				chunk_patcher_has_error = true;
			}
			bool b = writeRepeatFreeSpace(chunk_output_fn, buf, bsize, this);
			if (!b)
			{
				ServerLogger::Log(logid, "Error writing to file \"" + chunk_output_fn->getFilename() + "\" -3. "+os_last_error_str(), LL_ERROR);
				chunk_patcher_has_error = true;
			}
		}
		else
		{
#ifdef _WIN32
			if (!enabled_sparse)
			{
				//Punch hole once to enable sparse file on Windows
				chunk_output_fn->PunchHole(chunk_patch_pos, bsize);
				enabled_sparse = true;
			}
#endif
		}
	}
	chunk_patch_pos+=bsize;

	if(has_reflink && changed)
	{
		cow_filesize+=bsize;
	}
}

void BackupServerHash::next_sparse_extent_bytes(const char * buf, size_t bsize)
{
}

int64 BackupServerHash::chunk_patcher_pos()
{
	return chunk_patch_pos;
}

bool BackupServerHash::patchFile(IFile *patch, const std::string &source, const std::string &dest,
	const std::string hash_output, const std::string hash_dest, _i64 tfilesize, ExtentIterator* extent_iterator)
{
	_i64 dstfsize;
	{
		has_reflink=false;
		if( use_reflink )
		{
			if( (!snapshot_file_inplace || (os_get_file_type(os_file_prefix(dest)) & EFileType_File)==0)
				&& !os_create_hardlink(os_file_prefix(dest), os_file_prefix(source), true, NULL) )
			{
				ServerLogger::Log(logid, "Reflinking file \""+dest+"\" failed", LL_WARNING);
			}
			else
			{
				has_reflink=true;
			}
		}

		std::string errstr;
		chunk_output_fn=openFileRetry(dest, has_reflink?MODE_RW:MODE_WRITE, errstr);
		if (chunk_output_fn == NULL)
		{
			ServerLogger::Log(logid, "Error opening chunk output file \"" + dest + "\". "+ errstr, LL_ERROR);
			return false;
		}
		ObjectScope dst_s(chunk_output_fn);

		IFile *f_source=openFileRetry(source, MODE_READ, errstr);
		if (f_source == NULL)
		{
			ServerLogger::Log(logid, "Error opening patch source file \"" + source + "\". "+errstr, LL_ERROR);
			return false;
		}
		ObjectScope f_source_s(f_source);

		chunk_patch_pos=0;
		enabled_sparse = false;
		chunk_patcher_has_error = false;
		chunk_patcher.setRequireUnchanged(!has_reflink);
		bool b=chunk_patcher.ApplyPatch(f_source, patch, extent_iterator);

		if (!b)
		{
			ServerLogger::Log(logid, "Error applying patch to \"" + dest + "\" with source \"" + source + "\"", LL_ERROR);
		}

		IFsFile::SSparseExtent sparse_extent;

		if (extent_iterator != NULL)
		{
			extent_iterator->reset();

			sparse_extent = extent_iterator->nextExtent();
		}

		dstfsize = chunk_output_fn->Size();

		bool sparse_resize = false;
		bool has_punch_error = false;

		while (sparse_extent.offset != -1)
		{
			if (!punchHoleOrZero(chunk_output_fn, sparse_extent.offset, sparse_extent.size))
			{
				if (!has_punch_error)
				{
					ServerLogger::Log(logid, "Error punching hole into \"" + dest + "\"", LL_ERROR);
				}

				has_punch_error = true;
				chunk_patcher_has_error = true;
			}
			if (sparse_extent.offset + sparse_extent.size > dstfsize)
			{
				sparse_resize = true;
				dstfsize = sparse_extent.offset + sparse_extent.size;
			}
			sparse_extent = extent_iterator->nextExtent();
		}

		if (sparse_resize)
		{
			if (!chunk_output_fn->Resize(dstfsize))
			{
				ServerLogger::Log(logid, "Error resizing \"" + dest + "\" to " + convert(dstfsize), LL_ERROR);
				return false;
			}
		}
		
		if(chunk_patcher_has_error || !b)
		{
			return false;
		}
	}
	
	assert(chunk_patcher.getFilesize() == tfilesize);

	if( dstfsize > chunk_patcher.getFilesize() )
	{
		if (!os_file_truncate(dest, chunk_patcher.getFilesize()))
		{
			ServerLogger::Log(logid, "Error truncating \""+dest+"\" to "+convert(chunk_patcher.getFilesize()), LL_ERROR);
			return false;
		}
	}
	else
	{
		if (dstfsize != tfilesize)
		{
			ServerLogger::Log(logid, "dstfsize="+convert(dstfsize)+" tfilesize="+convert(tfilesize)+" dest=\""+ dest+"\" patch=\""+patch->getFilename()+"\" source=\""+ source+"\"", LL_ERROR);
		}
		assert(dstfsize==tfilesize);
	}

	IFile *f_hash_output=Server->openFile(os_file_prefix(hash_output), MODE_READ);
	if(f_hash_output==NULL)
	{
		ServerLogger::Log(logid, "Error opening hashoutput file -1", LL_ERROR);
		return false;
	}
	ObjectScope f_hash_output_s(f_hash_output);

	if(!copyFile(f_hash_output, hash_dest, NULL))
	{
		ServerLogger::Log(logid, "Error copying hashoutput file to destination", LL_ERROR);
		return false;
	}

	return true;
}

const size_t RP_COPY_BLOCKSIZE=1024;

bool BackupServerHash::replaceFile(IFile *tf, const std::string &dest, const std::string &orig_fn, ExtentIterator* extent_iterator)
{
	if( (!snapshot_file_inplace || (os_get_file_type(os_file_prefix(dest)) & EFileType_File) == 0)
		&& ! os_create_hardlink(os_file_prefix(dest), os_file_prefix(orig_fn), true, NULL) )
	{
		ServerLogger::Log(logid, "Reflinking file \""+dest+"\" failed -2", LL_ERROR);

		return copyFile(tf, dest, extent_iterator);
	}

	ServerLogger::Log(logid, "HT: Copying with reflink data from \""+orig_fn+"\"", LL_DEBUG);

	std::string errstr;
	std::auto_ptr<IFile> dst(openFileRetry(dest, MODE_RW, errstr));
	if (dst.get() == NULL)
	{
		ServerLogger::Log(logid, "Error opening destination file at \"" + dest + "\". " + errstr, LL_ERROR);
		return false;
	}


	IFsFile::SSparseExtent curr_extent;
	if (extent_iterator != NULL)
	{
		extent_iterator->reset();

		curr_extent = extent_iterator->nextExtent();
	}

	tf->Seek(0);
	_u32 read1;
	_u32 read2;
	char buf1[RP_COPY_BLOCKSIZE];
	char buf2[RP_COPY_BLOCKSIZE];
	_i64 dst_pos=0;
	bool dst_eof=false;
	do
	{
		while (curr_extent.offset != -1
			&& curr_extent.offset >= dst_pos
			&& curr_extent.offset + curr_extent.size > dst_pos)
		{
			if (!punchHoleOrZero(dst.get(), curr_extent.offset, curr_extent.size))
			{
				ServerLogger::Log(logid, "Error punching hole into \""+dest+"\"", LL_ERROR);
				return false;
			}

			dst_pos = curr_extent.offset + curr_extent.size;

			if (!tf->Seek(dst_pos) || !dst->Seek(dst_pos))
			{
				ServerLogger::Log(logid, "Error seeking after punching hole in replaceFile", LL_ERROR);
				return false;
			}

			curr_extent = extent_iterator->nextExtent();
		}

		bool has_read_error = false;
		read1=tf->Read(buf1, RP_COPY_BLOCKSIZE, &has_read_error);
		if(!dst_eof)
		{
			read2=dst->Read(buf2, RP_COPY_BLOCKSIZE);
		}
		else
		{
			read2=0;
		}

		if (has_read_error)
		{
			ServerLogger::Log(logid, "Error reading from \"" + tf->getFilename() + "\" -1", LL_ERROR);
			return false;
		}

		if(read2<read1)
			dst_eof=true;

		if(read1!=read2 || memcmp(buf1, buf2, read1)!=0)
		{
			dst->Seek(dst_pos);
			cow_filesize+=read1;
			bool b=writeRepeatFreeSpace(dst.get(), buf1, read1, this);
			if(!b)
			{
				ServerLogger::Log(logid, "Error writing to file \""+dest+"\" -2. "+os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		dst_pos+=read1;
	}
	while(read1>0);

	_i64 dst_size=dst->Size();

	dst.reset();

	if( dst_size!=tf->Size() )
	{
		if( !os_file_truncate(os_file_prefix(dest), tf->Size()) )
		{
			ServerLogger::Log(logid, "Error truncating file \""+dest+"\" -2", LL_ERROR);
			return false;
		}
	}

	return true;
}

bool BackupServerHash::replaceFileWithHashoutput(IFile *tf, const std::string &dest,
	const std::string hash_dest, const std::string &orig_fn, ExtentIterator* extent_iterator)
{
	if( (!snapshot_file_inplace || (os_get_file_type(os_file_prefix(dest)) & EFileType_File) == 0)
		&& !os_create_hardlink(os_file_prefix(dest), os_file_prefix(orig_fn), true, NULL) )
	{
		ServerLogger::Log(logid, "Reflinking file \""+dest+"\" failed -3", LL_ERROR);

		return copyFileWithHashoutput(tf, dest, hash_dest, extent_iterator);
	}

	ServerLogger::Log(logid, "HT: Copying with hashoutput with reflink data from \""+orig_fn+"\"", LL_DEBUG);

	std::string errstr;
	IFsFile *dst=openFileRetry(os_file_prefix(dest), MODE_RW, errstr);
	if (dst == NULL)
	{
		ServerLogger::Log(logid, "Error opening dest file at \"" + dest + "\". "+ errstr, LL_ERROR);
		return false;
	}
	ObjectScope dst_s(dst);

	if(tf->Size()>0)
	{
		IFile *dst_hash=openFileRetry(os_file_prefix(hash_dest), MODE_WRITE, errstr);
		if(dst_hash==NULL)
		{
			ServerLogger::Log(logid, "Error opening dest hash file at \"" + hash_dest + "\". "+ errstr, LL_ERROR);
			return false;
		}
		ObjectScope dst_hash_s(dst_hash);

		if (!build_chunk_hashs(tf, dst_hash, this, dst, true, &cow_filesize, NULL, false, NULL, extent_iterator))
		{
			ServerLogger::Log(logid, "Error copying file with chunk hashes to \""+dest+"\"", LL_ERROR);
			return false;
		}

		_i64 dst_size=dst->Size();

		dst_s.clear();

		if( dst_size!=tf->Size() )
		{
			if( !os_file_truncate(os_file_prefix(dest), tf->Size()) )
			{
				ServerLogger::Log(logid, "Error truncating file \""+dest+"\" -2", LL_ERROR);
				return false;
			}
		}
	}
	
	return true;
}

bool BackupServerHash::renameFileWithHashoutput(IFile *tf, const std::string &dest, const std::string hash_dest, ExtentIterator* extent_iterator)
{
	if(tf->Size()>0)
	{
		std::string errstr;
        IFile *dst_hash=openFileRetry(hash_dest, MODE_RW_CREATE, errstr);
		if(dst_hash==NULL)
		{
			return false;
		}
		ObjectScope dst_hash_s(dst_hash);

		//TODO: Already build hashes during hashing stage
		if (!build_chunk_hashs(tf, dst_hash, this, NULL, false, NULL, NULL, false, NULL, extent_iterator))
		{
			return false;
		}
	}


	return renameFile(tf, dest);
}

bool BackupServerHash::renameFile(IFile *tf, const std::string &dest)
{
	std::string tf_fn=tf->getFilename();
	Server->destroy(tf);

	if (!os_rename_file(os_file_prefix(tf_fn), os_file_prefix(dest)))
	{
		std::string err = os_last_error_str();
		if (use_reflink &&
			os_create_hardlink(os_file_prefix(dest), os_file_prefix(tf_fn), true, NULL))
		{
			Server->deleteFile(os_file_prefix(tf_fn));
		}
		else
		{
			ServerLogger::Log(logid, "Renaming file \""+ tf_fn+"\" to \"" + dest + "\" failed -4. "+err, LL_ERROR);
			return false;
		}
	}

	return true;
}

bool BackupServerHash::correctPath( std::string& ff, std::string& f_hashpath )
{
	if(!old_backupfolders_loaded)
	{
		old_backupfolders_loaded=true;
		ServerBackupDao backupdao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER));
		old_backupfolders= backupdao.getOldBackupfolders();
	}

	if(backupfolder.empty())
	{
		ServerSettings settings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER));
		backupfolder = settings.getSettings()->backupfolder;
	}

	for(size_t i=0;i<old_backupfolders.size();++i)
	{
		size_t erase_size = old_backupfolders[i].size() + os_file_sep().size();
		if(ff.size()>erase_size &&
			next(ff, 0, old_backupfolders[i]))
		{
			std::string tmp_ff = backupfolder + os_file_sep() + ff.substr(erase_size);
						
			IFile* f = Server->openFile(tmp_ff, MODE_READ);
			if(f!=NULL)
			{
				Server->destroy(f);

				if(f_hashpath.size()>erase_size)
				{
					f_hashpath = backupfolder + os_file_sep() + f_hashpath.substr(erase_size);
				}

				ff = tmp_ff;

				return true;
			}
		}
	}

	return false;
}

bool BackupServerHash::punchHoleOrZero(IFile * tf, int64 offset, int64 size)
{
	if (!tf->PunchHole(offset, size))
	{
		std::vector<char> zero_buf;
		zero_buf.resize(32768);

		if (tf->Seek(offset))
		{
			for (int64 written = 0; written < size;)
			{
				_u32 towrite = static_cast<_u32>((std::min)(size - written, static_cast<int64>(zero_buf.size())));
				if (!writeRepeatFreeSpace(tf, zero_buf.data(), towrite, this))
				{
					ServerLogger::Log(logid, "Error zeroing data in \"" + chunk_output_fn->getFilename() + "\" after punching hole failed -2. " + os_last_error_str(), LL_ERROR);
					return false;
				}
				written += towrite;
			}
		}
		else
		{
			ServerLogger::Log(logid, "Error zeroing data in \"" + chunk_output_fn->getFilename() + "\" after punching hole failed -1. "+os_last_error_str(), LL_ERROR);
			return false;
		}
	}

	return true;
}

