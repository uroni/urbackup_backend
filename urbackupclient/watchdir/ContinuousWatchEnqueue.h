#pragma once

#include "../ChangeJournalWatcher.h"
#include "../../common/data.h"
#include "../../Interface/Database.h"
#include "JournalDAO.h"
#include "../../urbackupcommon/change_ids.h"
#include "../client.h"

class ContinuousWatchEnqueue : public IChangeJournalListener
{
public:
	ContinuousWatchEnqueue();

	virtual int64 getStartUsn(int64 sequence_id);

	virtual void On_FileNameChanged( const std::string & strOldFileName, const std::string & strNewFileName, bool closed );

	virtual void On_DirNameChanged( const std::string & strOldFileName, const std::string & strNewFileName, bool closed );

	virtual void On_FileRemoved( const std::string & strFileName, bool closed );

	virtual void On_FileAdded( const std::string & strFileName, bool closed );

	virtual void On_DirAdded( const std::string & strFileName, bool closed );

	virtual void On_FileModified( const std::string & strFileName, bool closed );

	virtual void On_FileOpen(const std::string & strFileName);

	virtual void On_ResetAll( const std::string & vol );

	virtual void On_DirRemoved( const std::string & strDirName, bool closed );

	virtual void Commit(const std::vector<IChangeJournalListener::SSequence>& sequences);

	void setStartUsn(int64 sequence_id, int64 seq_start);

	struct SWatchItem
	{
		SWatchItem(const std::string& dir, const std::string& name)
			: dir(dir), name(name)
		{

		}

		std::string dir;
		std::string name;

		bool operator==(const SWatchItem& other)
		{
			return dir==other.dir &&
				name==other.dir;
		}
	};

	void addWatchdir(SWatchItem item);

	void removeWatchdir(SWatchItem item);

	void updatePatterns();

private:

	std::vector<std::string> getWatchDirs(const std::string& fn);
	std::vector<std::pair<std::string, std::string> > getWatchDirs(const std::string& fn1, const std::string& fn2);

	void On_FileNameChangedInt( const std::string & strOldFileName, const std::string & strNewFileName );

	void On_DirNameChangedInt( const std::string & strOldFileName, const std::string & strNewFileName);

	void On_FileRemovedInt( const std::string & strFileName );

	void On_FileAddedInt( const std::string & strFileName );

	void On_DirAddedInt( const std::string & strFileName );

	void On_FileModifiedInt( const std::string & strFileName );

	void On_ResetAllInt( const std::string & vol );

	void On_DirRemovedInt( const std::string & strDirName );

	bool pathIncluded(const std::string& path, const std::string& named_path);

	void readIncludeExcludePatterns();

	void setupDatabaseAccess();

	volatile bool update_patterns;
	std::vector<std::string> exclude_dirs;
	std::vector<SIndexInclude> include_dirs;
	std::vector<int> include_depth;
	std::vector<std::string> include_prefix;

	std::auto_ptr<JournalDAO> journal_dao;

	CWData queue;


	std::map<int64, int64> sequences_start;
	std::vector<SWatchItem> watchdirs;
};