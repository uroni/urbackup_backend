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
std::vector<std::string> DirectoryWatcherThread::open_files;


namespace
{
	const unsigned int max_change_ram_cache=10*60*1000;
}


DirectoryWatcherThread::DirectoryWatcherThread(const std::vector<std::string> &watchdirs,
	const std::vector<ContinuousWatchEnqueue::SWatchItem> &watchdirs_continuous)
{
	do_stop=false;
	watching=watchdirs;

	for(size_t i=0;i<watching.size();++i)
	{
		watching[i]=strlower(add_trailing_slash(watching[i]));
	}

	if(!watchdirs_continuous.empty())
	{
		continuous_watch.reset(new ContinuousWatchEnqueue);

		for(size_t i=0;i<watchdirs_continuous.size();++i)
		{
			continuous_watch->addWatchdir(watchdirs_continuous[i]);
		}
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
	q_update_last_backup_time=db->Prepare("INSERT OR REPLACE INTO misc (tkey, tvalue) VALUES ('last_backup_filetime', ?)");

	ChangeJournalWatcher dcw(this, db);

	dcw.add_listener(this);

	{
		db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='last_backup_filetime'");
		if(!res.empty())
		{
			dcw.set_last_backup_time(watoi64(res[0]["tvalue"]));
		}
	}
	
	
	for(size_t i=0;i<watching.size();++i)
	{
		dcw.watchDir(watching[i]);
	}

	if(continuous_watch.get())
	{
		dcw.add_listener(continuous_watch.get());
	}

	while(do_stop==false)
	{
		std::string msg;
		pipe->Read(&msg, 10000);

#ifdef CHANGE_JOURNAL
		if(msg.empty())
		{
			dcw.update();
		}
#endif

		if(msg.size()>0 )
		{
			if( msg[0]=='A' )
			{
				std::string dir=strlower(add_trailing_slash(msg.substr(1)));
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
				std::string dir=strlower(add_trailing_slash(msg.substr(1)));
				for(size_t i=0;i<watching.size();++i)
				{
					if(watching[i]==dir)
					{
						watching.erase(watching.begin()+i);
						break;
					}
				}
			}
			else if( msg[0]=='C')
			{
				std::string dir=strlower(add_trailing_slash(getuntil("|", msg.substr(1))));
				std::string name=getafter("|", msg.substr(1));

				if(continuous_watch.get()==NULL)
				{
					continuous_watch.reset(new ContinuousWatchEnqueue);
					dcw.add_listener(continuous_watch.get());
				}

				continuous_watch->addWatchdir(ContinuousWatchEnqueue::SWatchItem(dir, name));
			}
			else if( msg[0]=='X')
			{
				std::string dir=strlower(add_trailing_slash(getuntil("|", msg.substr(1))));
				std::string name=getafter("|", msg.substr(1));

				continuous_watch->removeWatchdir(ContinuousWatchEnqueue::SWatchItem(dir, name));
			}
			else if( msg[0]=='U' )
			{
				lastentries.clear();
				dcw.update();

				IScopedLock lock(update_mutex);
				open_files.clear();
				dcw.update_longliving();
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
	pipe->Write("U");
}

void DirectoryWatcherThread::init_mutex(void)
{
	pipe=Server->createMemoryPipe();
	update_mutex=Server->createMutex();
	update_cond=Server->createCondition();
}

void DirectoryWatcherThread::update_and_wait(std::vector<std::string>& r_open_files)
{
	IScopedLock lock(update_mutex);
	pipe->Write("U");
	update_cond->wait(&lock);

	r_open_files.insert(r_open_files.end(), open_files.begin(), open_files.end());
}

void DirectoryWatcherThread::freeze(void)
{
	IScopedLock lock(update_mutex);
	pipe->Write("K");
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::unfreeze(void)
{
	IScopedLock lock(update_mutex);
	pipe->Write("H");
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::update_last_backup_time(void)
{
	IScopedLock lock(update_mutex);
	pipe->Write("t");
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::commit_last_backup_time(void)
{
	IScopedLock lock(update_mutex);
	pipe->Write("T");
	update_cond->wait(&lock);
}

void DirectoryWatcherThread::OnDirMod(const std::string &dir)
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
		else if( (*it).dir==dir )
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
				dir_id=watoi64(res[0]["id"]);
				q_add_dir_with_id->Bind(dir_id);
				q_add_dir_with_id->Bind(dir);
				q_add_dir_with_id->Write();
				q_add_dir_with_id->Reset();
			}
		}		

		SLastEntries e;
		e.dir=dir;
		e.time=currtime;
		lastentries.push_back(e);
	}
}

void DirectoryWatcherThread::OnDirRm(const std::string &dir)
{
	q_add_del_dir->Bind(dir);
	q_add_del_dir->Bind(dir);
	q_add_del_dir->Write();
	q_add_del_dir->Reset();
}

void DirectoryWatcherThread::stop(void)
{
	do_stop=true;
	pipe->Write("Q");
}

IPipe *DirectoryWatcherThread::getPipe(void)
{
	return pipe;
}

void DirectoryWatcherThread::On_FileNameChanged(const std::string & strOldFileName, const std::string & strNewFileName, bool closed)
{
	On_FileModified(strOldFileName, closed);
	On_FileModified(strNewFileName, closed);
}

void DirectoryWatcherThread::On_DirNameChanged( const std::string & strOldFileName, const std::string & strNewFileName, bool closed )
{
	On_FileNameChanged(strOldFileName, strNewFileName, closed);
}

void DirectoryWatcherThread::On_FileRemoved(const std::string & strFileName, bool closed)
{
	On_FileModified(strFileName, closed);
}

void DirectoryWatcherThread::On_FileAdded(const std::string & strFileName, bool closed)
{
	On_FileModified(strFileName, closed);
}

void DirectoryWatcherThread::On_DirAdded( const std::string & strFileName, bool closed )
{
	On_FileModified(strFileName, closed);
}

void DirectoryWatcherThread::On_FileModified(const std::string & strFileName, bool closed)
{
	bool ok=false;
	std::string dir=strlower(ExtractFilePath(strFileName))+os_file_sep();
	for(size_t i=0;i<watching.size();++i)
	{
		if(dir.find(watching[i])==0)
		{
			OnDirMod(dir);
			break;
		}
	}	
}

void DirectoryWatcherThread::On_DirRemoved(const std::string & strDirName, bool closed)
{
	std::string rmDir=strlower(add_trailing_slash(strDirName));
	bool ok=false;
	for(size_t i=0;i<watching.size();++i)
	{
		if(rmDir.find(watching[i])==0)
		{
			OnDirRm(rmDir);
			break;
		}
	}

	On_FileModified(strDirName, closed);
}

bool DirectoryWatcherThread::is_stopped(void)
{
	return do_stop;
}

void DirectoryWatcherThread::On_ResetAll(const std::string & vol)
{
	OnDirMod("##-GAP-##"+strlower(vol));
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

int64 DirectoryWatcherThread::getStartUsn( int64 sequence_id )
{
	return -1;
}

void DirectoryWatcherThread::On_FileOpen( const std::string & strFileName )
{
	open_files.push_back(strlower(strFileName));
}
