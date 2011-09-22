#include "UrlFactory.h"
#include "../Interface/Server.h"
#include <memory.h>
#include <curl/curl.h>

#include "../stringtools.h"

struct RDUserS
{
	RDUserS(void) : pos(0) {}
	size_t pos;
	std::string text;
};

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp)
{
  struct RDUserS *pooh = (struct RDUserS*)userp;
  const char *data;
 
  if(size*nmemb < 1)
    return 0;

  if(pooh->pos<pooh->text.size())
  { 
	data = &pooh->text[pooh->pos];
 
	size_t len = (std::min)(pooh->text.size()-pooh->pos, size*nmemb);
    memcpy(ptr, data, len);
    pooh->pos+=len;
    return len;
  }
  return 0;
}

bool UrlFactory::sendMail(const MailServer &server, const std::vector<std::string> &to, 
		const std::string &subject,	const std::string &message, std::string *errmsg)
{
	CURL *curl=curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_URL, ("smtp://"+server.servername+":"+nconvert(server.port)).c_str());
	curl_easy_setopt(curl, CURLOPT_USE_SSL, server.ssl_only?CURLUSESSL_ALL:CURLUSESSL_TRY);
	if(!server.check_certificate)
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	}
	curl_easy_setopt(curl, CURLOPT_USERNAME, server.username.c_str());
	curl_easy_setopt(curl, CURLOPT_PASSWORD, server.password.c_str());
	curl_easy_setopt(curl, CURLOPT_MAIL_FROM, server.mailfrom.c_str());
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	curl_slist * recpt=NULL;
	for(size_t i=0;i<to.size();++i)
	{
		recpt=curl_slist_append(recpt, to[i].c_str());
	}
	curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recpt);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
	RDUserS rd;
	std::string header;
	header+="From: "+server.mailfrom+"\n"
		"Subject: "+subject+"\n"
		"\n";
	rd.text=header+message;
	curl_easy_setopt(curl, CURLOPT_READDATA, &rd);
	std::string errbuf;
	errbuf.resize(CURL_ERROR_SIZE*2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, (char*)errbuf.c_str());
	CURLcode res= curl_easy_perform(curl);
	if(res!=0)
	{
		errbuf.resize(strlen(errbuf.c_str()));
		if(errmsg==NULL)
		{
			Server->Log("Error during cURL operation occured. ec="+nconvert(res)+" -- "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg="ec="+nconvert(res)+" -- "+errbuf;
		}
		curl_slist_free_all(recpt);
		curl_easy_cleanup(curl);
		return false;
	}
	curl_slist_free_all(recpt);
	curl_easy_cleanup(curl);
	return true;
}