/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "helper.h"
#include "../../stringtools.h"
#include "../database.h"

#include <stdlib.h>
#include <algorithm>
#include "../server_settings.h"
#include "../../urlplugin/IUrlFactory.h"
#include "../../urbackupcommon/glob.h"

extern std::string server_identity;
extern IUrlFactory *url_fak;

Helper::Helper(THREAD_ID pTID, str_map *pGET, str_nmap *pPARAMS)
{
	session=NULL;
	update(pTID,pGET,pPARAMS);
}

void Helper::update(THREAD_ID pTID, str_map *pGET, str_nmap *pPARAMS)
{
	tid=pTID;
	GET=pGET;
	PARAMS=pPARAMS;

	if(GET==NULL)
	{
		return;
	}

	if( session==NULL )
	{	
		session=Server->getSessionMgr()->getUser( (*GET)[L"ses"], widen((*PARAMS)["REMOTE_ADDR"]+(*PARAMS)["HTTP_USER_AGENT"]) );

		if(session!=NULL)
		{
			str_map::iterator it = session->mStr.find(L"ldap_rights");
			if(it!=session->mStr.end())
			{
				ldap_rights = parseRightsString(wnarrow(it->second));
			}
		}
	}

	//Get language from ACCEPT_LANGUAGE
	str_map::iterator lit=GET->find(L"lang");
	if(lit!=GET->end() && lit->second!=L"-")
	{
		language=wnarrow(lit->second);
	}
	else
	{
		std::wstring langs=(*GET)[L"langs"];
		std::vector<std::wstring> clangs;
		Tokenize(langs, clangs, L",");
		for(size_t j=0;j<clangs.size();++j)
		{
			clangs[j]=strlower(clangs[j]);
		}
		str_nmap::iterator al=PARAMS->find("ACCEPT_LANGUAGE");
		if(al==PARAMS->end())
			al=PARAMS->find("HTTP_ACCEPT_LANGUAGE");
		
		if(al!=PARAMS->end())
		{
			std::vector<std::string> toks;
			Tokenize(al->second, toks, ",");
			for(size_t i=0;i<toks.size();++i)
			{
				std::string lstr=getuntil(";", toks[i]);
				if(lstr.empty())
					lstr=toks[i];

				std::string prefix=getuntil("-", lstr);
				if(prefix.empty())
					prefix=lstr;

				std::string sub=getafter("-", lstr);

				if(language.empty())
				{
					if(std::find(clangs.begin(), clangs.end(), strlower(widen(prefix+"_"+sub)))!=clangs.end())
					{
						language=strlower(prefix+"_"+sub);
						break;
					}

					if(std::find(clangs.begin(), clangs.end(), strlower(widen(prefix)))!=clangs.end())
					{
						language=strlower(prefix);
						break;
					}
				}
			}

			if(language.empty())
			{
				language="en";
			}
		}
		else
		{
			language="en";
		}
	}

	if( session==NULL)
		invalid_session=true;
	else
		invalid_session=false;
}

SUser *Helper::getSession(void)
{
	return session;
}

void Helper::OverwriteLanguage( std::string pLanguage)
{
	language=pLanguage;
}

ITemplate *Helper::createTemplate(std::string name)
{
	IDatabase* db=NULL;//Server->getDatabase(tid, TRANSLATIONDB);

	ITemplate *tmpl=Server->createTemplate("urbackup/templates/"+name);

	/*if( db!=NULL )
	{
		tmpl->addValueTable(db, "translation_"+language );
	}*/

	if( invalid_session==true )
		tmpl->setValue(L"INVALID_SESSION",L"true");
	else if(session!=NULL)
		tmpl->setValue(L"SESSION", session->session);

	if( session!=NULL && session->id==SESSION_ID_INVALID )
		tmpl->setValue(L"INVALID_ID",L"true");

	templates.push_back( tmpl );

	return tmpl;
}

Helper::~Helper(void)
{
	if( session!=NULL )
		Server->getSessionMgr()->releaseUser(session);

	for(size_t i=0;i<templates.size();++i)
	{
		Server->destroy( templates[i] );
	}
}

void Helper::Write(std::string str)
{
	Server->Write( tid, str );
}

void Helper::WriteTemplate(ITemplate *tmpl)
{
	Server->Write( tid, tmpl->getData() );
}

IDatabase *Helper::getDatabase(void)
{
	return Server->getDatabase(tid, URBACKUPDB_SERVER);
}

std::wstring Helper::generateSession(std::wstring username)
{
	return Server->getSessionMgr()->GenerateSessionIDWithUser( username, widen((*PARAMS)["REMOTE_ADDR"]+(*PARAMS)["HTTP_USER_AGENT"]) );
}

std::string Helper::getRights(const std::string &domain)
{
	if(session==NULL) return "none";
	if(session->id==SESSION_ID_ADMIN) return "all";
	if(session->id==SESSION_ID_TOKEN_AUTH && ldap_rights.empty()) return "none";

	if(getRightsInt("all")=="all")
		return "all";

	return getRightsInt(domain);
}

std::string Helper::getRightsInt(const std::string &domain)
{
	if(session==NULL) return "none";

	std::map<std::string, std::string>::iterator it=ldap_rights.find(domain);
	if(it!=ldap_rights.end())
	{
		return it->second;
	}

	IQuery *q=getDatabase()->Prepare("SELECT t_right FROM settings_db.si_permissions WHERE clientid=? AND t_domain=?");
	q->Bind(session->id);
	q->Bind(domain);
	db_results res=q->Read();
	q->Reset();
	if(!res.empty())
	{
		return wnarrow(res[0][L"t_right"]);
	}
	else
	{
		return "none";
	}
}

void Helper::releaseAll(void)
{
	if(session!=NULL)
	{
		Server->getSessionMgr()->releaseUser(session);
		session=NULL;
	}
}

std::string Helper::getTimeFormatString(void)
{
	return "%Y-%m-%d %H:%M";
}

std::string Helper::getLanguage(void)
{
	return language;
}

std::vector<int> Helper::getRightIDs(std::string rights)
{
	std::vector<int> clientid;
	if(rights!="all" && rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			clientid.push_back(atoi(s_clientid[i].c_str()));
		}
	}
	return clientid;
}

bool Helper::hasRights(int clientid, std::string rights, std::vector<int> right_ids)
{
	bool r_ok=false;
	if(rights!="all")
	{
		for(size_t i=0;i<right_ids.size();++i)
		{
			if(right_ids[i]==clientid)
			{
				r_ok=true;
				break;
			}
		}
	}
	else
	{
		r_ok=true;
	}
	return r_ok;
}

bool Helper::checkPassword(const std::wstring &username, const std::wstring &password, int *user_id, bool plainpw)
{
	IDatabase *db=getDatabase();
	IQuery *q=db->Prepare("SELECT id, name, password_md5, salt FROM settings_db.si_users WHERE name=?");
	q->Bind(username);
	db_results res=q->Read();
	if(!res.empty())
	{
		std::wstring password_md5=res[0][L"password_md5"];
		if(!plainpw)
		{
			std::string ui_password=wnarrow(password);
			std::string r_password=Server->GenerateHexMD5(Server->ConvertToUTF8(session->mStr[L"rnd"]+password_md5));
			if(r_password!=ui_password)
			{
				return false;
			}
			else
			{
				if(user_id!=NULL)
				{
					*user_id=watoi(res[0][L"id"]);
				}
				return true;
			}
		}
		else
		{
			std::string db_password = Server->GenerateHexMD5(Server->ConvertToUTF8(res[0][L"salt"]+password));
			if(db_password!=wnarrow(password_md5))
			{
				return false;
			}
			else
			{
				if(user_id!=NULL)
				{
					*user_id=watoi(res[0][L"id"]);
				}
				return true;
			}
		}
	}

	return false;
}

std::vector<int> Helper::clientRights(const std::string& right_name, bool& all_client_rights)
{
	std::string rights=getRights(right_name);
	std::vector<int> clientid;
	if(rights!="all" && rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			clientid.push_back(atoi(s_clientid[i].c_str()));
		}
	}

	if(rights=="all")
	{
		all_client_rights=true;
	}
	else
	{
		all_client_rights=false;
	}

	return clientid;
}

std::string Helper::getStrippedServerIdentity(void)
{
	std::string ret=server_identity;
	if(ret.size()>3)
	{
		if(next(ret, 0, "#I"))
		{
			ret=ret.substr(2);
		}
		
		if(ret[ret.size()-1]=='#')
		{
			ret=ret.substr(0, ret.size()-1);
		}
	}
	return ret;
}

void Helper::sleep(unsigned int ms)
{
	if(session!=NULL)
	{
		Server->getSessionMgr()->releaseUser(session);
	}

	Server->wait(ms);

	if(session!=NULL)
	{
		Server->getSessionMgr()->lockUser(session);
	}
}

bool Helper::ldapEnabled()
{
	IDatabase *db=getDatabase();
	IQuery *q=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=0 AND key='ldap_login_enabled'");
	if(q!=NULL)
	{
		db_results res = q->Read();
		if(!res.empty())
		{
			return res[0][L"value"]==L"true";
		}
	}
	return false;
}

bool Helper::ldapLogin( const std::wstring &username, const std::wstring &password,
	std::string* ret_errmsg, std::string* rights, bool dry_login)
{
	if(url_fak==NULL)
	{
		return false;
	}

	ServerSettings settings(getDatabase());
	SLDAPSettings ldap_settings = settings.getLDAPSettings();

	std::wstring sanitized_username = username;
	std::wstring to_sanitize = L"\"[]:;|=+*?<>/\\,";
	for(size_t i=0;i<sanitized_username.size();++i)
	{
		if(std::find(to_sanitize.begin(), to_sanitize.end(), sanitized_username[i])!=to_sanitize.end())
		{
			sanitized_username[i]='_';
		}
	}

	std::wstring group_class_query = greplace(L"{USERNAME}", sanitized_username, Server->ConvertToUnicode(ldap_settings.group_class_query));

	std::string ldap_query = "ldap://" + ldap_settings.server_name +
		(ldap_settings.server_port>0?(":" + nconvert(ldap_settings.server_port)):"") +
		"/" + Server->ConvertToUTF8(group_class_query);

	std::string errmsg;
	std::vector<std::multimap<std::string, std::string> > data = url_fak->queryLDAP(ldap_query, ldap_settings.username_prefix+Server->ConvertToUTF8(username)+ldap_settings.username_suffix,
		Server->ConvertToUTF8(password), &errmsg);

	if(data.empty())
	{
		if(!errmsg.empty())
		{
			Server->Log("Login via LDAP failed: "+errmsg, LL_ERROR);

			if(ret_errmsg)
			{
				*ret_errmsg = errmsg;
			}
		}
		return false;
	}
	else
	{
		if(data.size()!=1)
		{
			Server->Log("LDAP query returned "+nconvert(data.size())+" items, but should return only one. Login failed.", LL_ERROR);
			if(ret_errmsg)
			{
				*ret_errmsg="LDAP query returned more than one result";
			}
			return false;
		}

		IQuery* q = getDatabase()->Prepare("SELECT clientid FROM users_on_client WHERE username=?");
		q->Bind(username);
		db_results db_res = q->Read();
		q->Reset();

		std::string autoclients;
		for(size_t i=0;i<db_res.size();++i)
		{
			if(!autoclients.empty()) autoclients+=",";
			autoclients+=wnarrow(db_res[i][L"clientid"]);
		}

		std::multimap<std::string, std::string> sdata = data[0];

		std::string str_ldap_rights;

		std::multimap<std::string, std::string>::iterator it = sdata.find(ldap_settings.group_key_name);
		while(it!=sdata.end() && it->first == ldap_settings.group_key_name)
		{
			for(std::map<std::wstring, std::wstring>::iterator it_rights=
				ldap_settings.group_rights_map.begin();it_rights!=ldap_settings.group_rights_map.end();
				++it_rights)
			{
				if(amatch(Server->ConvertToUnicode(it->second).c_str(),
					it_rights->first.c_str()))
				{
					str_ldap_rights = greplace("{AUTOCLIENTS}", autoclients, Server->ConvertToUTF8(it_rights->second));
					break;
				}
			}
			
			++it;
		}

		if(str_ldap_rights.empty())
		{
			std::multimap<std::string, std::string>::iterator it = sdata.find(ldap_settings.class_key_name);
			while(it!=sdata.end() && it->first == ldap_settings.class_key_name)
			{
				for(std::map<std::wstring, std::wstring>::iterator it_rights=
					ldap_settings.class_rights_map.begin();it_rights!=ldap_settings.class_rights_map.end();
					++it_rights)
				{
					if(amatch(Server->ConvertToUnicode(it->second).c_str(),
						it_rights->first.c_str()))
					{
						str_ldap_rights = greplace("{AUTOCLIENTS}", autoclients, Server->ConvertToUTF8(it_rights->second));
						break;
					}
				}

				++it;
			}
		}

		if(!str_ldap_rights.empty() && !dry_login)
		{
			ldap_rights = parseRightsString(str_ldap_rights);
		}

		if(!str_ldap_rights.empty() && session!=NULL && !dry_login)
		{
			session->mStr[L"ldap_rights"] = widen(str_ldap_rights);
		}

		q= getDatabase()->Prepare("SELECT token FROM user_tokens WHERE username = ?");
		q->Bind(username);
		db_res = q->Read();
		q->Reset();

		std::wstring fileaccesstokens;
		for(size_t i=0;i<db_res.size();++i)
		{
			if(!fileaccesstokens.empty()) fileaccesstokens+=L";";
			fileaccesstokens+=db_res[i][L"token"];
		}

		if(!fileaccesstokens.empty() && !dry_login)
		{
			session->mStr[L"fileaccesstokens"]=fileaccesstokens;
		}

		if(rights)
		{
			*rights = str_ldap_rights;
		}

		return !str_ldap_rights.empty();
	}
}

std::map<std::string, std::string> Helper::parseRightsString( const std::string& rights )
{
	std::vector<std::string> toks;
	Tokenize(rights, toks, ",");
	std::map<std::string, std::string> ret;
	for(size_t i=0;i<toks.size();++i)
	{
		std::string domain = getuntil("=", toks[i]);
		std::string right = getafter("=", toks[i]);
		ret[domain]=right;
	}
	return ret;
}
