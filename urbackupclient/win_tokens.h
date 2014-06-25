#pragma once
#include "clientdao.h"
#include <memory>


bool write_tokens();

struct TokenCacheInt;

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

