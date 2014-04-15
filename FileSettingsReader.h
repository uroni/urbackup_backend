#include <vector>
#include <map>
#include "Interface/Mutex.h"
#include "SettingsReader.h"

struct SSetting
{
	std::string key;
	std::string value;
};

struct SCachedSettings
{
	std::map<std::wstring,std::wstring> mSettingsMap;
	IMutex *smutex;
	int refcount;
	std::string key;
};


class CFileSettingsReader : public CSettingsReader
{
public:
	CFileSettingsReader(std::string pFile);
	~CFileSettingsReader();

	virtual bool getValue(std::string key, std::string *value);
	virtual bool getValue(std::wstring key, std::wstring *value);

	static void cleanup();
	static void setup();

	virtual std::vector<std::wstring> getKeys();

private:
	
	static std::map<std::string, SCachedSettings*> *settings;
	static IMutex *settings_mutex;
	SCachedSettings *cached_settings;
};

