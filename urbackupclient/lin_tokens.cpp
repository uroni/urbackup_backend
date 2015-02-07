#include "tokens.h"
#include <iostream>
#include <fstream>
#include <map>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "../stringtools.h"
#include "../Interface/Server.h"
#include "../common/data.h"
#include <memory.h>
#include <stdlib.h>

namespace tokens
{

struct Token
{
	int id;
	bool is_user;
};

struct TokenCacheInt
{
	std::map<uid_t, unsigned int> uid_map;
	std::map<gid_t, unsigned int> gid_map;
};

TokenCache::TokenCache()
: token_cache(NULL)
{
}

TokenCache::~TokenCache()
{
}

void TokenCache::reset(TokenCacheInt* cache)
{
	token_cache.reset(cache);
}

TokenCacheInt* TokenCache::get()
{
	token_cache.get();
}

void TokenCache::operator=(const TokenCache& other)
{

}

TokenCache::TokenCache(const TokenCache& other)
{

}

std::wstring get_hostname()
{
	char hostname_c[255];
	hostname_c[0]=0;
	gethostname(hostname_c, 255);

	std::wstring hostname = widen(hostname_c);
	if(!hostname.empty())
		hostname+=L"\\";

	return hostname;
}

std::vector<std::wstring> get_users()
{
	std::ifstream passwd("/etc/passwd");

	if(!passwd.is_open())
	{
		return std::vector<std::wstring>();
	}

	std::vector<std::wstring> ret;
	for(std::string line; std::getline(passwd, line);)
	{
		std::string user = getuntil(":", line);
		ret.push_back(Server->ConvertToUnicode(user));
	}

	return ret;
}

std::vector<std::wstring> get_groups()
{
	std::ifstream group("/etc/group");

	if(!group.is_open())
	{
		return std::vector<std::wstring>();
	}

	std::vector<std::wstring> ret;
	for(std::string line; std::getline(group, line);)
	{
		std::string cgroup = getuntil(":", line);
		ret.push_back(Server->ConvertToUnicode(cgroup));
	}

	return ret;
}

std::vector<std::wstring> get_user_groups(const std::wstring& username)
{
	#ifdef __APPLE__
	#define gid_t int
	#endif
	std::string utf8_username = Server->ConvertToUTF8(username).c_str();
	struct passwd* pw = getpwnam(utf8_username.c_str());
	if(pw==NULL)
	{
		Server->Log(L"Error getting passwd structure for user with name \""+ username + L"\"", LL_ERROR);
		return std::vector<std::wstring>();
	}

	std::vector<gid_t> group_ids;

	int ngroups = 0;

	if(getgrouplist(utf8_username.c_str(), pw->pw_gid, group_ids.data(), &ngroups)==-1)
	{
		group_ids.resize(ngroups);
		if(getgrouplist(utf8_username.c_str(), pw->pw_gid, group_ids.data(), &ngroups)==-1)
		{
			Server->Log(L"Error getting group ids for user with name \""+ username + L"\"", LL_ERROR);
			return std::vector<std::wstring>();
		}

		std::vector<std::wstring> ret;
		for(size_t i=0;i<group_ids.size();++i)
		{
			struct group* gr =  getgrgid(group_ids[i]);
			ret.push_back(Server->ConvertToUnicode(gr->gr_name));
		}

		return ret;
	}
	else
	{
		return std::vector<std::wstring>();
	}

	#ifdef gid_t
	#undef gid_t
	#endif
}

int read_val(std::string val_name)
{
	std::string data=getFile("/etc/login.defs");

	size_t off = data.find(val_name);

	if(off!=std::string::npos)
	{
		size_t noff = data.find("\n", off);

		if(noff!=std::string::npos)
		{
			return atoi(trim(data.substr(off+val_name.size(), noff - off - val_name.size())).c_str());
		}
	}

	return -1;
}


bool write_token( std::wstring hostname, bool is_user, std::wstring accountname, const std::wstring &token_fn, ClientDAO &dao )
{
	int token_fd = creat(Server->ConvertToUTF8(token_fn).c_str(), is_user?S_IRWXU:S_IRWXG);

	if(token_fd==-1)
	{
		Server->Log(L"Error creating token file "+token_fn+L" errno="+convert(errno), LL_ERROR);
		return false;
	}

	bool is_system_user = false;

	if(is_user)
	{
		struct passwd* pw = getpwnam(Server->ConvertToUTF8(accountname).c_str());
		if(pw!=NULL)
		{
			static int uid_min = read_val("UID_MIN");
			static int uid_max = read_val("UID_MAX");

			if( uid_min!=-1 && uid_max!=-1 && (
				pw->pw_uid < uid_min ||
				pw->pw_uid > uid_max ) )
			{
				is_system_user = true;
			}
		}
	}

	std::string token = Server->secureRandomString(20);
	dao.updateFileAccessToken(accountname, widen(token),
			is_user ? (is_system_user ? ClientDAO::c_is_system_user : ClientDAO::c_is_user) : ClientDAO::c_is_group  );

	size_t written = 0;
	while(written<token.size())
	{
		ssize_t w = write(token_fd, token.data() + written, token.size()-written);
		if(w<=0 && errno!=EINTR)
		{
			Server->Log("Error writing to token file. Errno="+nconvert(errno), LL_ERROR);
			close(token_fd);
			return false;
		}
		else if(w>0)
		{
			written+=w;
		}
	}

	if(is_user)
	{
		struct passwd* pw = getpwnam(Server->ConvertToUTF8(accountname).c_str());
		if(pw==NULL)
		{
			Server->Log(L"Error getting passwd structure for user with name \""+ accountname + L"\"", LL_ERROR);
			close(token_fd);
			return false;
		}

		if(fchown(token_fd, pw->pw_uid, pw->pw_gid)!=0)
		{
			Server->Log(L"Error changing user ownership of token file \""+ token_fn + L"\"", LL_ERROR);
			close(token_fd);
			return false;
		}
	}
	else
	{
		struct group* gr = getgrnam(Server->ConvertToUTF8(accountname).c_str());
		if(gr==NULL)
		{
			Server->Log(L"Error getting group structure for group with name \""+ accountname + L"\"", LL_ERROR);
			close(token_fd);
			return false;
		}

		if(fchown(token_fd, 0, gr->gr_gid)!=0)
		{
			Server->Log(L"Error changing group ownership of token file \""+ token_fn + L"\"", LL_ERROR);
			close(token_fd);
			return false;
		}
	}

	close(token_fd);
	return true;
}

void read_all_tokens(ClientDAO* dao, TokenCache& token_cache)
{
	TokenCacheInt* cache = new TokenCacheInt;
	token_cache.reset(cache);

	std::vector<std::wstring> users = get_users();

	for(size_t i=0;i<users.size();++i)
	{
		struct passwd* pw = getpwnam(Server->ConvertToUTF8(users[i]).c_str());

		if(pw==NULL)
		{
			Server->Log(L"Error getting passwd structure for user with name \""+ users[i] + L"\"", LL_ERROR);
			continue;
		}

		ClientDAO::CondInt token_id = dao->getFileAccessTokenId(users[i], 1);

		if(!token_id.exists)
		{
			Server->Log("Token id for user not found", LL_ERROR);
			continue;
		}

		cache->uid_map[pw->pw_uid] = token_id.value;
	}

	std::vector<std::wstring> groups = get_groups();

	for(size_t i=0;i<groups.size();++i)
	{
		struct group* gr = getgrnam(Server->ConvertToUTF8(groups[i]).c_str());

		if(gr==NULL)
		{
			Server->Log(L"Error getting group structure for group with name \""+ groups[i] + L"\"", LL_ERROR);
			continue;
		}

		ClientDAO::CondInt token_id = dao->getFileAccessTokenId(groups[i], 0);

		if(!token_id.exists)
		{
			Server->Log("Token id for group not found", LL_ERROR);
			continue;
		}

		cache->gid_map[gr->gr_gid] = token_id.value;
	}
}

std::string get_file_tokens( const std::wstring& fn, ClientDAO* dao, TokenCache& token_cache )
{
	if(token_cache.get()==NULL)
	{
		read_all_tokens(dao, token_cache);
	}

	struct stat stat_data;

	if(stat(Server->ConvertToUTF8(fn.c_str()).c_str(), &stat_data)!=0)
	{
		Server->Log(L"Error stating file \"" + fn + L"\" to get file tokens. Errno: "+convert(errno), LL_ERROR);
		return std::string();
	}

	CWData token_info;

	if(stat_data.st_mode & S_IROTH)
	{
		//Allow all
		token_info.addChar(ID_GRANT_ACCESS);
		token_info.addUInt(-1);
	}
	else
	{
		if(stat_data.st_mode & S_IRUSR)
		{
			std::map<uid_t, unsigned int>::iterator it = token_cache.get()->uid_map.find(stat_data.st_uid);
			if(it==token_cache.get()->uid_map.end())
			{
				Server->Log("Error getting internal id for user with id "+nconvert(stat_data.st_uid), LL_ERROR);
			}
			else
			{
				token_info.addChar(ID_GRANT_ACCESS);
				token_info.addUInt(it->second);
			}
		}

		if(stat_data.st_mode & S_IRGRP)
		{
			std::map<gid_t, unsigned int>::iterator it = token_cache.get()->gid_map.find(stat_data.st_gid);
			if(it==token_cache.get()->gid_map.end())
			{
				Server->Log("Error getting internal id for group with id "+nconvert(stat_data.st_gid), LL_ERROR);
			}
			else
			{
				token_info.addChar(ID_GRANT_ACCESS);
				token_info.addUInt(it->second);
			}
		}
	}

	std::string ret;
	ret.resize(token_info.getDataSize());
	memcpy(&ret[0], token_info.getDataPtr(), token_info.getDataSize());
	return ret;
}

} //namespace tokens
