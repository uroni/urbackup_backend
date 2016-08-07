class IDatabase;
class IQuery;

class CDBSettingsReader : public CSettingsReader
{
public:
	CDBSettingsReader(THREAD_ID tid, DATABASE_ID did, const std::string &pTable, const std::string &pSQL="");
	CDBSettingsReader(IDatabase *pDB, const std::string &pTable, const std::string &pSQL="");
	~CDBSettingsReader();

	bool getValue(std::string key, std::string *value);	

	std::vector<std::string> getKeys();

private:
	std::string table;	

	IDatabase* db;
	IQuery *query;
};

class CDBMemSettingsReader : public CSettingsReader
{
public:
	CDBMemSettingsReader(THREAD_ID tid, DATABASE_ID did, const std::string &pTable, const std::string &pSQL = "");
	CDBMemSettingsReader(IDatabase *pDB, const std::string &pTable, const std::string &pSQL = "");

	bool getValue(std::string key, std::string *value);

	std::vector<std::string> getKeys();

private:
	str_map table;
};
