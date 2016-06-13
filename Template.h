#include <string>
#include <vector>
#include <map>

#include "Interface/Template.h"
#include "Interface/Table.h"
#include "Interface/Query.h"

class CTemplate : public ITemplate
{
public:
	CTemplate(std::string pFile);
	~CTemplate();

	virtual void Reset(void);

	virtual void setValue(std::string key, std::string value);
	virtual ITable* getTable(std::string key);
	virtual std::string getData(void);

	virtual void addValueTable( IDatabase* db, const std::string &table);

private:
	void AddDefaultReplacements(void);
	bool FindValue(const std::string &key, std::string &value, bool dbs=false);
	ITable *findTable(std::string key);
	void transform(std::string &output);
	ITable* createTableRecursive(std::string key);

	std::string data;
	std::string file;


	std::vector<std::pair<std::string, std::string> > mReplacements;
	ITable* mValuesRoot;
	ITable* mCurrValues;

	std::vector< std::pair<IDatabase*, IQuery*> > mTables;
};
