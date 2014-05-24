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

#include "ChangeJournalWatcher.h"
#include "../urbackupcommon/os_functions.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "DirectoryWatcherThread.h"

const unsigned int usn_update_freq=10*60*1000;
const DWORDLONG usn_reindex_num=1000000; // one million

#define VLOG(x) 

ChangeJournalWatcher::ChangeJournalWatcher(DirectoryWatcherThread * dwt, IDatabase *pDB, IChangeJournalListener *pListener)
	: dwt(dwt), listener(pListener), db(pDB), last_backup_time(0)
{
	createQueries();

	indexing_in_progress=false;
	has_error=false;
	last_index_update=0;
}

ChangeJournalWatcher::~ChangeJournalWatcher(void)
{
	for(std::map<std::wstring, SChangeJournal>::iterator it=wdirs.begin();it!=wdirs.end();++it)
	{
		CloseHandle(it->second.hVolume);
	}
}

void ChangeJournalWatcher::createQueries(void)
{
	q_get_dev_id=db->Prepare("SELECT journal_id, last_record, index_done FROM journal_ids WHERE device_name=?");
	q_has_root=db->Prepare("SELECT id FROM map_frn WHERE rid=-1 AND name=?");
	q_add_root=db->Prepare("INSERT INTO map_frn (name, pid, frn, rid) VALUES (?, -1, -1, -1)");
	q_add_frn=db->Prepare("INSERT INTO map_frn (name, pid, frn, rid) VALUES (?, ?, ?, ?)");
	q_reset_root=db->Prepare("DELETE FROM map_frn WHERE rid=?");
	q_get_entry=db->Prepare("SELECT id FROM map_frn WHERE frn=? AND rid=?");
	q_get_children=db->Prepare("SELECT id,frn FROM map_frn WHERE pid=? AND rid=?");
	q_del_entry=db->Prepare("DELETE FROM map_frn WHERE id=?");
	q_get_name=db->Prepare("SELECT name,pid FROM map_frn WHERE frn=? AND rid=?");
	q_add_journal=db->Prepare("INSERT INTO journal_ids (journal_id, device_name, last_record, index_done) VALUES (?,?,?,0)");
	q_update_journal_id=db->Prepare("UPDATE journal_ids SET journal_id=? WHERE device_name=?");
	q_update_lastusn=db->Prepare("UPDATE journal_ids SET last_record=? WHERE device_name=?");
	q_rename_entry=db->Prepare("UPDATE map_frn SET name=?, pid=? WHERE id=?");
	q_save_journal_data=db->Prepare("INSERT INTO journal_data (device_name, journal_id, usn, reason, filename, frn, parent_frn, next_usn, attributes) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
	q_get_journal_data=db->Prepare("SELECT usn, reason, filename, frn, parent_frn, next_usn FROM journal_data WHERE device_name=? ORDER BY usn ASC");
	q_set_index_done=db->Prepare("UPDATE journal_ids SET index_done=? WHERE device_name=?");
	q_del_journal_data=db->Prepare("DELETE FROM journal_data WHERE device_name=?");
	q_del_entry_frn=db->Prepare("DELETE FROM map_frn WHERE frn=? AND rid=?");
	q_del_journal_id=db->Prepare("DELETE FROM journal_ids WHERE device_name=?");
}

void ChangeJournalWatcher::deleteJournalData(const std::wstring &vol)
{
	q_del_journal_data->Bind(vol);
	q_del_journal_data->Write();
	q_del_journal_data->Reset();
}

void ChangeJournalWatcher::deleteJournalId(const std::wstring &vol)
{
	q_del_journal_id->Bind(vol);
	q_del_journal_id->Write();
	q_del_journal_id->Reset();
}

void ChangeJournalWatcher::setIndexDone(const std::wstring &vol, int s)
{
	q_set_index_done->Bind(s);
	q_set_index_done->Bind(vol);
	q_set_index_done->Write();
	q_set_index_done->Reset();
}

void ChangeJournalWatcher::saveJournalData(DWORDLONG journal_id, const std::wstring &vol, PUSN_RECORD rec, USN nextUsn)
{
	std::string t_fn;
	t_fn.resize(rec->FileNameLength);
	memcpy(&t_fn[0], rec->FileName, rec->FileNameLength);
	std::wstring fn=Server->ConvertFromUTF16(t_fn);
	if(fn==L"backup_client.db" || fn==L"backup_client.db-journal" )
		return;

	q_save_journal_data->Bind(vol);
	q_save_journal_data->Bind((_i64)journal_id);
	q_save_journal_data->Bind((_i64)rec->Usn);
	q_save_journal_data->Bind((_i64)rec->Reason);
	q_save_journal_data->Bind(fn);
	q_save_journal_data->Bind((_i64)rec->FileReferenceNumber);
	q_save_journal_data->Bind((_i64)rec->ParentFileReferenceNumber);
	q_save_journal_data->Bind(nextUsn);
	q_save_journal_data->Bind((_i64)rec->FileAttributes);
	
	q_save_journal_data->Write();
	q_save_journal_data->Reset();
}

std::vector<UsnInt> ChangeJournalWatcher::getJournalData(const std::wstring &vol)
{
	q_get_journal_data->Bind(vol);
	db_results res=q_get_journal_data->Read();
	q_get_journal_data->Reset();
	std::vector<UsnInt> ret;
	for(size_t i=0;i<res.size();++i)
	{
		UsnInt rec;
		rec.Usn=os_atoi64(wnarrow(res[i][L"usn"]));
		rec.Reason=(DWORD)os_atoi64(wnarrow(res[i][L"reason"]));
		rec.Filename=res[i][L"filename"];
		rec.FileReferenceNumber=os_atoi64(wnarrow(res[i][L"frn"]));
		rec.ParentFileReferenceNumber=os_atoi64(wnarrow(res[i][L"parent_frn"]));
		rec.NextUsn=os_atoi64(wnarrow(res[i][L"next_usn"]));
		rec.attributes=(DWORD)os_atoi64(wnarrow(res[i][L"attributes"]));
		ret.push_back(rec);
	}
	return ret;
}

void ChangeJournalWatcher::renameEntry(const std::wstring &name, _i64 id, _i64 pid)
{
	q_rename_entry->Bind(name);
	q_rename_entry->Bind(pid);
	q_rename_entry->Bind(id);
	q_rename_entry->Write();
	q_rename_entry->Reset();
}

std::vector<_i64 > ChangeJournalWatcher::getChildren(_i64 frn, _i64 rid)
{
	std::vector<_i64> ret;
	q_get_children->Bind(frn);
	q_get_children->Bind(rid);
	db_results res=q_get_children->Read();
	q_get_children->Reset();
	for(size_t i=0;i<res.size();++i)
	{
		ret.push_back(os_atoi64(wnarrow(res[i][L"frn"])));
	}
	return ret;
}

void ChangeJournalWatcher::deleteEntry(_i64 id)
{
	q_del_entry->Bind(id);
	q_del_entry->Write();
	q_del_entry->Reset();
}

void ChangeJournalWatcher::deleteEntry(_i64 frn, _i64 rid)
{
	q_del_entry_frn->Bind(frn);
	q_del_entry_frn->Bind(rid);
	q_del_entry_frn->Write();
	q_del_entry_frn->Reset();
}

_i64 ChangeJournalWatcher::hasEntry( _i64 rid, _i64 frn)
{
	q_get_entry->Bind(frn);
	q_get_entry->Bind(rid);
	db_results res=q_get_entry->Read();
	q_get_entry->Reset();
	if(res.empty())
		return -1;
	else
		return os_atoi64(wnarrow(res[0][L"id"]));
}

void ChangeJournalWatcher::resetRoot(_i64 rid)
{
	q_reset_root->Bind(rid);
	q_reset_root->Write();
	q_reset_root->Reset();
}

_i64 ChangeJournalWatcher::addRoot(const std::wstring &root)
{
	q_add_root->Bind(root);
	q_add_root->Write();
	q_add_root->Reset();
	return db->getLastInsertID();
}

void ChangeJournalWatcher::addFrn(const std::wstring &name, _i64 parent_id, _i64 frn, _i64 rid)
{
	q_add_frn->Bind(name);
	q_add_frn->Bind(parent_id);
	q_add_frn->Bind(frn);
	q_add_frn->Bind(rid);
	q_add_frn->Write();
	q_add_frn->Reset();
}

void ChangeJournalWatcher::addFrnTmp(const std::wstring &name, _i64 parent_id, _i64 frn, _i64 rid)
{
	q_add_frn_tmp->Bind(name);
	q_add_frn_tmp->Bind(parent_id);
	q_add_frn_tmp->Bind(frn);
	q_add_frn_tmp->Bind(rid);
	q_add_frn_tmp->Write();
	q_add_frn_tmp->Reset();
}

_i64 ChangeJournalWatcher::hasRoot(const std::wstring &root)
{
	q_has_root->Bind(root);
	db_results res=q_has_root->Read();
	q_has_root->Reset();
	if(res.empty())
		return -1;
	else
	{
		return os_atoi64(wnarrow(res[0][L"id"]));
	}
}

void ChangeJournalWatcher::deleteWithChildren(_i64 frn, _i64 rid)
{
	std::vector<_i64> children=getChildren(frn, rid);
	deleteEntry(frn, rid);
	for(size_t i=0;i<children.size();++i)
	{
		deleteWithChildren(children[i], rid);
	}
}

void ChangeJournalWatcher::watchDir(const std::wstring &dir)
{
	WCHAR volume_path[MAX_PATH]; 
	BOOL ok = GetVolumePathNameW(dir.c_str(), volume_path, MAX_PATH);
	if(!ok)
	{
		Server->Log("GetVolumePathName(dir, volume_path, MAX_PATH) failed in ChangeJournalWatcher::watchDir", LL_ERROR);
		listener->On_ResetAll(dir);
		has_error=true;
		error_dirs.push_back(dir);
		return;
	}

	std::wstring vol=volume_path;

	if(vol.size()>0)
	{
		if(vol[vol.size()-1]=='\\')
		{
			vol.erase(vol.size()-1,1);
		}
	}

	std::map<std::wstring, SChangeJournal>::iterator it=wdirs.find(vol);
	if(it!=wdirs.end())
	{
		it->second.path.push_back(dir);
		return;
	}

	bool do_index=false;

	_i64 rid=hasRoot(vol);
	if(rid==-1)
	{
		listener->On_ResetAll(vol);
		do_index=true;
		rid=addRoot(vol);
		setIndexDone(vol, 0);
	}

	HANDLE hVolume=CreateFileW((L"\\\\.\\"+vol).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		Server->Log(L"CreateFile of volume '"+vol+L"' failed. - watchDir", LL_ERROR);
		listener->On_ResetAll(vol);
		error_dirs.push_back(vol);
		CloseHandle(hVolume);
		has_error=true;
		return;
	}
	
	USN_JOURNAL_DATA data;
	DWORD r_bytes;
	BOOL b=DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &data, sizeof(USN_JOURNAL_DATA), &r_bytes, NULL);
	if(b==0)
	{
		DWORD err=GetLastError();
		if(err==ERROR_INVALID_FUNCTION)
		{
			Server->Log(L"Change Journals not supported for Volume '"+vol+L"'", LL_ERROR);
			listener->On_ResetAll(vol);
			error_dirs.push_back(vol);
			CloseHandle(hVolume);
			has_error=true;
			return;
		}
		else if(err==ERROR_JOURNAL_DELETE_IN_PROGRESS)
		{
			Server->Log(L"Change Journals for Volume '"+vol+L"' is being deleted", LL_ERROR);
			listener->On_ResetAll(vol);
			error_dirs.push_back(vol);
			CloseHandle(hVolume);
			has_error=true;
			return;
		}
		else if(err==ERROR_JOURNAL_NOT_ACTIVE)
		{
			CREATE_USN_JOURNAL_DATA dat;
			dat.AllocationDelta=10485760; //10 MB
			dat.MaximumSize=73400320; // 70 MB
			DWORD bret;
			BOOL r=DeviceIoControl(hVolume, FSCTL_CREATE_USN_JOURNAL, &dat, sizeof(CREATE_USN_JOURNAL_DATA), NULL, 0, &bret, NULL);
			if(r==0)
			{
				Server->Log(L"Error creating change journal for Volume '"+vol+L"'", LL_ERROR);
				listener->On_ResetAll(vol);
				error_dirs.push_back(vol);
				CloseHandle(hVolume);
				has_error=true;
				return;
			}
			b=DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &data, sizeof(USN_JOURNAL_DATA), &r_bytes, NULL);
			if(b==0)
			{
				Server->Log(L"Unknown error for Volume '"+vol+L"' after creation - watchDir", LL_ERROR);
				listener->On_ResetAll(vol);
				error_dirs.push_back(vol);
				CloseHandle(hVolume);
				has_error=true;
				return;
			}
		}
		else
		{
			Server->Log(L"Unknown error for Volume '"+vol+L"' - watchDir ec: "+convert((int)err), LL_ERROR);
			listener->On_ResetAll(vol);
			error_dirs.push_back(vol);
			CloseHandle(hVolume);
			has_error=true;
			return;
		}
	}

	SDeviceInfo info=getDeviceInfo(vol);

	if(info.has_info)
	{
		if(info.journal_id!=data.UsnJournalID)
		{
			Server->Log(L"Journal id for '"+vol+L"' wrong - reindexing", LL_WARNING);
			listener->On_ResetAll(vol);
			do_index=true;
			setIndexDone(vol, 0);
			info.last_record=data.NextUsn;

			q_update_journal_id->Bind((_i64)data.UsnJournalID);
			q_update_journal_id->Bind(vol);
			q_update_journal_id->Write();
			q_update_journal_id->Reset();
		}

		bool needs_reindex=false;

		if( do_index==false && (info.last_record<data.FirstUsn || info.last_record>data.NextUsn) )
		{
			Server->Log(L"Last record not readable at '"+vol+L"' - reindexing. "
				L"Last record USN is "+convert(info.last_record)+
				L" FirstUsn is "+convert(data.FirstUsn)+
				L" NextUsn is "+convert(data.NextUsn), LL_WARNING);
			needs_reindex=true;
		}

		if( do_index==false && data.NextUsn-info.last_record>usn_reindex_num )
		{
			Server->Log(L"There are "+convert(data.NextUsn-info.last_record)+L" new USN entries at '"+vol+L"' - reindexing", LL_WARNING);
			needs_reindex=true;
		}

		if(needs_reindex)
		{			
			listener->On_ResetAll(vol);
			do_index=true;
			setIndexDone(vol, 0);
			info.last_record=data.NextUsn;
		}

		if(do_index==false && info.index_done==0)
		{
			Server->Log(L"Indexing was not finished at '"+vol+L"' - reindexing", LL_WARNING);
			do_index=true;
			setIndexDone(vol, 0);
			info.last_record=data.NextUsn;
		}
	}
	else
	{
		listener->On_ResetAll(vol);
		Server->Log(L"Info not found at '"+vol+L"' - reindexing", LL_WARNING);
		do_index=true;
	}

	SChangeJournal cj;
	cj.journal_id=data.UsnJournalID;
	if(!info.has_info)
		cj.last_record=data.NextUsn;
	else
		cj.last_record=info.last_record;

	cj.path.push_back(dir);
	cj.hVolume=hVolume;
	cj.rid=rid;
	cj.last_record_update=false;
	cj.vol_str=vol;

	if(!info.has_info)
	{
		q_add_journal->Bind((_i64)data.UsnJournalID);
		q_add_journal->Bind(vol);
		q_add_journal->Bind(cj.last_record);
		q_add_journal->Write();
		q_add_journal->Reset();

		setIndexDone(vol, 0);
	}

	wdirs.insert(std::pair<std::wstring, SChangeJournal>(vol, cj) );

	if(do_index)
	{
		reindex(rid, vol, &cj);
		Server->Log(L"Reindexing of '"+vol+L"' done.", LL_INFO);
	}
}

void ChangeJournalWatcher::reindex(_i64 rid, std::wstring vol, SChangeJournal *sj)
{
	setIndexDone(vol, 0);

	indexing_in_progress=true;
	indexing_volume=vol;
	last_index_update=Server->getTimeMS();
	Server->Log("Deleting directory FRN info from database...", LL_DEBUG);
	resetRoot(rid);
	Server->Log("Deleting saved journal data from database...", LL_DEBUG);
	deleteJournalData(vol);
	Server->Log("Starting indexing process..", LL_DEBUG);
	indexRootDirs2(vol, sj);
	listener->On_ResetAll(vol);
	indexing_in_progress=false;
	indexing_volume.clear();

	if(dwt->is_stopped()==false)
	{
		Server->Log(L"Setting indexing to done for "+vol, LL_DEBUG);
		setIndexDone(vol, 1);
	}
}

void ChangeJournalWatcher::indexRootDirs(_i64 rid, const std::wstring &root, _i64 parent)
{
	if(indexing_in_progress)
	{
		if(Server->getTimeMS()-last_index_update>10000)
		{
			update();
			last_index_update=Server->getTimeMS();
		}
	}

	std::wstring dir=root+os_file_sep();
	HANDLE hDir;
	if(root.size()==2)
		hDir=CreateFileW(dir.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	else
		hDir=CreateFileW(root.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if(INVALID_HANDLE_VALUE==hDir)
		return;
	BY_HANDLE_FILE_INFORMATION fi;
	GetFileInformationByHandle(hDir, &fi);
	CloseHandle(hDir);
	LARGE_INTEGER frn;
	frn.LowPart=fi.nFileIndexLow;
	frn.HighPart=fi.nFileIndexHigh;

	addFrn(ExtractFileName(root)+os_file_sep(), parent, frn.QuadPart, rid);

	std::vector<SFile> files=getFiles(root);
	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir)
		{
			indexRootDirs(rid, dir+files[i].name, frn.QuadPart);
		}
	}
}

void ChangeJournalWatcher::indexRootDirs2(const std::wstring &root, SChangeJournal *sj)
{
	db->Write("CREATE TEMPORARY TABLE map_frn_tmp (name TEXT, pid INTEGER, frn INTEGER, rid INTEGER)");
	q_add_frn_tmp=db->Prepare("INSERT INTO map_frn_tmp (name, pid, frn, rid) VALUES (?, ?, ?, ?)", false);

	HANDLE hDir = CreateFile((root+os_file_sep()).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,	NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if(hDir==INVALID_HANDLE_VALUE)
	{
		Server->Log("Could not open root HANDLE.", LL_ERROR);
		return;
	}

	BY_HANDLE_FILE_INFORMATION fi;
	GetFileInformationByHandle(hDir, &fi);
	CloseHandle(hDir);
	LARGE_INTEGER frn;
	frn.LowPart=fi.nFileIndexLow;
	frn.HighPart=fi.nFileIndexHigh;

	addFrn(root, -1, frn.QuadPart, sj->rid);

	MFT_ENUM_DATA med;
	med.StartFileReferenceNumber = 0;
	med.LowUsn = 0;
	med.HighUsn = MAXLONGLONG; //sj->last_record;

	// Process MFT in 64k chunks
	BYTE *pData=new BYTE[sizeof(DWORDLONG) + 0x10000];
	DWORDLONG fnLast = 0;
	DWORD cb;
	size_t nDirFRNs=0;
	size_t nFRNs=0;
	while (DeviceIoControl(sj->hVolume, FSCTL_ENUM_USN_DATA, &med, sizeof(med),pData, sizeof(DWORDLONG) + 0x10000, &cb, NULL) != FALSE)
	{
		if(indexing_in_progress)
		{
			if(Server->getTimeMS()-last_index_update>10000)
			{
				Server->Log("Saving new journal data to database...", LL_DEBUG);
				update(false, sj->vol_str);
				last_index_update=Server->getTimeMS();
			}
		}

		if(dwt->is_stopped())
		{
			Server->Log("Stopped indexing process", LL_WARNING);
			break;
		}

		PUSN_RECORD pRecord = (PUSN_RECORD) &pData[sizeof(USN)];
		while ((PBYTE) pRecord < (pData + cb))
		{
			if((pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)!=0)
			{
				std::wstring filename;
				filename.resize(pRecord->FileNameLength / sizeof(wchar_t) );
				memcpy(&filename[0], (PBYTE) pRecord + pRecord->FileNameOffset, pRecord->FileNameLength);
				addFrnTmp(filename, pRecord->ParentFileReferenceNumber, pRecord->FileReferenceNumber, sj->rid);
				++nDirFRNs;
			}
			++nFRNs;
			pRecord = (PUSN_RECORD) ((PBYTE) pRecord + pRecord->RecordLength);
		}
		med.StartFileReferenceNumber = * (DWORDLONG *) pData;
	}

	Server->Log("Added "+nconvert(nDirFRNs)+" directory FRNs to temporary database...", LL_DEBUG);
	Server->Log("MFT has "+nconvert(nFRNs)+" FRNs.", LL_DEBUG);

	delete []pData;
	db->destroyQuery(q_add_frn_tmp);
	q_add_frn_tmp=NULL;
	Server->Log("Copying directory FRNs to database...", LL_DEBUG);
	db->Write("INSERT INTO map_frn (name, pid, frn, rid) SELECT name, pid, frn, rid FROM map_frn_tmp");
	Server->Log("Dropping temporary database...", LL_DEBUG);
	db->Write("DROP TABLE map_frn_tmp");

	Server->Log("Saving new journal data to database. Forcing update...", LL_DEBUG);
	update(true, sj->vol_str);
}

SDeviceInfo ChangeJournalWatcher::getDeviceInfo(const std::wstring &name)
{
	q_get_dev_id->Bind(name);
	db_results res=q_get_dev_id->Read();
	q_get_dev_id->Reset();
	SDeviceInfo r;
	r.has_info=false;
	if(!res.empty())
	{
		r.has_info=true;
		r.journal_id=os_atoi64(wnarrow(res[0][L"journal_id"]));
		r.last_record=os_atoi64(wnarrow(res[0][L"last_record"]));
		r.index_done=watoi(res[0][L"index_done"])>0;
	}
	return r;
}

std::wstring ChangeJournalWatcher::getFilename(_i64 frn, _i64 rid)
{
	std::wstring path;
	_i64 curr_id=frn;
	while(true)
	{
		q_get_name->Bind(curr_id);
		q_get_name->Bind(rid);
		db_results res=q_get_name->Read();
		q_get_name->Reset();

		if(!res.empty())
		{
			_i64 pid=os_atoi64(wnarrow(res[0][L"pid"]));
			if(pid!=-1)
			{
				path=res[0][L"name"]+os_file_sep()+path;
				curr_id=pid;
			}
			else
			{
				path=res[0][L"name"]+os_file_sep()+path;
				break;
			}
		}
		else
		{
			if(path!=L"$RmMetadata\\$TxfLog\\")
			{
				Server->Log(L"Couldn't follow up to root. Current path: "+path, LL_ERROR);
			}
			return L"";
		}
	}
	return path;
}

const int BUF_LEN=4096;


void ChangeJournalWatcher::update(bool force_write, std::wstring vol_str)
{
	char buffer[BUF_LEN];

	bool started_transaction=false;

	for(std::map<std::wstring, SChangeJournal>::iterator it=wdirs.begin();it!=wdirs.end();++it)
	{
		if(!vol_str.empty() && it->first!=vol_str)
			continue;

		if(!indexing_in_progress)
		{		
			std::vector<UsnInt> jd=getJournalData(it->first);
			if(!jd.empty())
			{
				Server->Log("Applying saved journal data...", LL_DEBUG);
				if(!started_transaction)
				{
					started_transaction=true;
					db->BeginTransaction();
				}
				for(size_t i=0;i<jd.size();++i)
				{
					updateWithUsn(it->first, it->second, &jd[i]);
					it->second.last_record=jd[i].NextUsn;
				}
				Server->Log("Deleting saved journal data...", LL_DEBUG);
				deleteJournalData(it->first);

				db->EndTransaction();
				started_transaction=false;
			}
		}

		if(it->second.last_record_update)
		{
			if(started_transaction==false)
			{
				started_transaction=true;
				db->BeginTransaction();
			}

			it->second.last_record_update=false;
			q_update_lastusn->Bind(it->second.last_record);
			q_update_lastusn->Bind(it->first);
			q_update_lastusn->Write();
			q_update_lastusn->Reset();
		}

		USN startUsn=it->second.last_record;
		unsigned int update_bytes=0;

		bool remove_it=false;
		bool c=true;
		while(c)
		{
			c=false;
			READ_USN_JOURNAL_DATA data;
			data.StartUsn=it->second.last_record;
			data.ReasonMask=0xFFFFFFFF;//USN_REASON_DATA_EXTEND|USN_REASON_BASIC_INFO_CHANGE|USN_REASON_DATA_OVERWRITE|USN_REASON_DATA_TRUNCATION|USN_REASON_EA_CHANGE|USN_REASON_FILE_CREATE|USN_REASON_FILE_DELETE|USN_REASON_HARD_LINK_CHANGE|USN_REASON_NAMED_DATA_EXTEND|USN_REASON_NAMED_DATA_OVERWRITE|USN_REASON_NAMED_DATA_TRUNCATION|USN_REASON_RENAME_NEW_NAME|USN_REASON_REPARSE_POINT_CHANGE|USN_REASON_SECURITY_CHANGE|USN_REASON_STREAM_CHANGE;
			data.ReturnOnlyOnClose=0;
			data.Timeout=0;
			data.BytesToWaitFor=0;
			data.UsnJournalID=it->second.journal_id;			

			DWORD read;
			memset(buffer, 0, BUF_LEN);
			BOOL b=DeviceIoControl(it->second.hVolume, FSCTL_READ_USN_JOURNAL, &data, sizeof(READ_USN_JOURNAL_DATA), buffer, BUF_LEN, &read, NULL);
			if(b!=0)
			{
				DWORD dwRetBytes=read-sizeof(USN);

				PUSN_RECORD TUsnRecord = (PUSN_RECORD)(((PUCHAR)buffer) + sizeof(USN));

				if(dwRetBytes>0)
				{
					c=true;
				}
				else if(update_bytes>5000)
				{
					update_bytes=0;
					/*q_update_lastusn->Bind(it->second.last_record);
					q_update_lastusn->Bind(it->first);
					q_update_lastusn->Write();
					q_update_lastusn->Reset();
					startUsn=it->second.last_record;*/
				}

				USN nextUsn=*(USN *)&buffer;

				while(dwRetBytes>0)
				{
					std::string fn;
					fn.resize(TUsnRecord->FileNameLength);
					memcpy(&fn[0], (char*)TUsnRecord->FileName, TUsnRecord->FileNameLength);
					dwRetBytes-=TUsnRecord->RecordLength;
					update_bytes+=TUsnRecord->RecordLength;

					if(!indexing_in_progress)
					{
						UsnInt UsnRecord;
						UsnRecord.Filename=Server->ConvertFromUTF16(fn);
						UsnRecord.FileReferenceNumber=TUsnRecord->FileReferenceNumber;
						UsnRecord.ParentFileReferenceNumber=TUsnRecord->ParentFileReferenceNumber;
						UsnRecord.Reason=TUsnRecord->Reason;
						UsnRecord.Usn=TUsnRecord->Usn;
						UsnRecord.attributes=TUsnRecord->FileAttributes;

						if(UsnRecord.Filename!=L"backup_client.db" && UsnRecord.Filename!=L"backup_client.db-journal")
						{
							if(started_transaction==false)
							{
								started_transaction=true;
								db->BeginTransaction();
							}
							updateWithUsn(it->first, it->second, &UsnRecord);
						}
					}
					else
					{
						if(fn!="backup_client.db" && fn!="backup_client.db-journal")
						{
							if(started_transaction==false)
							{
								started_transaction=true;
								db->BeginTransaction();
							}
							saveJournalData(it->second.journal_id, it->first, TUsnRecord, nextUsn);	
						}
					}

					TUsnRecord = (PUSN_RECORD)(((PCHAR)TUsnRecord) + TUsnRecord->RecordLength);
				}

				it->second.last_record=nextUsn;
			}
			else
			{
				DWORD err=GetLastError();
				if(err==ERROR_JOURNAL_ENTRY_DELETED)
				{
					Server->Log(L"Error for Volume '"+it->first+L"': Journal entry deleted (StartUsn="+convert(data.StartUsn)+L")", LL_ERROR);
					USN_JOURNAL_DATA data;
					DWORD r_bytes;
					BOOL bv=DeviceIoControl(it->second.hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &data, sizeof(USN_JOURNAL_DATA), &r_bytes, NULL);
					std::string deviceInfo;
					if(bv!=FALSE)
					{
						deviceInfo="FirstUsn="+nconvert(data.FirstUsn)
							+" NextUsn="+nconvert(data.NextUsn)
							+" MaximumSize="+nconvert(data.MaximumSize)
							+" AllocationDelta="+nconvert(data.AllocationDelta);
						Server->Log(deviceInfo, LL_INFO);
					}
					if(indexing_in_progress==false)
					{
						if(bv!=FALSE )
						{
							it->second.last_record=data.NextUsn;
							Server->Log(L"Reindexing Volume '"+it->first+L"'", LL_ERROR);
							Server->Log("DeviceInfo: "+deviceInfo, LL_ERROR);
							if(started_transaction)
							{
								started_transaction=false;
								db->EndTransaction();
							}
							reindex(it->second.rid, it->first, &it->second);
							return;
						}
						else if(indexing_in_progress==false)
						{
							Server->Log("Journal Data not acessible. Errorcode: "+nconvert((int)GetLastError())+" deviceInfo: "+deviceInfo, LL_ERROR);
							has_error=true;
							error_dirs.push_back(it->first);
						}
					}
					else
					{
						if(indexing_volume==it->first)
						{
							Server->Log("Access error during indexing. Change journal too small? DeviceInfo: "+deviceInfo, LL_ERROR);
							has_error=true;
							error_dirs.push_back(it->first);
						}
						else
						{
							Server->Log("Journal Data deleted on nonindexing volume. DeviceInfo: "+deviceInfo, LL_ERROR);
							has_error=true;
							error_dirs.push_back(it->first);
							deleteJournalId(it->first);
							CloseHandle(it->second.hVolume);
							remove_it=true;
						}
					}
					listener->On_ResetAll(it->first);
				}
				else
				{
					Server->Log(L"Unknown error for Volume '"+it->first+L"' - update err="+convert((int)err), LL_ERROR);
					listener->On_ResetAll(it->first);
					deleteJournalId(it->first);
					has_error=true;
					CloseHandle(it->second.hVolume);
					remove_it=true;
					error_dirs.push_back(it->first);
				}
			}
		}

		if(startUsn!=it->second.last_record)
		{
			it->second.last_record_update=true;
		}

		if(force_write)
		{
			q_update_lastusn->Bind(it->second.last_record);
			q_update_lastusn->Bind(it->first);
			q_update_lastusn->Write();
			q_update_lastusn->Reset();
		}

		if(remove_it)
		{
			wdirs.erase(it);
			break;
		}
	}

	if(started_transaction)
	{
		db->EndTransaction();
	}
}

void ChangeJournalWatcher::update_longliving(void)
{
	if(!freeze_open_write_files)
	{
		for(std::map<std::wstring, bool>::iterator it=open_write_files.begin();it!=open_write_files.end();++it)
		{
			listener->On_FileModified(it->first, true);
		}
	}
	else
	{
		for(std::map<std::wstring, bool>::iterator it=open_write_files_frozen.begin();it!=open_write_files_frozen.end();++it)
		{
			listener->On_FileModified(it->first, true);
		}
	}
	for(size_t i=0;i<error_dirs.size();++i)
	{
		listener->On_ResetAll(error_dirs[i]);
	}
}

void ChangeJournalWatcher::set_freeze_open_write_files(bool b)
{
	freeze_open_write_files=b;

	if(b)
	{
		open_write_files_frozen=open_write_files;
	}
	else
	{
		open_write_files_frozen.clear();
	}
}

void ChangeJournalWatcher::set_last_backup_time(int64 t)
{
	last_backup_time=t;
}

void ChangeJournalWatcher::logEntry(const std::wstring &vol, const UsnInt *UsnRecord)
{
#define ADD_REASON(x) { if (UsnRecord->Reason & x){ if(!reason.empty()) reason+=L"|"; reason+=L#x; } }
	std::wstring reason;
	ADD_REASON(USN_REASON_DATA_OVERWRITE);
	ADD_REASON(USN_REASON_DATA_EXTEND);
	ADD_REASON(USN_REASON_DATA_TRUNCATION);
	ADD_REASON(USN_REASON_NAMED_DATA_OVERWRITE);
	ADD_REASON(USN_REASON_NAMED_DATA_EXTEND);
	ADD_REASON(USN_REASON_NAMED_DATA_TRUNCATION);
	ADD_REASON(USN_REASON_FILE_CREATE);
	ADD_REASON(USN_REASON_FILE_DELETE);
	ADD_REASON(USN_REASON_EA_CHANGE);
	ADD_REASON(USN_REASON_SECURITY_CHANGE);
	ADD_REASON(USN_REASON_RENAME_OLD_NAME);
	ADD_REASON(USN_REASON_RENAME_NEW_NAME);
	ADD_REASON(USN_REASON_INDEXABLE_CHANGE);
	ADD_REASON(USN_REASON_BASIC_INFO_CHANGE);
	ADD_REASON(USN_REASON_HARD_LINK_CHANGE);
	ADD_REASON(USN_REASON_COMPRESSION_CHANGE);
	ADD_REASON(USN_REASON_ENCRYPTION_CHANGE);
	ADD_REASON(USN_REASON_OBJECT_ID_CHANGE);
	ADD_REASON(USN_REASON_REPARSE_POINT_CHANGE);
	ADD_REASON(USN_REASON_STREAM_CHANGE);
	ADD_REASON(USN_REASON_TRANSACTED_CHANGE);
	ADD_REASON(USN_REASON_CLOSE);
	std::wstring attributes;
#define ADD_ATTRIBUTE(x) { if(UsnRecord->attributes & x){ if(!attributes.empty()) attributes+=L"|"; attributes+=L#x; } }
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_READONLY);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_HIDDEN);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_SYSTEM);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_DIRECTORY);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_ARCHIVE);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_DEVICE);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_NORMAL);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_TEMPORARY);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_SPARSE_FILE);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_REPARSE_POINT);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_COMPRESSED);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_OFFLINE);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_ENCRYPTED);
	ADD_ATTRIBUTE(FILE_ATTRIBUTE_VIRTUAL);
	std::wstring lstr=L"Change: "+vol+L" [fn="+UsnRecord->Filename+L",reason="+reason+L",attributes="+
						attributes+L",USN="+convert(UsnRecord->Usn)+L"]";
	Server->Log(lstr, LL_DEBUG);
}

const DWORD watch_flags=USN_REASON_DATA_EXTEND | USN_REASON_EA_CHANGE | USN_REASON_HARD_LINK_CHANGE | USN_REASON_NAMED_DATA_EXTEND | USN_REASON_NAMED_DATA_OVERWRITE| USN_REASON_NAMED_DATA_TRUNCATION| USN_REASON_REPARSE_POINT_CHANGE| USN_REASON_SECURITY_CHANGE| USN_REASON_STREAM_CHANGE| USN_REASON_DATA_TRUNCATION | USN_REASON_BASIC_INFO_CHANGE | USN_REASON_DATA_OVERWRITE | USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME | USN_REASON_TRANSACTED_CHANGE;

void ChangeJournalWatcher::updateWithUsn(const std::wstring &vol, const SChangeJournal &cj, const UsnInt *UsnRecord)
{
	VLOG(logEntry(vol, UsnRecord));

	_i64 dir_id=hasEntry(cj.rid, UsnRecord->FileReferenceNumber);
	if(dir_id!=-1) //Is a directory
	{
		_i64 parent_id=hasEntry(cj.rid, UsnRecord->ParentFileReferenceNumber);
		if(parent_id==-1)
		{
			Server->Log(L"Error: Parent of "+UsnRecord->Filename+L" not found -1", LL_ERROR);
		}
		else if(UsnRecord->Reason & (USN_REASON_CLOSE | watch_flags ) )
		{
			std::wstring dir_fn=getFilename(UsnRecord->ParentFileReferenceNumber, cj.rid);
			if(dir_fn.empty())
			{
				Server->Log(L"Error: Path of "+UsnRecord->Filename+L" not found -2", LL_ERROR);
			}
			else
			{
				if(UsnRecord->Reason & USN_REASON_RENAME_NEW_NAME )
				{
					renameEntry(UsnRecord->Filename, dir_id, UsnRecord->ParentFileReferenceNumber);
				}
				else if(UsnRecord->Reason & USN_REASON_FILE_DELETE)
				{
					if(UsnRecord->attributes & FILE_ATTRIBUTE_DIRECTORY )
					{
						listener->On_DirRemoved(dir_fn+UsnRecord->Filename);
					}
					deleteWithChildren(UsnRecord->FileReferenceNumber, cj.rid);
				}

				if(UsnRecord->Reason & watch_flags )
				{
					listener->On_FileModified(dir_fn+UsnRecord->Filename, false );
				}
			}
		}
		else if(UsnRecord->Reason & USN_REASON_RENAME_OLD_NAME )
		{
			std::wstring dir_fn=getFilename(UsnRecord->ParentFileReferenceNumber, cj.rid);
			if(dir_fn.empty())
			{
				Server->Log(L"Error: Path of "+UsnRecord->Filename+L" not found -3", LL_ERROR);
			}
			else
			{
				if(UsnRecord->attributes & FILE_ATTRIBUTE_DIRECTORY )
				{
					listener->On_DirRemoved(dir_fn+UsnRecord->Filename);
				}
				listener->On_FileModified(dir_fn+UsnRecord->Filename, false);
			}
		}
	}
	else //Is a file
	{
		_i64 parent_id=hasEntry(cj.rid, UsnRecord->ParentFileReferenceNumber);
		if(parent_id==-1)
		{
			Server->Log(L"Error: Parent of file "+UsnRecord->Filename+L" not found -4", LL_ERROR);
		}
		else
		{
			std::wstring dir_fn=getFilename(UsnRecord->ParentFileReferenceNumber, cj.rid);
			std::wstring real_fn=dir_fn+UsnRecord->Filename;
			
			if( UsnRecord->attributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				if((UsnRecord->Reason & USN_REASON_FILE_CREATE) && (UsnRecord->Reason & USN_REASON_CLOSE) )
				{
					addFrn(UsnRecord->Filename, UsnRecord->ParentFileReferenceNumber, UsnRecord->FileReferenceNumber, cj.rid);
				}
			}
			else
			{
				if(UsnRecord->Reason & USN_REASON_CLOSE)
				{
					std::map<std::wstring, bool>::iterator it=open_write_files.find(real_fn);
					if(it!=open_write_files.end())
					{
						open_write_files.erase(it);
					}
				}
				else if(UsnRecord->Reason & watch_flags)
				{
					open_write_files[real_fn]=true;
					
					if(freeze_open_write_files)
					{
						open_write_files_frozen[real_fn]=true;
					}
				}
			}

			if( (UsnRecord->Reason & USN_REASON_RENAME_OLD_NAME)
				|| (UsnRecord->Reason & watch_flags) )
			{
				bool save_fn=false;
				if( (UsnRecord->Reason & USN_REASON_BASIC_INFO_CHANGE) && (UsnRecord->attributes & FILE_ATTRIBUTE_DIRECTORY)==0 )
				{
					save_fn=true;
				}
				else if( ( UsnRecord->Reason & USN_REASON_DATA_OVERWRITE ) &&
					     !( (UsnRecord->Reason & USN_REASON_DATA_EXTEND) || (UsnRecord->Reason & USN_REASON_DATA_TRUNCATION) ) )
				{
					WIN32_FILE_ATTRIBUTE_DATA fad;
					if(GetFileAttributesExW(os_file_prefix(real_fn).c_str(), GetFileExInfoStandard, &fad) )
					{
						int64 last_mod_time = static_cast<__int64>(fad.ftLastWriteTime.dwHighDateTime) << 32 | fad.ftLastWriteTime.dwLowDateTime;
						if(last_mod_time<=last_backup_time || last_backup_time==0)
						{
							save_fn=true;
						}
					}
				}
				listener->On_FileModified(real_fn, save_fn);
			}
		}
	}
}