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

#pragma comment(lib, "netapi32.lib")

namespace tokens
{

struct Token
{
	int id;
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

std::vector<std::wstring> get_users()
{
	LPUSER_INFO_0 buf;
	DWORD prefmaxlen = MAX_PREFERRED_LENGTH;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	DWORD resume_handle = 0;
	NET_API_STATUS status;
	std::vector<std::wstring> ret;
	do 
	{
		 status = NetUserEnum(NULL, 0, FILTER_NORMAL_ACCOUNT,
			(LPBYTE*)&buf, prefmaxlen, &entriesread,
			&totalentries, &resume_handle);

		 if (status == NERR_Success || status == ERROR_MORE_DATA)
		 {
			 for(DWORD i=0;i<entriesread;++i)
			 {
				 LPUSER_INFO_0 user_info = (buf+i);
				 ret.push_back(user_info->usri0_name);
			 }
		 }
		 else
		 {
			 Server->Log("Error while enumerating users: "+ nconvert((int)status), LL_ERROR);
		 }

		 if(buf!=NULL)
		 {
			 NetApiBufferFree(buf);
		 }
	}
	while (status == ERROR_MORE_DATA);

	return ret;
}

std::vector<std::wstring> get_user_groups(const std::wstring& username)
{
	LPLOCALGROUP_USERS_INFO_0 buf;
	DWORD prefmaxlen = MAX_PREFERRED_LENGTH;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	DWORD resume_handle = 0;
	NET_API_STATUS status;
	std::vector<std::wstring> ret;

	status = NetUserGetLocalGroups(NULL, username.c_str(), 0, LG_INCLUDE_INDIRECT,
		(LPBYTE*)&buf, prefmaxlen, &entriesread,
		&totalentries);

	if (status == NERR_Success)
	{
		for(DWORD i=0;i<entriesread;++i)
		{
			LPLOCALGROUP_USERS_INFO_0 user_info = (buf+i);
			ret.push_back(user_info->lgrui0_name);
		}
	}
	else
	{
		Server->Log("Error while enumerating users: "+ nconvert((int)status), LL_ERROR);
	}

	if(buf!=NULL)
	{
		NetApiBufferFree(buf);
	}

	return ret;
	
}

std::vector<std::wstring> get_groups()
{
	LPLOCALGROUP_INFO_0 buf;
	DWORD prefmaxlen = MAX_PREFERRED_LENGTH;
	DWORD entriesread = 0;
	DWORD totalentries = 0;
	NET_API_STATUS status;
	std::vector<std::wstring> ret;
	DWORD_PTR resume_handle = 0;
	do 
	{
		status = NetLocalGroupEnum(NULL, 0,
			(LPBYTE*)&buf, prefmaxlen, &entriesread,
			&totalentries, &resume_handle);

		if (status == NERR_Success || status == ERROR_MORE_DATA)
		{
			for(DWORD i=0;i<entriesread;++i)
			{
				LPLOCALGROUP_INFO_0 group_info = (buf+i);
				ret.push_back(group_info->lgrpi0_name);
			}
		}
		else
		{
			Server->Log("Error while enumerating groups: "+ nconvert((int)status), LL_ERROR);
		}

		if(buf!=NULL)
		{
			NetApiBufferFree(buf);
		}
	}
	while (status == ERROR_MORE_DATA);

	return ret;
}


bool write_token( std::wstring hostname, bool is_user, std::wstring accountname, const std::wstring &token_fn, ClientDAO &dao )
{
	std::vector<char> sid_buffer;
	sid_buffer.resize(sizeof(SID));

	DWORD account_sid_size = sizeof(SID);
	SID_NAME_USE sid_name_use;
	std::wstring referenced_domain;
	referenced_domain.resize(1);
	DWORD referenced_domain_size = 1;
	BOOL b=LookupAccountNameW(NULL, ((is_user?hostname:L"")+accountname).c_str(),
		&sid_buffer[0], &account_sid_size, &referenced_domain[0],
		&referenced_domain_size, &sid_name_use);

	if(!b && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
	{
		referenced_domain.resize(referenced_domain_size);
		sid_buffer.resize(account_sid_size);
		b=LookupAccountNameW(NULL, ((is_user?hostname:L"")+accountname).c_str(),
			&sid_buffer[0], &account_sid_size, &referenced_domain[0],
			&referenced_domain_size, &sid_name_use);
	}

	if(!b)
	{
		Server->Log("Error getting accout SID. Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
		return false;
	}

	SID* account_sid = reinterpret_cast<SID*>(&sid_buffer[0]);

	LPWSTR str_account_sid;
	b = ConvertSidToStringSidW(account_sid, &str_account_sid);
	if(!b)
	{
		Server->Log("Error converting SID to string SID. Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
		return false;
	}

	std::wstring dacl = std::wstring(L"D:(A;OICI;GA;;;") + str_account_sid + L")"
		+ L"(A;OICI;GA;;;BA)";

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
		Server->Log("Error creating security descriptor. Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
		return false;
	}

	HANDLE file = CreateFileW(token_fn.c_str(),
		GENERIC_READ | GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, 0, NULL);

	if(file==INVALID_HANDLE_VALUE)
	{
		Server->Log("Error opening file. Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
		LocalFree(sa.lpSecurityDescriptor);
		return false;
	}

	std::string token = Server->secureRandomString(20);
	dao.updateFileAccessToken(accountname, widen(token), is_user?1:0);

	DWORD written=0;
	while(written<token.size())
	{
		b = WriteFile(file, token.data()+written, static_cast<DWORD>(token.size())-written, &written, NULL);
		if(!b)
		{
			Server->Log("Error writing to token file.  Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
			CloseHandle(file);
			LocalFree(sa.lpSecurityDescriptor);
			return true;
		}
	}

	CloseHandle(file);
	LocalFree(sa.lpSecurityDescriptor);

	return true;
}

std::wstring get_hostname()
{
	char hostname_c[MAX_PATH];
	hostname_c[0]=0;
	gethostname(hostname_c, MAX_PATH);

	std::wstring hostname = widen(hostname_c);
	if(!hostname.empty())
		hostname+=L"\\";

	return hostname;
}

bool read_account_sid( std::vector<char>& sid, std::wstring hostname, std::wstring accountname, bool is_user )
{
	sid.resize(sizeof(SID));
	DWORD account_sid_size = sizeof(SID);
	SID_NAME_USE sid_name_use;
	std::wstring referenced_domain;
	referenced_domain.resize(1);
	DWORD referenced_domain_size = 1;
	BOOL b=LookupAccountNameW(NULL, ((is_user?hostname:L"")+accountname).c_str(),
		&sid[0], &account_sid_size, &referenced_domain[0],
		&referenced_domain_size, &sid_name_use);

	if(!b && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
	{
		referenced_domain.resize(referenced_domain_size);
		sid.resize(account_sid_size);
		b=LookupAccountNameW(NULL, ((is_user?hostname:L"")+accountname).c_str(),
			&sid[0], &account_sid_size, &referenced_domain[0],
			&referenced_domain_size, &sid_name_use);
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

	std::vector<std::wstring> users = get_users();

	char hostname_c[MAX_PATH];
	hostname_c[0]=0;
	gethostname(hostname_c, MAX_PATH);

	std::wstring hostname = widen(hostname_c);
	if(!hostname.empty())
		hostname+=L"\\";

	for(size_t i=0;i<users.size();++i)
	{
		Token new_token;

		std::vector<char> sid;
		if(!read_account_sid(sid, hostname, users[i], true))
		{
			Server->Log(L"Cannot get account SID for user "+users[i], LL_ERROR);
			continue;
		}

		ClientDAO::CondInt token_id = dao->getFileAccessTokenId(users[i], 1);

		if(!token_id.exists)
		{
			Server->Log("Token id for user not found", LL_ERROR);
			continue;
		}

		new_token.id=token_id.value;
		new_token.is_user=true;

		cache->tokens[sid]=new_token;
	}

	std::vector<std::wstring> groups = get_groups();

	for(size_t i=0;i<groups.size();++i)
	{
		Token new_token;
		
		std::vector<char> sid;
		if(!read_account_sid(sid, hostname, groups[i], false))
		{
			Server->Log(L"Cannot get account SID for group "+groups[i], LL_ERROR);
			continue;
		}

		ClientDAO::CondInt token_id = dao->getFileAccessTokenId(groups[i], 0);

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

std::string get_file_tokens( const std::wstring& fn, ClientDAO* dao, TokenCache& token_cache )
{
	if(token_cache.get()==NULL)
	{
		read_all_tokens(dao, token_cache);
	}

	TokenCacheInt* cache = token_cache.get();

	PACL dacl;
	PSECURITY_DESCRIPTOR sec_desc;

	DWORD rc = GetNamedSecurityInfoW(fn.c_str(), SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION|UNPROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, &dacl,
		NULL, &sec_desc);

	if(rc!=ERROR_SUCCESS)
	{
		Server->Log(L"Error getting DACL of file \""+fn+L"\". Errorcode: "+convert((int)rc), LL_ERROR);
		return std::string();
	}

	CWData token_info;

	if(dacl->AceCount==0)
	{
		//allow to all
		token_info.addChar(ID_GRANT_ACCESS);
		token_info.addUInt(-1);
	}

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
				if(allow)
				{
					token_info.addChar(ID_GRANT_ACCESS);
					token_info.addInt(token_it->second.id);
				}
				else
				{
					token_info.addChar(ID_DENY_ACCESS);
					token_info.addInt(token_it->second.id);
				}
			}
			else if(!allow)
			{
				Server->Log(L"Error getting SID of ACE entry of file \""+fn+L"\"", LL_ERROR);
				LocalFree(sec_desc);
				return std::string();
			}				
		}

		ptr+=header->AceSize;
	}

	LocalFree(sec_desc);

	std::string ret;
	ret.resize(token_info.getDataSize());
	memcpy(&ret[0], token_info.getDataPtr(), token_info.getDataSize());
	return ret;
}

void free_tokencache( TokenCacheInt* cache )
{
	delete cache;
}

} //namespace tokens
