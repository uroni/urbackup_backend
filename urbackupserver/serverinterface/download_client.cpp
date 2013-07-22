#include "action_header.h"
#include <algorithm>
#include "../../cryptoplugin/ICryptoFactory.h"
#include "../server_settings.h"

extern ICryptoFactory *crypto_fak;
extern std::string server_identity;

namespace
{
	bool verify_signature(void)
	{
		if(crypto_fak==NULL)
			return false;

		return crypto_fak->verifyFile("urbackup/urbackup_dsa.pub", "urbackup/UrBackupUpdate.exe", "urbackup/UrBackupUpdate.sig");
	}

	std::string constructClientSettings(Helper& helper, int clientid)
	{
		ServerSettings settings(helper.getDatabase(), clientid);
		SSettings *settingsptr=settings.getSettings();

		std::string ret="\r\n";
		ret+="internet_mode_enabled="+nconvert(settingsptr->internet_mode_enabled)+"\r\n";
		ret+="internet_server="+settingsptr->internet_server+"\r\n";
		ret+="internet_server_port="+nconvert(settingsptr->internet_server_port)+"\r\n";
		ret+="internet_authkey="+settingsptr->internet_authkey+"\r\n";
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

	bool replaceStrings(const std::string& settings, std::string& data)
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
				
				if(!replaceToken(ident_end_token, "\r\n"+server_identity+"\r\n", data, i))
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
	if(session!=NULL && session->id==-1) return;

	bool all_client_rights;
	std::vector<int> clientids = helper.clientRights(RIGHT_SETTINGS, all_client_rights);

	std::string errstr;
	if( session!=NULL && (all_client_rights || !clientids.empty() ) )
	{
		int clientid=watoi(GET[L"clientid"]);

		if(all_client_rights ||
			std::find(clientids.begin(), clientids.end(), clientid)!=clientids.end() )
		{
			Server->Log("Verifying UrBackupUpdate.exe signature...", LL_INFO);
			if(verify_signature())
			{
				std::string data=getFile("urbackup/UrBackupUpdate.exe");
				if( replaceStrings(constructClientSettings(helper, clientid), data) )
				{
					Server->setContentType(tid, "application/octet-stream");
					Server->addHeader(tid, "Content-Disposition: attachment; filename=\"UrBackup Client.exe\"");
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

	Server->Log(errstr, LL_ERROR);
	helper.Write("ERROR: "+errstr);
}