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

	std::string get_file_tokens(const std::wstring& fn, ClientDAO* dao, TokenCache& cache);

	std::wstring get_hostname();

	std::vector<std::wstring> get_users();

	std::vector<std::wstring> get_groups();

	std::vector<std::wstring> get_user_groups(const std::wstring& username);

	bool write_token( std::wstring hostname, bool is_user, std::wstring accountname, const std::wstring &token_fn, ClientDAO &dao );

}
