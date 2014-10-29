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

namespace usn
{
	typedef struct {
		DWORD         RecordLength;
		WORD          MajorVersion;
		WORD          MinorVersion;
		BYTE          FileReferenceNumber[16];
		BYTE          ParentFileReferenceNumber[16];
		USN           Usn;
		LARGE_INTEGER TimeStamp;
		DWORD         Reason;
		DWORD         SourceInfo;
		DWORD         SecurityId;
		DWORD         FileAttributes;
		WORD          FileNameLength;
		WORD          FileNameOffset;
		WCHAR         FileName[1];
	} USN_RECORD_V3, *PUSN_RECORD_V3;

	typedef struct {
		DWORDLONG StartFileReferenceNumber;
		USN       LowUsn;
		USN       HighUsn;
		WORD      MinMajorVersion;
		WORD      MaxMajorVersion;
	} MFT_ENUM_DATA_V1, *PMFT_ENUM_DATA_V1;

	UsnInt get_usn_record(PUSN_RECORD usn_record)
	{
		if(usn_record->MajorVersion==2)
		{
			std::wstring filename;
			filename.resize(usn_record->FileNameLength / sizeof(wchar_t) );
			memcpy(&filename[0], (PBYTE) usn_record + usn_record->FileNameOffset, usn_record->FileNameLength);

			UsnInt ret = {
				2,
				uint128(usn_record->FileReferenceNumber),
				uint128(usn_record->ParentFileReferenceNumber),
				usn_record->Usn,
				usn_record->Reason,
				filename,
				0,
				usn_record->FileAttributes
			};

			return ret;
		}
		else if(usn_record->MajorVersion==3)
		{
			PUSN_RECORD_V3 usn_record_v3 = (PUSN_RECORD_V3)usn_record;
			std::wstring filename;
			filename.resize(usn_record_v3->FileNameLength / sizeof(wchar_t) );
			memcpy(&filename[0], (PBYTE) usn_record_v3 + usn_record_v3->FileNameOffset, usn_record_v3->FileNameLength);

			UsnInt ret = {
				3,
				uint128(usn_record->FileReferenceNumber),
				uint128(usn_record->ParentFileReferenceNumber),
				usn_record->Usn,
				usn_record->Reason,
				filename,
				0,
				usn_record->FileAttributes
			};

			return ret;
		}
		else
		{
			assert("USN record does not have major version 2 or 3");
		}

		return UsnInt();
	}

	bool enum_usn_data(HANDLE hVolume, DWORDLONG StartFileReferenceNumber, BYTE* pData, DWORD dataSize, DWORD& firstError, DWORD& version, DWORD& cb)
	{
		if(version==0)
		{
			MFT_ENUM_DATA med;
			med.StartFileReferenceNumber = StartFileReferenceNumber;
			med.LowUsn = 0;
			med.HighUsn = MAXLONGLONG;

			BOOL b = DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA, &med, sizeof(med), pData, dataSize, &cb, NULL);

			if(b==FALSE)
			{
				firstError=GetLastError();
				version=1;
				return enum_usn_data(hVolume, StartFileReferenceNumber, pData, dataSize, firstError, version, cb);
			}

			return true;
		}
		else if(version==1)
		{
			usn::MFT_ENUM_DATA_V1 med_v1;
			med_v1.StartFileReferenceNumber = StartFileReferenceNumber;
			med_v1.LowUsn = 0;
			med_v1.HighUsn = MAXLONGLONG;

			BOOL b = DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA, &med_v1, sizeof(med_v1), pData, dataSize, &cb, NULL);

			return b!=FALSE;
		}
		else
		{
			assert(false);
			return FALSE;
		}
	}

	uint64 atoiu64(const std::wstring& str)
	{
		return static_cast<uint64>(watoi64(str));
	}
}

const unsigned int usn_update_freq=10*60*1000;
const DWORDLONG usn_reindex_num=1000000; // one million

//#define MFT_ON_DEMAND_LOOKUP
#define VLOG(x) x

ChangeJournalWatcher::ChangeJournalWatcher(DirectoryWatcherThread * dwt, IDatabase *pDB, IChangeJournalListener *pListener)
	: dwt(dwt), listener(pListener), db(pDB), last_backup_time(0), unsupported_usn_version_err(false)
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
	q_add_frn=db->Prepare("INSERT INTO map_frn (name, pid, pid_high, frn, frn_high, rid) VALUES (?, ?, ?, ?)");
	q_reset_root=db->Prepare("DELETE FROM map_frn WHERE rid=?");
	q_get_entry=db->Prepare("SELECT id FROM map_frn WHERE frn=? AND frn_high=? AND rid=?");
	q_get_children=db->Prepare("SELECT id, frn, frn_high FROM map_frn WHERE pid=? AND pid_high=? AND rid=?");
	q_del_entry=db->Prepare("DELETE FROM map_frn WHERE id=?");
	q_get_name=db->Prepare("SELECT name, pid, pid_high FROM map_frn WHERE frn=? AND frn_high=? AND rid=?");
	q_add_journal=db->Prepare("INSERT INTO journal_ids (journal_id, device_name, last_record, index_done) VALUES (?,?,?,0)");
	q_update_journal_id=db->Prepare("UPDATE journal_ids SET journal_id=? WHERE device_name=?");
	q_update_lastusn=db->Prepare("UPDATE journal_ids SET last_record=? WHERE device_name=?");
	q_rename_entry=db->Prepare("UPDATE map_frn SET name=?, pid=?, pid_high=? WHERE id=?");
	q_save_journal_data=db->Prepare("INSERT INTO journal_data (device_name, journal_id, usn, reason, filename, frn, frn_high, parent_frn, parent_frn_high, next_usn, attributes) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
	q_get_journal_data=db->Prepare("SELECT usn, reason, filename, frn, frn_high, parent_frn, parent_frn_high, next_usn FROM journal_data WHERE device_name=? ORDER BY usn ASC");
	q_set_index_done=db->Prepare("UPDATE journal_ids SET index_done=? WHERE device_name=?");
	q_del_journal_data=db->Prepare("DELETE FROM journal_data WHERE device_name=?");
	q_del_entry_frn=db->Prepare("DELETE FROM map_frn WHERE frn=? AND frn_high=? AND rid=?");
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

void ChangeJournalWatcher::saveJournalData(DWORDLONG journal_id, const std::wstring &vol, const UsnInt& rec, USN nextUsn)
{	
	if(rec.Filename==L"backup_client.db" || rec.Filename==L"backup_client.db-journal" )
		return;

	q_save_journal_data->Bind(vol);
	q_save_journal_data->Bind((_i64)journal_id);
	q_save_journal_data->Bind((_i64)rec.Usn);
	q_save_journal_data->Bind((_i64)rec.Reason);
	q_save_journal_data->Bind(rec.Filename);
	q_save_journal_data->Bind((_i64)rec.FileReferenceNumber.lowPart);
	q_save_journal_data->Bind((_i64)rec.FileReferenceNumber.highPart);
	q_save_journal_data->Bind((_i64)rec.ParentFileReferenceNumber.lowPart);
	q_save_journal_data->Bind((_i64)rec.ParentFileReferenceNumber.highPart);
	q_save_journal_data->Bind(nextUsn);
	q_save_journal_data->Bind((_i64)rec.attributes);
	
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
		rec.Usn=usn::atoiu64(res[i][L"usn"]);
		rec.Reason=(DWORD)usn::atoiu64(res[i][L"reason"]);
		rec.Filename=res[i][L"filename"];
		rec.FileReferenceNumber.set(usn::atoiu64(res[i][L"frn"]),
			usn::atoiu64(res[i][L"frn_high"]));
		rec.ParentFileReferenceNumber.set(usn::atoiu64(res[i][L"parent_frn"]),
			usn::atoiu64(res[i][L"parent_frn_high"]));
		rec.NextUsn=usn::atoiu64(res[i][L"next_usn"]);
		rec.attributes=(DWORD)usn::atoiu64(res[i][L"attributes"]);
		ret.push_back(rec);
	}
	return ret;
}

void ChangeJournalWatcher::renameEntry(const std::wstring &name, _i64 id, uint128 pid)
{
	q_rename_entry->Bind(name);
	q_rename_entry->Bind((_i64)pid.lowPart);
	q_rename_entry->Bind((_i64)pid.highPart);
	q_rename_entry->Bind(id);
	q_rename_entry->Write();
	q_rename_entry->Reset();
}

std::vector<uint128 > ChangeJournalWatcher::getChildren(uint128 frn, _i64 rid)
{
	std::vector<uint128> ret;
	q_get_children->Bind((_i64)frn.lowPart);
	q_get_children->Bind((_i64)frn.highPart);
	q_get_children->Bind(rid);
	db_results res=q_get_children->Read();
	q_get_children->Reset();
	for(size_t i=0;i<res.size();++i)
	{
		ret.push_back(uint128(usn::atoiu64(res[i][L"frn"]),
			usn::atoiu64(res[i][L"frn_high"])));
	}
	return ret;
}

void ChangeJournalWatcher::deleteEntry(_i64 id)
{
	q_del_entry->Bind(id);
	q_del_entry->Write();
	q_del_entry->Reset();
}

void ChangeJournalWatcher::deleteEntry(uint128 frn, _i64 rid)
{
	q_del_entry_frn->Bind(static_cast<_i64>(frn.lowPart));
	q_del_entry_frn->Bind(static_cast<_i64>(frn.highPart));
	q_del_entry_frn->Bind(rid);
	q_del_entry_frn->Write();
	q_del_entry_frn->Reset();
}

_i64 ChangeJournalWatcher::hasEntry( _i64 rid, uint128 frn)
{
	q_get_entry->Bind(static_cast<_i64>(frn.lowPart));
	q_get_entry->Bind(static_cast<_i64>(frn.highPart));
	q_get_entry->Bind(rid);
	db_results res=q_get_entry->Read();
	q_get_entry->Reset();
	if(res.empty())
		return -1;
	else
		return usn::atoiu64(res[0][L"id"]);
}

void ChangeJournalWatcher::resetRoot(_i64 rid)
{
	q_reset_root->Bind(rid);
	q_reset_root->Write();
	q_reset_root->Reset();
}

int64 ChangeJournalWatcher::addFrn(const std::wstring &name, uint128 parent_id, uint128 frn, _i64 rid)
{
	q_add_frn->Bind(name);
	q_add_frn->Bind(static_cast<_i64>(parent_id.lowPart));
	q_add_frn->Bind(static_cast<_i64>(parent_id.highPart));
	q_add_frn->Bind(static_cast<_i64>(frn.lowPart));
	q_add_frn->Bind(static_cast<_i64>(frn.highPart));
	q_add_frn->Bind(rid);
	q_add_frn->Write();
	q_add_frn->Reset();
	return db->getLastInsertID();
}

void ChangeJournalWatcher::addFrnTmp(const std::wstring &name, uint128 parent_id, uint128 frn, _i64 rid)
{
	q_add_frn_tmp->Bind(name);
	q_add_frn_tmp->Bind(static_cast<_i64>(parent_id.lowPart));
	q_add_frn_tmp->Bind(static_cast<_i64>(parent_id.highPart));
	q_add_frn_tmp->Bind(static_cast<_i64>(frn.lowPart));
	q_add_frn_tmp->Bind(static_cast<_i64>(frn.highPart));
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
		return usn::atoiu64(res[0][L"id"]);
	}
}

void ChangeJournalWatcher::deleteWithChildren(uint128 frn, _i64 rid)
{
	std::vector<uint128> children=getChildren(frn, rid);
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
		rid=addFrn(vol, frn_root, frn_root, -1);
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
#ifndef MFT_ON_DEMAND_LOOKUP
	Server->Log("Starting indexing process..", LL_DEBUG);
	indexRootDirs2(vol, sj);
#endif
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
	db->Write("CREATE TEMPORARY TABLE map_frn_tmp (name TEXT, pid INTEGER, pid_high INTEGER, frn INTEGER, frn_high INTEGER, rid INTEGER)");
	q_add_frn_tmp=db->Prepare("INSERT INTO map_frn_tmp (name, pid, pid_high, frn, frn_high, rid) VALUES (?, ?, ?, ?, ?, ?)", false);

	int64 root_frn = getRootFRN(root);

	addFrn(root, -1, root_frn, sj->rid);

	USN StartFileReferenceNumber=0;
	BYTE *pData=new BYTE[sizeof(DWORDLONG) + 0x10000];
	DWORD version = 0;
	DWORD cb;
	size_t nDirFRNs=0;
	size_t nFRNs=0;
	bool has_warning=false;
	DWORD firstError;
	while (usn::enum_usn_data(sj->hVolume, StartFileReferenceNumber, pData, sizeof(DWORDLONG) + 0x10000, firstError, version, cb))
	{
		if(indexing_in_progress)
		{
			if(Server->getTimeMS()-last_index_update>10000)
			{
				Server->Log("Saving new journal data to database...", LL_DEBUG);
				update(sj->vol_str);
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
			if(pRecord->MajorVersion!=2 && pRecord->MajorVersion!=3 && !has_warning)
			{
				Server->Log("Journal entry with major version "+nconvert(pRecord->MajorVersion)+" not supported.", LL_WARNING);
				has_warning=true;
				break;
			}
			else
			{
				UsnInt usn_record = usn::get_usn_record(pRecord);
				if((usn_record.attributes & FILE_ATTRIBUTE_DIRECTORY)!=0)
				{
					addFrnTmp(usn_record.Filename, usn_record.ParentFileReferenceNumber, usn_record.FileReferenceNumber, sj->rid);
					++nDirFRNs;
				}
				++nFRNs;
				pRecord = (PUSN_RECORD) ((PBYTE) pRecord + pRecord->RecordLength);
			}
		}
		StartFileReferenceNumber = * (DWORDLONG *) pData;
	}

	Server->Log("Added "+nconvert(nDirFRNs)+" directory FRNs to temporary database...", LL_DEBUG);
	Server->Log("MFT has "+nconvert(nFRNs)+" FRNs.", LL_DEBUG);

	delete []pData;
	db->destroyQuery(q_add_frn_tmp);
	q_add_frn_tmp=NULL;
	Server->Log("Copying directory FRNs to database...", LL_DEBUG);
	db->Write("INSERT INTO map_frn (name, pid, pid_high, frn, frn_high, rid) SELECT name, pid, pid_high, frn, frn_high, rid FROM map_frn_tmp");
	
	Server->Log("Dropping temporary database...", LL_DEBUG);
	db->Write("DROP TABLE map_frn_tmp");

	Server->Log("Saving new journal data to database. Forcing update...", LL_DEBUG);
	update(sj->vol_str);
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
		r.journal_id=usn::atoiu64(res[0][L"journal_id"]);
		r.last_record=usn::atoiu64(res[0][L"last_record"]);
		r.index_done=watoi(res[0][L"index_done"])>0;
	}
	return r;
}

std::wstring ChangeJournalWatcher::getFilename(const SChangeJournal &cj, uint128 frn,
	bool fallback_to_mft, bool& filter_error, bool& has_error)
{
	std::wstring path;
	uint128 curr_id=frn;
	while(true)
	{
		q_get_name->Bind(static_cast<_i64>(curr_id.lowPart));
		q_get_name->Bind(static_cast<_i64>(curr_id.highPart));
		q_get_name->Bind(cj.rid);
		db_results res=q_get_name->Read();
		q_get_name->Reset();

		if(!res.empty())
		{
			uint128 pid(usn::atoiu64(res[0][L"pid"]),
				usn::atoiu64(res[0][L"pid_high"]));
			if(pid!=frn_root)
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
				if(fallback_to_mft)
				{
					Server->Log(L"Couldn't follow up to root via Database. Falling back to MFT. Current path: "+path, LL_WARNING);

					uint128 parent_frn;
					has_error=false;
					std::wstring dirname = getNameFromMFTByFRN(cj, curr_id, parent_frn, has_error);
					if(!dirname.empty())
					{
						path = dirname + os_file_sep() + path;
						addFrn(dirname, parent_frn, curr_id, cj.rid);
						curr_id = parent_frn;
					}
					else if(!has_error)
					{
						Server->Log(L"Could not follow up to root. Current path: "+path+L". Lookup in MFT failed. Directory was probably deleted.", LL_WARNING);
						return std::wstring();
					}
					else
					{
						Server->Log(L"Could not follow up to root. Current path: "+path+L". Lookup in MFT failed.", LL_ERROR);
						has_error=true;
						return std::wstring();
					}

					if(curr_id==-1)
					{
						break;
					}
				}
				else
				{
					Server->Log(L"Couldn't follow up to root. Current path: "+path, LL_ERROR);
					return std::wstring();
				}
			}
			else
			{
				filter_error=true;
				return std::wstring();
			}
		}
	}
	return path;
}

const int BUF_LEN=4096;


void ChangeJournalWatcher::update(std::wstring vol_str)
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
					updateWithUsn(it->first, it->second, &jd[i], true);
					it->second.last_record=jd[i].NextUsn;
				}
				Server->Log("Deleting saved journal data...", LL_DEBUG);
				deleteJournalData(it->first);

				db->EndTransaction();
				started_transaction=false;
			}
		}

		USN startUsn=it->second.last_record;

		bool remove_it=false;
		bool c=true;
		while(c)
		{
			c=false;
			READ_USN_JOURNAL_DATA data;
			data.StartUsn=it->second.last_record;
			data.ReasonMask=0xFFFFFFFF;
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

				USN nextUsn=*(USN *)&buffer;

				while(dwRetBytes>0)
				{
					if(TUsnRecord->MajorVersion!=2 && TUsnRecord->MajorVersion!=3)
					{
						if(!unsupported_usn_version_err)
						{
							Server->Log("USN record with major version "+nconvert(TUsnRecord->MajorVersion)+" not supported", LL_ERROR);
							listener->On_ResetAll(it->first);
							unsupported_usn_version_err=true;
						}
					}
					else
					{
						UsnInt usn_record = usn::get_usn_record(TUsnRecord);

						dwRetBytes-=TUsnRecord->RecordLength;

						if(!indexing_in_progress)
						{
							if(usn_record.Filename!=L"backup_client.db" &&
								usn_record.Filename!=L"backup_client.db-journal")
							{
								if(!started_transaction)
								{
									started_transaction=true;
									db->BeginTransaction();
								}
								updateWithUsn(it->first, it->second, &usn_record, true);
							}
						}
						else
						{
							if(usn_record.Filename!=L"backup_client.db" &&
								usn_record.Filename!=L"backup_client.db-journal")
							{
								if(started_transaction==false)
								{
									started_transaction=true;
									db->BeginTransaction();
								}
								saveJournalData(it->second.journal_id, it->first, usn_record, nextUsn);	
							}
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

		if(startUsn!=it->second.last_record &&
			started_transaction)
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
						attributes+L",USN="+convert(UsnRecord->Usn)+L",FRN="+convert(UsnRecord->FileReferenceNumber.lowPart)+
						L",Parent FRN="+convert(UsnRecord->ParentFileReferenceNumber.lowPart)+L",Version="+convert((int)UsnRecord->version)+L"]";
	Server->Log(lstr, LL_DEBUG);
}

const DWORD watch_flags=USN_REASON_DATA_EXTEND | USN_REASON_EA_CHANGE | USN_REASON_HARD_LINK_CHANGE | USN_REASON_NAMED_DATA_EXTEND | USN_REASON_NAMED_DATA_OVERWRITE| USN_REASON_NAMED_DATA_TRUNCATION| USN_REASON_REPARSE_POINT_CHANGE| USN_REASON_SECURITY_CHANGE| USN_REASON_STREAM_CHANGE| USN_REASON_DATA_TRUNCATION | USN_REASON_BASIC_INFO_CHANGE | USN_REASON_DATA_OVERWRITE | USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME | USN_REASON_TRANSACTED_CHANGE;

void ChangeJournalWatcher::updateWithUsn(const std::wstring &vol, const SChangeJournal &cj, const UsnInt *UsnRecord, bool fallback_to_mft)
{
	VLOG(logEntry(vol, UsnRecord));

	bool curr_has_error = false;

	_i64 dir_id=hasEntry(cj.rid, UsnRecord->FileReferenceNumber);

	if(dir_id==-1 && (UsnRecord->attributes & FILE_ATTRIBUTE_DIRECTORY) 
		&& !(UsnRecord->Reason & USN_REASON_FILE_CREATE)
		&& fallback_to_mft)
	{
		Server->Log(L"File entry with FRN "+convert(UsnRecord->FileReferenceNumber.lowPart)+L" (Name \""+UsnRecord->Filename+L"\") is a directory not being created, but not in database. Added it to database", LL_WARNING);
		dir_id=addFrn(UsnRecord->Filename, UsnRecord->ParentFileReferenceNumber, UsnRecord->FileReferenceNumber, cj.rid);
	}
	
	if(dir_id!=-1) //Is a directory
	{
		_i64 parent_id=hasEntry(cj.rid, UsnRecord->ParentFileReferenceNumber);
		if(parent_id==-1)
		{
			if(fallback_to_mft)
			{
				Server->Log(L"Parent of directory with FRN "+convert(UsnRecord->FileReferenceNumber.lowPart)+L" (Name \""+UsnRecord->Filename+L"\") with FRN "+convert(UsnRecord->ParentFileReferenceNumber.lowPart)+L" not found. Searching via MFT as fallback.", LL_WARNING);
				uint128 parent_parent_frn;
				bool has_error=false;
				std::wstring parent_name = getNameFromMFTByFRN(cj, UsnRecord->ParentFileReferenceNumber, parent_parent_frn, has_error);
				if(parent_name.empty())
				{
					Server->Log("Parent not found. Was probably deleted.", LL_WARNING);
					curr_has_error = true;
				}
				else
				{
					addFrn(parent_name, parent_parent_frn, UsnRecord->ParentFileReferenceNumber, cj.rid);
					updateWithUsn(vol, cj, UsnRecord, false);
				}
			}
			else
			{
				Server->Log(L"Error: Parent of \""+UsnRecord->Filename+L"\" not found -1", LL_ERROR);
				curr_has_error=true;
			}
		}
		else if(UsnRecord->Reason & (USN_REASON_CLOSE | watch_flags ) )
		{
			bool filter_error = false;
			bool has_error = false;
			std::wstring dir_fn=getFilename(cj, UsnRecord->ParentFileReferenceNumber, true, filter_error, has_error);
			if(dir_fn.empty())
			{
				if(!filter_error || has_error)
				{
					Server->Log(L"Error: Path of "+UsnRecord->Filename+L" not found -2", LL_ERROR);
					curr_has_error = true;
				}
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
			bool filter_error = false;
			bool has_error = false;
			std::wstring dir_fn=getFilename(cj, UsnRecord->ParentFileReferenceNumber, true, filter_error, has_error);
			if(dir_fn.empty())
			{
				if(!filter_error || has_error)
				{
					Server->Log(L"Error: Path of "+UsnRecord->Filename+L" not found -3", LL_ERROR);
					curr_has_error = true;
				}
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
			if(fallback_to_mft)
			{
				Server->Log(L"Parent of file with FRN "+convert(UsnRecord->FileReferenceNumber.lowPart)+L" (Name \""+UsnRecord->Filename+L"\") with FRN "+convert(UsnRecord->ParentFileReferenceNumber.lowPart)+L" not found. Searching via MFT as fallback.", LL_WARNING);
				uint128 parent_parent_frn;
				bool has_error=false;
				std::wstring parent_name = getNameFromMFTByFRN(cj, UsnRecord->ParentFileReferenceNumber, parent_parent_frn, has_error);
				if(parent_name.empty())
				{
					if(!has_error)
					{
						Server->Log("Parent directory not found in MFT. Was probably deleted.", LL_WARNING);
					}
					else
					{
						Server->Log("Parent directory not found in MFT.", LL_ERROR);
						curr_has_error = true;
					}
				}
				else
				{
					addFrn(parent_name, parent_parent_frn, UsnRecord->ParentFileReferenceNumber, cj.rid);
					updateWithUsn(vol, cj, UsnRecord, false);
				}
			}
			else
			{
				Server->Log(L"Error: Parent of file "+UsnRecord->Filename+L" not found -4", LL_ERROR);
				curr_has_error = true;
			}
		}
		else
		{
			bool filter_error = false;
			bool has_error = false;
			std::wstring dir_fn=getFilename(cj, UsnRecord->ParentFileReferenceNumber, true, filter_error, has_error);

			if(dir_fn.empty())
			{
				if(!filter_error || has_error)
				{
					Server->Log(L"Error: Path of file \""+UsnRecord->Filename+L"\" not found -3", LL_ERROR);
					curr_has_error = true;
				}
			}
			else
			{
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
					if( ( UsnRecord->Reason & USN_REASON_DATA_OVERWRITE || UsnRecord->Reason & USN_REASON_RENAME_NEW_NAME) &&
							 !( (UsnRecord->Reason & USN_REASON_DATA_EXTEND) || (UsnRecord->Reason & USN_REASON_DATA_TRUNCATION) ) )
					{
						save_fn=true;
					}

					if(save_fn)
					{
						save_fn=false;

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

	if(curr_has_error)
	{
		listener->On_ResetAll(cj.vol_str);
	}
}

std::wstring ChangeJournalWatcher::getNameFromMFTByFRN(const SChangeJournal &cj, uint128 frn, uint128& parent_frn, bool& has_error)
{
	MFT_ENUM_DATA med;
	med.StartFileReferenceNumber = frn.lowPart;
	med.LowUsn = 0;
	med.HighUsn = MAXLONGLONG;

	BYTE pData[16*1024];
	DWORD cb;
	size_t nDirFRNs=0;
	size_t nFRNs=0;
	DWORD firstError;
	DWORD version = 0;

	if(usn::enum_usn_data(cj.hVolume, frn.lowPart, pData, sizeof(pData), firstError, version, cb))
	{
		PUSN_RECORD pRecord = (PUSN_RECORD) &pData[sizeof(USN)];

		if(pRecord->MajorVersion!=2 && pRecord->MajorVersion!=3)
		{
			Server->Log(L"Getting name by FRN from MFT for volume "+cj.vol_str+L" returned USN record with major version "+convert(pRecord->MajorVersion)+L". This version is not supported.", LL_ERROR);
			parent_frn=-1;
			return std::wstring();
		}

		UsnInt usn_record = usn::get_usn_record(pRecord);

		if(usn_record.FileReferenceNumber!=frn)
		{
			if(frn == getRootFRN(cj.vol_str))
			{
				parent_frn=-1;
				return cj.vol_str;
			}
			
			return std::wstring();
		}
		else
		{
			parent_frn = usn_record.ParentFileReferenceNumber;
			return usn_record.Filename;
		}
	}
	else
	{
		Server->Log(L"Getting name by FRN from MFT failed for volume "+cj.vol_str+L" with error code "+convert((int)GetLastError())+L" and first error "+convert((int)firstError), LL_ERROR);
		has_error=true;
		return std::wstring();
	}
}

int64 ChangeJournalWatcher::getRootFRN( const std::wstring & root )
{
	HANDLE hDir = CreateFile((root+os_file_sep()).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,	NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if(hDir==INVALID_HANDLE_VALUE)
	{
		Server->Log("Could not open root HANDLE.", LL_ERROR);
		return -1;
	}

	BY_HANDLE_FILE_INFORMATION fi;
	GetFileInformationByHandle(hDir, &fi);
	CloseHandle(hDir);
	LARGE_INTEGER frn;
	frn.LowPart=fi.nFileIndexLow;
	frn.HighPart=fi.nFileIndexHigh;

	return frn.QuadPart;
}
