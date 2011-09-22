#include "IUrlFactory.h"

class UrlFactory : public IUrlFactory
{
public:
	virtual bool sendMail(const MailServer &server, const std::vector<std::string> &to,
		const std::string &subject,	const std::string &message, std::string *errmsg=NULL);
};