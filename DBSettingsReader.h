class IDatabase;
class IQuery;

class CDBSettingsReader : public CSettingsReader
{
public:
	CDBSettingsReader(THREAD_ID tid, DATABASE_ID did, const std::string &pTable, const std::string &pSQL="");
	CDBSettingsReader(IDatabase *pDB, const std::string &pTable, const std::string &pSQL="");

	bool getValue(std::string key, std::string *value);	
	bool getValue(std::wstring key, std::wstring *value);

private:
	std::string table;	

	IQuery *query;
};
