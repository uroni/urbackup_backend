#pragma once

#include <memory>
#include "../Interface/File.h"
#include "../Interface/Types.h"
#include "IFilesystem.h"


class ClientBitmap : public IReadOnlyBitmap
{
public:
	ClientBitmap(std::string fn);

	bool hasError();

	int64 getBlocksize();
	bool hasBlock(int64 block);

private:
	bool has_error;
	unsigned int bitmap_blocksize;
	std::vector<char> bitmap_data;
};