#include <vector>
#include <map>
#include "SettingsReader.h"

class CMemorySettingsReader : public CSettingsReader
{
public:
	CMemorySettingsReader(const std::string &pData);

	virtual bool getValue(std::string key, std::string *value);
	virtual bool getValue(std::wstring key, std::wstring *value);

private:
	std::map<std::wstring,std::wstring> mSettingsMap;
};