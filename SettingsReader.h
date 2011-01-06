#ifndef CSETTINGSREADER_H
#define CSETTINGSREADER_H

#include "Interface/SettingsReader.h"

class CSettingsReader : public ISettingsReader
{
public:
	virtual bool getValue(std::string key, std::string *value)=0;
	virtual bool getValue(std::wstring key, std::wstring *value)=0;

	
	std::string getValue(std::string key,std::string def);
	std::string getValue(std::string key);
	int getValue(std::string key, int def);
	float getValue(std::string key, float def);

	std::wstring getValue(std::wstring key,std::wstring def);
	std::wstring getValue(std::wstring key);
	int getValue(std::wstring key, int def);
	float getValue(std::wstring key, float def);
};

#endif //CSETTINGSREADER_H
