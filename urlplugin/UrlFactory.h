#include "IUrlFactory.h"

class UrlFactory : public IUrlFactory
{
public:
	virtual bool sendMail(const MailServer &server, const std::vector<std::string> &to,
		const std::string &subject,	const std::string &message, std::string *errmsg=NULL);

	virtual std::string downloadString(const std::string& url, const std::string& http_proxy = "", std::string *errmsg=NULL);

	virtual std::vector<std::multimap<std::string, std::string> > queryLDAP(const std::string& url, const std::string& username, const std::string& password, std::string *errmsg=NULL);

	virtual bool downloadFile(const std::string& url, IFile* output, const std::string& http_proxy = "", std::string *errmsg=NULL);

	virtual bool requestUrl(const std::string & url, str_map & params, std::string& ret, long& http_code, std::string * errmsg = NULL);
};