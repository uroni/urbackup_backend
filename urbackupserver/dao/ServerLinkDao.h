#pragma once
#include "../../Interface/Database.h"

class ServerLinkDao
{
public:
	ServerLinkDao(IDatabase *db);
	~ServerLinkDao();

	IDatabase* getDatabase();

	int64 getLastId();

	int getLastChanges();

	//@-SQLGenFunctionsBegin
	struct DirectoryLinkEntry
	{
		std::string name;
		std::string target;
	};


	void addDirectoryLink(int clientid, const std::string& name, const std::string& target);
	void removeDirectoryLink(int clientid, const std::string& target);
	void removeDirectoryLinkWithName(int clientid, const std::string& target, const std::string& name);
	void removeDirectoryLinkGlob(int clientid, const std::string& target);
	int getDirectoryRefcount(int clientid, const std::string& name);
	std::vector<DirectoryLinkEntry> getLinksInDirectory(int clientid, const std::string& dir);
	std::vector<DirectoryLinkEntry> getLinksByPoolName(int clientid, const std::string& name);
	void deleteLinkReferenceEntry(int64 id);
	void updateLinkReferenceTarget(const std::string& new_target, int64 id);
	//@-SQLGenFunctionsEnd

private:
	ServerLinkDao(ServerLinkDao& other) {}
	void operator=(ServerLinkDao& other) {}

	void prepareQueries();
	void destroyQueries();

	//@-SQLGenVariablesBegin
	IQuery* q_addDirectoryLink;
	IQuery* q_removeDirectoryLink;
	IQuery* q_removeDirectoryLinkWithName;
	IQuery* q_removeDirectoryLinkGlob;
	IQuery* q_getDirectoryRefcount;
	IQuery* q_getLinksInDirectory;
	IQuery* q_getLinksByPoolName;
	IQuery* q_deleteLinkReferenceEntry;
	IQuery* q_updateLinkReferenceTarget;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
