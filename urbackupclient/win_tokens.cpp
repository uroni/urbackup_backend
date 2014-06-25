#include <Windows.h>
#include <LM.h>
#include <Sddl.h>
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "database.h"
#include "clientdao.h"
#include "Aclapi.h"
#include "win_tokens.h"

#pragma comment(lib, "netapi32.lib")

struct Token
{
	int64 id;
	std::vector<char> account_buf;
};

struct TokenCacheInt
{
	std::vector<Token> tokens;
	int64 max_id;
};


TokenCache::TokenCache( const TokenCache& other )
{

}

TokenCache::TokenCache() : token_cache(NULL)
{

}


TokenCache::~TokenCache()
{

}


void TokenCache::operator=(const TokenCache& other )
{

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
				 USER_INFO_0 user_info = *(buf+i);
				 ret.push_back(user_info.usri0_name);
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

bool write_tokens()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	ClientDAO dao(db);

	char hostname_c[MAX_PATH];
	hostname_c[0]=0;
	gethostname(hostname_c, MAX_PATH);

	std::wstring hostname = widen(hostname_c);
	if(!hostname.empty())
		hostname+=L"\\";

	db->BeginTransaction();

	bool has_new_token=false;
	std::vector<std::wstring> users = get_users();

	os_create_dir("tokens");

	std::vector<SFile> files = getFiles(L"tokens", NULL, false, false);

	for(size_t i=0;i<users.size();++i)
	{
		std::wstring token_fn = L"tokens" + os_file_sep()+users[i];
		bool file_found=false;
		for(size_t j=0;j<files.size();++j)
		{
			if(files[j].name==users[i])
			{
				file_found=true;
				break;
			}
		}

		if(file_found)
		{
			continue;
		}

		std::vector<char> sid_buffer;
		sid_buffer.resize(sizeof(SID));

		DWORD account_sid_size = sizeof(SID);
		SID_NAME_USE sid_name_use;
		std::wstring referenced_domain;
		referenced_domain.resize(1);
		DWORD referenced_domain_size = 1;
		BOOL b=LookupAccountNameW(NULL, (hostname+users[i]).c_str(),
			&sid_buffer[0], &account_sid_size, &referenced_domain[0],
			&referenced_domain_size, &sid_name_use);

		if(!b && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
		{
			referenced_domain.resize(referenced_domain_size);
			sid_buffer.resize(account_sid_size);
			b=LookupAccountNameW(NULL, (hostname+users[i]).c_str(),
				&sid_buffer[0], &account_sid_size, &referenced_domain[0],
				&referenced_domain_size, &sid_name_use);
		}

		if(!b)
		{
			Server->Log("Error getting accout SID. Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
			continue;
		}

		SID* account_sid = reinterpret_cast<SID*>(&sid_buffer[0]);

		LPWSTR str_account_sid;
		b = ConvertSidToStringSidW(account_sid, &str_account_sid);
		if(!b)
		{
			Server->Log("Error converting SID to string SID. Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
			continue;
		}

		std::wstring dacl = std::wstring(L"D:(A;OICI;GA;;;") + str_account_sid + L")";

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
			continue;
		}

		HANDLE file = CreateFileW(token_fn.c_str(),
			GENERIC_READ | GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, 0, NULL);

		if(file==INVALID_HANDLE_VALUE)
		{
			Server->Log("Error opening file. Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
			LocalFree(sa.lpSecurityDescriptor);
			continue;
		}

		std::string token = Server->secureRandomString(20);
		dao.updateFileAccessToken(users[i], widen(token));
		has_new_token=true;

		DWORD written=0;
		while(written<token.size())
		{
			b = WriteFile(file, token.data()+written, static_cast<DWORD>(token.size())-written, &written, NULL);
			if(!b)
			{
				Server->Log("Error writing to token file.  Errorcode: "+nconvert((int)GetLastError()), LL_ERROR);
				CloseHandle(file);
				LocalFree(sa.lpSecurityDescriptor);
				continue;
			}
		}

		CloseHandle(file);
		LocalFree(sa.lpSecurityDescriptor);
	}

	if(has_new_token)
	{
		dao.updateMiscValue("has_new_token", L"true");
	}

	db->EndTransaction();

	return has_new_token;
}

void read_all_tokens(ClientDAO* dao, TokenCache& token_cache)
{
	TokenCacheInt* cache = new TokenCacheInt;
	token_cache.reset(cache);

	cache->max_id=-1;

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

		new_token.account_buf.resize(sizeof(SID));
		DWORD account_sid_size = sizeof(SID);
		SID_NAME_USE sid_name_use;
		std::wstring referenced_domain;
		referenced_domain.resize(1);
		DWORD referenced_domain_size = 1;
		BOOL b=LookupAccountNameW(NULL, (hostname+users[i]).c_str(),
			&new_token.account_buf[0], &account_sid_size, &referenced_domain[0],
			&referenced_domain_size, &sid_name_use);

		if(!b && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
		{
			referenced_domain.resize(referenced_domain_size);
			new_token.account_buf.resize(account_sid_size);
			b=LookupAccountNameW(NULL, (hostname+users[i]).c_str(),
				&new_token.account_buf[0], &account_sid_size, &referenced_domain[0],
				&referenced_domain_size, &sid_name_use);
		}

		ClientDAO::CondInt64 token_id = dao->getFileAccessTokenId(users[i]);

		if(!token_id.exists)
		{
			Server->Log("Token id for user not found", LL_ERROR);
			continue;
		}

		new_token.id=token_id.value;

		cache->tokens.push_back(new_token);

		if(new_token.id>cache->max_id)
		{
			cache->max_id=new_token.id;
		}
	}
}

void set_bit(int64 bit_num, std::string& bits)
{
	size_t bitmap_byte=(size_t)(bit_num/8);
	size_t bitmap_bit=bit_num%8;

	unsigned char b=static_cast<unsigned char>(bits[bitmap_byte]);

	b=b|(1<<(7-bitmap_bit));

	bits[bitmap_byte]=static_cast<char>(b);
}

std::string get_file_tokens( const std::wstring& fn, ClientDAO* dao, TokenCache& token_cache )
{
	if(token_cache.get()==NULL)
	{
		read_all_tokens(dao, token_cache);
	}

	TokenCacheInt* cache = token_cache.get();

	PACL dacl;

	DWORD rc = GetNamedSecurityInfoW(fn.c_str(), SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION, NULL, NULL, &dacl,
		NULL, NULL);

	if(rc!=ERROR_SUCCESS)
	{
		Server->Log(L"Error getting DACL of file \""+fn+L"\". Errorcode: "+convert((int)rc), LL_ERROR);
		return std::string();
	}

	std::string bits;
	bits.resize(cache->max_id/8 + (cache->max_id%8?1:0));

	for(size_t i=0;i<cache->tokens.size();++i)
	{
		Token& token = cache->tokens[i];

		TRUSTEE trustee;
		BuildTrusteeWithSid(&trustee, reinterpret_cast<SID*>(&token.account_buf[0]));

		ACCESS_MASK access_mask;
		rc = GetEffectiveRightsFromAclW(dacl, &trustee, &access_mask);

		if(rc!=ERROR_SUCCESS)
		{
			Server->Log(L"Error getting effective rights. Errorcode: "+convert((int)rc), LL_ERROR);
			continue;
		}

		if(access_mask & GENERIC_READ || access_mask & GENERIC_ALL
			|| access_mask & FILE_GENERIC_READ || access_mask & FILE_ALL_ACCESS)
		{
			set_bit(token.id, bits);
		}
	}

	return bits;
}

void free_tokencache( TokenCacheInt* cache )
{
	delete cache;
}
