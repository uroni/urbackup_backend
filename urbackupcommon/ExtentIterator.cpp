#include "ExtentIterator.h"

namespace
{
	int64 roundUp(int64 numToRound, int64 multiple)
	{
		return ((numToRound + multiple - 1) / multiple) * multiple;
	}

	int64 roundDown(int64 numToRound, int64 multiple)
	{
		return ((numToRound / multiple) * multiple);
	}
}

ExtentIterator::ExtentIterator(IFile * sparse_extents_f, bool take_file_ownership, int64 blocksize)
	: sparse_extents_f(sparse_extents_f),
	num_sparse_extents(-1),
	next_sparse_extent_num(0),
	take_file_ownership(take_file_ownership),
	blocksize(blocksize)
{
	if (sparse_extents_f != NULL)
	{
		sparse_extents_f->Seek(0);
	}
}

ExtentIterator::~ExtentIterator()
{
	if (!take_file_ownership)
	{
		sparse_extents_f.release();
	}
}

IFsFile::SSparseExtent ExtentIterator::nextExtent()
{
	if (num_sparse_extents == -1)
	{
		if (sparse_extents_f.get() == NULL
			|| sparse_extents_f->Read(reinterpret_cast<char*>(&num_sparse_extents), sizeof(num_sparse_extents)) != sizeof(num_sparse_extents))
		{
			num_sparse_extents = 0;
		}
	}

	IFsFile::SSparseExtent ret;
	while (next_sparse_extent_num < num_sparse_extents
		&& ret.offset==-1)
	{
		if (sparse_extents_f->Read(reinterpret_cast<char*>(&ret), sizeof(IFsFile::SSparseExtent)) != sizeof(IFsFile::SSparseExtent))
		{
			num_sparse_extents = 0;
			return IFsFile::SSparseExtent();
		}
		else
		{
			++next_sparse_extent_num;

			int64 ret_end = roundDown(ret.offset + ret.size, blocksize);
			ret.offset = roundUp(ret.offset, blocksize);

			if (ret.offset < ret_end)
			{
				ret.size = ret_end - ret.offset;
				return ret;
			}
		}
	}

	return IFsFile::SSparseExtent();
}

void ExtentIterator::reset()
{
	num_sparse_extents = -1;
	next_sparse_extent_num = 0;
	if (sparse_extents_f.get() != NULL)
	{
		sparse_extents_f->Seek(0);
	}
}

FsExtentIterator::FsExtentIterator(IFsFile * backing_file, int64 blocksize)
	: backing_file(backing_file), blocksize(blocksize)
{
	backing_file->resetSparseExtentIter();
}

IFsFile::SSparseExtent FsExtentIterator::nextExtent()
{
	while (true)
	{
		IFsFile::SSparseExtent next = backing_file->nextSparseExtent();

		if (next.offset == -1)
		{
			return next;
		}

		int64 next_end = roundDown(next.offset + next.size, blocksize);
		next.offset = roundUp(next.offset, blocksize);
		if (next.offset < next_end)
		{
			next.size = next_end - next.offset;
			return next;
		}
	}
}

void FsExtentIterator::reset()
{
	backing_file->resetSparseExtentIter();
}
