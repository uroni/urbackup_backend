#include "ClientBitmap.h"
#include "../Interface/Server.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../stringtools.h"
#include <memory.h>

namespace
{
	const unsigned int sha_size = 32;
}


ClientBitmap::ClientBitmap(std::string fn)
	: has_error(false)
{
	std::auto_ptr<IFile> bitmap_file(Server->openFile(fn, MODE_READ));

	init(bitmap_file.get());
}

ClientBitmap::ClientBitmap(IFile * bitmap_file)
	: has_error(false)
{
	init(bitmap_file);
}

void ClientBitmap::init(IFile * bitmap_file)
{
	if (bitmap_file == NULL)
	{
		has_error = true;
		return;
	}

	bitmap_file->Seek(0);

	if (bitmap_file->Read(8) != "UrBBMM8C")
	{
		Server->Log("Bitmap file at " + bitmap_file->getFilename() + " has wrong magic", LL_ERROR);
		has_error = true;
		return;
	}

	sha256_ctx shactx;
	sha256_init(&shactx);

	if (bitmap_file->Read(reinterpret_cast<char*>(&bitmap_blocksize), sizeof(bitmap_blocksize)) != sizeof(bitmap_blocksize))
	{
		Server->Log("Error reading blocksize from bitmap file " + bitmap_file->getFilename(), LL_ERROR);
		has_error = true;
		return;
	}

	sha256_update(&shactx, reinterpret_cast<unsigned char*>(&bitmap_blocksize), sizeof(bitmap_blocksize));

	bitmap_blocksize = little_endian(bitmap_blocksize);

	bitmap_data.resize(bitmap_file->Size() - 8 - sha_size - sizeof(bitmap_blocksize));

	if (bitmap_file->Read(bitmap_data.data(), static_cast<_u32>(bitmap_data.size())) != bitmap_data.size())
	{
		Server->Log("Error reading bitmap data from " + bitmap_file->getFilename(), LL_ERROR);
		has_error = true;
		return;
	}

	sha256_update(&shactx, reinterpret_cast<unsigned char*>(bitmap_data.data()), static_cast<_u32>(bitmap_data.size()));

	char sha_dig[sha_size];

	if (bitmap_file->Read(sha_dig, sha_size) != sha_size)
	{
		Server->Log("Error reading checksum from " + bitmap_file->getFilename(), LL_ERROR);
		has_error = true;
		return;
	}

	unsigned char dig[sha_size];
	sha256_final(&shactx, dig);

	if (memcmp(dig, sha_dig, sha_size) != 0)
	{
		Server->Log("Checksum of bitmap file " + bitmap_file->getFilename() + " wrong", LL_ERROR);
		has_error = true;
		return;
	}
}

bool ClientBitmap::hasError()
{
	return has_error;
}

int64 ClientBitmap::getBlocksize()
{
	return bitmap_blocksize;
}

bool ClientBitmap::hasBlock(int64 pBlock)
{
	size_t bitmap_byte = (size_t)(pBlock / 8);
	size_t bitmap_bit = pBlock % 8;

	if (bitmap_byte >= bitmap_data.size())
	{
		return true;
	}

	unsigned char b = bitmap_data[bitmap_byte];

	bool has_bit = ((b & (1 << bitmap_bit))>0);

	return has_bit;
}

