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

#include <Windows.h>
#include <LM.h>
#include <Sddl.h>
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "database.h"
#include "clientdao.h"
#include "Aclapi.h"
#include "tokens.h"
#include "../common/data.h"
#include <assert.h>
#include "../Interface/Types.h"

#pragma comment(lib, "netapi32.lib")

namespace tokens
{

	const DWORD max_dc_user_groups = 100;
	const DWORD max_dc_users = 100;
	const DWORD max_dc_groups = 100;

struct Token
{
	int64 id;
	bool is_user;
};

struct TokenCacheInt
{
	std::map<std::vector<char>, Token> tokens;
};


TokenCache::TokenCache( const TokenCache& other )
{
	assert(false);
}

TokenCache::TokenCache() : token_cache(NULL)
{

}


TokenCache::~TokenCache()
{

}


void TokenCache::operator=(const TokenCache& other )
{
	assert(false);
}

TokenCacheInt* TokenCache::get()
{
	return token_cache.get();
}

void TokenCache::reset(TokenCacheInt* cache)
{
	token_cache.reset(cache);
}

std::wstring get_dc_name()
{
	LPBYTE dc_name;
	if (NetGetDCName(NULL, NULL, &dc_name) != NERR_Success)
	{
		return std::wstring();
	}
	
	std::wstring ret(reinterpret_cast<wchar_t*>(dc_name));
	NetApiBufferFree(dc_name);

	return ret;
}

std::string get_domain_account(const wchar_t* hostname, const wchar_t* accountname)
{
	std::vector<char> sid_buffer;
	sid_buffer.resize(sizeof(SID));
	DWORD account_sid_size = sizeof(SID);
	SID_NAME_USE sid_name_use;
	std::wstring referenced_domain;
	referenced_domain.resize(1);
	DWORD referenced_domain_size = 1;

	BOOL b = LookupAccountNameW(hostname,
		accountname,
		&sid_buffer[0], &account_sid_size, &referenced_domain[0],
		&referenced_domain_size, &sid_name_use);

	if (!b && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		referenced_domain.resize(referenced_domain_size);
		sid_buffer.resize(account_sid_size);
		b = LookupAccountNameW(hostname,
			accountname,
			&sid_buffer[0], &account_sid_size, &referenced_domain[0],
			&referenced_domain_size, &sid_name_use);
	}

	if (referenced_domain.size() != referenced_domain_size)
	{
		referenced_domain.resize(referenced_domain_size);
	}

	std::string pub_accountname = Server->ConvertFromWchar(accountname);
	if (referenced_domain_size>0)
	{
		pub_accountname = Server->ConvertFromWchar(referenced_domain) + "\\" + pub_accountname;
	}

	return pub_accountname;
}

std::vector<std::string> get_dc_users()
{
	std::wstring dc_name = get_dc_name();
	if (dc_name.empty())
	{
		return std::vector<std::string>();
	}

	LPUSER_INFO_0 buf;
	DWORD prefmaxlen = max_dc_users;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	DWORD resume_handle = 0;
	NET_API_STATUS status;
	std::vector<std::string> ret;

	do
	{
		status = NetUserEnum(dc_name.c_str(), 0, 0,
			(LPBYTE*)&buf, prefmaxlen, &entriesread,
			&totalentries, &resume_handle);

		if (status == NERR_Success || status == ERROR_MORE_DATA)
		{
			for (DWORD i = 0; i<entriesread; ++i)
			{
				LPUSER_INFO_0 user_info = (buf + i);
				ret.push_back(get_domain_account(dc_name.c_str(), user_info->usri0_name));
			}
		}
		else
		{
			Server->Log("Error while enumerating DC users: " + convert((int)status), LL_ERROR);
		}

		if (status == ERROR_MORE_DATA
			&& ret.size() >= max_dc_users)
		{
			Server->Log("Limiting number of DC users to " + convert(ret.size()), LL_WARNING);
			status = NERR_Success;
		}

		if (buf != NULL)
		{
			NetApiBufferFree(buf);
		}
	} while (status == ERROR_MORE_DATA);

	return ret;
}

std::vector<std::string> get_users()
{
	LPUSER_INFO_0 buf;
	DWORD prefmaxlen = MAX_PREFERRED_LENGTH;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	DWORD resume_handle = 0;
	NET_API_STATUS status;
	std::vector<std::string> ret;

	do 
	{
		 status = NetUserEnum(NULL, 0, 0,
			(LPBYTE*)&buf, prefmaxlen, &entriesread,
			&totalentries, &resume_handle);
		 
		 if (status == NERR_Success || status == ERROR_MORE_DATA)
		 {
			 for(DWORD i=0;i<entriesread;++i)
			 {
				 LPUSER_INFO_0 user_info = (buf+i);
				 ret.push_back(Server->ConvertFromWchar(user_info->usri0_name));
			 }
		 }
		 else
		 {
			 Server->Log("Error while enumerating users: "+ convert((int)status), LL_ERROR);
		 }

		 if(buf!=NULL)
		 {
			 NetApiBufferFree(buf);
		 }
	}
	while (status == ERROR_MORE_DATA);

	std::vector<std::string> dc_users = get_dc_users();
	ret.insert(ret.end(), dc_users.begin(), dc_users.end());

	return ret;
}


std::vector<std::string> get_dc_user_groups(std::string username)
{
	std::wstring dc_name = get_dc_name();
	if (dc_name.empty())
	{
		return std::vector<std::string>();
	}

	if (username.find("\\") != std::string::npos)
	{
		username = getafter("\\", username);
	}

	LPGROUP_USERS_INFO_0 buf;
	DWORD prefmaxlen = max_dc_user_groups;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	DWORD resume_handle = 0;
	NET_API_STATUS status;
	std::vector<std::string> ret;

	status = NetUserGetGroups(dc_name.c_str(), Server->ConvertToWchar(username).c_str(), 0,
		(LPBYTE*)&buf, prefmaxlen, &entriesread,
		&totalentries);

	if (status == NERR_Success)
	{
		for (DWORD i = 0; i<entriesread; ++i)
		{
			LPGROUP_USERS_INFO_0 user_info = (buf + i);
			ret.push_back(get_domain_account(dc_name.c_str(), user_info->grui0_name));
		}
	}
	else
	{
		Server->Log("Error while enumerating DC user groups: " + convert((int)status), LL_ERROR);
	}

	if (buf != NULL)
	{
		NetApiBufferFree(buf);
	}

	return ret;
}

std::vector<std::string> get_user_groups(const std::string& username)
{
	LPLOCALGROUP_USERS_INFO_0 buf;
	DWORD prefmaxlen = MAX_PREFERRED_LENGTH;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	DWORD resume_handle = 0;
	NET_API_STATUS status;
	std::vector<std::string> ret;

	status = NetUserGetLocalGroups(NULL, Server->ConvertToWchar(username).c_str(), 0, LG_INCLUDE_INDIRECT,
		(LPBYTE*)&buf, prefmaxlen, &entriesread,
		&totalentries);

	if (status == NERR_Success)
	{
		for(DWORD i=0;i<entriesread;++i)
		{
			LPLOCALGROUP_USERS_INFO_0 user_info = (buf+i);
			ret.push_back(Server->ConvertFromWchar(user_info->lgrui0_name));
		}
	}
	else
	{
		Server->Log("Error while enumerating user groups: "+ convert((int)status), LL_ERROR);
	}

	if(buf!=NULL)
	{
		NetApiBufferFree(buf);
	}

	if (username.find("\\") != std::string::npos)
	{
		std::vector<std::string> dc_user_groups = get_dc_user_groups(username);
		ret.insert(ret.end(), dc_user_groups.begin(), dc_user_groups.end());
	}

	return ret;
}

std::vector<std::string> get_dc_groups()
{
	std::vector<std::string> ret;
	std::wstring dc_name = get_dc_name();
	if (!dc_name.empty())
	{
		std::string dc_prefix = Server->ConvertFromWchar(dc_name);
		if (next(dc_prefix, 0, "\\\\"))
		{
			dc_prefix = dc_prefix.substr(2);
		}

		DWORD prefmaxlen = max_dc_groups;
		DWORD entriesread = 0;
		DWORD totalentries = 0;
		NET_API_STATUS status;
		DWORD_PTR resume_handle = 0;
		LPGROUP_INFO_0 buf2;
		do
		{
			status = NetGroupEnum(dc_name.c_str(), 0,
				(LPBYTE*)&buf2, prefmaxlen, &entriesread,
				&totalentries, &resume_handle);

			if (status == NERR_Success || status == ERROR_MORE_DATA)
			{
				for (DWORD i = 0; i < entriesread; ++i)
				{
					LPGROUP_INFO_0 group_info = (buf2 + i);
					ret.push_back(get_domain_account(dc_name.c_str(), group_info->grpi0_name));
				}
			}
			else
			{
				Server->Log("Error while enumerating groups: " + convert((int)status), LL_ERROR);
			}

			if (status == ERROR_MORE_DATA
				&& ret.size() >= max_dc_groups)
			{
				Server->Log("Limiting number of DC groups to "+convert(ret.size()), LL_WARNING);
				status = NERR_Success;
			}

			if (buf2 != NULL)
			{
				NetApiBufferFree(buf2);
			}
		} while (status == ERROR_MORE_DATA);
	}

	return ret;
}

std::vector<std::string> get_groups()
{
	LPLOCALGROUP_INFO_0 buf;
	DWORD prefmaxlen = MAX_PREFERRED_LENGTH;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	NET_API_STATUS status;
	std::vector<std::string> ret;
	DWORD_PTR resume_handle = 0;
	do
	{
		status = NetLocalGroupEnum(NULL, 0,
			(LPBYTE*)&buf, prefmaxlen, &entriesread,
			&totalentries, &resume_handle);

		if (status == NERR_Success || status == ERROR_MORE_DATA)
		{
			for (DWORD i = 0; i<entriesread; ++i)
			{
				LPLOCALGROUP_INFO_0 group_info = (buf + i);
				ret.push_back(Server->ConvertFromWchar(group_info->lgrpi0_name));
			}
		}
		else
		{
			Server->Log("Error while enumerating groups: " + convert((int)status), LL_ERROR);
		}

		if (buf != NULL)
		{
			NetApiBufferFree(buf);
		}
	} while (status == ERROR_MORE_DATA);

	std::vector<std::string> dc_groups = get_dc_groups();
	ret.insert(ret.end(), dc_groups.begin(), dc_groups.end());

	return ret;
}

bool sid_has_profile(const std::string& account_sid)
{
	HKEY res;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		Server->ConvertToWchar("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\" + account_sid).c_str(),
		0, KEY_READ, &res) == ERROR_SUCCESS)
	{
		RegCloseKey(res);
		return true;
	}
	return false;
}

bool write_token( std::string hostname, bool is_user, std::string accountname, const std::string &token_fn, ClientDAO &dao, const std::string& ext_token)
{
	std::vector<char> sid_buffer;
	sid_buffer.resize(sizeof(SID));

	DWORD account_sid_size = sizeof(SID);
	SID_NAME_USE sid_name_use;
	std::wstring referenced_domain;
	referenced_domain.resize(1);
	DWORD referenced_domain_size = 1;

	BOOL b = false;

	bool lookup_user = is_user;
	while (!b)
	{
		std::string local_username;
		if (accountname.find("\\") != std::string::npos)
		{
			local_username = accountname;
		}
		else if (lookup_user)
		{
			local_username = hostname + "\\" + accountname;
		}

		b = LookupAccountNameW(NULL,
			Server->ConvertToWchar(local_username).c_str(),
			&sid_buffer[0], &account_sid_size, &referenced_domain[0],
			&referenced_domain_size, &sid_name_use);

		if (!b && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			referenced_domain.resize(referenced_domain_size);
			sid_buffer.resize(account_sid_size);
			b = LookupAccountNameW(NULL,
				Server->ConvertToWchar(local_username).c_str(),
				&sid_buffer[0], &account_sid_size, &referenced_domain[0],
				&referenced_domain_size, &sid_name_use);
		}

		if (!b && lookup_user)
		{
			lookup_user = false;
			sid_buffer.resize(sizeof(SID));
			account_sid_size = sizeof(SID);
		}
		else
		{
			break;
		}
	}

	if(!b)
	{
		std::string errmsg;
		int64 err = os_last_error(errmsg);
		Server->Log("Error getting account SID of user "+accountname + ". Code: " + convert(err) + " - " + errmsg, LL_ERROR);
		return false;
	}

	if (referenced_domain.size() != referenced_domain_size)
	{
		referenced_domain.resize(referenced_domain_size);
	}

	SID* account_sid = reinterpret_cast<SID*>(&sid_buffer[0]);

	LPWSTR str_account_sid;
	b = ConvertSidToStringSidW(account_sid, &str_account_sid);
	if(!b)
	{
		Server->Log("Error converting SID to string SID. Errorcode: "+convert((int)GetLastError()), LL_ERROR);
		return false;
	}

	std::wstring dacl = std::wstring(L"D:(A;OICI;GA;;;") + str_account_sid + L")"
		+ L"(A;OICI;GA;;;BA)";

	std::string local_account_sid = Server->ConvertFromWchar(str_account_sid);

	LocalFree(str_account_sid);

	SECURITY_ATTRIBUTES  sa;      
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = FALSE;


	b=ConvertStringSecurityDescriptorToSecurityDescriptor(
		dacl.c_str(),
		SDDL_REVISION_1,
		&(sa.lpSecurityDescriptor),
		NULL);

	if(!b)
	{
		Server->Log("Error creating security descriptor. Errorcode: "+convert((int)GetLastError()), LL_ERROR);
		return false;
	}

	HANDLE file = CreateFileW(Server->ConvertToWchar(token_fn).c_str(),
		GENERIC_READ | GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, 0, NULL);

	if(file==INVALID_HANDLE_VALUE)
	{
		Server->Log("Error opening file. Errorcode: "+convert((int)GetLastError()), LL_ERROR);
		LocalFree(sa.lpSecurityDescriptor);
		return false;
	}

	std::string token;
	if (ext_token.empty())
	{
		token = Server->secureRandomString(20);
	}
	else
	{
		token = ext_token;
	}

	int i_is_user;
	if (is_user)
	{
		if (sid_has_profile(local_account_sid))
		{
			i_is_user = ClientDAO::c_is_user;
		}
		else
		{
			i_is_user = ClientDAO::c_is_system_user;
		}		
	}
	else
	{
		i_is_user = ClientDAO::c_is_group;
	}

	dao.updateFileAccessToken(accountname_normalize(accountname), token, i_is_user);

	DWORD written=0;
	while(written<token.size())
	{
		b = WriteFile(file, token.data()+written, static_cast<DWORD>(token.size())-written, &written, NULL);
		if(!b)
		{
			Server->Log("Error writing to token file.  Errorcode: "+convert((int)GetLastError()), LL_ERROR);
			CloseHandle(file);
			LocalFree(sa.lpSecurityDescriptor);
			return true;
		}
	}

	CloseHandle(file);
	LocalFree(sa.lpSecurityDescriptor);

	return true;
}

std::string permissions_allow_all()
{
	CWData token_info;
	//allow to all
	token_info.addChar(ID_GRANT_ACCESS);
	token_info.addVarInt(0);

	return std::string(token_info.getDataPtr(), token_info.getDataSize());
}

std::string get_hostname()
{
	char hostname_c[MAX_PATH];
	hostname_c[0]=0;
	gethostname(hostname_c, MAX_PATH);

	std::string hostname = hostname_c;
	if(!hostname.empty())
		hostname+="\\";

	return hostname;
}

bool read_account_sid( std::vector<char>& sid, std::string hostname, std::string accountname, bool is_user )
{
	sid.resize(sizeof(SID));
	DWORD account_sid_size = sizeof(SID);
	SID_NAME_USE sid_name_use;
	std::wstring referenced_domain;
	referenced_domain.resize(1);
	DWORD referenced_domain_size = 1;

	BOOL b = FALSE;

	std::string local_username = accountname;
	if (local_username.find("\\") == std::string::npos
		&& is_user )
	{
		local_username = hostname + accountname;
	}

	while (!b)
	{
		b = LookupAccountNameW(NULL, Server->ConvertToWchar(local_username).c_str(),
			&sid[0], &account_sid_size, &referenced_domain[0],
			&referenced_domain_size, &sid_name_use);

		if (!b && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			referenced_domain.resize(referenced_domain_size);
			sid.resize(account_sid_size);
			b = LookupAccountNameW(NULL, Server->ConvertToWchar(local_username).c_str(),
				&sid[0], &account_sid_size, &referenced_domain[0],
				&referenced_domain_size, &sid_name_use);
		}

		if (!b && is_user)
		{
			is_user = false;
			sid.resize(sizeof(SID));
			account_sid_size = sizeof(SID);
		}
		else
		{
			break;
		}
	}

	if(!b)
	{
		sid.clear();
		return false;
	}

	return true;
}


void read_all_tokens(ClientDAO* dao, TokenCache& token_cache)
{
	TokenCacheInt* cache = new TokenCacheInt;
	token_cache.reset(cache);

	std::vector<std::string> users = get_users();

	char hostname_c[MAX_PATH];
	hostname_c[0]=0;
	gethostname(hostname_c, MAX_PATH);

	std::string hostname = hostname_c;
	if(!hostname.empty())
		hostname+="\\";

	for(size_t i=0;i<users.size();++i)
	{
		Token new_token;

		std::vector<char> sid;
		if(!read_account_sid(sid, hostname, users[i], true))
		{
			std::string errmsg;
			int64 err = os_last_error(errmsg);
			Server->Log("Cannot get account SID for user "+users[i]+" code: "+convert(err)+" - "+errmsg, LL_ERROR);
			continue;
		}

		ClientDAO::CondInt64 token_id = dao->getFileAccessTokenId2Alts(accountname_normalize(users[i]), ClientDAO::c_is_user, ClientDAO::c_is_system_user);

		if(!token_id.exists)
		{
			Server->Log("Token id for user not found", LL_ERROR);
			continue;
		}

		new_token.id=token_id.value;
		new_token.is_user=true;

		cache->tokens[sid]=new_token;
	}

	std::vector<std::string> groups = get_groups();

	for(size_t i=0;i<groups.size();++i)
	{
		Token new_token;
		
		std::vector<char> sid;
		if(!read_account_sid(sid, hostname, groups[i], false))
		{
			std::string errmsg;
			int64 err = os_last_error(errmsg);
			Server->Log("Cannot get account SID for group "+groups[i] + " code: " + convert(err) + " - " + errmsg, LL_ERROR);
			continue;
		}

		ClientDAO::CondInt64 token_id = dao->getFileAccessTokenId(accountname_normalize(groups[i]), ClientDAO::c_is_group);

		if(!token_id.exists)
		{
			Server->Log("Token id for user not found", LL_ERROR);
			continue;
		}

		new_token.id=token_id.value;
		new_token.is_user=false;

		cache->tokens[sid]=new_token;
	}
}

std::string get_file_tokens( const std::string& fn, ClientDAO* dao, TokenCache& token_cache )
{
	if(token_cache.get()==NULL)
	{
		read_all_tokens(dao, token_cache);
	}

	TokenCacheInt* cache = token_cache.get();

	PACL dacl=NULL;
	PSECURITY_DESCRIPTOR sec_desc;

	DWORD rc = GetNamedSecurityInfoW(Server->ConvertToWchar(fn).c_str(), SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION|UNPROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, &dacl,
		NULL, &sec_desc);

	if(rc!=ERROR_SUCCESS)
	{
		Server->Log("Error getting DACL of file \""+fn+"\". Errorcode: "+convert((int)rc), LL_ERROR);
		return std::string();
	}

	CWData token_info;

	if(dacl==NULL || dacl->AceCount==0)
	{
		//allow to all
		token_info.addChar(ID_GRANT_ACCESS);
		token_info.addVarInt(0);
	}

	if(dacl!=NULL)
	{
		char* ptr=reinterpret_cast<char*>(dacl+1);
		for(WORD i=0;i<dacl->AceCount;++i)
		{
			ACE_HEADER* header = reinterpret_cast<ACE_HEADER*>(ptr);
			std::vector<char> sid;
			bool allow;
			bool known;
			ACCESS_MASK mask;
			switch(header->AceType)
			{
			case ACCESS_ALLOWED_ACE_TYPE:
				{
					ACCESS_ALLOWED_ACE* allowed_ace = reinterpret_cast<ACCESS_ALLOWED_ACE*>(ptr);

					size_t sid_size = header->AceSize+sizeof(DWORD)-sizeof(ACCESS_ALLOWED_ACE);
					sid.resize(sid_size);
					memcpy(&sid[0], &allowed_ace->SidStart, sid_size);

					allow=true;
					known=true;
					mask = allowed_ace->Mask;
				}
				break;
			case ACCESS_DENIED_ACE_TYPE:
				{
					ACCESS_DENIED_ACE* denied_ace = reinterpret_cast<ACCESS_DENIED_ACE*>(ptr);

					size_t sid_size = header->AceSize+sizeof(DWORD)-sizeof(ACCESS_DENIED_ACE);
					sid.resize(sid_size);
					memcpy(&sid[0], &denied_ace->SidStart, sid_size);

					allow=false;
					known=true;
					mask = denied_ace->Mask;
				}			
				break;
			default:
				known=false;
				break;
			}

			if(known && (
				mask & GENERIC_READ || mask & GENERIC_ALL
				|| mask & FILE_GENERIC_READ || mask & FILE_ALL_ACCESS
				) )
			{			
				std::map<std::vector<char>, Token>::iterator token_it =
					token_cache.get()->tokens.find(sid);

				if(token_it!=token_cache.get()->tokens.end())
				{
					assert(token_it->second.id!=0);

					if(allow)
					{
						token_info.addChar(ID_GRANT_ACCESS);
						token_info.addVarInt(token_it->second.id);
					}
					else
					{
						token_info.addChar(ID_DENY_ACCESS);
						token_info.addVarInt(token_it->second.id);
					}
				}
				else if(!allow)
				{
					Server->Log("Error getting SID of ACE entry of file \""+fn+"\"", LL_ERROR);
					LocalFree(sec_desc);
					return std::string();
				}				
			}

			ptr+=header->AceSize;
		}
	}

	LocalFree(sec_desc);

	return std::string(token_info.getDataPtr(), token_info.getDataSize());
}

void free_tokencache( TokenCacheInt* cache )
{
	delete cache;
}

std::string translate_tokens(int64 uid, int64 gid, int64 mode, ClientDAO* dao, TokenCache& cache)
{
	CWData token_info;
	//allow to all
	token_info.addChar(ID_GRANT_ACCESS);
	token_info.addVarInt(0);

	return std::string(token_info.getDataPtr(), token_info.getDataSize());
}

std::string accountname_normalize(const std::string& accountname)
{
	return strlower(accountname);
}

} //namespace tokens
