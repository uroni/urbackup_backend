#include "JournalDAO.h"
#include "../../stringtools.h"


JournalDAO::JournalDAO( IDatabase *pDB )
	: db(pDB)
{
	prepareQueries();
}


JournalDAO::~JournalDAO()
{
	destroyQueries();
}



//@-SQLGenSetup
void JournalDAO::prepareQueries()
{
	q_getDeviceInfo=NULL;
	q_getRootId=NULL;
	q_addFrn=NULL;
	q_resetRoot=NULL;
	q_getFrnEntryId=NULL;
	q_getFrnChildren=NULL;
	q_delFrnEntry=NULL;
	q_getNameAndPid=NULL;
	q_insertJournal=NULL;
	q_updateJournalId=NULL;
	q_updateJournalLastUsn=NULL;
	q_updateFrnNameAndPid=NULL;
	q_insertJournalData=NULL;
	q_getJournalData=NULL;
	q_updateSetJournalIndexDone=NULL;
	q_delJournalData=NULL;
	q_delFrnEntryViaFrn=NULL;
	q_delJournalDeviceId=NULL;
}

//@-SQLGenDestruction
void JournalDAO::destroyQueries()
{
	db->destroyQuery(q_getDeviceInfo);
	db->destroyQuery(q_getRootId);
	db->destroyQuery(q_addFrn);
	db->destroyQuery(q_resetRoot);
	db->destroyQuery(q_getFrnEntryId);
	db->destroyQuery(q_getFrnChildren);
	db->destroyQuery(q_delFrnEntry);
	db->destroyQuery(q_getNameAndPid);
	db->destroyQuery(q_insertJournal);
	db->destroyQuery(q_updateJournalId);
	db->destroyQuery(q_updateJournalLastUsn);
	db->destroyQuery(q_updateFrnNameAndPid);
	db->destroyQuery(q_insertJournalData);
	db->destroyQuery(q_getJournalData);
	db->destroyQuery(q_updateSetJournalIndexDone);
	db->destroyQuery(q_delJournalData);
	db->destroyQuery(q_delFrnEntryViaFrn);
	db->destroyQuery(q_delJournalDeviceId);
}

/**
* @-SQLGenAccess
* @func SDeviceInfo JournalDAO::getDeviceInfo
* @return int64 journal_id, int64 last_record, int index_done
* @sql
*    SELECT journal_id, last_record, index_done
*	 FROM journal_ids WHERE device_name=:device_name(string)
*/
JournalDAO::SDeviceInfo JournalDAO::getDeviceInfo(const std::wstring& device_name)
{
	if(q_getDeviceInfo==NULL)
	{
		q_getDeviceInfo=db->Prepare("SELECT journal_id, last_record, index_done FROM journal_ids WHERE device_name=?", false);
	}
	q_getDeviceInfo->Bind(device_name);
	db_results res=q_getDeviceInfo->Read();
	q_getDeviceInfo->Reset();
	SDeviceInfo ret = { false, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.journal_id=watoi64(res[0][L"journal_id"]);
		ret.last_record=watoi64(res[0][L"last_record"]);
		ret.index_done=watoi(res[0][L"index_done"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SDeviceInfo JournalDAO::getRootId
* @return int64 id
* @sql
*    SELECT id FROM map_frn WHERE rid=-1 AND name=:name(string)
*/
JournalDAO::CondInt64 JournalDAO::getRootId(const std::wstring& name)
{
	if(q_getRootId==NULL)
	{
		q_getRootId=db->Prepare("SELECT id FROM map_frn WHERE rid=-1 AND name=?", false);
	}
	q_getRootId->Bind(name);
	db_results res=q_getRootId->Read();
	q_getRootId->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void JournalDAO::addFrn
* @sql
*    INSERT INTO map_frn (name, pid, pid_high, frn, frn_high, rid)
*    VALUES (:name(string), :pid(int64), :pid_high(int64), :frn(int64), :frn_high(int64),
*            :rid(int64) )
*/
void JournalDAO::addFrn(const std::wstring& name, int64 pid, int64 pid_high, int64 frn, int64 frn_high, int64 rid)
{
	if(q_addFrn==NULL)
	{
		q_addFrn=db->Prepare("INSERT INTO map_frn (name, pid, pid_high, frn, frn_high, rid) VALUES (?, ?, ?, ?, ?, ? )", false);
	}
	q_addFrn->Bind(name);
	q_addFrn->Bind(pid);
	q_addFrn->Bind(pid_high);
	q_addFrn->Bind(frn);
	q_addFrn->Bind(frn_high);
	q_addFrn->Bind(rid);
	q_addFrn->Write();
	q_addFrn->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::resetRoot
* @sql
*    DELETE FROM map_frn WHERE rid=:rid(int64)
*/
void JournalDAO::resetRoot(int64 rid)
{
	if(q_resetRoot==NULL)
	{
		q_resetRoot=db->Prepare("DELETE FROM map_frn WHERE rid=?", false);
	}
	q_resetRoot->Bind(rid);
	q_resetRoot->Write();
	q_resetRoot->Reset();
}

/**
* @-SQLGenAccess
* @func int64 JournalDAO::getFrnEntryId
* @return int64 id
* @sql
*    SELECT id FROM map_frn WHERE frn=:frn(int64) AND frn_high=:frn_high(int64) AND rid=:rid(int64)
*/
JournalDAO::CondInt64 JournalDAO::getFrnEntryId(int64 frn, int64 frn_high, int64 rid)
{
	if(q_getFrnEntryId==NULL)
	{
		q_getFrnEntryId=db->Prepare("SELECT id FROM map_frn WHERE frn=? AND frn_high=? AND rid=?", false);
	}
	q_getFrnEntryId->Bind(frn);
	q_getFrnEntryId->Bind(frn_high);
	q_getFrnEntryId->Bind(rid);
	db_results res=q_getFrnEntryId->Read();
	q_getFrnEntryId->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SFrn> JournalDAO::getFrnChildren
* @return int64 frn, int64 frn_high
* @sql
*    SELECT frn, frn_high FROM map_frn WHERE pid=:pid(int64) AND pid_high=:pid_high(int64) AND rid=:rid(int64)
*/
std::vector<JournalDAO::SFrn> JournalDAO::getFrnChildren(int64 pid, int64 pid_high, int64 rid)
{
	if(q_getFrnChildren==NULL)
	{
		q_getFrnChildren=db->Prepare("SELECT frn, frn_high FROM map_frn WHERE pid=? AND pid_high=? AND rid=?", false);
	}
	q_getFrnChildren->Bind(pid);
	q_getFrnChildren->Bind(pid_high);
	q_getFrnChildren->Bind(rid);
	db_results res=q_getFrnChildren->Read();
	q_getFrnChildren->Reset();
	std::vector<JournalDAO::SFrn> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].frn=watoi64(res[i][L"frn"]);
		ret[i].frn_high=watoi64(res[i][L"frn_high"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void JournalDAO::delFrnEntry
* @sql
*    DELETE FROM map_frn WHERE id=:id(int64)
*/
void JournalDAO::delFrnEntry(int64 id)
{
	if(q_delFrnEntry==NULL)
	{
		q_delFrnEntry=db->Prepare("DELETE FROM map_frn WHERE id=?", false);
	}
	q_delFrnEntry->Bind(id);
	q_delFrnEntry->Write();
	q_delFrnEntry->Reset();
}

/**
* @-SQLGenAccess
* @func SNameAndPid JournalDAO::getNameAndPid
* @return string name, int64 pid, int64 pid_high
* @sql
*    SELECT name, pid, pid_high FROM map_frn WHERE frn=:frn(int64) AND frn_high=:frn_high(int64) AND rid=:rid(int64)
*/
JournalDAO::SNameAndPid JournalDAO::getNameAndPid(int64 frn, int64 frn_high, int64 rid)
{
	if(q_getNameAndPid==NULL)
	{
		q_getNameAndPid=db->Prepare("SELECT name, pid, pid_high FROM map_frn WHERE frn=? AND frn_high=? AND rid=?", false);
	}
	q_getNameAndPid->Bind(frn);
	q_getNameAndPid->Bind(frn_high);
	q_getNameAndPid->Bind(rid);
	db_results res=q_getNameAndPid->Read();
	q_getNameAndPid->Reset();
	SNameAndPid ret = { false, L"", 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.name=res[0][L"name"];
		ret.pid=watoi64(res[0][L"pid"]);
		ret.pid_high=watoi64(res[0][L"pid_high"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void JournalDAO::insertJournal
* @sql
*    INSERT INTO journal_ids (journal_id, device_name, last_record, index_done)
*		VALUES (:journal_id(int64), :device_name(string), :last_record(int64), 0)
*/
void JournalDAO::insertJournal(int64 journal_id, const std::wstring& device_name, int64 last_record)
{
	if(q_insertJournal==NULL)
	{
		q_insertJournal=db->Prepare("INSERT INTO journal_ids (journal_id, device_name, last_record, index_done) VALUES (?, ?, ?, 0)", false);
	}
	q_insertJournal->Bind(journal_id);
	q_insertJournal->Bind(device_name);
	q_insertJournal->Bind(last_record);
	q_insertJournal->Write();
	q_insertJournal->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::updateJournalId
* @sql
*    UPDATE journal_ids SET journal_id=:journal_id(int64) WHERE device_name=:device_name(string)
*/
void JournalDAO::updateJournalId(int64 journal_id, const std::wstring& device_name)
{
	if(q_updateJournalId==NULL)
	{
		q_updateJournalId=db->Prepare("UPDATE journal_ids SET journal_id=? WHERE device_name=?", false);
	}
	q_updateJournalId->Bind(journal_id);
	q_updateJournalId->Bind(device_name);
	q_updateJournalId->Write();
	q_updateJournalId->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::updateJournalLastUsn
* @sql
*    UPDATE journal_ids SET last_record=:last_record(int64) WHERE device_name=:device_name(string)
*/
void JournalDAO::updateJournalLastUsn(int64 last_record, const std::wstring& device_name)
{
	if(q_updateJournalLastUsn==NULL)
	{
		q_updateJournalLastUsn=db->Prepare("UPDATE journal_ids SET last_record=? WHERE device_name=?", false);
	}
	q_updateJournalLastUsn->Bind(last_record);
	q_updateJournalLastUsn->Bind(device_name);
	q_updateJournalLastUsn->Write();
	q_updateJournalLastUsn->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::updateFrnNameAndPid
* @sql
*    UPDATE map_frn SET name=:name(string), pid=:pid(int64), pid_high=:pid_high(int64) WHERE id=:id(int64)
*/
void JournalDAO::updateFrnNameAndPid(const std::wstring& name, int64 pid, int64 pid_high, int64 id)
{
	if(q_updateFrnNameAndPid==NULL)
	{
		q_updateFrnNameAndPid=db->Prepare("UPDATE map_frn SET name=?, pid=?, pid_high=? WHERE id=?", false);
	}
	q_updateFrnNameAndPid->Bind(name);
	q_updateFrnNameAndPid->Bind(pid);
	q_updateFrnNameAndPid->Bind(pid_high);
	q_updateFrnNameAndPid->Bind(id);
	q_updateFrnNameAndPid->Write();
	q_updateFrnNameAndPid->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::insertJournalData
* @sql
*    INSERT INTO journal_data (device_name, journal_id, usn, reason, filename, frn, frn_high, parent_frn, parent_frn_high, next_usn, attributes)
*      VALUES (:device_name(string), :journal_id(int64), :usn(int64), :reason(int64), :filename(string), :frn(int64), :frn_high(int64),
*			   :parent_frn(int64), :parent_frn_high(int64), :next_usn(int64), :attributes(int64) )
*/
void JournalDAO::insertJournalData(const std::wstring& device_name, int64 journal_id, int64 usn, int64 reason, const std::wstring& filename, int64 frn, int64 frn_high, int64 parent_frn, int64 parent_frn_high, int64 next_usn, int64 attributes)
{
	if(q_insertJournalData==NULL)
	{
		q_insertJournalData=db->Prepare("INSERT INTO journal_data (device_name, journal_id, usn, reason, filename, frn, frn_high, parent_frn, parent_frn_high, next_usn, attributes) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )", false);
	}
	q_insertJournalData->Bind(device_name);
	q_insertJournalData->Bind(journal_id);
	q_insertJournalData->Bind(usn);
	q_insertJournalData->Bind(reason);
	q_insertJournalData->Bind(filename);
	q_insertJournalData->Bind(frn);
	q_insertJournalData->Bind(frn_high);
	q_insertJournalData->Bind(parent_frn);
	q_insertJournalData->Bind(parent_frn_high);
	q_insertJournalData->Bind(next_usn);
	q_insertJournalData->Bind(attributes);
	q_insertJournalData->Write();
	q_insertJournalData->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SJournalData> JournalDAO::getJournalData
* @return int64 usn, int64 reason, string filename, int64 frn, int64 frn_high, int64 parent_frn, int64 parent_frn_high, int64 next_usn, int64 attributes
* @sql
*    SELECT usn, reason, filename, frn, parent_frn, next_usn FROM journal_data
*		WHERE device_name=:device_name(string) ORDER BY usn ASC
*/
std::vector<JournalDAO::SJournalData> JournalDAO::getJournalData(const std::wstring& device_name)
{
	if(q_getJournalData==NULL)
	{
		q_getJournalData=db->Prepare("SELECT usn, reason, filename, frn, parent_frn, next_usn FROM journal_data WHERE device_name=? ORDER BY usn ASC", false);
	}
	q_getJournalData->Bind(device_name);
	db_results res=q_getJournalData->Read();
	q_getJournalData->Reset();
	std::vector<JournalDAO::SJournalData> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].usn=watoi64(res[i][L"usn"]);
		ret[i].reason=watoi64(res[i][L"reason"]);
		ret[i].filename=res[i][L"filename"];
		ret[i].frn=watoi64(res[i][L"frn"]);
		ret[i].frn_high=watoi64(res[i][L"frn_high"]);
		ret[i].parent_frn=watoi64(res[i][L"parent_frn"]);
		ret[i].parent_frn_high=watoi64(res[i][L"parent_frn_high"]);
		ret[i].next_usn=watoi64(res[i][L"next_usn"]);
		ret[i].attributes=watoi64(res[i][L"attributes"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void JournalDAO::updateSetJournalIndexDone
* @sql
*    UPDATE journal_ids SET index_done=:index_done(int) WHERE device_name=:device_name(string)
*/
void JournalDAO::updateSetJournalIndexDone(int index_done, const std::wstring& device_name)
{
	if(q_updateSetJournalIndexDone==NULL)
	{
		q_updateSetJournalIndexDone=db->Prepare("UPDATE journal_ids SET index_done=? WHERE device_name=?", false);
	}
	q_updateSetJournalIndexDone->Bind(index_done);
	q_updateSetJournalIndexDone->Bind(device_name);
	q_updateSetJournalIndexDone->Write();
	q_updateSetJournalIndexDone->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::delJournalData
* @sql
*    DELETE FROM journal_data WHERE device_name=:device_name(string)
*/
void JournalDAO::delJournalData(const std::wstring& device_name)
{
	if(q_delJournalData==NULL)
	{
		q_delJournalData=db->Prepare("DELETE FROM journal_data WHERE device_name=?", false);
	}
	q_delJournalData->Bind(device_name);
	q_delJournalData->Write();
	q_delJournalData->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::delFrnEntryViaFrn
* @sql
*    DELETE FROM map_frn WHERE frn=:frn(int64) AND frn_high=:frn_high(int64) AND rid=:rid(int64)
*/
void JournalDAO::delFrnEntryViaFrn(int64 frn, int64 frn_high, int64 rid)
{
	if(q_delFrnEntryViaFrn==NULL)
	{
		q_delFrnEntryViaFrn=db->Prepare("DELETE FROM map_frn WHERE frn=? AND frn_high=? AND rid=?", false);
	}
	q_delFrnEntryViaFrn->Bind(frn);
	q_delFrnEntryViaFrn->Bind(frn_high);
	q_delFrnEntryViaFrn->Bind(rid);
	q_delFrnEntryViaFrn->Write();
	q_delFrnEntryViaFrn->Reset();
}

/**
* @-SQLGenAccess
* @func void JournalDAO::delJournalDeviceId
* @sql
*    DELETE FROM journal_ids WHERE device_name=:device_name(string)
*/
void JournalDAO::delJournalDeviceId(const std::wstring& device_name)
{
	if(q_delJournalDeviceId==NULL)
	{
		q_delJournalDeviceId=db->Prepare("DELETE FROM journal_ids WHERE device_name=?", false);
	}
	q_delJournalDeviceId->Bind(device_name);
	q_delJournalDeviceId->Write();
	q_delJournalDeviceId->Reset();
}


