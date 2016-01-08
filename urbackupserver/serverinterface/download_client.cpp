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

#include "action_header.h"
#include <algorithm>
#include "../../cryptoplugin/ICryptoFactory.h"
#include "../server_settings.h"

extern ICryptoFactory *crypto_fak;

namespace
{
	bool verify_signature(void)
	{
		if(crypto_fak==NULL)
			return false;

#ifdef _WIN32
		std::string pubkey_fn="urbackup_ecdsa409k1.pub";
#else
		std::string pubkey_fn="urbackup/urbackup_ecdsa409k1.pub";
#endif
		std::string p_pubkey_fn = Server->getServerParameter("urbackup_public_key");

		if(!p_pubkey_fn.empty())
		{
			pubkey_fn = p_pubkey_fn;
		}

		return crypto_fak->verifyFile(pubkey_fn, "urbackup/UrBackupUpdate.exe", "urbackup/UrBackupUpdate.sig2");
	}

	std::string constructClientSettings(Helper& helper, int clientid, const std::string& clientname, const std::string& authkey)
	{
		ServerSettings settings(helper.getDatabase(), clientid);
		SSettings *settingsptr=settings.getSettings();

		std::string ret="\r\n";
		ret+="internet_mode_enabled="+convert(settingsptr->internet_mode_enabled)+"\r\n";
		ret+="internet_server="+settingsptr->internet_server+"\r\n";
		ret+="internet_server_port="+convert(settingsptr->internet_server_port)+"\r\n";
		ret+="internet_authkey="+(authkey.empty() ? settingsptr->internet_authkey : authkey ) +"\r\n";
		if(!clientname.empty())
		{
			ret+="computername="+clientname+"\r\n";
		}
		return ret;
	}

	bool replaceToken(const std::string end_token, const std::string& repl, std::string& data, size_t& offset)
	{
		for(size_t i=offset;i<data.size();++i)
		{
			if(next(data, i, end_token) )
			{
				if(i-offset>repl.size())
				{
					data.replace(data.begin()+offset, data.begin()+offset+repl.size(), repl.begin(), repl.end());
					offset=i+end_token.size();
					return true;
				}
				else
				{
					Server->Log("Cannot replace, because data is too large", LL_ERROR);
					return false;
				}
			}
		}
		return false;
	}

	bool replaceStrings(Helper& helper, const std::string& settings, std::string& data)
	{
		const std::string settings_start_token="#48692563-17e4-4ccb-a078-f14372fdbe20";
		const std::string settings_end_token="#6e7f6ba0-8478-4946-b70a-f1c7e83d28cc";

		const std::string ident_start_token="#17460620-769b-4add-85aa-a764efe84ab7";
		const std::string ident_end_token="#569d42d2-1b40-4745-a426-e86a577c7f1a";

		bool replaced_settings=false;
		bool replaced_ident=false;

		for(size_t i=0;i<data.size();++i)
		{
			if(next(data, i, settings_start_token))
			{
				i+=settings_start_token.size();
				
				if(!replaceToken(settings_end_token, settings, data, i))
				{
					return false;
				}
				replaced_settings=true;
			}

			if(next(data, i, ident_start_token))
			{
				i+=ident_start_token.size();
				
				if(!replaceToken(ident_end_token, "\r\n"+helper.getStrippedServerIdentity()+"\r\n", data, i))
				{
					return false;
				}
				replaced_ident=true;
			}

			if( replaced_ident && replaced_settings)
				return true;
		}

		return false;
	}
}

ACTION_IMPL(download_client)
{
	Helper helper(tid, &GET, &PARAMS);

	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;

	bool all_client_rights;
	std::vector<int> clientids = helper.clientRights(RIGHT_SETTINGS, all_client_rights);

	std::string authkey = GET["authkey"];

	std::string errstr;
	if( session!=NULL && (all_client_rights || !clientids.empty() || !authkey.empty()) )
	{
		int clientid=watoi(GET["clientid"]);

		if(!authkey.empty() || all_client_rights ||
			std::find(clientids.begin(), clientids.end(), clientid)!=clientids.end() )
		{
			Server->Log("Verifying UrBackupUpdate.exe signature...", LL_INFO);
			if(verify_signature())
			{
				IQuery *q=helper.getDatabase()->Prepare("SELECT name FROM clients WHERE id=?");
				q->Bind(clientid);
				db_results res=q->Read();
				q->Reset();

				std::string clientname;
				if(!res.empty())
				{
					clientname=(res[0]["name"]);
				}

				std::string data=getFile("urbackup/UrBackupUpdate.exe");
				if( replaceStrings(helper, constructClientSettings(helper, clientid, clientname, authkey), data) )
				{
					Server->setContentType(tid, "application/octet-stream");
					Server->addHeader(tid, "Content-Disposition: attachment; filename=\"UrBackup Client ("+clientname+").exe\"");
					Server->addHeader(tid, "Content-Length: "+convert(data.size()) );
					Server->WriteRaw(tid, data.c_str(), data.size(), false);
				}
				else
				{
					errstr="Replacing data in install file failed";
				}
			}
			else
			{
				errstr="Signature verification failed";
			}
		}
		else
		{
			errstr="No right to download client";
		}
	}
	else
	{
		errstr="No right to download any client";
	}

	if(!errstr.empty())
	{
		Server->Log(errstr, LL_ERROR);
		helper.Write("ERROR: "+errstr);
	}
}