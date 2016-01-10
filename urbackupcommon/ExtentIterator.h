#pragma once

#include <memory>
#include "../Interface/File.h"


class ExtentIterator
{
public:
	ExtentIterator(IFile* sparse_extents_f);

	IFsFile::SSparseExtent nextExtent();

	void reset();

private:
	std::auto_ptr<IFile> sparse_extents_f;
	int64 num_sparse_extents;
	int64 next_sparse_extent_num;
};