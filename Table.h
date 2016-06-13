#include <map>
#include <vector>
#include <string>

#include "Interface/Table.h"

class CRATable : public ITable
{
public:
	~CRATable();

	virtual void addObject(std::string key, ITable *tab);
	virtual ITable* getObject(size_t n);
	virtual ITable* getObject(std::string key);
	virtual std::string getValue();
	virtual size_t getSize();
	virtual void addString(std::string key, std::string str);

private:
	std::map<std::string, ITable*> table_map;
	std::vector<ITable*> tables;
};

class CTable : public ITable
{
public:
	~CTable();

	virtual void addObject(std::string key, ITable *tab);
	virtual ITable* getObject(size_t n);
	virtual ITable* getObject(std::string key);
	virtual std::string getValue();
	virtual size_t getSize();
	virtual void addString(std::string key, std::string str);

private:
	std::map<std::string, ITable*> table_map;
};


class CTablestring : public ITable
{
public:
	CTablestring(std::string pStr);

	virtual void addObject(std::string key, ITable *tab);
	virtual ITable* getObject(size_t n);
	virtual ITable* getObject(std::string key);
	virtual std::string getValue();
	virtual size_t getSize();
	virtual void addString(std::string key, std::string str);

private:
	std::string str;
};
