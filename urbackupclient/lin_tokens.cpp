#include "tokens.h"

struct TokenCacheInt
{

};

TokenCache::TokenCache()
{
}

TokenCache::~TokenCache()
{
}

void TokenCache::reset(TokenCacheInt* cache)
{
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

bool write_tokens()
{
	return false;
}

std::string get_file_tokens(const std::wstring& fn, ClientDAO* dao, TokenCache& cache)
{
	return std::string();
}
