#include <map>
#include <vector>
#include <string>

#include "Interface/Table.h"

class CRATable : public ITable
{
public:
	~CRATable();

	virtual void addObject(std::wstring key, ITable *tab);
	virtual ITable* getObject(size_t n);
	virtual ITable* getObject(std::wstring key);
	virtual std::wstring getValue();
	virtual size_t getSize();
	virtual void addString(std::wstring key, std::wstring str);

private:
	std::map<std::wstring, ITable*> table_map;
	std::vector<ITable*> tables;
};

class CTable : public ITable
{
public:
	~CTable();

	virtual void addObject(std::wstring key, ITable *tab);
	virtual ITable* getObject(size_t n);
	virtual ITable* getObject(std::wstring key);
	virtual std::wstring getValue();
	virtual size_t getSize();
	virtual void addString(std::wstring key, std::wstring str);

private:
	std::map<std::wstring, ITable*> table_map;
};


class CTablestring : public ITable
{
public:
	CTablestring(std::wstring pStr);

	virtual void addObject(std::wstring key, ITable *tab);
	virtual ITable* getObject(size_t n);
	virtual ITable* getObject(std::wstring key);
	virtual std::wstring getValue();
	virtual size_t getSize();
	virtual void addString(std::wstring key, std::wstring str);

private:
	std::wstring str;
};
