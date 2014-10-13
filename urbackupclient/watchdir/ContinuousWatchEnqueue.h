#pragma once

#include "../ChangeJournalWatcher.h"
#include "../../common/data.h"
#include "../../Interface/Database.h"
#include "JournalDAO.h"
#include "../../urbackupcommon/change_ids.h"

class ContinuousWatchEnqueue : public IChangeJournalListener
{
public:
	ContinuousWatchEnqueue();

	virtual int64 getStartUsn(int64 sequence_id);

	virtual void On_FileNameChanged( const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool save_fn, bool closed );

	virtual void On_DirNameChanged( const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool closed );

	virtual void On_FileRemoved( const std::wstring & strFileName, bool closed );

	virtual void On_FileAdded( const std::wstring & strFileName, bool closed );

	virtual void On_DirAdded( const std::wstring & strFileName, bool closed );

	virtual void On_FileModified( const std::wstring & strFileName, bool save_fn, bool closed );

	virtual void On_ResetAll( const std::wstring & vol );

	virtual void On_DirRemoved( const std::wstring & strDirName, bool closed );

	virtual void Commit(const std::vector<IChangeJournalListener::SSequence>& sequences);

	void setStartUsn(int64 sequence_id, int64 seq_start);

	struct SWatchItem
	{
		SWatchItem(const std::wstring& dir, const std::wstring& name)
			: dir(dir), name(name)
		{

		}

		std::wstring dir;
		std::wstring name;

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

	std::vector<std::wstring> getWatchDirs(const std::wstring& fn);
	std::vector<std::pair<std::wstring, std::wstring> > getWatchDirs(const std::wstring& fn1, const std::wstring& fn2);

	void On_FileNameChangedInt( const std::wstring & strOldFileName, const std::wstring & strNewFileName );

	void On_DirNameChangedInt( const std::wstring & strOldFileName, const std::wstring & strNewFileName);

	void On_FileRemovedInt( const std::wstring & strFileName );

	void On_FileAddedInt( const std::wstring & strFileName );

	void On_DirAddedInt( const std::wstring & strFileName );

	void On_FileModifiedInt( const std::wstring & strFileName );

	void On_ResetAllInt( const std::wstring & vol );

	void On_DirRemovedInt( const std::wstring & strDirName );

	bool pathIncluded(const std::wstring& path, const std::wstring& named_path);

	void readIncludeExcludePatterns();

	void setupDatabaseAccess();

	volatile bool update_patterns;
	std::vector<std::wstring> exlude_dirs;
	std::vector<std::wstring> include_dirs;
	std::vector<int> include_depth;
	std::vector<std::wstring> include_prefix;

	std::auto_ptr<JournalDAO> journal_dao;

	CWData queue;


	std::map<int64, int64> sequences_start;
	std::vector<SWatchItem> watchdirs;
};