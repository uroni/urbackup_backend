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
	std::map<std::string,std::string> mSettingsMap;
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

	static void cleanup();
	static void setup();

	virtual std::vector<std::string> getKeys();

private:

	void read(const std::string& fdata, const std::string& pFile);
	
	static std::map<std::string, SCachedSettings*> *settings;
	static IMutex *settings_mutex;
	SCachedSettings *cached_settings;
};

