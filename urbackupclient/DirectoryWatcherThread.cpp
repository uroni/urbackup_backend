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

#include "DirectoryWatcherThread.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "database.h"
#include "client.h"

#define CHANGE_JOURNAL

IPipe *DirectoryWatcherThread::pipe=NULL;
IMutex *DirectoryWatcherThread::update_mutex=NULL;
ICondition *DirectoryWatcherThread::update_cond=NULL;

namespace
{
	const unsigned int max_change_ram_cache=10*60*1000;
}


DirectoryWatcherThread::DirectoryWatcherThread(const std::vector<std::wstring> &watchdirs)
{
	do_stop=false;
	watching=watchdirs;

	for(size_t i=0;i<watching.size();++i)
	{
		watching[i]=strlower(add_trailing_slash(watching[i]));
	}
}

void DirectoryWatcherThread::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	q_get_dir_backup=db->Prepare("SELECT id FROM mdirs_backup WHERE name=?");
	q_get_dir=db->Prepare("SELECT id FROM mdirs WHERE name=?");
	q_add_dir=db->Prepare("INSERT INTO mdirs (name) VALUES (?)");
	q_add_dir_with_id=db->Prepare("INSERT INTO mdirs (id, name) VALUES (?, ?)");
	q_add_del_dir=db->Prepare("INSERT INTO del_dirs SELECT ? AS NAME WHERE NOT EXISTS (SELECT * FROM del_dirs WHERE name=?)");
	q_add_file=db->Prepare("INSERT INTO mfiles SELECT ? AS dir_id, ? AS name WHERE NOT EXISTS (SELECT * FROM mfiles WHERE dir_id=? AND name=?)");
	q_update_last_backup_time=db->Prepare("INSERT OR REPLACE INTO misc (tkey, tvalue) VALUES ('last_backup_filetime', ?)");

	ChangeJournalWatcher dcw(this, db);

	{
		db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='last_backup_filetime'");
		if(!res.empty())
		{
			dcw.set_last_backup_time(watoi64(res[0][L"tvalue"]));
		}
	}
	
	
	for(size_t i=0;i<watching.size();++i)
	{
		dcw.watchDir(watching[i]);
	}

	while(do_stop==false)
	{
		std::string r;
		pipe->Read(&r, 10000);
		std::wstring msg;
		if(r.size()/sizeof(wchar_t)>0)
		{
			msg.resize( r.size()/sizeof(wchar_t));
			memcpy(&msg[0], &r[0], r.size());
		}

#ifdef CHANGE_JOURNAL
		if(r.empty())
		{
			dcw.update();
		}
#endif

		if(msg.size()>0 )
		{
			if( msg[0]=='A' )
			{
				std::wstring dir=strlower(add_trailing_slash(msg.substr(1)));
				bool w=false;
				for(size_t i=0;i<watching.size();++i)
				{
					if(watching[i]==dir)
					{
						w=true;
						break;
					}
				}
				if(w==false && !dir.empty())
				{
					dcw.watchDir(dir);
					watching.push_back(dir);
					On_ResetAll(dir);
				}
			}
			else if( msg[0]=='D' )
			{
				std::wstring dir=strlower(add_trailing_slash(msg.substr(1)));
				for(size_t i=0;i<watching.size();++i)
				{
					if(watching[i]==dir)
					{
						watching.erase(watching.begin()+i);
						break;
					}
				}
			}
			else if( msg[0]=='U' )
			{
				lastentries.clear();
				dcw.update_longliving();
				dcw.update();

				IScopedLock lock(update_mutex);
				update_cond->notify_all();
			}
			else if( msg[0]=='Q' )
			{
				continue;
			}
			else if( msg[0]=='t' )
			{
				last_backup_filetime=get_current_filetime();				

				IScopedLock lock(update_mutex);
				update_cond->notify_all();
			}
			else if( msg[0]=='T' )
			{
				dcw.set_last_backup_time(last_backup_filetime);

				q_update_last_backup_time->Bind(last_backup_filetime);
				q_update_last_backup_time->Write();
				q_update_last_backup_time->Reset();

				IScopedLock lock(update_mutex);
				update_cond->notify_all();
			}
			else if(msg[0]=='K' )
			{
				dcw.set_freeze_open_write_files(true);

				IScopedLock lock(update_mutex);
				update_cond->notify_all();
			}
			else if(msg[0]=='H' )
			{
				dcw.set_freeze_open_write_files(false);

				IScopedLock lock(update_mutex);
				update_cond->notify_all();
			}
		}
	}

	db->destroyAllQueries();
}

void DirectoryWatcherThread::update(void)
{
	std::wstring msg=L"U";
	pipe->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
}

void DirectoryWatcherThread::init_mutex(void)
{
	pipe=Server->createMemoryPipe();
	update_mutex=Server->createMutex();
	update_cond=Server->createCondition();
}

void DirectoryWatcherThread::update_and_wait(void)
{
	IScopedLock lock(update_mutex);
	std::wstring msg=L"U";
	pipe->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::freeze(void)
{
	IScopedLock lock(update_mutex);
	std::wstring msg=L"K";
	pipe->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::unfreeze(void)
{
	IScopedLock lock(update_mutex);
	std::wstring msg=L"H";
	pipe->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::update_last_backup_time(void)
{
	IScopedLock lock(update_mutex);
	std::wstring msg=L"t";
	pipe->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::commit_last_backup_time(void)
{
	IScopedLock lock(update_mutex);
	std::wstring msg=L"T";
	pipe->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::OnDirMod(const std::wstring &dir, const std::wstring &fn)
{
	bool found=false;
	int64 currtime=Server->getTimeMS();

	for(std::list<SLastEntries>::iterator it=lastentries.begin();it!=lastentries.end();)
	{
		if(currtime-(*it).time>max_change_ram_cache)
		{
			lastentries.erase(it++);
			continue;
		}
		else if( (*it).dir==dir && (*it).fn==fn)
		{
			(*it).time=currtime;
			found=true;
			break;
		}
		it++;
	}

	if(found==false)
	{
		q_get_dir->Bind(dir);
		db_results res=q_get_dir->Read();
		q_get_dir->Reset();

		if(res.empty())
		{
			q_get_dir_backup->Bind(dir);
			db_results res=q_get_dir_backup->Read();
			q_get_dir_backup->Reset();
		
			_i64 dir_id;
			if(res.empty())
			{
				q_add_dir->Bind(dir);
				q_add_dir->Write();
				q_add_dir->Reset();
				dir_id=db->getLastInsertID();
			}
			else
			{
				dir_id=watoi64(res[0][L"id"]);
				q_add_dir_with_id->Bind(dir_id);
				q_add_dir_with_id->Bind(dir);
				q_add_dir_with_id->Write();
				q_add_dir_with_id->Reset();
			}

			if(!fn.empty())
			{
				q_add_file->Bind(dir_id);
				q_add_file->Bind(fn);
				q_add_file->Bind(dir_id);
				q_add_file->Bind(fn);
				q_add_file->Write();
				q_add_file->Reset();
			}
		}
		else if(!fn.empty())
		{
			_i64 dir_id=watoi64(res[0][L"id"]);
			q_add_file->Bind(dir_id);
			q_add_file->Bind(fn);
			q_add_file->Bind(dir_id);
			q_add_file->Bind(fn);
			q_add_file->Write();
			q_add_file->Reset();
		}			

		SLastEntries e;
		e.dir=dir;
		e.fn=fn;
		e.time=currtime;
		lastentries.push_back(e);
	}
}

void DirectoryWatcherThread::OnDirRm(const std::wstring &dir)
{
	q_add_del_dir->Bind(dir);
	q_add_del_dir->Bind(dir);
	q_add_del_dir->Write();
	q_add_del_dir->Reset();
}

void DirectoryWatcherThread::stop(void)
{
	do_stop=true;
	std::wstring msg=L"Q";
	pipe->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
}

IPipe *DirectoryWatcherThread::getPipe(void)
{
	return pipe;
}

void DirectoryWatcherThread::On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool save_fn, bool closed)
{
	On_FileModified(strOldFileName, false, closed);
	On_FileModified(strNewFileName, save_fn, closed);
}

void DirectoryWatcherThread::On_DirNameChanged( const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool closed )
{
	On_FileNameChanged(strOldFileName, strNewFileName, false, closed);
}

void DirectoryWatcherThread::On_FileRemoved(const std::wstring & strFileName, bool closed)
{
	On_FileModified(strFileName, false, closed);
}

void DirectoryWatcherThread::On_FileAdded(const std::wstring & strFileName, bool closed)
{
	On_FileModified(strFileName, false, closed);
}

void DirectoryWatcherThread::On_DirAdded( const std::wstring & strFileName, bool closed )
{
	On_FileModified(strFileName, false, closed);
}

void DirectoryWatcherThread::On_FileModified(const std::wstring & strFileName, bool save_fn, bool closed)
{
	bool ok=false;
	std::wstring dir=strlower(ExtractFilePath(strFileName))+os_file_sep();
	std::wstring fn;
	if(save_fn)
	{
		fn=ExtractFileName(strFileName);
	}
	for(size_t i=0;i<watching.size();++i)
	{
		if(dir.find(watching[i])==0)
		{
			OnDirMod(dir, fn);
			return;
		}
	}	
}

void DirectoryWatcherThread::On_DirRemoved(const std::wstring & strDirName, bool closed)
{
	std::wstring rmDir=strlower(add_trailing_slash(strDirName));
	bool ok=false;
	for(size_t i=0;i<watching.size();++i)
	{
		if(rmDir.find(watching[i])==0)
		{
			OnDirRm(rmDir);
			return;
		}
	}

	On_FileModified(strDirName, false, closed);
}

bool DirectoryWatcherThread::is_stopped(void)
{
	return do_stop;
}

void DirectoryWatcherThread::On_ResetAll(const std::wstring & vol)
{
	OnDirMod(L"##-GAP-##"+strlower(vol), L"");
}

_i64 DirectoryWatcherThread::get_current_filetime()
{
	FILETIME ft;
	SYSTEMTIME st;
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	return static_cast<__int64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

void DirectoryWatcherThread::Commit(const std::vector<IChangeJournalListener::SSequence>& sequences)
{

}


