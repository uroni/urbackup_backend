#include "TreeHash.h"
#include "../common/adler32.h"
#include <algorithm>
#include <assert.h>
#include "../stringtools.h"
#include <limits.h>
#include <memory.h>

TreeHash::TreeHash()
	: offset(0), has_sparse(false)
{
	offset = 0;
	for (size_t i = 0; i < 12; ++i)
	{
		adlers[i] = urb_adler32(0, NULL, 0);
	}
	sha512_init(&sparse_ctx);

	level_hash.push_back(std::vector<std::string>());
}

void TreeHash::hash(const char * buf, _u32 bsize)
{
	assert(offset != UINT_MAX);

	_u32 offset_end = offset + bsize;
	_u32 buf_off = 0;
	for (_u32 i = offset; i < offset_end; i += treehash_smallblock)
	{
		_u32 adler_idx = (i / treehash_smallblock) % 12;
		_u32 tohash = (std::min)(treehash_smallblock, offset_end - i);

		if (offset%treehash_smallblock != 0)
		{
			tohash = (std::min)(tohash, treehash_smallblock - offset%treehash_smallblock);
		}

		adlers[adler_idx] = urb_adler32(adlers[adler_idx], buf + buf_off, tohash);
		md5sum.update(const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(buf + buf_off)), tohash);
		offset += tohash;
		buf_off += tohash;

		assert(offset <= treehash_blocksize);
		if (offset == treehash_blocksize)
		{
			if (offset_end > treehash_blocksize)
			{
				offset_end -= treehash_blocksize;
			}
			offset = 0;

			finalize_curr();
		}
	}
}

void TreeHash::sparse_hash(const char * buf, _u32 bsize)
{
	assert(offset != UINT_MAX);

	has_sparse = true;
	sha512_update(&sparse_ctx, reinterpret_cast<const unsigned char*>(buf), bsize);
}

std::string TreeHash::finalize()
{
	if (offset != 0)
	{
		offset = 0;
		finalize_curr();
	}

	offset = UINT_MAX;

	for (size_t i = 0; i < level_hash.size()-1; ++i)
	{
		if (!level_hash[i].empty())
		{
			finalize_level(i);
		}
	}

	if (level_hash.size() == 1 && level_hash[0].empty())
	{
		sha512_ctx c;
		sha512_init(&c);
		std::string nh;
		nh.resize(SHA512_DIGEST_SIZE);
		sha512_final(&c, reinterpret_cast<unsigned char*>(&nh[0]));
		return nh;
	}

	if (level_hash[level_hash.size() - 1].size()>1)
	{
		finalize_level(level_hash.size() - 1);
	}

	std::string tree_hash = level_hash[level_hash.size() - 1][0];

	if (has_sparse)
	{
		sha512_update(&sparse_ctx, reinterpret_cast<const unsigned char*>(tree_hash.data()), static_cast<_u32>(tree_hash.size()));

		std::string nh;
		nh.resize(SHA512_DIGEST_SIZE);
		sha512_final(&sparse_ctx, reinterpret_cast<unsigned char*>(&nh[0]));
		return nh;
	}
	else
	{
		return tree_hash;
	}
}

void TreeHash::addHash(const char* h)
{
	assert(offset == 0);

	level_hash[0].push_back(std::string(h, 64));

	for (size_t i = 0; i < level_hash.size(); ++i)
	{
		assert(level_hash[i].size() <= max_level_size);
		if (level_hash[i].size() == max_level_size)
		{
			finalize_level(i);
		}
	}
}

void TreeHash::addHashAllAdler(const char * h, size_t size, size_t hashed_size)
{
	assert(offset == 0);

	size_t num_adlers = (size - 16) / sizeof(_u32);
	const unsigned int* input_adler = reinterpret_cast<const unsigned int*>(h + 16);

	char nh[64];
	memcpy(nh, h, 16);

	unsigned int* n_adlers = reinterpret_cast<unsigned int*>(nh + 16);

	for (size_t i = 0; i < 12; ++i)
	{
		if (i>=num_adlers)
		{
			n_adlers[i] = urb_adler32(0, NULL, 0);
		}
		else
		{
			n_adlers[i] = input_adler[i];
		}
	}
	

	for (size_t i = 12; i < num_adlers; ++i)
	{
		size_t adler_idx = i % 12;

		_u32 curr_len2 = treehash_smallblock;
		if (i + 1 == num_adlers
			&& hashed_size%treehash_smallblock != 0)
		{
			curr_len2 = hashed_size%treehash_smallblock;
		}

		n_adlers[adler_idx] = urb_adler32_combine(n_adlers[adler_idx], input_adler[i], curr_len2);
	}

	for (size_t j = 0; j < 12; ++j)
	{
		nh[j] = little_endian(nh[j]);
	}

	addHash(nh);
}

void TreeHash::finalize_curr()
{
	md5sum.finalize();

	char h[64];
	memcpy(h, md5sum.raw_digest_int(), 16);

	for (size_t j = 0; j < 12; ++j)
	{
		adlers[j] = little_endian(adlers[j]);
	}

	memcpy(h + 16, adlers, 12 * sizeof(_u32));

	addHash(h);

	md5sum.init();

	for (size_t i = 0; i < 12; ++i)
	{
		adlers[i] = urb_adler32(0, NULL, 0);
	}
}

void TreeHash::finalize_level(size_t idx)
{
	sha512_ctx c;
	sha512_init(&c);

	for (size_t j = 0; j < level_hash[idx].size(); ++j)
	{
		sha512_update(&c, reinterpret_cast<const unsigned char*>(level_hash[idx][j].data()), static_cast<_u32>(level_hash[idx][j].size()));
	}

	std::string nh;
	nh.resize(SHA512_DIGEST_SIZE);
	sha512_final(&c, reinterpret_cast<unsigned char*>(&nh[0]));

	if (idx + 1 >= level_hash.size())
	{
		level_hash.push_back(std::vector<std::string>());
	}

	level_hash[idx + 1].push_back(nh);

	level_hash[idx].clear();
}


