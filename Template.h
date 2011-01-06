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

	virtual void setValue(std::wstring key, std::wstring value);
	virtual ITable* getTable(std::wstring key);
	virtual std::string getData(void);

	virtual void addValueTable( IDatabase* db, const std::string &table);

private:
	void AddDefaultReplacements(void);
	bool FindValue(const std::wstring &key, std::wstring &value, bool dbs=false);
	ITable *findTable(std::wstring key);
	void transform(std::wstring &output);
	ITable* createTableRecursive(std::wstring key);

	std::wstring data;
	std::string file;


	std::vector<std::pair<std::wstring, std::wstring> > mReplacements;
	ITable* mValuesRoot;
	ITable* mCurrValues;

	std::vector< std::pair<IDatabase*, IQuery*> > mTables;
};
