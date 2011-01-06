#ifndef ITABLE_H
#define ITABLE_H

#include <string>
#include "Object.h"

class ITable : public IObject
{
public:
	virtual void addObject(std::wstring key, ITable *tab)=0;
	virtual ITable* getObject(size_t n)=0;
	virtual ITable* getObject(std::wstring key)=0;
	virtual std::wstring getValue()=0;
	virtual size_t getSize()=0;
	virtual void addString(std::wstring key, std::wstring str)=0;
};

#endif //ITABLE_H