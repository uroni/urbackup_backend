#pragma once
#include "clientdao.h"
#include <memory>

namespace tokens
{
	namespace
	{
#ifdef _WIN32
		const char* tokens_path = "tokens";
#else
		const char* tokens_path = "urbackup/tokens";
#endif
	}

	bool write_tokens();

	struct TokenCacheInt;

	const char ID_GRANT_ACCESS=0;
	const char ID_DENY_ACCESS=1;

	class TokenCache
	{
	public:
		TokenCache();
		~TokenCache();

		void reset(TokenCacheInt* cache=NULL);

		TokenCacheInt* get();

	private:
		void operator=(const TokenCache& other);
		TokenCache(const TokenCache& other);

		std::auto_ptr<TokenCacheInt> token_cache;
	};

	enum ETokenRight
	{
		ETokenRight_Read,
		ETokenRight_Write,
		ETokenRight_Delete,
		ETokenRight_DeleteFromDir
	};

	std::string get_file_tokens(const std::string& fn, ClientDAO* dao, ETokenRight right, TokenCache& cache);

	std::string translate_tokens(int64 uid, int64 gid, int64 mode, ClientDAO* dao, ETokenRight right, TokenCache& cache);

	std::string get_hostname();

	std::vector<std::string> get_local_users();

	std::vector<std::string> get_users();

	std::vector<std::string> get_groups();

	std::vector<std::string> get_user_groups(const std::string& username);

	bool write_token( std::string hostname, bool is_user, std::string accountname, const std::string &token_fn, ClientDAO &dao, const std::string& ext_token=std::string());

	std::string permissions_allow_all();

	std::string accountname_normalize(const std::string& accountname);
}
