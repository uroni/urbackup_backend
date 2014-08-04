#include "UrlFactory.h"
#include "../Interface/Server.h"
#include <memory.h>
#include <curl/curl.h>

#include "../stringtools.h"


namespace
{

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

	std::string format_time(std::string fs)
	{
		time_t rawtime;		
		char buffer [100];
		time ( &rawtime );
#ifdef _WIN32
		struct tm  timeinfo;
		localtime_s(&timeinfo, &rawtime);
		strftime (buffer,100,fs.c_str(),&timeinfo);
#else
		struct tm *timeinfo;
		timeinfo = localtime ( &rawtime );
		strftime (buffer,100,fs.c_str(),timeinfo);
#endif	
		std::string r(buffer);
		return r;
	}

	size_t write_string_callback( char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		size_t bytes = size*nmemb;

		if(bytes>0)
		{
			std::string* output = reinterpret_cast<std::string*>(userdata);
			output->append(ptr, ptr+bytes);
		}

		return bytes;
	}

	size_t write_file_callback( char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		size_t bytes = size*nmemb;

		if(bytes>0)
		{
			IFile* output = reinterpret_cast<IFile*>(userdata);
			return output->Write(ptr, static_cast<_u32>(bytes));
		}

		return bytes;
	}
}

bool UrlFactory::sendMail(const MailServer &server, const std::vector<std::string> &to, 
		const std::string &subject,	const std::string &message, std::string *errmsg)
{
	CURL *curl=curl_easy_init();

	std::string mailfrom=server.mailfrom;

	if(mailfrom.find("<")==std::string::npos)
	{
		mailfrom="<"+mailfrom+">";
	}

	curl_easy_setopt(curl, CURLOPT_URL, ("smtp://"+server.servername+":"+nconvert(server.port)).c_str());
	curl_easy_setopt(curl, CURLOPT_USE_SSL, server.ssl_only?CURLUSESSL_ALL:CURLUSESSL_TRY);
	if(!server.check_certificate)
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	}
	if(!server.username.empty())
	{
	    curl_easy_setopt(curl, CURLOPT_USERNAME, server.username.c_str());
	    curl_easy_setopt(curl, CURLOPT_PASSWORD, server.password.c_str());
	}
	curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mailfrom.c_str());
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	curl_slist * recpt=NULL;
	for(size_t i=0;i<to.size();++i)
	{
		recpt=curl_slist_append(recpt, trim(to[i]).c_str());
	}
	curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recpt);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
	RDUserS rd;
	std::string header;
	header+="From: "+mailfrom+"\r\n"
		"Subject: "+subject+"\r\n"
		"Date: "+format_time("%a, %d %b %Y %H:%M:%S %z")+"\r\n"
		"\r\n";

	rd.text=header+message;
	curl_easy_setopt(curl, CURLOPT_READDATA, &rd);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

	std::string errbuf;
	errbuf.resize(CURL_ERROR_SIZE*2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, (char*)errbuf.c_str());
	CURLcode res= curl_easy_perform(curl);
	if(res!=CURLE_OK)
	{
		errbuf.resize(strlen(errbuf.c_str()));
		if(errmsg==NULL)
		{
			Server->Log(std::string("Error during cURL operation occured: ")+curl_easy_strerror(res)+" ("+nconvert(res)+") -- "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg=std::string(curl_easy_strerror(res)) + "(ec=" + nconvert(res) + "), " + errbuf;
		}
		curl_slist_free_all(recpt);
		curl_easy_cleanup(curl);
		return false;
	}
	curl_slist_free_all(recpt);
	curl_easy_cleanup(curl);
	return true;
}

std::string UrlFactory::downloadString( const std::string& url, const std::string& http_proxy, std::string *errmsg/*=NULL*/ )
{
	if(errmsg!=NULL)
	{
		errmsg->clear();
	}

	CURL *curl=curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION , 1);
	
	if(!http_proxy.empty())
	{
		curl_easy_setopt(curl, CURLOPT_PROXY, http_proxy.c_str() );
	}

	std::string output;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
	
	std::string errbuf;
	errbuf.resize(CURL_ERROR_SIZE*2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, (char*)errbuf.c_str());
	CURLcode res = curl_easy_perform(curl);
	if(res!=CURLE_OK)
	{
		errbuf.resize(strlen(errbuf.c_str()));
		if(errmsg==NULL)
		{
			Server->Log(std::string("Error during cURL operation occured: ")+curl_easy_strerror(res)+" (ec="+nconvert(res)+"), "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg=std::string(curl_easy_strerror(res)) + "(ec=" + nconvert(res) + "), " + errbuf;
		}
		output.clear();
	}

	curl_easy_cleanup(curl);
	return output;
}

bool UrlFactory::downloadFile(const std::string& url, IFile* output, const std::string& http_proxy, std::string *errmsg)
{
	if(errmsg!=NULL)
	{
		errmsg->clear();
	}

	CURL *curl=curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION , 1);

	if(!http_proxy.empty())
	{
		curl_easy_setopt(curl, CURLOPT_PROXY, http_proxy.c_str() );
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, output);

	std::string errbuf;
	errbuf.resize(CURL_ERROR_SIZE*2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, (char*)errbuf.c_str());
	CURLcode res = curl_easy_perform(curl);
	if(res!=CURLE_OK)
	{
		errbuf.resize(strlen(errbuf.c_str()));
		if(errmsg==NULL)
		{
			Server->Log(std::string("Error during cURL operation occured: ")+curl_easy_strerror(res)+" (ec="+nconvert(res)+"), "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg=std::string(curl_easy_strerror(res)) + "(ec=" + nconvert(res) + "), " + errbuf;
		}
	}

	curl_easy_cleanup(curl);
	return res==CURLE_OK;
}