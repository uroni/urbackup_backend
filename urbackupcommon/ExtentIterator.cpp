#include "ExtentIterator.h"

ExtentIterator::ExtentIterator(IFile * sparse_extents_f)
	: sparse_extents_f(sparse_extents_f),
	num_sparse_extents(-1),
	next_sparse_extent_num(0)
{
	if (sparse_extents_f != NULL)
	{
		sparse_extents_f->Seek(0);
	}
}

IFsFile::SSparseExtent ExtentIterator::nextExtent()
{
	IFsFile::SSparseExtent ret;
	if (num_sparse_extents == -1)
	{
		if (sparse_extents_f.get() == NULL
			|| sparse_extents_f->Read(reinterpret_cast<char*>(&num_sparse_extents), sizeof(num_sparse_extents)) != sizeof(num_sparse_extents))
		{
			num_sparse_extents = 0;
		}
	}

	if (next_sparse_extent_num < num_sparse_extents)
	{
		if (sparse_extents_f->Read(reinterpret_cast<char*>(&ret), sizeof(IFsFile::SSparseExtent)) != sizeof(IFsFile::SSparseExtent))
		{
			num_sparse_extents = 0;
			ret = IFsFile::SSparseExtent();
		}
		else
		{
			++next_sparse_extent_num;
		}
	}

	return ret;
}

void ExtentIterator::reset()
{
	next_sparse_extent_num = 0;
	sparse_extents_f->Seek(sizeof(num_sparse_extents));
}
