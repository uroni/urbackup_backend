#pragma once
#include "../../Interface/Database.h"

class ServerLinkJournalDao
{
public:
	ServerLinkJournalDao(IDatabase *db);
	~ServerLinkJournalDao();


	//@-SQLGenFunctionsBegin
	struct JournalEntry
	{
		std::string linkname;
		std::string linktarget;
	};


	void addDirectoryLinkJournalEntry(const std::string& linkname, const std::string& linktarget);
	void removeDirectoryLinkJournalEntry(int64 entry_id);
	std::vector<JournalEntry> getDirectoryLinkJournalEntries(void);
	void removeDirectoryLinkJournalEntries(void);
	//@-SQLGenFunctionsEnd

private:
	ServerLinkJournalDao(ServerLinkJournalDao& other) {}
	void operator=(ServerLinkJournalDao& other) {}

	void prepareQueries();
	void destroyQueries();

	//@-SQLGenVariablesBegin
	IQuery* q_addDirectoryLinkJournalEntry;
	IQuery* q_removeDirectoryLinkJournalEntry;
	IQuery* q_getDirectoryLinkJournalEntries;
	IQuery* q_removeDirectoryLinkJournalEntries;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
