#include <vector>
#include <map>
#include "SettingsReader.h"

class CMemorySettingsReader : public CSettingsReader
{
public:
	CMemorySettingsReader(const std::string &pData);

	virtual bool getValue(std::string key, std::string *value);

	virtual std::vector<std::string> getKeys();

private:
	std::map<std::string,std::string> mSettingsMap;
};