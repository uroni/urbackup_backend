#include <vector>
#include <map>
#include "Interface/Mutex.h"
#include "SettingsReader.h"

class CFileSettingsReader : public CSettingsReader
{
public:
	CFileSettingsReader(std::string pFile);
	~CFileSettingsReader();

	virtual bool getValue(std::string key, std::string *value);

	virtual std::vector<std::string> getKeys();

	bool hasError();

private:

	void read(const std::string& pFile);
	
	std::map<std::string, std::string> mSettingsMap;

	bool has_error;
};

