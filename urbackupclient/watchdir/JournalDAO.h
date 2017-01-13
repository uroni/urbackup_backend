#pragma once

#include "../../Interface/Database.h"
#include "../../Interface/Query.h"

class JournalDAO
{
public:
	JournalDAO(IDatabase *pDB);
	~JournalDAO();

	void prepareQueries();
	void destroyQueries();

	//@-SQLGenFunctionsBegin
	struct CondInt
	{
		bool exists;
		int value;
	};
	struct CondInt64
	{
		bool exists;
		int64 value;
	};
	struct SDeviceInfo
	{
		bool exists;
		int64 journal_id;
		int64 last_record;
		int index_done;
	};
	struct SFrn
	{
		int64 frn;
		int64 frn_high;
	};
	struct SJournalData
	{
		int64 usn;
		int64 reason;
		std::string filename;
		int64 frn;
		int64 frn_high;
		int64 parent_frn;
		int64 parent_frn_high;
		int64 next_usn;
		int64 attributes;
	};
	struct SNameAndPid
	{
		bool exists;
		std::string name;
		int64 pid;
		int64 pid_high;
	};
	struct SParentFrn
	{
		int64 parent_frn_high;
		int64 parent_frn_low;
	};


	SDeviceInfo getDeviceInfo(const std::string& device_name);
	CondInt64 getRootId(const std::string& name);
	void addFrn(const std::string& name, int64 pid, int64 pid_high, int64 frn, int64 frn_high, int64 rid);
	void resetRoot(int64 rid);
	CondInt64 getFrnEntryId(int64 frn, int64 frn_high, int64 rid);
	std::vector<SFrn> getFrnChildren(int64 pid, int64 pid_high, int64 rid);
	void delFrnEntry(int64 id);
	SNameAndPid getNameAndPid(int64 frn, int64 frn_high, int64 rid);
	void insertJournal(int64 journal_id, const std::string& device_name, int64 last_record);
	void updateJournalId(int64 journal_id, const std::string& device_name);
	void updateJournalLastUsn(int64 last_record, const std::string& device_name);
	void updateFrnNameAndPid(const std::string& name, int64 pid, int64 pid_high, int64 id);
	void insertJournalData(const std::string& device_name, int64 journal_id, int64 usn, int64 reason, const std::string& filename, int64 frn, int64 frn_high, int64 parent_frn, int64 parent_frn_high, int64 next_usn, int64 attributes);
	std::vector<SJournalData> getJournalData(const std::string& device_name);
	CondInt getJournalDataSingle(const std::string& device_name);
	void updateSetJournalIndexDone(int index_done, const std::string& device_name);
	void delJournalData(const std::string& device_name);
	void delFrnEntryViaFrn(int64 frn, int64 frn_high, int64 rid);
	void delJournalDeviceId(const std::string& device_name);
	std::vector<SParentFrn> getHardLinkParents(const std::string& volume, int64 frn_high, int64 frn_low);
	void deleteHardlink(const std::string& vol, int64 frn_high, int64 frn_low);
	//@-SQLGenFunctionsEnd
	IQuery* getJournalDataQ();

private:
	IDatabase *db;

	//@-SQLGenVariablesBegin
	IQuery* q_getDeviceInfo;
	IQuery* q_getRootId;
	IQuery* q_addFrn;
	IQuery* q_resetRoot;
	IQuery* q_getFrnEntryId;
	IQuery* q_getFrnChildren;
	IQuery* q_delFrnEntry;
	IQuery* q_getNameAndPid;
	IQuery* q_insertJournal;
	IQuery* q_updateJournalId;
	IQuery* q_updateJournalLastUsn;
	IQuery* q_updateFrnNameAndPid;
	IQuery* q_insertJournalData;
	IQuery* q_getJournalData;
	IQuery* q_getJournalDataSingle;
	IQuery* q_updateSetJournalIndexDone;
	IQuery* q_delJournalData;
	IQuery* q_delFrnEntryViaFrn;
	IQuery* q_delJournalDeviceId;
	IQuery* q_getHardLinkParents;
	IQuery* q_deleteHardlink;
	//@-SQLGenVariablesEnd
};