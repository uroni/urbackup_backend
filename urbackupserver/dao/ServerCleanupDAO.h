#pragma once
#include "../../Interface/Database.h"

class ServerCleanupDAO
{
public:

	ServerCleanupDAO(IDatabase *db);
	~ServerCleanupDAO(void);

	//@-SQLGenFunctionsBegin
	struct SIncompleteImages
	{
		int id;
		std::wstring path;
	};
	struct SImageLetter
	{
		int id;
		std::wstring letter;
	};
	struct SImageRef
	{
		int id;
		int complete;
	};
	struct CondString
	{
		bool exists;
		std::wstring path;
	};


	std::vector<SIncompleteImages> getIncompleteImages(void);
	void removeImage(int id);
	std::vector<int> getClientsSortFilebackups(void);
	std::vector<int> getClientsSortImagebackups(void);
	std::vector<SImageLetter> getFullNumImages(int clientid);
	std::vector<SImageRef> getImageRefs(int incremental_ref);
	CondString getImagePath(int id);
	std::vector<SImageLetter> getIncrNumImages(int clientid);
	//@-SQLGenFunctionsEnd

private:
	void createQueries(void);
	void destroyQueries(void);

	IDatabase *db;

	//@-SQLGenVariablesBegin
	IQuery* q_getIncompleteImages;
	IQuery* q_removeImage;
	IQuery* q_getClientsSortFilebackups;
	IQuery* q_getClientsSortImagebackups;
	IQuery* q_getFullNumImages;
	IQuery* q_getImageRefs;
	IQuery* q_getImagePath;
	IQuery* q_getIncrNumImages;
	//@-SQLGenVariablesEnd
};