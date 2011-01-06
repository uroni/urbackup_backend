#include "../../Interface/Database.h"
#include "../../Interface/Server.h"
#include "../../Interface/SessionMgr.h"
#include "../../Interface/Template.h"

class Helper
{
public:
	Helper(THREAD_ID pTID, str_map *pGET, str_nmap *pPARAMS);
	void update(THREAD_ID pTID, str_map *pGET, str_nmap *pPARAMS);
	~Helper(void);
	SUser *getSession(void);
	std::wstring generateSession(std::wstring username);
	void OverwriteLanguage(std::string pLanguage);
	ITemplate *createTemplate(std::string name);
	IDatabase *getDatabase(void);
	std::string getRights(const std::string &domain);

	std::string getTimeFormatString(void);

	std::string getLanguage(void);

	void Write(std::string str);
	void WriteTemplate(ITemplate *tmpl);

	void releaseAll(void);	
private:
	std::string getRightsInt(const std::string &domain);

	SUser* session;
	std::vector<ITemplate*> templates;
	std::string language;

	bool invalid_session;

	str_map *GET;
	str_nmap *PARAMS;

	THREAD_ID tid;
};