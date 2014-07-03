#pragma once

#include "../ChangeJournalWatcher.h"
#include "../../common/data.h"
#include "../../Interface/Database.h"
#include "JournalDAO.h"

const char CHANGE_REN_FILE=0;
const char CHANGE_REN_DIR=1;
const char CHANGE_DEL_FILE=2;
const char CHANGE_ADD_FILE=3;
const char CHANGE_ADD_DIR=4;
const char CHANGE_MOD=5;
const char CHANGE_MOD_ALL=6;
const char CHANGE_DEL_DIR=7;
const char CHANGE_COMMIT=8;

class ContinuousWatchEnqueue : public IChangeJournalListener
{
public:
	ContinuousWatchEnqueue(IDatabase* db);

	virtual void On_FileNameChanged( const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool save_fn, bool closed );

	virtual void On_DirNameChanged( const std::wstring & strOldFileName, const std::wstring & strNewFileName, bool closed );

	virtual void On_FileRemoved( const std::wstring & strFileName, bool closed );

	virtual void On_FileAdded( const std::wstring & strFileName, bool closed );

	virtual void On_DirAdded( const std::wstring & strFileName, bool closed );

	virtual void On_FileModified( const std::wstring & strFileName, bool save_fn, bool closed );

	virtual void On_ResetAll( const std::wstring & vol );

	virtual void On_DirRemoved( const std::wstring & strDirName, bool closed );

	virtual void Commit(const std::vector<IChangeJournalListener::SSequence>& sequences);


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

	volatile bool update_patterns;
	std::vector<std::wstring> exlude_dirs;
	std::vector<std::wstring> include_dirs;
	std::vector<int> include_depth;
	std::vector<std::wstring> include_prefix;

	JournalDAO journal_dao;

	CWData queue;

	std::vector<SWatchItem> watchdirs;
};