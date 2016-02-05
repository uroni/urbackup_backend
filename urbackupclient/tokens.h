#pragma once
#include "clientdao.h"
#include <memory>

namespace tokens
{

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

	std::string get_file_tokens(const std::string& fn, ClientDAO* dao, TokenCache& cache);

	std::string translate_tokens(int64 uid, int64 gid, int64 mode, ClientDAO* dao, TokenCache& cache);

	std::string get_hostname();

	std::vector<std::string> get_users();

	std::vector<std::string> get_groups();

	std::vector<std::string> get_user_groups(const std::string& username);

	bool write_token( std::string hostname, bool is_user, std::string accountname, const std::string &token_fn, ClientDAO &dao );

}
