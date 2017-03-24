#include "tokens.h"
#include <iostream>
#include <fstream>
#include <sstream>
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
#include <assert.h>
#include <limits.h>

namespace tokens
{

struct Token
{
	int64 id;
	bool is_user;
};

struct TokenCacheInt
{
	std::map<uid_t, int64> uid_map;
	std::map<gid_t, int64> gid_map;
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
	return token_cache.get();
}

void TokenCache::operator=(const TokenCache& other)
{
	assert(false);
}

TokenCache::TokenCache(const TokenCache& other)
{
	assert(false);
}

std::string get_hostname()
{
	char hostname_c[255];
	hostname_c[0]=0;
	gethostname(hostname_c, 255);

	std::string hostname = hostname_c;
	if(!hostname.empty())
		hostname+="\\";

	return hostname;
}

std::vector<std::map<std::string, std::string> > read_kv(const std::string& cmd)
{
	std::string data;
	int rc = os_popen(cmd, data);
	std::vector<std::map<std::string, std::string> >  ret;	
	if(rc!=0)
	{
		return ret;
	}

	std::map<std::string, std::string> cur;
	std::istringstream iss(data);
	for(std::string line; std::getline(iss, line);)
	{	
		if(trim(line).empty())
		{
			if(!cur.empty())
			{
				ret.push_back(cur);
			}
			cur.clear();
		}

		std::string key = strlower(trim(getuntil(":", line)));
		std::string value = trim(getafter(":", line));

		cur[key]=value;
	}
	
	if(!cur.empty())
	{
		ret.push_back(cur);
	}
	
	return ret;
}

std::vector<std::string> read_single_k(const std::string& col, const std::string& cmd)
{
	std::vector<std::map<std::string, std::string> > res = read_kv(cmd);
	std::vector<std::string> ret;
	for(size_t i=0;i<res.size();++i)
	{
		std::string single = trim(res[i][col]);
		if(!single.empty())
		{
			ret.push_back(single);
		}
	}
	return ret;
}

std::vector<std::string> get_users()
{
#ifndef __APPLE__
	std::ifstream passwd("/etc/passwd");

	if(!passwd.is_open())
	{
		return std::vector<std::string>();
	}

	std::vector<std::string> ret;
	for(std::string line; std::getline(passwd, line);)
	{
		if(line.empty() || line[0]=='#')
		{
			continue;
		}
		std::string user = trim(getuntil(":", line));
		if(!user.empty())
		{
			ret.push_back(user);
		}
	}

	return ret;
#else
	return read_single_k("name", "dscacheutil -q user");
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

std::vector<std::string> get_local_users()
{
	std::vector<std::string> users = get_users();
	std::vector<std::string> ret;

	for(size_t i=0;i<users.size();++i)
	{
		struct passwd* pw = getpwnam(users[i].c_str());
                if(pw!=NULL)
                {
        #ifndef __APPLE__
                        static int uid_min = read_val("UID_MIN");
                        static int uid_max = read_val("UID_MAX");
        #else
                        static int uid_min = 500;
                        static int uid_max = INT_MAX;
        #endif

                        if( (uid_min==-1 || uid_max==-1 || (
                                pw->pw_uid >= uid_min &&
                                pw->pw_uid <= uid_max )) &&
				strlen(pw->pw_dir)>0 &&
				os_directory_exists(pw->pw_dir))
                        {
                                ret.push_back(users[i]);
                        }
                }
	}

	return ret;
}

std::vector<std::string> get_groups()
{
#ifndef __APPLE__
	std::ifstream group("/etc/group");

	if(!group.is_open())
	{
		return std::vector<std::string>();
	}

	std::vector<std::string> ret;
	for(std::string line; std::getline(group, line);)
	{
		if(line.empty() || line[0]=='#')
		{
			continue;
		}
		
		std::string cgroup = trim(getuntil(":", line));
		if(!cgroup.empty())
		{
			ret.push_back(cgroup);
		}
	}

	return ret;
#else
	return read_single_k("name", "dscacheutil -q group");
#endif
}

std::vector<std::string> get_user_groups(const std::string& username)
{
	#ifdef __APPLE__
	#define gid_t int
	#endif
	std::string utf8_username = (username).c_str();
	struct passwd* pw = getpwnam(utf8_username.c_str());
	if(pw==NULL)
	{
		Server->Log("Error getting passwd structure for user with name \""+ username + "\"", LL_ERROR);
		return std::vector<std::string>();
	}

	std::vector<gid_t> group_ids;

	int ngroups = 0;

	if(getgrouplist(utf8_username.c_str(), pw->pw_gid, group_ids.data(), &ngroups)==-1)
	{
		group_ids.resize(ngroups);
		if(getgrouplist(utf8_username.c_str(), pw->pw_gid, group_ids.data(), &ngroups)==-1)
		{
			Server->Log("Error getting group ids for user with name \""+ username + "\"", LL_ERROR);
			return std::vector<std::string>();
		}

		std::vector<std::string> ret;
		for(size_t i=0;i<group_ids.size();++i)
		{
			struct group* gr =  getgrgid(group_ids[i]);
			ret.push_back((gr->gr_name));
		}

		return ret;
	}
	else
	{
		return std::vector<std::string>();
	}

	#ifdef gid_t
	#undef gid_t
	#endif
}


bool write_token( std::string hostname, bool is_user, std::string accountname, const std::string &token_fn, ClientDAO &dao, const std::string& ext_token)
{
	int token_fd = creat((token_fn).c_str(), is_user?S_IRWXU:S_IRWXG);

	if(token_fd==-1)
	{
		Server->Log("Error creating token file "+token_fn+" errno="+convert(errno), LL_ERROR);
		return false;
	}

	bool is_system_user = false;

	if(is_user)
	{
		struct passwd* pw = getpwnam((accountname).c_str());
		if(pw!=NULL)
		{
	#ifndef __APPLE__
			static int uid_min = read_val("UID_MIN");
			static int uid_max = read_val("UID_MAX");
	#else
			static int uid_min = 500;
			static int uid_max = INT_MAX;
	#endif

			if( uid_min!=-1 && uid_max!=-1 && (
				pw->pw_uid < uid_min ||
				pw->pw_uid > uid_max ) )
			{
				is_system_user = true;
			}
		}
	}

	std::string token;
	if(ext_token.empty())
	{
		token = Server->secureRandomString(20);
	}
	else
	{
		token = ext_token;
	}
	
	dao.updateFileAccessToken(accountname_normalize(accountname), token,
			is_user ? (is_system_user ? ClientDAO::c_is_system_user : ClientDAO::c_is_user) : ClientDAO::c_is_group  );

	size_t written = 0;
	while(written<token.size())
	{
		ssize_t w = write(token_fd, token.data() + written, token.size()-written);
		if(w<=0 && errno!=EINTR)
		{
			Server->Log("Error writing to token file. Errno="+convert(errno), LL_ERROR);
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
		struct passwd* pw = getpwnam(accountname.c_str());
		if(pw==NULL)
		{
			Server->Log("Error getting passwd structure for user with name \""+ accountname + "\"", LL_ERROR);
			close(token_fd);
			return false;
		}

		if(fchown(token_fd, pw->pw_uid, pw->pw_gid)!=0)
		{
			Server->Log("Error changing user ownership of token file \""+ token_fn + "\"", LL_ERROR);
			close(token_fd);
			return false;
		}
	}
	else
	{
		struct group* gr = getgrnam(accountname.c_str());
		if(gr==NULL)
		{
			Server->Log("Error getting group structure for group with name \""+ accountname + "\"", LL_ERROR);
			close(token_fd);
			return false;
		}

		if(fchown(token_fd, 0, gr->gr_gid)!=0)
		{
			Server->Log("Error changing group ownership of token file \""+ token_fn + "\"", LL_ERROR);
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

	std::vector<std::string> users = get_users();

	for(size_t i=0;i<users.size();++i)
	{
		struct passwd* pw = getpwnam(users[i].c_str());

		if(pw==NULL)
		{
			Server->Log("Error getting passwd structure for user with name \""+ users[i] + "\"", LL_ERROR);
			continue;
		}

		ClientDAO::CondInt64 token_id = dao->getFileAccessTokenId2Alts(accountname_normalize(users[i]), ClientDAO::c_is_user, ClientDAO::c_is_system_user);

		if(!token_id.exists)
		{
			Server->Log("Token id for user not found", LL_ERROR);
			continue;
		}

		cache->uid_map[pw->pw_uid] = token_id.value;
	}

	std::vector<std::string> groups = get_groups();

	for(size_t i=0;i<groups.size();++i)
	{
		struct group* gr = getgrnam(groups[i].c_str());

		if(gr==NULL)
		{
			Server->Log("Error getting group structure for group with name \""+ groups[i] + "\"", LL_ERROR);
			continue;
		}

		ClientDAO::CondInt64 token_id = dao->getFileAccessTokenId(accountname_normalize(groups[i]), ClientDAO::c_is_group);

		if(!token_id.exists)
		{
			Server->Log("Token id for group not found", LL_ERROR);
			continue;
		}

		cache->gid_map[gr->gr_gid] = token_id.value;
	}
}

std::string get_file_tokens( const std::string& fn, ClientDAO* dao, ETokenRight right, TokenCache& token_cache )
{
	struct stat stat_data;

	if(stat(fn.c_str(), &stat_data)!=0)
	{
		Server->Log("Error stating file \"" + fn + "\" to get file tokens. Errno: "+convert(errno), LL_ERROR);
		return std::string();
	}
	
	return translate_tokens(stat_data.st_uid, stat_data.st_gid, stat_data.st_mode, dao, right, token_cache);
}

int64 read_token_lazy_cache(TokenCache& token_cache, ClientDAO* dao, bool is_user, int64 uid)
{
	if(is_user)
	{
		std::map<uid_t, int64>::iterator it = token_cache.get()->uid_map.find(uid);
		if(it!=token_cache.get()->uid_map.end())
			return it->second;
	}
	else
	{
		std::map<uid_t, int64>::iterator it = token_cache.get()->gid_map.find(uid);
		if(it!=token_cache.get()->gid_map.end())
			return it->second;
	}

	std::string name;
	if(is_user)
	{
		struct passwd* pw = getpwuid(uid);
		if(pw!=NULL)
		{
			name = pw->pw_name;
		}
	}
	else
	{
		struct group* grp = getgrgid(uid);
		if(grp!=NULL)
		{
			name = grp->gr_name;
		}
	}

	if(name.empty())
	{
		if(is_user)
			token_cache.get()->uid_map[uid]=0;
		else
			token_cache.get()->gid_map[uid]=0;
		return 0;
	}


	ClientDAO::CondInt64 token_id;

        if (is_user)
        {
                token_id = dao->getFileAccessTokenId2Alts(name,
                        ClientDAO::c_is_user, ClientDAO::c_is_system_user);
        }
        else
        {
                token_id = dao->getFileAccessTokenId(name, ClientDAO::c_is_group);
        }

	if(token_id.exists)
	{
		if(is_user)
		{
			token_cache.get()->uid_map[uid]=token_id.value;
		}
		else
		{
			token_cache.get()->gid_map[uid]=token_id.value;
		}
		return token_id.value;
	}

	std::string user_fn = (is_user ? "user_" : "group_") + bytesToHex(name);
       	std::string token_fn = tokens_path + os_file_sep() + user_fn;
        if (!write_token(get_hostname(), is_user, name, token_fn, *dao))
        {
		if(is_user)
			token_cache.get()->uid_map[uid]=0;
		else
			token_cache.get()->gid_map[uid]=0;
		return 0;
        }

        if (is_user)
        {
                token_id = dao->getFileAccessTokenId2Alts(name,
                        ClientDAO::c_is_user, ClientDAO::c_is_system_user);
        }
        else
        {
                token_id = dao->getFileAccessTokenId(name, ClientDAO::c_is_group);
        }


	if(token_id.exists)
	{
		if(is_user)
		{
			token_cache.get()->uid_map[uid]=token_id.value;
		}
		else
		{
			token_cache.get()->gid_map[uid]=token_id.value;
		}
		return token_id.value;
	}
	else
	{
		if(is_user)
			token_cache.get()->uid_map[uid]=0;
		else
			token_cache.get()->gid_map[uid]=0;
		return 0;
	}
}

std::string translate_tokens(int64 uid, int64 gid, int64 mode, ClientDAO* dao, ETokenRight right, TokenCache& cache)
{
	if(cache.get()==NULL)
	{
		read_all_tokens(dao, cache);
	}
	
	CWData token_info;

	mode_t iall;
	mode_t iusr;
	mode_t igrp;

	switch(right)
	{
		case ETokenRight_Read:
			if(S_ISDIR(mode))
			{
				iall = S_IROTH & S_IXOTH;
				iusr = S_IRUSR & S_IXUSR;
				igrp = S_IRGRP & S_IXGRP;
			}
			else
			{
				iall = S_IROTH;
				iusr = S_IRUSR;
				igrp = S_IRGRP;
			}
			break;
		case ETokenRight_Write:
			iall = S_IWOTH;
			iusr = S_IWUSR;
			igrp = S_IWGRP;
			break;
		case ETokenRight_DeleteFromDir:
			iall = S_IROTH & S_IWOTH;
			iusr = S_IRUSR & S_IWUSR;
			igrp = S_IRGRP & S_IWGRP;
			break;
	};
	

	if(right == ETokenRight_Delete)
	{
		//Allow all
                token_info.addChar(ID_GRANT_ACCESS);
                token_info.addVarInt(0);
	}
	else if(mode & iall)
	{
		//Allow all
		token_info.addChar(ID_GRANT_ACCESS);
		token_info.addVarInt(0);
	}
	else
	{
		//Allow root
		{
			int64 tid = read_token_lazy_cache(cache, dao, true, 0);
			if(tid!=0)
			{
				token_info.addChar(ID_GRANT_ACCESS);
				token_info.addVarInt(tid);
			}
		}
	
		if(mode & iusr)
		{
			int64 tid = read_token_lazy_cache(cache, dao, true, uid);
			std::map<uid_t, int64>::iterator it = cache.get()->uid_map.find(uid);
			if(tid==0)
			{
				Server->Log("Error getting internal id for user with id "+convert(uid), LL_ERROR);
			}
			else
			{
				token_info.addChar(ID_GRANT_ACCESS);
				token_info.addVarInt(tid);
			}
		}

		if(mode & igrp)
		{
			int64 tid = read_token_lazy_cache(cache, dao, false, uid);
			if(tid==0)
			{
				Server->Log("Error getting internal id for group with id "+convert(gid), LL_ERROR);
			}
			else
			{
				token_info.addChar(ID_GRANT_ACCESS);
				token_info.addVarInt(tid);
			}
		}
	}

	std::string ret;
	ret.resize(token_info.getDataSize());
	memcpy(&ret[0], token_info.getDataPtr(), token_info.getDataSize());
	return ret;
}

std::string permissions_allow_all()
{
	CWData token_info;
	//allow to all
	token_info.addChar(ID_GRANT_ACCESS);
	token_info.addVarInt(0);

	return std::string(token_info.getDataPtr(), token_info.getDataSize());
}

std::string get_domain_account(const std::string& accountname)
{
	return accountname;
}

std::string accountname_normalize(const std::string& accountname)
{
	//TODO: Make this conditional on LDAP being used and LDAP being case-insensitive
	return accountname;
}

} //namespace tokens
