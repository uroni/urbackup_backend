#pragma once

#include "sha2/sha2.h"
#include "../md5.h"
#include "../Interface/Types.h"

class IHashFunc
{
public:
	virtual void hash(const char* buf, _u32 bsize) = 0;
	virtual void sparse_hash(const char* buf, _u32 bsize) = 0;
	virtual std::string finalize() = 0;
	virtual void addHashAllAdler(const char* h, size_t size, size_t hashed_size) = 0;
};

class IHashOutput
{
public:
	virtual void hash_output_all_adlers(int64 pos, const char* hash, size_t hsize) = 0;
};

namespace
{
	class HashSha512 : public IHashFunc
	{
	public:
		HashSha512()
			: has_sparse(false)
		{
			sha512_init(&ctx);
			sha512_init(&sparse_ctx);
		}

		virtual void hash(const char* buf, _u32 bsize)
		{
			sha512_update(&ctx, reinterpret_cast<const unsigned char*>(buf), bsize);
		}

		virtual void sparse_hash(const char* buf, _u32 bsize)
		{
			has_sparse = true;
			sha512_update(&sparse_ctx, reinterpret_cast<const unsigned char*>(buf), bsize);
		}

		std::string finalize()
		{
			std::string skip_hash;
			skip_hash.resize(SHA512_DIGEST_SIZE);

			if (has_sparse)
			{
				sha512_final(&sparse_ctx, (unsigned char*)&skip_hash[0]);
				sha512_update(&ctx, reinterpret_cast<unsigned char*>(&skip_hash[0]), SHA512_DIGEST_SIZE);
			}

			sha512_final(&ctx, (unsigned char*)&skip_hash[0]);

			return skip_hash;
		}

		virtual void addHashAllAdler(const char* h, size_t size, size_t hashed_size)
		{}


	private:
		bool has_sparse;
		sha512_ctx ctx;
		sha512_ctx sparse_ctx;
	};

	class HashSha256 : public IHashFunc
	{
	public:
		HashSha256()
			: has_sparse(false)
		{
			sha256_init(&ctx);
			sha256_init(&sparse_ctx);
		}

		virtual void hash(const char* buf, _u32 bsize)
		{
			sha256_update(&ctx, reinterpret_cast<const unsigned char*>(buf), bsize);
		}

		virtual void sparse_hash(const char* buf, _u32 bsize)
		{
			has_sparse = true;
			sha256_update(&sparse_ctx, reinterpret_cast<const unsigned char*>(buf), bsize);
		}

		std::string finalize()
		{
			std::string skip_hash;
			skip_hash.resize(SHA256_DIGEST_SIZE);

			if (has_sparse)
			{
				sha256_final(&sparse_ctx, (unsigned char*)&skip_hash[0]);
				sha256_update(&ctx, reinterpret_cast<unsigned char*>(&skip_hash[0]), SHA256_DIGEST_SIZE);
			}

			sha256_final(&ctx, (unsigned char*)&skip_hash[0]);

			return skip_hash;
		}

		virtual void addHashAllAdler(const char* h, size_t size, size_t hashed_size)
		{}

	private:
		bool has_sparse;
		sha256_ctx ctx;
		sha256_ctx sparse_ctx;
	};
}

const unsigned int treehash_blocksize = 512 * 1024;
const unsigned int treehash_smallblock = 4096;
const size_t max_level_size = 16;

class TreeHash : public IHashFunc
{
public:
	TreeHash(IHashOutput* hash_output);

	virtual void hash(const char * buf, _u32 bsize);

	virtual void sparse_hash(const char * buf, _u32 bsize);

	virtual std::string finalize();

	//64 bytes
	virtual void addHash(const char* h, size_t hashed_size);
	//528 bytes
	virtual void addHashAllAdler(const char* h, size_t size, size_t hashed_size);

	static void allAdlerTo64byteHash(const char * h, size_t size, size_t hashed_size, char * byteout);

private:
	void finalize_curr();
	void finalize_level(size_t idx);

	bool has_sparse;
	sha512_ctx sparse_ctx;

	MD5 md5sum;
	unsigned int adlers[12];
	unsigned int offset;

	std::vector<char> hash_all_adlers;

	IHashOutput* hash_output;
	int64 hash_pos;

	std::vector<std::vector<std::string> > level_hash;
};