/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include "server_hash.h"
#include "server_prepare_hash.h"
#include "server_settings.h"
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
#include "file_metadata.h"
#include <assert.h>

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

BackupServerHash::BackupServerHash(IPipe *pPipe, int pClientid, bool use_snapshots, bool use_reflink, bool use_tmpfiles)
	: use_snapshots(use_snapshots), use_reflink(use_reflink), use_tmpfiles(use_tmpfiles), copy_limit(1000), backupdao(NULL), old_backupfolders_loaded(false)
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
		Server->Log("Reflink copying is enabled", LL_DEBUG);
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
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	{
	    bool r;
	    do
	    {
		r=db->Write("CREATE TEMPORARY TABLE files_tmp ( backupid INTEGER, fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, created DATE DEFAULT CURRENT_TIMESTAMP, rsize INTEGER, clientid INTEGER, incremental INTEGER);");
		if(!r)
		    Server->wait(1000);
		    
	    }while(!r);
	}

	backupdao = new ServerBackupDao(db);

	fileindex=create_lmdb_files_index(); 
}

void BackupServerHash::deinitDatabase(void)
{
	db->freeMemory();

	delete fileindex;
	fileindex=NULL;

	delete backupdao;
	backupdao=NULL;
}

void BackupServerHash::operator()(void)
{
	setupDatabase();

	db->DetachDBs();
	

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
			db->AttachDBs();
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

				std::string tfn;
				rd.getStr(&tfn);

				std::string hashpath;
				rd.getStr(&hashpath);

				std::string sha2;
				if(!rd.getStr(&sha2))
					ServerLogger::Log(clientid, "Reading hash from pipe failed", LL_ERROR);

				if(sha2.size()!=64)
					ServerLogger::Log(clientid, "SHA512 length of file \""+tfn+"\" wrong.", LL_ERROR);

				std::string hashoutput_fn;
				rd.getStr(&hashoutput_fn);

				std::string old_file_fn;
				rd.getStr(&old_file_fn);

				int64 t_filesize;
				rd.getInt64(&t_filesize);

				FileMetadata metadata;
				metadata.read(rd);
				metadata.set_shahash(sha2);

				FileMetadata parent_metadata;
				parent_metadata.read(rd);

				IFile *tf=Server->openFile(os_file_prefix(Server->ConvertToUnicode(temp_fn)), MODE_READ_SEQUENTIAL);

				if(tf==NULL)
				{
					ServerLogger::Log(clientid, "Error opening file \""+temp_fn+"\" from pipe for reading ec="+nconvert(os_last_error()), LL_ERROR);
					has_error=true;
				}
				else
				{
					addFile(backupid, incremental, tf, Server->ConvertToUnicode(tfn), Server->ConvertToUnicode(hashpath), sha2,
						old_file_fn, hashoutput_fn, t_filesize, metadata, parent_metadata);
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
				if(read_metadata(os_file_prefix(Server->ConvertToUnicode(hash_src)),
					src_metadata))
				{
					metadata.set_shahash(src_metadata.shahash);
				}

				std::auto_ptr<IFile> tf(Server->openFile(os_file_prefix(Server->ConvertToUnicode(source)), MODE_READ_SEQUENTIAL));

				if(!tf.get())
				{
					ServerLogger::Log(clientid, "Error opening file \""+source+"\" from pipe for reading ec="+nconvert(os_last_error()), LL_ERROR);
					has_error=true;
				}
				else
				{
					if(!copyFile(tf.get(), Server->ConvertToUnicode(dest)))
					{
						ServerLogger::Log(clientid, "Error while copying file \""+source+"\" to \""+dest+"\"", LL_ERROR);
						has_error=true;
					}

					if(!hash_src.empty())
					{
						std::auto_ptr<IFile> hashf(Server->openFile(os_file_prefix(Server->ConvertToUnicode(hash_src)), MODE_READ_SEQUENTIAL));
						if(hashf.get())
						{
							copyFile(hashf.get(), Server->ConvertToUnicode(hash_dest));
						}

						if(!write_file_metadata(os_file_prefix(Server->ConvertToUnicode(hash_dest)),
							this, metadata))
						{
							ServerLogger::Log(clientid, "Error while writing metadata to \""+hash_dest+"\"", LL_ERROR);
						}
					}
				}
			}
		}
	}
}

void BackupServerHash::addFileSQL(int backupid, int clientid, int incremental, const std::wstring &fp, const std::wstring &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid, int64 next_entry)
{
	addFileSQL(*backupdao, *fileindex, backupid, clientid, incremental, fp, hash_path, shahash, filesize, rsize, prev_entry, prev_entry_clientid, next_entry);
}

void BackupServerHash::addFileSQL(ServerBackupDao& backupdao, FileIndex& fileindex, int backupid, int clientid, int incremental, const std::wstring &fp, const std::wstring &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int64 prev_entry, int64 prev_entry_clientid, int64 next_entry)
{
	bool new_for_client=false;

	if(prev_entry_clientid!=clientid || prev_entry==0)
	{
		new_for_client=true;

		//new file for this client
		prev_entry=0;

		std::wstring clients;

		if(prev_entry_clientid!=0)
		{
			//Other clients have this file

			std::map<int, int64> all_clients = fileindex.get_all_clients_with_cache(FileIndex::SIndexKey(shahash.c_str(), filesize));

			for(std::map<int, int64>::iterator it=all_clients.begin();it!=all_clients.end();++it)
			{
				if(it->first==clientid && it->second!=0)
				{
					prev_entry=it->second;
				}

				if(!clients.empty())
				{
					clients+=L",";
				}

				clients+=convert(it->first);
			}
		}
		
		assert(prev_entry==0);
		
		backupdao.addIncomingFile(filesize, clientid, backupid, clients, ServerBackupDao::c_direction_incoming, incremental);
	}

	int64 entryid = backupdao.addFileEntryExternal(backupid, fp, hash_path, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, new_for_client?1:0);

	if(new_for_client)
	{
		fileindex.put_delayed(FileIndex::SIndexKey(shahash.c_str(), filesize, clientid), entryid);
	}
}

void BackupServerHash::deleteFileSQL(ServerBackupDao& backupdao, FileIndex& fileindex, const char* pHash, _i64 filesize, _i64 rsize, int clientid, int backupid, int incremental, int64 id, int64 prev_id, int64 next_id, int pointed_to,
	bool use_transaction, bool del_entry)
{
	if(use_transaction)
	{
		backupdao.detachDbs();
		backupdao.beginTransaction();
	}

	if(prev_id==0 && next_id==0)
	{
		//client does not have this file anymore
		std::map<int, int64> all_clients = fileindex.get_all_clients_with_cache(FileIndex::SIndexKey(pHash, filesize));

		std::wstring clients;
		if(!all_clients.empty())
		{			
			for(std::map<int, int64>::iterator it=all_clients.begin();it!=all_clients.end();++it)
			{
				if(!clients.empty())
				{
					clients+=L",";
				}

				clients+=convert(it->first);
			}
		}
		else
		{
			Server->Log("File entry with id "+nconvert(id)+" with filesize "+nconvert(filesize)+" found in entry index while deleting, but should be there. The file entry index may be damaged.", LL_WARNING);
			clients+=convert(clientid);
		}
		

		backupdao.addIncomingFile((rsize>0 && rsize!=filesize)?rsize:filesize, clientid, backupid, clients, ServerBackupDao::c_direction_outgoing, incremental);

		if(pointed_to)
		{
			fileindex.del_delayed(FileIndex::SIndexKey(pHash, filesize, clientid));
		}
	}
	else if(pointed_to)
	{
		if(next_id!=0)
		{
			fileindex.put_delayed(FileIndex::SIndexKey(pHash, filesize, clientid), next_id);
		}
		else
		{
			fileindex.put_delayed(FileIndex::SIndexKey(pHash, filesize, clientid), prev_id);
		}
	}

	if(next_id!=0)
	{
		backupdao.setPrevEntry(prev_id, next_id);
	}

	if(prev_id!=0)
	{
		backupdao.setNextEntry(next_id, prev_id);
	}

	if(del_entry)
	{
		backupdao.delFileEntry(id);
	}

	if(use_transaction)
	{
		backupdao.endTransaction();
		backupdao.attachDbs();
	}
}

bool BackupServerHash::findFileAndLink(const std::wstring &tfn, IFile *tf, std::wstring& hash_fn, const std::string &sha2,
	bool diff_file, _i64 t_filesize, const std::string &hashoutput_fn, bool copy_from_hardlink_if_failed,
	bool &tries_once, std::wstring &ff_last, bool &hardlink_limit, bool &copied_file, int64& entryid, int64& entryclientid
	, int64& rsize, int64& next_entry, const FileMetadata& metadata, const FileMetadata& parent_metadata)
{
	hardlink_limit=false;
	copied_file=false;


	tries_once=false;	
	bool first_logmsg=true;
	bool copy=true;

	SFindState find_state;
	ServerBackupDao::SFindFileEntry existing_file = findFileHash(sha2, t_filesize, clientid, find_state);

	while(existing_file.exists)
	{
		ff_last=existing_file.fullpath;
		tries_once=true;
		bool too_many_hardlinks;
		bool b=os_create_hardlink(os_file_prefix(tfn), os_file_prefix(existing_file.fullpath), use_snapshots, &too_many_hardlinks);
		if(!b)
		{
			if(too_many_hardlinks)
			{
				ServerLogger::Log(clientid, L"HT: Hardlinking failed (Maximum hardlink count reached): \""+existing_file.fullpath+L"\"", LL_DEBUG);
				hardlink_limit = true;
				break;
			}
			else
			{
				hardlink_limit = false;

				IFile *ctf=Server->openFile(os_file_prefix(existing_file.fullpath), MODE_READ);
				if(ctf==NULL)
				{
					if(correctPath(existing_file.fullpath, existing_file.hashpath))
					{
						ServerLogger::Log(clientid, L"HT: Using new backupfolder for: \""+existing_file.fullpath+L"\"", LL_DEBUG);
						continue;
					}

					if(!first_logmsg)
					{
						ServerLogger::Log(clientid, L"HT: Hardlinking failed (File doesn't exist): \""+existing_file.fullpath+L"\"", LL_DEBUG);
					}
					first_logmsg=true;

					deleteFileSQL(*backupdao, *fileindex, sha2.c_str(), t_filesize, existing_file.rsize, clientid, existing_file.backupid, existing_file.incremental,
						existing_file.id, existing_file.prev_entry, existing_file.next_entry, existing_file.pointed_to, true, true);

					existing_file = findFileHash(sha2, t_filesize, clientid, find_state);
				}
				else
				{
					ServerLogger::Log(clientid, L"HT: Hardlinking failed (unkown error): \""+existing_file.fullpath+L"\"", LL_DEBUG);

					if(copy_from_hardlink_if_failed)
					{
						ServerLogger::Log(clientid, L"HT: Copying from file \""+existing_file.fullpath+L"\"", LL_DEBUG);

						if(!copyFile(ctf, tfn))
						{
							ServerLogger::Log(clientid, "Error copying file to destination -3", LL_ERROR);
							has_error=true;
						}
						else
						{
							copied_file=true;
						}

						if(!hash_fn.empty() && !existing_file.hashpath.empty())
						{
							IFile *ctf_hash=Server->openFile(os_file_prefix(existing_file.hashpath), MODE_READ);

							if(ctf_hash)
							{
								if(!copyFile(ctf_hash, hash_fn))
								{
									ServerLogger::Log(clientid, "Error copying hashfile to destination -3", LL_ERROR);
									has_error=true;
									hash_fn.clear();
								}
								Server->destroy(ctf_hash);
							}
							else
							{
								ServerLogger::Log(clientid, "Error opening hash source file", LL_ERROR);
							}
						}

						copy=false;
					}

					Server->destroy(ctf);
					break;
				}		
			}
		}
		else
		{
			entryid = existing_file.id;
			entryclientid = existing_file.clientid;
			next_entry=existing_file.next_entry;

			if(existing_file.rsize!=0 &&
				existing_file.rsize!=existing_file.filesize)
			if(parent_metadata.exist)
			{
				std::wstring parent_path = ExtractFilePath(tfn);
				if(!os_set_file_time(os_file_prefix(parent_path),
					parent_metadata.created, parent_metadata.last_modified))
				{
					ServerLogger::Log(clientid, L"HT: Error setting creation and last modified time of parent directory \""+parent_path+L"\"", LL_WARNING);
				}
			}

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

			bool do_write_metadata=false;
			if(!f_hashpath.empty() &&
				has_metadata(f_hashpath, metadata) )
			{
				b=os_create_hardlink(os_file_prefix(hash_fn), os_file_prefix(existing_file.hashpath), use_snapshots, NULL);
				if(!b)
				{
					IFile *ctf=Server->openFile(os_file_prefix(existing_file.hashpath), MODE_READ);
					if(ctf==NULL)
					{
						ServerLogger::Log(clientid, "HT: Hardlinking hash file failed (File doesn't exist)", LL_DEBUG);

						if(!hashoutput_fn.empty())
						{
							IFile *src=openFileRetry(Server->ConvertToUnicode(hashoutput_fn), MODE_READ);
							if(src!=NULL)
							{
								if(!copyFile(src, hash_fn))
								{
									ServerLogger::Log(clientid, "Error copying hashoutput to destination -1", LL_ERROR);
									has_error=true;
									hash_fn.clear();
								}
								Server->destroy(src);
							}
							else
							{
								ServerLogger::Log(clientid, "HT: Error opening hashoutput", LL_ERROR);
								has_error=true;
								hash_fn.clear();
							}
						}
						else
						{
							do_write_metadata=true;
						}
					}
					else
					{
						if(!copyFile(ctf, hash_fn))
						{
							ServerLogger::Log(clientid, "Error copying hashfile to destination -2", LL_ERROR);
							has_error=true;
							hash_fn.clear();
						}
						Server->destroy(ctf);
					}
				}
			}
			else
			{
				do_write_metadata=true;
			}

			if(do_write_metadata)
			{
				if(!write_file_metadata(hash_fn, this, metadata))
				{
					ServerLogger::Log(clientid, "Error writing file metadata -1", LL_ERROR);
					has_error=true;
				}
			}

			copy=false;
			break;
		}
	}

	return !copy;
}

void BackupServerHash::addFile(int backupid, int incremental, IFile *tf, const std::wstring &tfn,
	std::wstring hash_fn, const std::string &sha2, const std::string &orig_fn, const std::string &hashoutput_fn, int64 t_filesize,
	const FileMetadata& metadata, const FileMetadata& parent_metadata)
{
	bool copy=true;
	bool tries_once;
	std::wstring ff_last;
	bool hardlink_limit;
	bool copied_file;
	int64 entryid = 0;
	int64 entryclientid = 0;
	int64 next_entryid = 0;
	int64 rsize = 0;
	if(findFileAndLink(tfn, tf, hash_fn, sha2, diff_file, t_filesize,hashoutput_fn,
		false, tries_once, ff_last, hardlink_limit, copied_file, entryid, entryclientid, rsize, next_entryid,
		metadata, parent_metadata))
	{
		ServerLogger::Log(clientid, L"HT: Linked file: \""+tfn+L"\"", LL_DEBUG);
		copy=false;
		std::wstring temp_fn=tf->getFilenameW();
		Server->destroy(tf);
		tf=NULL;
		Server->deleteFile(temp_fn);
		addFileSQL(backupid, clientid, incremental, tfn, hash_fn, sha2, t_filesize, rsize, entryid, entryclientid, next_entryid);
	}

	if(tries_once && copy && !hardlink_limit)
	{
		if(link_logcnt<5)
		{
			ServerLogger::Log(clientid, L"HT: Error creating hardlink from \""+ff_last+L"\" to \""+tfn+L"\"", LL_WARNING);
		}
		else if(link_logcnt==5)
		{
			ServerLogger::Log(clientid, L"HT: More hardlink errors. Skipping... ", LL_WARNING);
		}
		else
		{
			Server->Log(L"HT: Error creating hardlink from \""+ff_last+L"\" to \""+tfn+L"\"", LL_WARNING);
		}
		++link_logcnt;
	}
	
	if(copy)
	{
		ServerLogger::Log(clientid, L"HT: Copying file: \""+tfn+L"\"", LL_DEBUG);
		int64 fs=tf->Size();
		int64 available_space=0;
		cow_filesize=0;
		if(fs>0)
		{
			available_space=os_free_space(os_file_prefix(ExtractFilePath(tfn)));
		}
		if(available_space==-1)
		{
			if(space_logcnt==0)
			{
				ServerLogger::Log(clientid, L"HT: Error getting free space for path \""+tfn+L"\"", LL_ERROR);
				++space_logcnt;
			}
			else
			{
				Server->Log(L"HT: Error getting free space for path \""+tfn+L"\"", LL_ERROR);
			}
			std::wstring temp_fn=tf->getFilenameW();
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
					ServerLogger::Log(clientid, "HT: No free space available deleting backups...", LL_WARNING);
				}
				else
				{
					Server->Log("HT: No free space available deleting backups...", LL_WARNING);
				}
				free_ok=freeSpace(fs, os_file_prefix(tfn));
			}

			if(!free_ok)
			{
				if(space_logcnt==0)
				{
					ServerLogger::Log(clientid, "HT: FATAL: Error freeing space", LL_ERROR);
					++space_logcnt;
				}
				else
				{
					Server->Log("HT: FATAL: Error freeing space", LL_ERROR);
				}
				has_error=true;
				std::wstring temp_fn=tf->getFilenameW();
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
						if(!hash_fn.empty())
						{
							if(use_tmpfiles)
							{
								r=copyFileWithHashoutput(tf, tfn, hash_fn, metadata);
							}
							else
							{
								r=renameFileWithHashoutput(tf, tfn, hash_fn, metadata);
								tf=NULL;
							}
						}
						else
						{
							if(use_tmpfiles)
							{
								r=copyFile(tf, tfn);
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
						if(!hash_fn.empty())
						{
							r=replaceFileWithHashoutput(tf, tfn, hash_fn, Server->ConvertToUnicode(orig_fn), metadata);
						}
						else
						{
							r=replaceFile(tf, tfn, Server->ConvertToUnicode(orig_fn));
						}
					}
				}
				else
				{
					r=patchFile(tf, Server->ConvertToUnicode(orig_fn), tfn, Server->ConvertToUnicode(hashoutput_fn), hash_fn, metadata);
				}
				
				if(!r)
				{
					has_error=true;
					ServerLogger::Log(clientid, "Storing file failed -1", LL_ERROR);
				}
				else
				{
					if(!os_set_file_time(os_file_prefix(tfn), metadata.created, metadata.last_modified))
					{
						ServerLogger::Log(clientid, L"Error setting created and last modified time on file \""+tfn+L"\"", LL_WARNING);
					}

					std::wstring parent_fn = ExtractFilePath(tfn);
					if(!os_set_file_time(os_file_prefix(parent_fn), parent_metadata.created, parent_metadata.last_modified))
					{
						ServerLogger::Log(clientid, L"Error setting created and last modified time on parent directory \""+parent_fn+L"\"", LL_WARNING);
					}
				}

				if(tf!=NULL)
				{
					std::wstring temp_fn=tf->getFilenameW();
					Server->destroy(tf);
					tf=NULL;
					Server->deleteFile(os_file_prefix(temp_fn));
				}

				if(r)
				{
					addFileSQL(backupid, clientid, incremental, tfn, hash_fn, sha2, t_filesize, cow_filesize>0?cow_filesize:t_filesize, 0, 0, 0);
				}
			}
		}
	}
}

bool BackupServerHash::freeSpace(int64 fs, const std::wstring &fp)
{
	IScopedLock lock(delete_mutex);

	int64 available_space=os_free_space(ExtractFilePath(fp));
	if(available_space==-1)
	{
		if(space_logcnt==0)
		{
			ServerLogger::Log(clientid, L"Error getting free space for path \""+fp+L"\"", LL_ERROR);
			++space_logcnt;
		}
		else
		{
			Server->Log(L"Error getting free space for path \""+fp+L"\"", LL_ERROR);
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

ServerBackupDao::SFindFileEntry BackupServerHash::findFileHash(const std::string &pHash, _i64 filesize, int clientid, SFindState& state)
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
		state.entryids = fileindex->get_all_clients_with_cache(FileIndex::SIndexKey(pHash.c_str(), filesize, 0));
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
		ServerBackupDao::SFindFileEntry ret;
		ret.exists=false;
		return ret;
	}

	state.prev = backupdao->getFileEntry(entryid);

	if(memcmp(state.prev.shahash.data(), pHash.data(), pHash.size())!=0)
	{
		Server->Log("Hash of file entry differs from file entry index result. Something may be wrong with the file entry index or this is a hash collision. Ignoring existing file and downloading anew.", LL_WARNING);
		Server->Log(L"While searching for file with size "+convert(filesize)+L" and clientid "+convert(clientid)+L". Resulting file path is \""+state.prev.fullpath+L"\"", LL_WARNING);
		ServerBackupDao::SFindFileEntry ret;
		ret.exists=false;
		return ret;
	}

	if(save_orig && state.prev.exists)
	{
		state.orig_prev=state.prev.prev_entry;
	}

	return state.prev;
}

IFile* BackupServerHash::openFileRetry(const std::wstring &dest, int mode)
{
	IFile *dst=NULL;
	int count_t=0;
	while(dst==NULL)
	{
		dst=Server->openFile(os_file_prefix(dest), mode);
		if(dst==NULL)
		{
			ServerLogger::Log(clientid, L"Error opening file... \""+dest+L"\" retrying...", LL_DEBUG);
			Server->wait(500);
			++count_t;
			if(count_t>=10)
			{
				ServerLogger::Log(clientid, L"Error opening file... \""+dest+L"\"", LL_ERROR);
				return NULL;
			}
		}
	}

	return dst;
}

bool BackupServerHash::copyFile(IFile *tf, const std::wstring &dest)
{
	IFile *dst=openFileRetry(dest, MODE_WRITE);
	if(dst==NULL) return false;

	tf->Seek(0);
	_u32 read;
	char buf[BUFFER_SIZE];
	do
	{
		read=tf->Read(buf, BUFFER_SIZE);
		bool b=BackupServerPrepareHash::writeRepeatFreeSpace(dst, buf, read, this);
		if(!b)
		{
			Server->Log(L"Error writing to file \""+dest+L"\" -2", LL_ERROR);
			Server->destroy(dst);
			return false;
		}
	}
	while(read>0);

	Server->destroy(dst);

	return true;
}

bool BackupServerHash::copyFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest,
	const FileMetadata& metadata)
{
	IFile *dst=openFileRetry(dest, MODE_WRITE);
	if(dst==NULL) return false;
	ObjectScope dst_s(dst);

	if(tf->Size()>0)
	{
		IFile *dst_hash=openFileRetry(hash_dest, MODE_WRITE);
		if(dst_hash==NULL)
		{
			return false;
		}
		ObjectScope dst_hash_s(dst_hash);

		std::string r=BackupServerPrepareHash::build_chunk_hashs(tf, dst_hash, this, false, dst, false);
		if(r=="")
			return false;

		if(!write_file_metadata(dst_hash, this, metadata))
		{
			return false;
		}
	}
	else
	{
		if(!write_file_metadata(os_file_prefix(hash_dest), this, metadata))
		{
			return false;
		}
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

bool BackupServerHash::createChunkHashes(IFile *tf, const std::wstring hash_fn)
{
	IFile *hashoutput=Server->openFile(os_file_prefix(hash_fn), MODE_WRITE);
	if(hashoutput==NULL) return false;

	bool b=BackupServerPrepareHash::build_chunk_hashs(tf, hashoutput, this, false, NULL, false)=="";

	Server->destroy(hashoutput);
	return !b;
}

bool BackupServerHash::handle_not_enough_space(const std::wstring &path)
{
	int64 available_space=os_free_space(ExtractFilePath(path));
	if(available_space==-1)
	{
		if(space_logcnt==0)
		{
			ServerLogger::Log(clientid, L"Error writing to file \""+path+L"\"", LL_ERROR);
			++space_logcnt;
		}
		else
		{
			Server->Log(L"Error writing to file \""+path+L"\"", LL_ERROR);
		}
		return false;
	}
	else
	{
		if(available_space<=freespace_mod)
		{
			if(space_logcnt==0)
			{
				ServerLogger::Log(clientid, "HT: No free space available deleting backups...", LL_WARNING);
			}
			else
			{
				Server->Log("HT: No free space available deleting backups...", LL_WARNING);
			}
			
			return freeSpace(0, path);
		}

		return true;
	}
}

void BackupServerHash::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed)
{
	if(!use_reflink || changed )
	{
		chunk_output_fn->Seek(chunk_patch_pos);
		bool b=BackupServerPrepareHash::writeRepeatFreeSpace(chunk_output_fn, buf, bsize, this);
		if(!b)
		{
			Server->Log(L"Error writing to file \""+chunk_output_fn->getFilenameW()+L"\" -3", LL_ERROR);
			has_error=true;
		}
	}
	chunk_patch_pos+=bsize;

	if(use_reflink)
	{
		cow_filesize+=bsize;
	}
}

bool BackupServerHash::patchFile(IFile *patch, const std::wstring &source, const std::wstring &dest, const std::wstring hash_output, const std::wstring hash_dest,
	const FileMetadata& metadata)
{
	_i64 dstfsize;
	{
		bool has_reflink=false;
		if( use_reflink )
		{
			if(! os_create_hardlink(os_file_prefix(dest), os_file_prefix(source), true, NULL) )
			{
				Server->Log(L"Reflinking file \""+dest+L"\" failed", LL_ERROR);
			}
			else
			{
				has_reflink=true;
			}
		}

		chunk_output_fn=openFileRetry(dest, has_reflink?MODE_RW:MODE_WRITE);
		if(chunk_output_fn==NULL) return false;
		ObjectScope dst_s(chunk_output_fn);

		IFile *f_source=openFileRetry(source, MODE_READ);
		if(f_source==NULL) return false;
		ObjectScope f_source_s(f_source);

		chunk_patch_pos=0;
		chunk_patcher.setRequireUnchanged(!has_reflink);
		bool b=chunk_patcher.ApplyPatch(f_source, patch);

		dstfsize=chunk_output_fn->Size();

		if(has_error || !b)
		{
			return false;
		}
	}

	if( dstfsize > chunk_patcher.getFilesize() )
	{
		os_file_truncate(dest, chunk_patcher.getFilesize());
	}

	IFile *f_hash_output=openFileRetry(hash_output, MODE_READ);
	if(f_hash_output==NULL)
	{
		Server->Log("Error opening hashoutput file -1", LL_ERROR);
		return false;
	}
	ObjectScope f_hash_output_s(f_hash_output);

	if(!copyFile(f_hash_output, hash_dest))
	{
		Server->Log("Error copying hashoutput file to destination", LL_ERROR);
		return false;
	}

	if(!write_file_metadata(os_file_prefix(hash_dest), this, metadata))
	{
		return false;
	}

	return true;
}

const size_t RP_COPY_BLOCKSIZE=1024;

bool BackupServerHash::replaceFile(IFile *tf, const std::wstring &dest, const std::wstring &orig_fn)
{
	if(! os_create_hardlink(os_file_prefix(dest), os_file_prefix(orig_fn), true, NULL) )
	{
		Server->Log(L"Reflinking file \""+dest+L"\" failed -2", LL_ERROR);

		return copyFile(tf, dest);
	}

	Server->Log(L"HT: Copying with reflink data from \""+orig_fn+L"\"", LL_DEBUG);

	IFile *dst=openFileRetry(dest, MODE_RW);
	if(dst==NULL) return false;

	tf->Seek(0);
	_u32 read1;
	_u32 read2;
	char buf1[RP_COPY_BLOCKSIZE];
	char buf2[RP_COPY_BLOCKSIZE];
	_i64 dst_pos=0;
	bool dst_eof=false;
	do
	{
		read1=tf->Read(buf1, RP_COPY_BLOCKSIZE);
		if(!dst_eof)
		{
			read2=dst->Read(buf2, RP_COPY_BLOCKSIZE);
		}
		else
		{
			read2=0;
		}

		if(read2<read1)
			dst_eof=true;

		if(read1!=read2 || memcmp(buf1, buf2, read1)!=0)
		{
			dst->Seek(dst_pos);
			cow_filesize+=read1;
			bool b=BackupServerPrepareHash::writeRepeatFreeSpace(dst, buf1, read1, this);
			if(!b)
			{
				Server->Log(L"Error writing to file \""+dest+L"\" -2", LL_ERROR);
				Server->destroy(dst);
				return false;
			}
		}

		dst_pos+=read1;
	}
	while(read1>0);

	_i64 dst_size=dst->Size();

	Server->destroy(dst);

	if( dst_size!=tf->Size() )
	{
		if( !os_file_truncate(os_file_prefix(dest), tf->Size()) )
		{
			Server->Log(L"Error truncating file \""+dest+L"\" -2", LL_ERROR);
			return false;
		}
	}

	return true;
}

bool BackupServerHash::replaceFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest, const std::wstring &orig_fn,
	const FileMetadata& metadata)
{
	if(! os_create_hardlink(os_file_prefix(dest), os_file_prefix(orig_fn), true, NULL) )
	{
		Server->Log(L"Reflinking file \""+dest+L"\" failed -3", LL_ERROR);

		return copyFileWithHashoutput(tf, dest, hash_dest, metadata);
	}

	Server->Log(L"HT: Copying with hashoutput with reflink data from \""+orig_fn+L"\"", LL_DEBUG);

	IFile *dst=openFileRetry(os_file_prefix(dest), MODE_RW);
	if(dst==NULL) return false;
	ObjectScope dst_s(dst);

	if(tf->Size()>0)
	{
		IFile *dst_hash=openFileRetry(os_file_prefix(hash_dest), MODE_WRITE);
		if(dst_hash==NULL)
		{
			return false;
		}
		ObjectScope dst_hash_s(dst_hash);

		std::string r=BackupServerPrepareHash::build_chunk_hashs(tf, dst_hash, this, false, dst, true, &cow_filesize);
		if(r=="")
			return false;

		if(!write_file_metadata(dst_hash, this, metadata))
		{
			return false;
		}

		_i64 dst_size=dst->Size();

		dst_s.clear();

		if( dst_size!=tf->Size() )
		{
			if( !os_file_truncate(os_file_prefix(dest), tf->Size()) )
			{
				Server->Log(L"Error truncating file \""+dest+L"\" -2", LL_ERROR);
				return false;
			}
		}
	}
	else
	{
		dst_s.clear();

		if(!write_file_metadata(os_file_prefix(hash_dest), this, metadata))
		{
			return false;
		}
		
		if( !os_file_truncate(dest, 0) )
		{
			Server->Log(L"Error truncating file \""+dest+L"\" -2", LL_ERROR);
			return false;
		}
	}
	
	return true;
}

bool BackupServerHash::renameFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest,
	const FileMetadata& metadata)
{
	if(tf->Size()>0)
	{
		IFile *dst_hash=openFileRetry(hash_dest, MODE_WRITE);
		if(dst_hash==NULL)
		{
			return false;
		}
		ObjectScope dst_hash_s(dst_hash);

		std::string r=BackupServerPrepareHash::build_chunk_hashs(tf, dst_hash, this, false, NULL, false);
		if(r=="")
			return false;

		if(!write_file_metadata(dst_hash, this, metadata))
		{
			return false;
		}
	}
	else
	{
		if(!write_file_metadata(os_file_prefix(hash_dest), this, metadata))
		{
			return false;
		}
	}

	std::wstring tf_fn=tf->getFilenameW();

	Server->destroy(tf);

	if(!use_reflink)
	{
		return os_rename_file(os_file_prefix(tf_fn), os_file_prefix(dest) );
	}
	else
	{
		if(! os_create_hardlink(os_file_prefix(dest), os_file_prefix(tf_fn), true, NULL) )
		{
			Server->Log(L"Reflinking file \""+dest+L"\" failed -3", LL_ERROR);

			return os_rename_file(os_file_prefix(tf_fn), os_file_prefix(dest));
		}
		
		Server->deleteFile(os_file_prefix(tf_fn));
		
		return true;
	}
}

bool BackupServerHash::renameFile(IFile *tf, const std::wstring &dest)
{
	std::wstring tf_fn=tf->getFilenameW();
	Server->destroy(tf);

	if(!use_reflink)
	{
		return os_rename_file(os_file_prefix(tf_fn), os_file_prefix(dest) );
	}
	else
	{
		if(! os_create_hardlink(os_file_prefix(dest), os_file_prefix(tf_fn), true, NULL) )
		{
			Server->Log(L"Reflinking file \""+dest+L"\" failed -4", LL_ERROR);

			return os_rename_file(os_file_prefix(tf_fn), os_file_prefix(dest) );
		}
		
		Server->deleteFile(os_file_prefix(tf_fn));
		
		return true;
	}
}

bool BackupServerHash::correctPath( std::wstring& ff, std::wstring& f_hashpath )
{
	if(!old_backupfolders_loaded)
	{
		old_backupfolders_loaded=true;
		db->AttachDBs();
		old_backupfolders=backupdao->getOldBackupfolders();
		db->DetachDBs();
	}

	if(backupfolder.empty())
	{
		db->AttachDBs();
		ServerSettings settings(db);
		backupfolder = settings.getSettings()->backupfolder;
		db->DetachDBs();
	}

	for(size_t i=0;i<old_backupfolders.size();++i)
	{
		size_t erase_size = old_backupfolders[i].size() + os_file_sep().size();
		if(ff.size()>erase_size &&
			next(ff, 0, old_backupfolders[i]))
		{
			std::wstring tmp_ff = backupfolder + os_file_sep() + ff.substr(erase_size);
						
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


#endif //CLIENT_ONLY
