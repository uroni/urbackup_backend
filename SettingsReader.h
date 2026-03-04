#ifndef CSETTINGSREADER_H
#define CSETTINGSREADER_H

#include "Interface/SettingsReader.h"

class CSettingsReader : public ISettingsReader
{
public:
	virtual bool getValue(std::string key, std::string *value)=0;

	
	std::string getValue(const std::string& key, const char* def);
	std::string getValue(std::string key, const std::string& def);	
	std::string getValue(std::string key);
	int getValue(std::string key, int def);
	float getValue(std::string key, float def);
	int64 getValue(std::string key, int64 def);
	bool getValue(const std::string& key, const bool def);
};

#endif //CSETTINGSREADER_H
