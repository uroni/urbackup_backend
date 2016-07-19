#include "ContinuousWatchEnqueue.h"
#include "../../Interface/Server.h"
#include <algorithm>
#include "../../stringtools.h"
#include "../ClientService.h"
#include <memory>
#include "../../Interface/SettingsReader.h"
#include "../../urbackupserver/database.h"


ContinuousWatchEnqueue::ContinuousWatchEnqueue()
	: update_patterns(true)
{

}

void ContinuousWatchEnqueue::On_FileNameChanged( const std::string & strOldFileName, const std::string & strNewFileName, bool closed )
{
	if(!closed)
		return;

	std::vector<std::pair<std::string, std::string> > dirs =
		getWatchDirs(strOldFileName, strNewFileName);

	for(size_t i=0;i<dirs.size();++i)
	{
		if(!dirs[i].first.empty()
			&& !dirs[i].second.empty()
			&& pathIncluded(strOldFileName, dirs[i].first)
			&& pathIncluded(strNewFileName, dirs[i].second) )
		{
			On_FileNameChangedInt(dirs[i].first, dirs[i].second);
		}
		else if(!dirs[i].first.empty()
			&& pathIncluded(strOldFileName, dirs[i].first) )
		{
			On_FileRemovedInt(dirs[i].first);
		}
		else if(!dirs[i].second.empty()
			&& pathIncluded(strNewFileName, dirs[i].second) )
		{
			On_FileAddedInt(dirs[i].second);
		}
	}	
}


void ContinuousWatchEnqueue::On_DirNameChanged( const std::string & strOldFileName, const std::string & strNewFileName, bool closed )
{
	if(!closed)
		return;

	std::vector<std::pair<std::string, std::string> > dirs =
		getWatchDirs(strOldFileName, strNewFileName);

	for(size_t i=0;i<dirs.size();++i)
	{
		if(!dirs[i].first.empty()
			&& !dirs[i].second.empty()
			&& pathIncluded(strOldFileName, dirs[i].first)
			&& pathIncluded(strNewFileName, dirs[i].second) )
		{
			On_DirNameChangedInt(dirs[i].first, dirs[i].second);
		}
		else if(!dirs[i].first.empty()
			&& pathIncluded(strOldFileName, dirs[i].first) )
		{
			On_DirRemovedInt(dirs[i].first);
		}
		else if(!dirs[i].second.empty()
			&& pathIncluded(strNewFileName, dirs[i].second) )
		{
			On_DirAddedInt(dirs[i].second);
		}
	}
}


void ContinuousWatchEnqueue::On_FileRemoved( const std::string & strFileName, bool closed )
{
	if(!closed)
		return;

	std::vector<std::string> dirs =
		getWatchDirs(strFileName);

	for(size_t i=0;i<dirs.size();++i)
	{
		if(pathIncluded(strFileName, dirs[i]))
		{
			On_FileRemovedInt(dirs[i]);
		}
	}
}

void ContinuousWatchEnqueue::On_FileAdded( const std::string & strFileName, bool closed )
{
	if(!closed)
		return;

	std::vector<std::string> dirs =
		getWatchDirs(strFileName);

	for(size_t i=0;i<dirs.size();++i)
	{
		if(pathIncluded(strFileName, dirs[i]))
		{
			On_FileAddedInt(dirs[i]);
		}
	}
}


void ContinuousWatchEnqueue::On_DirAdded( const std::string & strFileName, bool closed )
{
	if(!closed)
		return;

	std::vector<std::string> dirs =
		getWatchDirs(strFileName);

	for(size_t i=0;i<dirs.size();++i)
	{
		if(pathIncluded(strFileName, dirs[i]))
		{
			On_DirAddedInt(dirs[i]);
		}
	}
}


void ContinuousWatchEnqueue::On_FileModified( const std::string & strFileName, bool closed )
{
	if(!closed)
		return;

	std::vector<std::string> dirs =
		getWatchDirs(strFileName);

	for(size_t i=0;i<dirs.size();++i)
	{
		if(pathIncluded(strFileName, dirs[i]))
		{
			On_FileModifiedInt(dirs[i]);
		}
	}
}

void ContinuousWatchEnqueue::On_ResetAll( const std::string & vol )
{
	On_ResetAllInt(vol);
}

void ContinuousWatchEnqueue::On_DirRemoved( const std::string & strDirName, bool closed )
{
	if(!closed)
		return;

	std::vector<std::string> dirs =
		getWatchDirs(strDirName);

	for(size_t i=0;i<dirs.size();++i)
	{
		if(pathIncluded(strDirName, dirs[i]))
		{
			On_DirRemovedInt(dirs[i]);
		}
	}
}

void ContinuousWatchEnqueue::Commit(const std::vector<IChangeJournalListener::SSequence>& sequences)
{
	if(queue.getDataSize()==0)
		return;

	std::string data(queue.getDataPtr(), queue.getDataPtr()+queue.getDataSize());
	
	CWData header;
	header.addChar(CHANGE_HEADER);
	header.addUInt(static_cast<unsigned int>(sequences.size()));
	for(size_t i=0;i<sequences.size();++i)
	{
		header.addInt64(sequences[i].id);

		std::map<int64, int64>::iterator seq_start_it = sequences_start.find(sequences[i].id);
		if(seq_start_it!=sequences_start.end())
		{
			header.addInt64(seq_start_it->second);
		}
		else
		{
			continue;
		}

		header.addInt64(sequences[i].stop);

		sequences_start[sequences[i].id]=sequences[i].stop;
	}
	
	data.insert(data.begin(), header.getDataPtr(), header.getDataPtr()+header.getDataSize());

	if(ClientConnector::tochannelSendChanges(data.data(), data.size()))
	{
		queue.clear();
	}
}

void ContinuousWatchEnqueue::addWatchdir( SWatchItem item )
{
	item.dir = strlower(item.dir);
	std::vector<SWatchItem>::iterator iter=std::find(watchdirs.begin(),
		watchdirs.end(), item);

	if(iter==watchdirs.end())
	{
		watchdirs.push_back(item);

		On_ResetAllInt(item.dir);
	}
}

void ContinuousWatchEnqueue::removeWatchdir( SWatchItem item )
{
	item.dir = strlower(item.dir);
	std::vector<SWatchItem>::iterator iter=std::find(watchdirs.begin(),
		watchdirs.end(), item);

	if(iter!=watchdirs.end())
	{
		watchdirs.erase(iter);
		On_ResetAllInt(item.dir);
	}
}

std::vector<std::string> ContinuousWatchEnqueue::getWatchDirs( const std::string& fn )
{
	std::string fn_lower = strlower(fn);

	std::vector<std::string> dirs;

	for(size_t i=0;i<watchdirs.size();++i)
	{
		if(next(fn_lower, 0, watchdirs[i].dir))
		{
			std::string fn_sub = fn.substr(watchdirs[i].dir.size());
			dirs.push_back(watchdirs[i].name+fn_sub);
		}
	}

	return dirs;
}


std::vector<std::pair<std::string, std::string> > ContinuousWatchEnqueue::getWatchDirs( const std::string& fn1, const std::string& fn2 )
{
	std::string fn1_lower = strlower(fn1);
	std::string fn2_lower = strlower(fn1);

	std::vector<std::pair<std::string, std::string> > dirs;

	for(size_t i=0;i<watchdirs.size();++i)
	{
		bool fn1_ins = next(fn1_lower, 0, watchdirs[i].dir);
		bool fn2_ins = next(fn2_lower, 0, watchdirs[i].dir);

		std::string fn1_sub;
		if(fn1_ins)
		{
			fn1_sub = watchdirs[i].name+fn1.substr(watchdirs[i].dir.size());
		}
		std::string fn2_sub;
		if(fn2_ins)
		{
			fn2_sub = watchdirs[i].name+fn2.substr(watchdirs[i].dir.size());
		}

		dirs.push_back(std::make_pair(fn1_sub, fn2_sub));
	}

	return dirs;
}


void ContinuousWatchEnqueue::On_FileNameChangedInt( const std::string & strOldFileName, const std::string & strNewFileName )
{
	queue.addChar(CHANGE_REN_FILE);
	queue.addString((strOldFileName));
	queue.addString((strNewFileName));
}

void ContinuousWatchEnqueue::On_DirNameChangedInt( const std::string & strOldFileName, const std::string & strNewFileName )
{
	queue.addChar(CHANGE_REN_DIR);
	queue.addString((strOldFileName));
	queue.addString((strNewFileName));
}

void ContinuousWatchEnqueue::On_FileRemovedInt( const std::string & strFileName )
{
	queue.addChar(CHANGE_DEL_FILE);
	queue.addString((strFileName));
}

void ContinuousWatchEnqueue::On_FileAddedInt( const std::string & strFileName )
{
	queue.addChar(CHANGE_ADD_FILE);
	queue.addString((strFileName));
}

void ContinuousWatchEnqueue::On_DirAddedInt( const std::string & strFileName )
{
	queue.addChar(CHANGE_ADD_DIR);
	queue.addString((strFileName));
}

void ContinuousWatchEnqueue::On_FileModifiedInt( const std::string & strFileName )
{
	queue.addChar(CHANGE_MOD);
	queue.addString((strFileName));
}

void ContinuousWatchEnqueue::On_ResetAllInt( const std::string & vol )
{
	queue.addChar(CHANGE_MOD_ALL);
	queue.addString((vol));
}

void ContinuousWatchEnqueue::On_DirRemovedInt( const std::string & strDirName )
{
	readIncludeExcludePatterns();

	queue.addChar(CHANGE_DEL_DIR);
	queue.addString((strDirName));
}

void ContinuousWatchEnqueue::readIncludeExcludePatterns()
{
	if(!update_patterns)
		return;

	update_patterns=false;

	std::auto_ptr<ISettingsReader> curr_settings(Server->createFileSettingsReader("urbackup/data/settings.cfg"));

	if(curr_settings.get())
	{
		std::string val;
		if(curr_settings->getValue("continuous_exclude_files", &val)
			|| curr_settings->getValue("continuous_exclude_files_def", &val) )
		{
			exlude_dirs = IndexThread::parseExcludePatterns(val);
		}
		else
		{
			exlude_dirs = IndexThread::parseExcludePatterns(std::string());
		}

		if(curr_settings->getValue("continuous_include_files", &val)
			|| curr_settings->getValue("continuous_include_files_def", &val) )
		{
			include_dirs = IndexThread::parseIncludePatterns(val);
		}
	}
	else
	{
		IndexThread::parseExcludePatterns(std::string());
	}
}

bool ContinuousWatchEnqueue::pathIncluded( const std::string& path, const std::string& named_path )
{
	if( IndexThread::isExcluded(exlude_dirs, path)
		|| IndexThread::isExcluded(exlude_dirs, named_path) )
	{
		return false;
	}

	if( !IndexThread::isIncluded(include_dirs, path, NULL)
		&& !IndexThread::isIncluded(include_dirs, named_path, NULL) )
	{
		return false;
	}

	return true;
}

void ContinuousWatchEnqueue::setupDatabaseAccess()
{
	if(journal_dao.get()==NULL)
	{
		journal_dao.reset(new JournalDAO(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER)));
	}
}

int64 ContinuousWatchEnqueue::getStartUsn( int64 sequence_id )
{
	return sequences_start[sequence_id];
}

void ContinuousWatchEnqueue::setStartUsn( int64 sequence_id, int64 seq_start )
{
	sequences_start[sequence_id]=seq_start;
}

void ContinuousWatchEnqueue::On_FileOpen( const std::string & strFileName )
{

}

