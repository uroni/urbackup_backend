#ifndef IURLFACTORY_H
#define IURLFACTORY_H

#include "../Interface/Server.h"
#include "../Interface/Plugin.h"
#include "../Interface/File.h"

#include <string>
#include <vector>

struct MailServer
{
	MailServer(void) : port(22), ssl_only(false), check_certificate(true) {}
	std::string servername;
	unsigned short port;
	std::string username;
	std::string password;
	std::string mailfrom;
	bool ssl_only;
	bool check_certificate;
};

class IUrlFactory : public IPlugin
{
public:
	virtual bool sendMail(const MailServer &server, const std::vector<std::string> &to,
		const std::string &subject,	const std::string &message, std::string *errmsg=NULL)=0;

	virtual std::string downloadString(const std::string& url, const std::string& http_proxy = "",
		std::string *errmsg=NULL) = 0;

	virtual bool downloadFile(const std::string& url, IFile* output, const std::string& http_proxy = "",
		std::string *errmsg=NULL) = 0;
};

#endif //IURLFACTORY_H