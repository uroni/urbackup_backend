/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "helper.h"
#include "../../stringtools.h"
#include "../database.h"

#include <stdlib.h>
#include <algorithm>

extern std::string server_identity;

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

	if( session==NULL )
	{	
		session=Server->getSessionMgr()->getUser( (*GET)[L"ses"], widen((*PARAMS)["REMOTE_ADDR"]+(*PARAMS)["HTTP_USER_AGENT"]) );
	}

	//Get language from ACCEPT_LANGUAGE
	str_map::iterator lit=GET->find(L"lang");
	if(lit!=pGET->end() && lit->second!=L"-")
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

	if( db!=NULL )
	{
		tmpl->addValueTable(db, "translation_"+language );
	}

	if( invalid_session==true )
		tmpl->setValue(L"INVALID_SESSION",L"true");
	else if(session!=NULL)
		tmpl->setValue(L"SESSION", session->session);

	if( session!=NULL && session->id==-1 )
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
	if(session->id==0) return "all";

	if(getRightsInt("all")=="all")
		return "all";

	return getRightsInt(domain);
}

std::string Helper::getRightsInt(const std::string &domain)
{
	if(session==NULL) return "none";

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
	if(language=="de")
	{
		return "%d.%m.%Y %H:%M";
	}
	else
	{
		return "%Y-%m-%d %H:%M";
	}
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

bool Helper::checkPassword(const std::wstring &username, const std::wstring &password, int *user_id)
{
	IDatabase *db=getDatabase();
	IQuery *q=db->Prepare("SELECT id, name, password_md5 FROM settings_db.si_users WHERE name=?");
	q->Bind(username);
	db_results res=q->Read();
	if(!res.empty())
	{
		std::wstring password_md5=res[0][L"password_md5"];
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