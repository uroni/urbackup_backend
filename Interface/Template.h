#include "Object.h"

class IDatabase;
class ITable;

class ITemplate : public IObject
{
public:
	virtual void Reset(void)=0;

	virtual void setValue(std::wstring key, std::wstring value)=0;
	virtual ITable* getTable(std::wstring key)=0;
	virtual std::string getData(void)=0;

	virtual void addValueTable( IDatabase* db, const std::string &table)=0;
};
