#ifndef CHANGEJOURNALWATCHER_H
#define CHANGEJOURNALWATCHER_H

#include <string>
#include <vector>
#include <windows.h>
#include <WinIoCtl.h>

#include "../Interface/Database.h"
#include "../Interface/Query.h"

class DirectoryWatcherThread;

struct SChangeJournal
{
	std::vector<std::wstring> path;
	USN last_record;
	DWORDLONG journal_id;
	HANDLE hVolume;
	_i64 rid;
	bool last_record_update;
};

struct SDeviceInfo
{
	bool has_info;
	DWORDLONG journal_id;
	USN last_record;
	bool index_done;
};

struct UsnInt
{
	DWORDLONG FileReferenceNumber;
    DWORDLONG ParentFileReferenceNumber;
	USN Usn;
	DWORD Reason;
	std::wstring Filename;
	USN NextUsn;
	DWORD attributes;
};

class IChangeJournalListener;

class ChangeJournalWatcher
{
public:
	ChangeJournalWatcher(DirectoryWatcherThread * dwt, IDatabase *pDB, IChangeJournalListener *pListener);
	~ChangeJournalWatcher(void);

	void watchDir(const std::wstring &dir);

	void update(bool force_write=false);
	void update_longliving(void);
private:
	IChangeJournalListener *listener;
	std::map<std::wstring, SChangeJournal> wdirs;

	void createQueries(void);
	SDeviceInfo getDeviceInfo(const std::wstring &name);
	_i64 hasRoot(const std::wstring &root);
	_i64 addRoot(const std::wstring &root);
	void addFrn(const std::wstring &name, _i64 parent_id, _i64 frn, _i64 rid);
	void addFrnTmp(const std::wstring &name, _i64 parent_id, _i64 frn, _i64 rid);
	void renameEntry(const std::wstring &name, _i64 id, _i64 pid);
	void resetRoot(_i64 rid);
	_i64 hasEntry( _i64 rid, _i64 frn);
	std::vector<_i64> getChildren(_i64 frn, _i64 rid);
	void deleteEntry(_i64 id);
	void deleteEntry(_i64 frn, _i64 rid);
	void saveJournalData(DWORDLONG journal_id, const std::wstring &vol, PUSN_RECORD rec, USN nextUsn);
	std::vector<UsnInt> getJournalData( const std::wstring &vol);
	void setIndexDone(const std::wstring &vol, int s);
	void deleteJournalData(const std::wstring &vol);
	void deleteJournalId(const std::wstring &vol);

	void deleteWithChildren( _i64 frn, _i64 rid);
	std::wstring getFilename(_i64 frn, _i64 rid);

	void indexRootDirs(_i64 rid, const std::wstring &root, _i64 parent);
	void indexRootDirs2(const std::wstring &root, SChangeJournal *sj);

	void updateWithUsn(const std::wstring &vol, const SChangeJournal &cj, const UsnInt *UsnRecord);

	void reindex(_i64 rid, std::wstring vol, SChangeJournal *sj);
	void logEntry(const std::wstring &vol, const UsnInt *UsnRecord);

	IDatabase *db;

	IQuery *q_get_dev_id;
	IQuery *q_has_root;
	IQuery *q_add_root;
	IQuery *q_add_frn;
	IQuery *q_add_frn_tmp;
	IQuery *q_reset_root;
	IQuery *q_get_entry;
	IQuery *q_get_children;
	IQuery *q_del_entry;
	IQuery *q_get_name;
	IQuery *q_update_lastusn;
	IQuery *q_add_journal;
	IQuery *q_update_journal_id;
	IQuery *q_rename_entry;
	IQuery *q_create_tmp_table;
	IQuery *q_save_journal_data;
	IQuery *q_get_journal_data;
	IQuery *q_set_index_done;
	IQuery *q_del_journal_data;
	IQuery *q_del_entry_frn;
	IQuery *q_del_journal_id;

	unsigned int last_index_update;

	bool has_error;
	bool indexing_in_progress;
	std::wstring indexing_volume;

	std::map<std::wstring, bool> open_write_files;
	std::vector<std::wstring> error_dirs;

	DirectoryWatcherThread * dwt;
};

class IChangeJournalListener
{
public:
	virtual void On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName)=0;
    virtual void On_FileRemoved(const std::wstring & strFileName)=0;
    virtual void On_FileAdded(const std::wstring & strFileName)=0;
    virtual void On_FileModified(const std::wstring & strFileName)=0;
	virtual void On_ResetAll(const std::wstring & vol)=0;
	virtual void On_DirRemoved(const std::wstring & strDirName)=0;
};

#endif //CHANGEJOURNALWATCHER_H