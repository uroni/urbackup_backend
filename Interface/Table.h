#ifndef ITABLE_H
#define ITABLE_H

#include <string>
#include "Object.h"

class ITable : public IObject
{
public:
	virtual void addObject(std::string key, ITable *tab)=0;
	virtual ITable* getObject(size_t n)=0;
	virtual ITable* getObject(std::string key)=0;
	virtual std::string getValue()=0;
	virtual size_t getSize()=0;
	virtual void addString(std::string key, std::string str)=0;
};

#endif //ITABLE_H