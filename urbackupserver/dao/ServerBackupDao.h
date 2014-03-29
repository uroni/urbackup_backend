#pragma once
#include "../../Interface/Database.h"

class ServerBackupDao
{
public:
	ServerBackupDao(IDatabase *db);
	~ServerBackupDao();


	void commit();
	int64 getLastId();

	//@-SQLGenFunctionsBegin
	struct DirectoryLinkEntry
	{
		std::wstring name;
		std::wstring target;
	};
	struct JournalEntry
	{
		std::wstring linkname;
		std::wstring linktarget;
	};


	void addDirectoryLink(int clientid, const std::wstring& name, const std::wstring& target);
	void removeDirectoryLink(int clientid, const std::wstring& name);
	int getDirectoryRefcount(int clientid, const std::wstring& name);
	void addDirectoryLinkJournalEntry(const std::wstring& linkname, const std::wstring& linktarget);
	void removeDirectoryLinkJournalEntry(int64 entry_id);
	std::vector<JournalEntry> getDirectoryLinkJournalEntries(void);
	void removeDirectoryLinkJournalEntries(void);
	std::vector<DirectoryLinkEntry> getLinksInDirectory(int clientid, const std::wstring& dir);
	//@-SQLGenFunctionsEnd

private:
	ServerBackupDao(ServerBackupDao& other) {}
	void operator=(ServerBackupDao& other) {}

	void prepareQueries(void);
	void destroyQueries(void);

	//@-SQLGenVariablesBegin
	IQuery* q_addDirectoryLink;
	IQuery* q_removeDirectoryLink;
	IQuery* q_getDirectoryRefcount;
	IQuery* q_addDirectoryLinkJournalEntry;
	IQuery* q_removeDirectoryLinkJournalEntry;
	IQuery* q_getDirectoryLinkJournalEntries;
	IQuery* q_removeDirectoryLinkJournalEntries;
	IQuery* q_getLinksInDirectory;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
