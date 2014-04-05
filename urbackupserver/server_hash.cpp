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
#include "create_files_cache.h"
#include <algorithm>
#include <memory.h>

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
	tmp_count=0;
	space_logcnt=0;
	working=false;
	has_error=false;
	chunk_patcher.setCallback(this);
	filecache=NULL;

	if(use_reflink)
		Server->Log("Reflink copying is enabled", LL_DEBUG);
}

BackupServerHash::~BackupServerHash(void)
{
	if(pipe!=NULL)
	{
		Server->destroy(pipe);
	}

	delete filecache;
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

	prepareSQL();
	backupdao = new ServerBackupDao(db);
	copyFilesFromTmp();

	{
		ServerSettings server_settings(db, clientid);

		copy_limit=static_cast<int>(server_settings.getSettings()->file_hash_collect_amount);		

		if(server_settings.getSettings()->filescache_type=="lmdb")
		{
			filecache=create_lmdb_files_cache(); 
		}
		else if(server_settings.getSettings()->filescache_type=="sqlite")
		{
			filecache=create_sqlite_files_cache();
		}
	}
}

void BackupServerHash::deinitDatabase(void)
{
	copyFilesFromTmp();
	db->Write("DROP TABLE files_tmp");
	db->freeMemory();

	db->destroyQuery(q_find_file_hash);
	db->destroyQuery(q_del_file);
	db->destroyQuery(q_add_file);
	db->destroyQuery(q_delete_files_tmp);
	db->destroyQuery(q_del_file_tmp);
	db->destroyQuery(q_copy_files);
	db->destroyQuery(q_copy_files_to_new);
	db->destroyQuery(q_delete_all_files_tmp);
	db->destroyQuery(q_count_files_tmp);
	db->destroyQuery(q_move_del_file);

	delete filecache;
	filecache=NULL;

	delete backupdao;
	backupdao=NULL;
}

void BackupServerHash::operator()(void)
{
	setupDatabase();

	int big_cache_size=20;	
	size_t timeoutms;
	size_t file_hash_collect_cachesize;

	{
		ServerSettings server_settings(db, clientid);
		timeoutms=server_settings.getSettings()->file_hash_collect_timeout;
		file_hash_collect_cachesize=server_settings.getSettings()->file_hash_collect_cachesize;
	}

	db->DetachDBs();
	

	while(true)
	{
		working=false;
		std::string data;
		size_t rc=pipe->Read(&data, static_cast<int>(timeoutms) );
		if(rc==0)
		{
			tmp_count=0;
			int c=countFilesInTmp();
			if(c==-1) Server->Log("Counting files in tmp table failed", LL_ERROR);
			if(c>0)
			{
			        Server->Log("Copying files from tmp table...",LL_DEBUG);
					copyFilesFromTmp();
					Server->Log("done.", LL_DEBUG);
			}
			else
			{
				++big_cache_size;
				if(big_cache_size>10)
				{
					db->Write("PRAGMA cache_size = 1000");
					db->Write("PRAGMA shrink_memory");
				}
			}
			link_logcnt=0;
			tmp_count=0;
			space_logcnt=0;
			continue;
		}
		else if(big_cache_size>0)
		{
			if(big_cache_size>10)
			{
				Server->Log("Setting cachesize to "+nconvert(file_hash_collect_cachesize), LL_DEBUG);
				db->Write("PRAGMA cache_size = -"+nconvert(file_hash_collect_cachesize));
			}
			big_cache_size=0;			
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
			copyFilesFromTmp();
			big_cache_size=20;
			db->Write("PRAGMA cache_size = 1000");
			db->Write("PRAGMA shrink_memory");
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

				char incremental;
				rd.getChar(&incremental);

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

				bool diff_file=!hashoutput_fn.empty();

				std::string old_file_fn;
				rd.getStr(&old_file_fn);

				IFile *tf=Server->openFile(os_file_prefix(Server->ConvertToUnicode(temp_fn)), MODE_READ_SEQUENTIAL);

				if(tf==NULL)
				{
					ServerLogger::Log(clientid, "Error opening file \""+temp_fn+"\" from pipe for reading", LL_ERROR);
					has_error=true;
				}
				else
				{
					addFile(backupid, incremental, tf, Server->ConvertToUnicode(tfn), Server->ConvertToUnicode(hashpath), sha2,
						diff_file, old_file_fn, hashoutput_fn);
				}

				if(diff_file)
				{
					Server->deleteFile(hashoutput_fn);
				}
			}
			else if(action==EAction_Copy)
			{
				std::string source;
				rd.getStr(&source);
				
				std::string dest;
				rd.getStr(&dest);

				IFile *tf=Server->openFile(os_file_prefix(Server->ConvertToUnicode(source)), MODE_READ_SEQUENTIAL);

				if(tf==NULL)
				{
					ServerLogger::Log(clientid, "Error opening file \""+source+"\" from pipe for reading", LL_ERROR);
					has_error=true;
				}
				else
				{
					if(!copyFile(tf, Server->ConvertToUnicode(dest)))
					{
						ServerLogger::Log(clientid, "Error while copying file \""+source+"\" to \""+dest+"\"", LL_ERROR);
						has_error=true;
					}
				}

				Server->destroy(tf);
			}
		}

		copyFromTmpTable(false);
	}
}

void BackupServerHash::copyFromTmpTable(bool force)
{
	if(tmp_count>copy_limit || force)
	{
		tmp_count=0;
		Server->Log("Copying files from tmp table...",LL_DEBUG);
		copyFilesFromTmp();
		Server->Log("done.", LL_DEBUG);
	}
}

void BackupServerHash::prepareSQL(void)
{
	q_find_file_hash=db->Prepare("SELECT fullpath, hashpath, backupid FROM files WHERE shahash=? AND filesize=? ORDER BY created DESC LIMIT 1", false);
	q_delete_files_tmp=db->Prepare("DELETE FROM files_tmp WHERE backupid=?", false);
	q_add_file=db->Prepare("INSERT INTO files_tmp (backupid, fullpath, hashpath, shahash, filesize, rsize, clientid, incremental) VALUES (?, ?, ?, ?, ?, ?, ?, ?)", false);
	q_del_file=db->Prepare("DELETE FROM files WHERE shahash=? AND filesize=? AND fullpath=? AND backupid=?", false);
	q_del_file_tmp=db->Prepare("DELETE FROM files_tmp WHERE shahash=? AND filesize=? AND fullpath=? AND backupid=?", false);
	q_copy_files=db->Prepare("INSERT INTO files (backupid, fullpath, hashpath, shahash, filesize, created, rsize, did_count, clientid, incremental) SELECT backupid, fullpath, hashpath, shahash, filesize, created, rsize, 0 AS did_count, clientid, incremental FROM files_tmp", false);
	q_copy_files_to_new=db->Prepare("INSERT INTO files_new (backupid, fullpath, hashpath, shahash, filesize, created, rsize, clientid, incremental) SELECT backupid, fullpath, hashpath, shahash, filesize, created, rsize, clientid, incremental FROM files_tmp", false);
	q_delete_all_files_tmp=db->Prepare("DELETE FROM files_tmp", false);
	q_count_files_tmp=db->Prepare("SELECT count(*) AS c FROM files_tmp", false);
	q_move_del_file=db->Prepare("INSERT INTO files_del (backupid, fullpath, hashpath, shahash, filesize, created, rsize, clientid, incremental, is_del) SELECT backupid, fullpath, hashpath, shahash, filesize, created, rsize, clientid, incremental, 0 AS is_del FROM files WHERE shahash=? AND filesize=? AND fullpath=? AND backupid=?", false);
}

void BackupServerHash::addFileSQL(int backupid, char incremental, const std::wstring &fp, const std::wstring &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize)
{
	if(filecache==NULL)
	{
		addFileTmp(backupid, fp, hash_path, shahash, filesize);
	}
	else
	{
		filecache->put_delayed(FileCache::SCacheKey(shahash.c_str(), filesize), FileCache::SCacheValue(Server->ConvertToUTF8(fp), Server->ConvertToUTF8(hash_path)));
	}

	q_add_file->Bind(backupid);
	q_add_file->Bind(fp);
	q_add_file->Bind(hash_path);
	q_add_file->Bind(shahash.c_str(), (_u32)shahash.size());
	q_add_file->Bind(filesize);
	q_add_file->Bind(rsize);
	q_add_file->Bind(clientid);
	q_add_file->Bind(incremental);
	q_add_file->Write();
	q_add_file->Reset();
}

void BackupServerHash::addFileTmp(int backupid, const std::wstring &fp, const std::wstring &hash_path, const std::string &shahash, _i64 filesize)
{
	files_tmp[std::pair<std::string, _i64>(shahash, filesize)].push_back(STmpFile(backupid, fp, hash_path));
}

void BackupServerHash::deleteFileSQL(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid)
{
	db->BeginTransaction();
	q_move_del_file->Bind(pHash.c_str(), (_u32)pHash.size());
	q_move_del_file->Bind(filesize);
	q_move_del_file->Bind(fp);
	q_move_del_file->Bind(backupid);
	q_move_del_file->Write();
	q_move_del_file->Reset();
	q_del_file->Bind(pHash.c_str(), (_u32)pHash.size());
	q_del_file->Bind(filesize);
	q_del_file->Bind(fp);
	q_del_file->Bind(backupid);
	q_del_file->Write();
	q_del_file->Reset();
	db->EndTransaction();

	q_del_file_tmp->Bind(pHash.c_str(), (_u32)pHash.size());
	q_del_file_tmp->Bind(filesize);
	q_del_file_tmp->Bind(fp);
	q_del_file_tmp->Bind(backupid);
	q_del_file_tmp->Write();
	q_del_file_tmp->Reset();	

	if(filecache==NULL)
	{
		deleteFileTmp(pHash, fp, filesize, backupid);
	}
}

bool BackupServerHash::findFileAndLink(const std::wstring &tfn, IFile *tf, std::wstring& hash_fn, const std::string &sha2, bool diff_file, _i64 t_filesize, const std::string &hashoutput_fn, bool &tries_once, std::wstring &ff_last)
{
	int f_backupid;
	std::wstring ff;
	std::wstring f_hashpath;
	bool cache_hit=false;
	tries_once=false;
	if(t_filesize>0)
	{
		ff=findFileHash(sha2, t_filesize, f_backupid, f_hashpath, cache_hit);
	}
	ff_last=ff;

	bool cache_requires_update=false;
	bool first_logmsg=true;
	bool copy=true;

	while(!ff.empty())
	{
		tries_once=true;
		bool too_many_hardlinks;
		bool b=os_create_hardlink(os_file_prefix(tfn), os_file_prefix(ff), use_snapshots, &too_many_hardlinks);
		if(!b)
		{
			if(cache_hit && !cache_requires_update)
			{
				ServerLogger::Log(clientid, L"HT: Cache miss for: \""+ff+L"\"", LL_DEBUG);
				cache_requires_update=true;
			}

			if(too_many_hardlinks)
			{
				ServerLogger::Log(clientid, L"HT: Hardlinking failed (Maximum hardlink count reached): \""+ff+L"\"", LL_DEBUG);
				break;
			}
			else
			{
				IFile *ctf=Server->openFile(os_file_prefix(ff), MODE_READ);
				if(ctf==NULL)
				{
					if(!cache_hit && correctPath(ff, f_hashpath))
					{
						ServerLogger::Log(clientid, L"HT: Using new backupfolder for: \""+ff+L"\"", LL_DEBUG);
						continue;
					}

					if(!cache_hit || first_logmsg==false)
					{
						ServerLogger::Log(clientid, L"HT: Hardlinking failed (File doesn't exist): \""+ff+L"\"", LL_DEBUG);
					}
					first_logmsg=true;
					deleteFileSQL(sha2, ff, t_filesize, f_backupid);
					ff=findFileHash(sha2, t_filesize, f_backupid, f_hashpath, cache_hit);
					if(!ff.empty()) ff_last=ff;
				}
				else
				{
					Server->destroy(ctf);
					ServerLogger::Log(clientid, L"HT: Hardlinking failed (Maximum hardlink count reached?): \""+ff+L"\"", LL_DEBUG);
					break;
				}		
			}
		}
		else
		{
			if(!hash_fn.empty() && !f_hashpath.empty())
			{
				b=os_create_hardlink(os_file_prefix(hash_fn), os_file_prefix(f_hashpath), use_snapshots, NULL);
				if(!b)
				{
					IFile *ctf=Server->openFile(os_file_prefix(f_hashpath), MODE_READ);
					if(ctf==NULL)
					{
						ServerLogger::Log(clientid, "HT: Hardlinking hash file failed (File doesn't exist)", LL_DEBUG);
						if(!diff_file)
						{
							if(tf!=NULL && !createChunkHashes(tf, hash_fn))
								ServerLogger::Log(clientid, "HT: Creating chunk hash file failed", LL_ERROR);
						}
						else
						{
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
			copy=false;
			break;
		}
	}

	if(cache_requires_update)
	{
		if(!ff.empty())
		{
			filecache->put_delayed(FileCache::SCacheKey(sha2.c_str(), t_filesize), FileCache::SCacheValue(Server->ConvertToUTF8(ff), Server->ConvertToUTF8(f_hashpath)));
		}
		else
		{
			filecache->del_delayed(FileCache::SCacheKey(sha2.c_str(), t_filesize));
		}
	}

	return !copy;
}

void BackupServerHash::addFile(int backupid, char incremental, IFile *tf, const std::wstring &tfn,
	std::wstring hash_fn, const std::string &sha2, bool diff_file, const std::string &orig_fn, const std::string &hashoutput_fn)
{
	_i64 t_filesize=tf->Size();
	
	bool copy=true;
	bool tries_once;
	std::wstring ff_last;
	if(findFileAndLink(tfn, tf, hash_fn, sha2, diff_file, t_filesize,hashoutput_fn, tries_once, ff_last))
	{
		ServerLogger::Log(clientid, L"HT: Linked file: \""+tfn+L"\"", LL_DEBUG);
		copy=false;
		std::wstring temp_fn=tf->getFilenameW();
		Server->destroy(tf);
		tf=NULL;
		Server->deleteFile(temp_fn);
		addFileSQL(backupid, incremental, tfn, hash_fn, sha2, t_filesize, 0);
		++tmp_count;
	}

	if(tries_once && copy)
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
				if(!diff_file)
				{
					if(!use_reflink || orig_fn.empty())
					{
						if(!hash_fn.empty())
						{
							if(use_tmpfiles)
							{
								r=copyFileWithHashoutput(tf, tfn, hash_fn);
							}
							else
							{
								r=renameFileWithHashoutput(tf, tfn, hash_fn);
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
							r=replaceFileWithHashoutput(tf, tfn, hash_fn, Server->ConvertToUnicode(orig_fn));
						}
						else
						{
							r=replaceFile(tf, tfn, Server->ConvertToUnicode(orig_fn));
						}
					}
				}
				else
				{
					r=patchFile(tf, Server->ConvertToUnicode(orig_fn), tfn, Server->ConvertToUnicode(hashoutput_fn), hash_fn);
				}
				
				if(!r)
				{
					has_error=true;
					ServerLogger::Log(clientid, "Storing file failed -1", LL_ERROR);
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
					addFileSQL(backupid, incremental, tfn, hash_fn, sha2, t_filesize, t_filesize);
					++tmp_count;
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

std::wstring BackupServerHash::findFileHash(const std::string &pHash, _i64 filesize, int &backupid, std::wstring &hashpath, bool& cache_hit)
{
	if(filecache==NULL)
	{
		std::wstring ret=findFileHashTmp(pHash, filesize, backupid, hashpath);
		if(!ret.empty())
			return ret;
	}

	if(filecache!=NULL && !cache_hit)
	{
		FileCache::SCacheValue tval=filecache->get_with_cache(FileCache::SCacheKey(pHash.c_str(), filesize));
		if(tval.exists)
		{
			cache_hit=true;
			hashpath=Server->ConvertToUnicode(tval.hashpath);
			return Server->ConvertToUnicode(tval.fullpath);
		}
		else
		{
			return std::wstring();
		}
	}

	q_find_file_hash->Bind(pHash.c_str(), (_u32)pHash.size());
	q_find_file_hash->Bind(filesize);
	db_results res=q_find_file_hash->Read();
	q_find_file_hash->Reset();

	if(res.size()>0)
	{
		backupid=watoi(res[0][L"backupid"]);
		hashpath=res[0][L"hashpath"];
		return res[0][L"fullpath"];
	}
	else
	{
		backupid=-1;
		return L"";
	}
}

std::wstring BackupServerHash::findFileHashTmp(const std::string &pHash, _i64 filesize, int &backupid, std::wstring &hashpath)
{
	std::map<std::pair<std::string, _i64>, std::vector<STmpFile > >::iterator iter=files_tmp.find(std::pair<std::string, _i64>(pHash, filesize));

	if(iter!=files_tmp.end())
	{
		if(!iter->second.empty())
		{
			backupid=iter->second[iter->second.size()-1].backupid;
			hashpath=iter->second[iter->second.size()-1].hashpath;
			return iter->second[iter->second.size()-1].fp;
		}
	}

	return L"";
}

void BackupServerHash::deleteFileTmp(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid)
{
	std::map<std::pair<std::string, _i64>, std::vector<STmpFile > >::iterator iter=files_tmp.find(std::pair<std::string, _i64>(pHash, filesize));

	if(iter!=files_tmp.end())
	{
		for(size_t i=0;i<iter->second.size();++i)
		{
			if(iter->second[i].backupid==backupid && iter->second[i].fp==fp)
			{
				iter->second.erase(iter->second.begin()+i);
				--i;
			}
		}
	}
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

bool BackupServerHash::copyFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest)
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
	}
	
	return true;
}

void BackupServerHash::copyFilesFromTmp(void)
{
	if(filecache==NULL)
	{
		q_copy_files->Write();
		q_copy_files->Reset();
	}
	else
	{
		q_copy_files_to_new->Write();
		q_copy_files_to_new->Reset();
	}
	q_delete_all_files_tmp->Write();
	q_delete_all_files_tmp->Reset();

	files_tmp.clear();
}

int BackupServerHash::countFilesInTmp(void)
{
	db_results res=q_count_files_tmp->Read();
	q_count_files_tmp->Reset();
	if(res.empty())return -1;
	
	return watoi(res[0][L"c"]);
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
}

bool BackupServerHash::patchFile(IFile *patch, const std::wstring &source, const std::wstring &dest, const std::wstring hash_output, const std::wstring hash_dest)
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

bool BackupServerHash::replaceFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest, const std::wstring &orig_fn)
{
	if(! os_create_hardlink(os_file_prefix(dest), os_file_prefix(orig_fn), true, NULL) )
	{
		Server->Log(L"Reflinking file \""+dest+L"\" failed -3", LL_ERROR);

		return copyFileWithHashoutput(tf, dest, hash_dest);
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

		std::string r=BackupServerPrepareHash::build_chunk_hashs(tf, dst_hash, this, false, dst, true);
		if(r=="")
			return false;

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
		
		if( !os_file_truncate(dest, 0) )
		{
			Server->Log(L"Error truncating file \""+dest+L"\" -2", LL_ERROR);
			return false;
		}
	}
	
	return true;
}

bool BackupServerHash::renameFileWithHashoutput(IFile *tf, const std::wstring &dest, const std::wstring hash_dest)
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
