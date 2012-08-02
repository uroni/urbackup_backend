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

#include "DirectoryWatcherThread.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "database.h"

#define CHANGE_JOURNAL

IPipe *DirectoryWatcherThread::pipe=NULL;
IMutex *DirectoryWatcherThread::update_mutex=NULL;
ICondition *DirectoryWatcherThread::update_cond=NULL;

const unsigned int mindifftime=120000;

DirectoryWatcherThread::DirectoryWatcherThread(const std::vector<std::wstring> &watchdirs)
{
	do_stop=false;
	watching=watchdirs;
}

void DirectoryWatcherThread::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	q_add_dir=db->Prepare("INSERT INTO mdirs SELECT ? AS NAME WHERE NOT EXISTS (SELECT * FROM mdirs WHERE name=?)");
	q_add_del_dir=db->Prepare("INSERT INTO del_dirs SELECT ? AS NAME WHERE NOT EXISTS (SELECT * FROM del_dirs WHERE name=?)");

	ChangeListener cl;
#ifdef CHANGE_JOURNAL
	ChangeJournalWatcher dcw(this, db, this);
#else
	CDirectoryChangeWatcher dcw(false);
#endif
	
	
	for(size_t i=0;i<watching.size();++i)
	{
	    #ifndef CHANGE_JOURNAL
			dcw.WatchDirectory(watching[i], FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE, &cl, TRUE);
		#else
		    dcw.watchDir(watching[i]);
		#endif
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
				std::wstring dir=msg.substr(1);
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
#ifndef CHANGE_JOURNAL
					dcw.WatchDirectory(dir, FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE, &cl, TRUE);
#else
					dcw.watchDir(dir);
#endif
					watching.push_back(dir);
					On_ResetAll(dir);
				}
			}
			else if( msg[0]=='D' )
			{
				std::wstring dir=msg.substr(1);
#ifndef CHANGE_JOURNAL
				dcw.UnwatchDirectory(dir);
#endif
				for(size_t i=0;i<watching.size();++i)
				{
					if(watching[i]==dir)
					{
						watching.erase(watching.begin()+i);
						break;
					}
				}
			}
#ifdef CHANGE_JOURNAL
			else if( msg[0]=='U' )
			{
				lastentries.clear();
				dcw.update();
				dcw.update_longliving();

				IScopedLock lock(update_mutex);
				update_cond->notify_all();
			}
#endif
			else if( msg[0]=='Q' )
			{
				continue;
			}
			else if( msg[0]=='R' )
			{
				std::wstring dir=msg.substr(1);
				OnDirRm(dir);
			}
			else
			{
				std::wstring dir=msg.substr(1);
				OnDirMod(dir);
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
void DirectoryWatcherThread::OnDirMod(const std::wstring &dir)
{
	bool found=false;
	bool c=true;
	unsigned int currtime=Server->getTimeMS();
	while(c)
	{
		c=false;
		for(std::list<SLastEntries>::iterator it=lastentries.begin();it!=lastentries.end();++it)
		{
			if(currtime-(*it).time>mindifftime)
			{
				lastentries.erase(it);
				c=true;
				break;
			}
			else if( (*it).dir==dir )
			{
				(*it).time=currtime;
				found=true;
				break;
			}
		}
	}
	
	if(found==false)
	{
		q_add_dir->Bind(dir);
		q_add_dir->Bind(dir);
		q_add_dir->Write();
		q_add_dir->Reset();

		SLastEntries e;
		e.dir=dir;
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

void DirectoryWatcherThread::On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName)
{
	On_FileModified(strOldFileName);
	On_FileModified(strNewFileName);
}

void DirectoryWatcherThread::On_FileRemoved(const std::wstring & strFileName)
{
	On_FileModified(strFileName);
}

void DirectoryWatcherThread::On_FileAdded(const std::wstring & strFileName)
{
	On_FileModified(strFileName);
}

void DirectoryWatcherThread::On_FileModified(const std::wstring & strFileName)
{
	bool ok=false;
	std::wstring dir=ExtractFilePath(strFileName);
	for(size_t i=0;i<watching.size();++i)
	{
		if(dir.find(watching[i])==0)
		{
			OnDirMod(dir);
			return;
		}
	}	
}

void DirectoryWatcherThread::On_DirRemoved(const std::wstring & strDirName)
{
	bool ok=false;
	for(size_t i=0;i<watching.size();++i)
	{
		if(strDirName.find(watching[i])==0)
		{
			OnDirRm(strDirName);
			return;
		}
	}
}

bool DirectoryWatcherThread::is_stopped(void)
{
	return do_stop;
}

void DirectoryWatcherThread::On_ResetAll(const std::wstring & vol)
{
	OnDirMod(L"##-GAP-##"+vol);
}

void ChangeListener::On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName)
{
	std::wstring dir1=L"S"+ExtractFilePath(strOldFileName);
	DirectoryWatcherThread::getPipe()->Write((char*)dir1.c_str(), dir1.size()*sizeof(wchar_t));
	dir1=L"S"+ExtractFilePath(strNewFileName);
	DirectoryWatcherThread::getPipe()->Write((char*)dir1.c_str(), dir1.size()*sizeof(wchar_t));
}

void ChangeListener::On_FileRemoved(const std::wstring & strFileName)
{
	std::wstring dir1=L"S"+ExtractFilePath(strFileName);
	DirectoryWatcherThread::getPipe()->Write((char*)dir1.c_str(), dir1.size()*sizeof(wchar_t));
}

void ChangeListener::On_FileAdded(const std::wstring & strFileName)
{
	std::wstring dir1=L"S"+ExtractFilePath(strFileName);
	DirectoryWatcherThread::getPipe()->Write((char*)dir1.c_str(), dir1.size()*sizeof(wchar_t));
}

void ChangeListener::On_FileModified(const std::wstring & strFileName)
{
	std::wstring dir1=L"S"+ExtractFilePath(strFileName);
	DirectoryWatcherThread::getPipe()->Write((char*)dir1.c_str(), dir1.size()*sizeof(wchar_t));
}

void ChangeListener::On_ResetAll(const std::wstring & vol)
{
	std::wstring dir1=L"S##-GAP-##"+vol;
	DirectoryWatcherThread::getPipe()->Write((char*)dir1.c_str(), dir1.size()*sizeof(wchar_t));
}

void ChangeListener::On_DirRemoved(const std::wstring & strDirName)
{
	std::wstring dir1=L"R"+ExtractFilePath(strDirName);
	DirectoryWatcherThread::getPipe()->Write((char*)dir1.c_str(), dir1.size()*sizeof(wchar_t));
}