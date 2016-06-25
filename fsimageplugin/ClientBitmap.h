#pragma once

#include <memory>
#include "../Interface/File.h"
#include "../Interface/Types.h"
#include "IFilesystem.h"


class ClientBitmap : public IReadOnlyBitmap
{
public:
	ClientBitmap(std::string fn);
	ClientBitmap(IFile* bitmap_file);

	bool hasError();

	int64 getBlocksize();
	bool hasBlock(int64 block);

private:
	void init(IFile* bitmap_file);

	bool has_error;
	unsigned int bitmap_blocksize;
	std::vector<char> bitmap_data;
};