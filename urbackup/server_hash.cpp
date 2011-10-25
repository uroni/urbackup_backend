/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
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
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../Interface/Mutex.h"
#include "fileclient/data.h"
#include "database.h"
#include "sha2/sha2.h"
#include "../stringtools.h"
#include "server_log.h"
#include "server_cleanup.h"
#include <algorithm>
#include <memory.h>

const size_t freespace_mod=50*1024*1024; //50 MB
const size_t c_backup_count=1000;
const int c_copy_limit=1000;

IMutex * delete_mutex=NULL;

void init_mutex1(void)
{
	delete_mutex=Server->createMutex();
}

BackupServerHash::BackupServerHash(IPipe *pPipe, IPipe *pExitpipe, int pClientid)
{
	pipe=pPipe;
	clientid=pClientid;
	exitpipe=pExitpipe;
	link_logcnt=0;
	tmp_count=0;
	space_logcnt=0;
	working=false;
	has_error=false;
}

BackupServerHash::~BackupServerHash(void)
{
	db->destroyQuery(q_find_file_hash);
	db->destroyQuery(q_del_file);
	db->destroyQuery(q_add_file);
	db->destroyQuery(q_delete_files_tmp);
	db->destroyQuery(q_del_file_tmp);
	db->destroyQuery(q_copy_files);
	db->destroyQuery(q_delete_all_files_tmp);
	db->destroyQuery(q_count_files_tmp);
	db->destroyQuery(q_move_del_file);

	Server->destroy(pipe);
}

void BackupServerHash::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	{
	    bool r;
	    do
	    {
		r=db->Write("CREATE TEMPORARY TABLE files_tmp ( backupid INTEGER, fullpath TEXT, shahash BLOB, filesize INTEGER, created DATE DEFAULT CURRENT_TIMESTAMP, rsize INTEGER);");
		if(!r)
		    Server->wait(1000);
		    
	    }while(!r);
	}
	int big_cache_size=0;
	prepareSQL();
	copyFilesFromTmp();

	while(true)
	{
		working=false;
		std::string data;
		size_t rc=pipe->Read(&data, 10000);
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
				db->Write("PRAGMA cache_size = 10000");
			}
			big_cache_size=0;			
		}
		
		working=true;

		if(data=="exitnow")
		{
			copyFilesFromTmp();
			exitpipe->Write("ok");
			db->Write("DROP TABLE files_tmp");
			Server->Log("server_hash Thread finished - exitnow ");
			delete this;
			return;
		}
		else if(data=="exit")
		{
			copyFilesFromTmp();
			db->Write("DROP TABLE files_tmp");
			Server->destroy(exitpipe);
			Server->Log("server_hash Thread finished - normal");
			delete this;
			return;
		}
		else if(data=="flush")
		{
			copyFilesFromTmp();
			big_cache_size=20;
			db->Write("PRAGMA cache_size = 1000");
			continue;
		}

		if(rc>0)
		{
			CRData rd(&data);

			std::string temp_fn;
			rd.getStr(&temp_fn);

			unsigned int backupid;
			rd.getUInt(&backupid);

			std::string tfn;
			rd.getStr(&tfn);

			std::string sha2;
			if(!rd.getStr(&sha2))
				ServerLogger::Log(clientid, "Reading hash from pipe failed", LL_ERROR);

			IFile *tf=Server->openFile(os_file_prefix()+Server->ConvertToUnicode(temp_fn), MODE_READ);

			if(tf==NULL)
			{
				ServerLogger::Log(clientid, "Error opening file \""+temp_fn+"\" from pipe for reading", LL_ERROR);
			}
			else
			{
				addFile(backupid, tf, Server->ConvertToUnicode(tfn), sha2);
			}
		}

		if(tmp_count>c_copy_limit)
		{
			tmp_count=0;
			Server->Log("Copying files from tmp table...",LL_DEBUG);
			copyFilesFromTmp();
			Server->Log("done.", LL_DEBUG);
		}
	}
}

void BackupServerHash::prepareSQL(void)
{
	q_find_file_hash=db->Prepare("SELECT fullpath, backupid FROM files WHERE shahash=? AND filesize=? ORDER BY created DESC LIMIT 1", false);
	q_delete_files_tmp=db->Prepare("DELETE FROM files_tmp WHERE backupid=?", false);
	q_add_file=db->Prepare("INSERT INTO files_tmp (backupid, fullpath, shahash, filesize, rsize) VALUES (?, ?, ?, ?, ?)", false);
	q_del_file=db->Prepare("DELETE FROM files WHERE shahash=? AND fullpath=? AND filesize=? AND backupid=?", false);
	q_del_file_tmp=db->Prepare("DELETE FROM files_tmp WHERE shahash=? AND fullpath=? AND filesize=? AND backupid=?", false);
	q_copy_files=db->Prepare("INSERT INTO files (backupid, fullpath, shahash, filesize, created, rsize, did_count) SELECT backupid, fullpath, shahash, filesize, created, rsize, 0 AS did_count FROM files_tmp", false);
	q_delete_all_files_tmp=db->Prepare("DELETE FROM files_tmp", false);
	q_count_files_tmp=db->Prepare("SELECT count(*) AS c FROM files_tmp", false);
	q_move_del_file=db->Prepare("INSERT INTO files_del (backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del) SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 0 AS is_del FROM (files INNER JOIN backups ON files.backupid=backups.id) WHERE shahash=? AND fullpath=? AND filesize=? AND backupid=?", false);
}

void BackupServerHash::addFileSQL(int backupid, const std::wstring &fp, const std::string &shahash, _i64 filesize, _i64 rsize)
{
	addFileTmp(backupid, fp, shahash, filesize);

	q_add_file->Bind(backupid);
	q_add_file->Bind(fp);
	q_add_file->Bind(shahash.c_str(), (_u32)shahash.size());
	q_add_file->Bind(filesize);
	q_add_file->Bind(rsize);
	q_add_file->Write();
	q_add_file->Reset();
}

void BackupServerHash::addFileTmp(int backupid, const std::wstring &fp, const std::string &shahash, _i64 filesize)
{
	std::vector<std::pair<int, std::wstring> > tmp;
	tmp.push_back(std::pair<int, std::wstring>(backupid, fp));
	std::pair<std::pair<std::string, _i64>, std::vector<std::pair<int, std::wstring> > > nv(
		std::pair<std::string, _i64>(shahash, filesize),
		tmp );
	files_tmp.insert(nv);
}

void BackupServerHash::deleteFileSQL(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid)
{
	db->BeginTransaction();
	q_move_del_file->Bind(pHash.c_str(), (_u32)pHash.size());
	q_move_del_file->Bind(fp);
	q_move_del_file->Bind(filesize);
	q_move_del_file->Bind(backupid);
	q_move_del_file->Write();
	q_move_del_file->Reset();
	q_del_file->Bind(pHash.c_str(), (_u32)pHash.size());
	q_del_file->Bind(fp);
	q_del_file->Bind(filesize);
	q_del_file->Bind(backupid);
	q_del_file->Write();
	q_del_file->Reset();
	db->EndTransaction();

	q_del_file_tmp->Bind(pHash.c_str(), (_u32)pHash.size());
	q_del_file_tmp->Bind(fp);
	q_del_file_tmp->Bind(filesize);
	q_del_file_tmp->Bind(backupid);
	q_del_file_tmp->Write();
	q_del_file_tmp->Reset();	

	deleteFileTmp(pHash, fp, filesize, backupid);
}

void BackupServerHash::addFile(unsigned int backupid, IFile *tf, const std::wstring &tfn, const std::string &sha2)
{
	_i64 t_filesize=tf->Size();
	int f_backupid;
	std::wstring ff;
	if(t_filesize>0)
	{
		ff=findFileHash(sha2, t_filesize, f_backupid);
	}
	std::wstring ff_last=ff;
	bool copy=true;

	bool tries_once=false;

	while(!ff.empty())
	{
		tries_once=true;
		bool b=os_create_hardlink(os_file_prefix()+tfn, os_file_prefix()+ff);
		if(!b)
		{
			IFile *tf=Server->openFile(os_file_prefix()+ff, MODE_READ);
			if(tf==NULL)
				ServerLogger::Log(clientid, "HT: Hardlinking failed (File doesn't exist)", LL_DEBUG);
			else
			{
				Server->destroy(tf);
				ServerLogger::Log(clientid, "HT: Hardlinking failed (Maximum hardlink count reached?)", LL_DEBUG);
			}
			deleteFileSQL(sha2, ff, t_filesize, f_backupid);
			ff=findFileHash(sha2, t_filesize, f_backupid);
			if(!ff.empty()) ff_last=ff;
		}
		else
		{
			ServerLogger::Log(clientid, L"HT: Linked file: \""+tfn+L"\"", LL_DEBUG);
			copy=false;
			std::wstring temp_fn=tf->getFilenameW();
			Server->destroy(tf);
			Server->deleteFile(temp_fn);
			addFileSQL(backupid, tfn, sha2, t_filesize, 0);
			++tmp_count;
			break;
		}
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
			available_space=os_free_space(os_file_prefix()+ExtractFilePath(tfn));
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
				free_ok=freeSpace(fs, tfn);
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
				std::wstring temp_fn=tf->getFilenameW();
				Server->destroy(tf);
				Server->deleteFile(temp_fn);
			}
			else
			{
				bool r=copyFile(tf, tfn);
				if(!r)
				{
					has_error=true;
				}
				std::wstring temp_fn=tf->getFilenameW();
				Server->destroy(tf);
				Server->deleteFile(temp_fn);

				if(r)
				{
					addFileSQL(backupid, tfn, sha2, t_filesize, t_filesize);
					++tmp_count;
				}
			}
		}
	}
}

bool BackupServerHash::freeSpace(int64 fs, const std::wstring &fp)
{
	IScopedLock lock(delete_mutex);

	int64 available_space=os_free_space(os_file_prefix()+ExtractFilePath(fp));
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

	ServerCleanupThread cleanup;
	return cleanup.do_cleanup(freespace_mod+fs);
}

std::wstring BackupServerHash::findFileHash(const std::string &pHash, _i64 filesize, int &backupid)
{
	std::wstring ret=findFileHashTmp(pHash, filesize, backupid);
	if(!ret.empty())
		return ret;

	q_find_file_hash->Bind(pHash.c_str(), (_u32)pHash.size());
	q_find_file_hash->Bind(filesize);
	db_results res=q_find_file_hash->Read();
	q_find_file_hash->Reset();

	if(res.size()>0)
	{
		backupid=watoi(res[0][L"backupid"]);
		return res[0][L"fullpath"];
	}
	else
	{
		backupid=-1;
		return L"";
	}
}

std::wstring BackupServerHash::findFileHashTmp(const std::string &pHash, _i64 filesize, int &backupid)
{
	std::map<std::pair<std::string, _i64>, std::vector<std::pair<int, std::wstring> > >::iterator iter=files_tmp.find(std::pair<std::string, _i64>(pHash, filesize));

	if(iter!=files_tmp.end())
	{
		if(!iter->second.empty())
		{
			backupid=iter->second[iter->second.size()-1].first;
			return iter->second[iter->second.size()-1].second;
		}
	}

	return L"";
}

void BackupServerHash::deleteFileTmp(const std::string &pHash, const std::wstring &fp, _i64 filesize, int backupid)
{
	std::map<std::pair<std::string, _i64>, std::vector<std::pair<int, std::wstring> > >::iterator iter=files_tmp.find(std::pair<std::string, _i64>(pHash, filesize));

	if(iter!=files_tmp.end())
	{
		for(size_t i=0;i<iter->second.size();++i)
		{
			if(iter->second[i].first==backupid && iter->second[i].second==fp)
			{
				iter->second.erase(iter->second.begin()+i);
				--i;
			}
		}
	}
}

bool BackupServerHash::copyFile(IFile *tf, const std::wstring &dest)
{
	IFile *dst=NULL;
	int count_t=0;
	while(dst==NULL)
	{
		dst=Server->openFile(os_file_prefix()+dest, MODE_WRITE);
		if(dst==NULL)
		{
			ServerLogger::Log(clientid, L"Error opening destination file... \""+dest+L"\" retrying...", LL_DEBUG);
			Server->wait(500);
			++count_t;
			if(count_t>=10)
			{
				ServerLogger::Log(clientid, L"Error opening destination file... \""+dest+L"\"", LL_ERROR);
				return false;
			}
		}
	}
	tf->Seek(0);
	_u32 read;
	char buf[4096];
	do
	{
		read=tf->Read(buf, 4096);
		_u32 rc=dst->Write(buf, read);
		if(rc!=read && read>0)
		{
			int64 available_space=os_free_space(os_file_prefix()+ExtractFilePath(dest));
			if(available_space==-1)
			{
				if(space_logcnt==0)
				{
					ServerLogger::Log(clientid, L"Error writing to file \""+dest+L"\"", LL_ERROR);
					++space_logcnt;
				}
				else
				{
					Server->Log(L"Error writing to file \""+dest+L"\"", LL_ERROR);
				}
				Server->destroy(dst);
				return false;
			}
			else
			{
				bool free_ok=true;

				if( available_space>=freespace_mod )
				{
					if(space_logcnt==0)
					{
						ServerLogger::Log(clientid, "HT: No free space available deleting backups...", LL_WARNING);
					}
					else
					{
						Server->Log("HT: No free space available deleting backups...", LL_WARNING);
					}
					free_ok=freeSpace(0, dest);
				}

				if(!free_ok)
				{
					if(space_logcnt==0)
					{
						ServerLogger::Log(clientid, L"Error writing to file \""+dest+L"\"", LL_ERROR);
						++space_logcnt;
					}
					else
					{
						Server->Log(L"Error writing to file \""+dest+L"\"", LL_ERROR);
					}
					Server->destroy(dst);
					return false;
				}

				_u32 written=rc;
				do
				{
					rc=dst->Write(buf+written, read-written);
					written+=rc;
				}
				while(written<read && rc>0);
				
				if(rc==0)
				{
					Server->Log(L"Error writing to file \""+dest+L"\" -2", LL_ERROR);
					Server->destroy(dst);
					return false;
				}
			}
		}
	}
	while(read>0);

	Server->destroy(dst);

	return true;
}

void BackupServerHash::copyFilesFromTmp(void)
{
	q_copy_files->Write();
	q_copy_files->Reset();
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

#endif //CLIENT_ONLY