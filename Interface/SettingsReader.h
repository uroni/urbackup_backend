#ifndef INTERFACE_SETTINGSREADER_H
#define INTERFACE_SETTINGSREADER_H

#include <string>
#include <vector>
#include "Object.h"

class ISettingsReader : public IObject
{
public:
	virtual bool getValue(std::string key, std::string *value)=0;
	virtual bool getValue(std::wstring key, std::wstring *value)=0;
	
	virtual std::string getValue(std::string key,std::string def)=0;
	virtual std::string getValue(std::string key)=0;
	virtual int getValue(std::string key, int def)=0;
	virtual float getValue(std::string key, float def)=0;
	virtual int64 getValue(std::string key, int64 def)=0;

	virtual std::wstring getValue(std::wstring key,std::wstring def)=0;
	virtual std::wstring getValue(std::wstring key)=0;
	virtual int getValue(std::wstring key, int def)=0;
	virtual float getValue(std::wstring key, float def)=0;
	virtual int64 getValue(std::wstring key, int64 def)=0;
	
	virtual std::vector<std::wstring> getKeys() = 0;
};

#endif //INTERFACE_SETTINGSREADER_H