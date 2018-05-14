/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

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

	size_t read_string_callback(char *buffer, size_t size, size_t nitems, void *instream)
	{
		std::string* data = reinterpret_cast<std::string*>(instream);

		size_t bytes = size*nitems;

		size_t toread = (std::min)(bytes, data->size());

		if (toread > 0)
		{
			memcpy(buffer, &(*data)[0], toread);
			data->erase(0, toread);
		}

		return toread;
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

	curl_easy_setopt(curl, CURLOPT_URL, ("smtp://"+server.servername+":"+convert(server.port)).c_str());
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
			Server->Log(std::string("Error during cURL operation occurred: ")+curl_easy_strerror(res)+" ("+convert(res)+") -- "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg=std::string(curl_easy_strerror(res)) + "(ec=" + convert(res) + "), " + errbuf;
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
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
	
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
			Server->Log(std::string("Error during cURL operation occurred: ")+curl_easy_strerror(res)+" (ec="+convert(res)+"), "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg=std::string(curl_easy_strerror(res)) + "(ec=" + convert(res) + "), " + errbuf;
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
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

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
			Server->Log(std::string("Error during cURL operation occurred: ")+curl_easy_strerror(res)+" (ec="+convert(res)+"), "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg=std::string(curl_easy_strerror(res)) + "(ec=" + convert(res) + "), " + errbuf;
		}
	}

	curl_easy_cleanup(curl);
	return res==CURLE_OK;
}

bool UrlFactory::requestUrl(const std::string & url, str_map & params, std::string& ret, long& http_code, std::string * errmsg)
{
	if (errmsg != NULL)
	{
		errmsg->clear();
	}
	ret.clear();
	http_code = 0;

	CURL *curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

	if (!params["http_proxy"].empty())
	{
		curl_easy_setopt(curl, CURLOPT_PROXY, params["http_proxy"].c_str());
	}

	if (params.find("method") != params.end())
	{
		std::string method = strlower(params["method"]);

		if (method == "post")
		{
			curl_easy_setopt(curl, CURLOPT_POST, 1);
		}
		else if (method == "put")
		{
			curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
		}
		else if (method == "delete")
		{
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		}
	}

	if (params.find("transfer_encoding") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_TRANSFER_ENCODING, params["transfer_encoding"].c_str());
	}

	struct curl_slist *headers = NULL;
	if (params.find("content_type") != params.end())
	{
		headers = curl_slist_append(headers, ("content-type: "+params["content_type"]).c_str());
	}

	if (params.find("postdata") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(params["postdata"].size()));
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, params["postdata"].c_str());
	}

	if (params.find("putdata") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_string_callback);
		curl_easy_setopt(curl, CURLOPT_READDATA, params["postdata"].size());
		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(params["putdata"].size()));
	}

	if (params.find("addheader") != params.end())
	{
		headers = curl_slist_append(headers, params["addheader"].c_str());
	}

	size_t idx = 0;
	while (params.find("addheader"+convert(idx)) != params.end())
	{
		headers = curl_slist_append(headers, params["addheader" + convert(idx)].c_str());
		++idx;
	}

	if (params.find("cookie") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_COOKIE, params["cookie"].size());
	}

	if (params.find("expect_100_timeout_ms") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_EXPECT_100_TIMEOUT_MS, static_cast<long>(watoi(params["expect_100_timeout_ms"])));
	}

	if (params.find("timeout") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(watoi(params["timeout"])));
	}

	if (params.find("timeout_ms") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(watoi(params["timeout_ms"])));
	}

	if (params.find("low_speed_limit") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, static_cast<long>(watoi(params["low_speed_limit"])));
	}
	
	if (params.find("low_speed_time") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(watoi(params["low_speed_time"])));
	}

	if (params.find("connecttimeout") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(watoi(params["connecttimeout"])));
	}

	if (params.find("connecttimeout_ms") != params.end())
	{
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(watoi(params["connecttimeout_ms"])));
	}

	if (params.find("use_ssl") != params.end())
	{
		long level;
		std::string use_ssl = strlower(params["use_ssl"]);
		if (use_ssl == "try")
		{
			level = CURLUSESSL_TRY;
		}
		else if (use_ssl == "control")
		{
			level = CURLUSESSL_CONTROL;
		}
		else if (use_ssl == "all"
			|| use_ssl=="1"
			|| use_ssl=="true")
		{
			level = CURLUSESSL_ALL;
		}
		else
		{
			level = CURLUSESSL_NONE;
		}
		curl_easy_setopt(curl, CURLOPT_USE_SSL, level);
	}

	if (params.find("ssl_verifypeer") != params.end())
	{
		if (params["ssl_verifypeer"]=="0"
			|| strlower(params["ssl_verify_peer"])=="false")
		{
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
		}
	}

	std::string basic_authorization;
	if (params.find("basic_authorization") != params.end())
	{
		basic_authorization = base64_encode(reinterpret_cast<const unsigned char*>(params["basic_authorization"].c_str()),
			static_cast<unsigned int>(params["basic_authorization"].size()));
		basic_authorization = "Authorization: Basic " + basic_authorization;
		headers = curl_slist_append(headers, basic_authorization.c_str());
	}

	if (headers != NULL)
	{
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);

	std::string errbuf;
	errbuf.resize(CURL_ERROR_SIZE * 2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, (char*)errbuf.c_str());
	CURLcode res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	bool has_error = false;
	if (res != CURLE_OK)
	{
		errbuf.resize(strlen(errbuf.c_str()));
		if (errmsg == NULL)
		{
			Server->Log(std::string("Error during cURL operation occurred: ") + curl_easy_strerror(res) + " (ec=" + convert(res) + "), " + errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg = std::string(curl_easy_strerror(res)) + "(ec=" + convert(res) + "), " + errbuf;
		}
		has_error = true;
	}

	if (headers != NULL)
	{
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);
	return !has_error;
}

namespace
{
	std::vector<std::multimap<std::string, std::string> > parseLDIF(const std::string& output)
	{
		std::vector<std::string> lines;
		Tokenize(output, lines, "\n");
		std::vector<std::multimap<std::string, std::string> > ret;
		std::multimap<std::string, std::string> elem;
		for(size_t i=0;i<lines.size();++i)
		{
			const std::string& line = lines[i];
			if(line.find(":")==std::string::npos)
			{
				continue;
			}
			std::string key=trim(getuntil(":", line));
			std::string value=trim(getafter(":", line));
			if(!line.empty() && line[0]!='\t')
			{
				if(!elem.empty())
				{
					ret.push_back(elem);
				}
				elem.clear();
			}
			
			if(!line.empty() && !key.empty())
			{
				elem.insert(std::make_pair(key, value));
			}
		}
		if(!elem.empty())
		{
			ret.push_back(elem);
		}
		return ret;
	}
}

std::vector<std::multimap<std::string, std::string> > UrlFactory::queryLDAP( const std::string& url, const std::string& username, const std::string& password, std::string *errmsg)
{
	CURL *curl=curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
	curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());

	std::string output;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);

	std::string errbuf;
	errbuf.resize(CURL_ERROR_SIZE*2);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, (char*)errbuf.c_str());
	CURLcode res= curl_easy_perform(curl);
	if(res!=CURLE_OK)
	{
		errbuf.resize(strlen(errbuf.c_str()));
		if(errmsg==NULL)
		{
			Server->Log(std::string("Error during cURL operation occurred: ")+curl_easy_strerror(res)+" (ec="+convert(res)+"), "+errbuf, LL_DEBUG);
		}
		else
		{
			*errmsg=std::string(curl_easy_strerror(res)) + "(ec=" + convert(res) + "), " + errbuf;
		}

		output.clear();
	}

	curl_easy_cleanup(curl);
	return parseLDIF(output);
}
