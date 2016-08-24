#include "SparseFile.h"
#include <algorithm>
#include <assert.h>

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


SparseFile::SparseFile(IFsFile * backing_file, IFile * sparse_extends_f, bool read_only, int64 blocksize, bool take_file_ownership, int64 max_size)
	: backing_file(backing_file), has_error(false), seek_pos(0), backing_pos(0), take_file_ownership(take_file_ownership), max_size(max_size)
{
	int64 n_extents;
	sparse_extends_f->Seek(0);
	if (sparse_extends_f->Read(reinterpret_cast<char*>(&n_extents), sizeof(n_extents)) != sizeof(n_extents))
	{
		has_error = true;
		return;
	}

	std::vector<IFsFile::SSparseExtent> sparse_extents;
	sparse_extents.resize(static_cast<size_t>(n_extents));

	if (sparse_extends_f->Read(reinterpret_cast<char*>(sparse_extents.data()), static_cast<_u32>(sparse_extents.size()*sizeof(IFsFile::SSparseExtent)))
		!= static_cast<_u32>(sparse_extents.size()*sizeof(IFsFile::SSparseExtent)))
	{
		has_error = true;
		return;
	}

	initWithSparseExtents(sparse_extents, read_only, blocksize);
}

SparseFile::SparseFile(IFsFile * backing_file, const std::vector<IFsFile::SSparseExtent>& sparse_extents, bool read_only, int64 blocksize, bool take_file_ownership, int64 max_size)
	: backing_file(backing_file), has_error(false), seek_pos(0), backing_pos(0), take_file_ownership(take_file_ownership), max_size(max_size)
{
	initWithSparseExtents(sparse_extents, read_only, blocksize);
}

SparseFile::~SparseFile()
{
	if (take_file_ownership)
	{
		Server->destroy(backing_file);
	}
}

bool SparseFile::hasError()
{
	return has_error;
}

namespace
{
	class BackingReadStrOp : public SparseFile::IOrigOp
	{
	public:
		BackingReadStrOp(IFile* backing_file)
			: backing_file(backing_file)
		{}

		virtual _u32 origOp(_u32 buffer_offset, _u32 max_size, bool * has_error)
		{
			std::string ret_add = backing_file->Read(max_size, has_error);
			ret += ret_add;
			return static_cast<_u32>(ret_add.size());
		}

		std::string getRet()
		{
			return ret;
		}

	private:
		std::string ret;
		IFile* backing_file;
	};
}


std::string SparseFile::Read(int64 spos, _u32 tr, bool * has_error)
{
	if (!Seek(spos))
	{
		if (has_error) *has_error = true;
		return std::string();
	}

	return Read(tr, has_error);
}

std::string SparseFile::Read(_u32 tr, bool * has_error)
{
	BackingReadStrOp read_str_op(backing_file);
	_u32 read = mappedOrigOp(&read_str_op, tr, has_error);
	return read_str_op.getRet();
}

namespace
{
	class BackingReadBufOp : public SparseFile::IOrigOp
	{
	public:
		BackingReadBufOp(IFile* backing_file, char * buffer)
			: backing_file(backing_file), buffer(buffer)
		{}

		virtual _u32 origOp(_u32 buffer_offset, _u32 max_size, bool * has_error)
		{
			return backing_file->Read(buffer+buffer_offset, max_size, has_error);
		}

	private:
		char * buffer;
		IFile* backing_file;
	};
}


_u32 SparseFile::Read(char * buffer, _u32 bsize, bool * has_error)
{
	BackingReadBufOp read_buf_op(backing_file, buffer);
	return mappedOrigOp(&read_buf_op, bsize, has_error);
}

_u32 SparseFile::Read(int64 spos, char * buffer, _u32 bsize, bool * has_error)
{
	if (!Seek(spos))
	{
		if (has_error) *has_error = true;
		return 0;
	}

	return Read(buffer, bsize, has_error);
}

_u32 SparseFile::Write(const std::string & tw, bool * has_error)
{
	return Write(tw.data(), static_cast<_u32>(tw.size()), has_error);
}

_u32 SparseFile::Write(int64 spos, const std::string & tw, bool * has_error)
{
	if (!Seek(spos))
	{
		if (has_error) *has_error = true;
		return 0;
	}

	return Write(tw, has_error);
}

namespace
{
	class BackingWriteBufOp : public SparseFile::IOrigOp
	{
	public:
		BackingWriteBufOp(IFile* backing_file, const char * buffer)
			: backing_file(backing_file), buffer(buffer)
		{}

		virtual _u32 origOp(_u32 buffer_offset, _u32 max_size, bool * has_error)
		{
			return backing_file->Write(buffer + buffer_offset, max_size, has_error);
		}

	private:
		const char * buffer;
		IFile* backing_file;
	};
}

_u32 SparseFile::Write(const char * buffer, _u32 bsize, bool * has_error)
{
	BackingWriteBufOp write_buf_op(backing_file, buffer);
	return mappedOrigOp(&write_buf_op, bsize, has_error);
}

_u32 SparseFile::Write(int64 spos, const char * buffer, _u32 bsize, bool * has_error)
{
	if (!Seek(spos))
	{
		if (has_error) *has_error = true;
		return 0;
	}

	return Write(buffer, bsize, has_error);
}

bool SparseFile::Seek(_i64 spos)
{
	seek_pos = spos;
	backing_pos = mapToBackingOffset(spos);
	return backing_file->Seek(backing_pos);
}

_i64 SparseFile::Size(void)
{
	return backing_file->Size()-sparse_extents_size;
}

_i64 SparseFile::RealSize()
{
	return backing_file->Size();
}

bool SparseFile::PunchHole(_i64 spos, _i64 size)
{
	int64 punch_start = mapToBackingOffset(spos);
	int64 punch_end = mapToBackingOffset(spos + size);
	return backing_file->PunchHole(punch_start, punch_end - punch_start);
}

bool SparseFile::Sync()
{
	return backing_file->Sync();
}

std::string SparseFile::getFilename(void)
{
	return backing_file->getFilename();
}

void * SparseFile::getOsHandle()
{
	return backing_file->getOsHandle();
}

_i64 SparseFile::getSparseSize()
{
	return sparse_extents_size;
}

void SparseFile::initWithSparseExtents(const std::vector<IFsFile::SSparseExtent>& sparse_extents, bool read_only, int64 blocksize)
{
	int64 fpos = 0;
	int64 last_offset = 0;
	int64 max_offset = 0;
	sparse_extents_size = 0;
	for (size_t i = 0; i < sparse_extents.size(); ++i)
	{
		IFsFile::SSparseExtent curr_extent = sparse_extents[i];

		int64 offset = roundUp(curr_extent.offset, blocksize);
		int64 offset_end = roundDown(curr_extent.offset + curr_extent.size, blocksize);

		if (offset < offset_end)
		{
			int64 extent_size = offset_end - offset;

			fpos += offset - last_offset;
			last_offset = offset + extent_size;

			sparse_extents_size += extent_size;

			sparse_offsets.push_back(SPosMap(fpos, offset + extent_size));

			max_offset = (std::max)(max_offset, curr_extent.offset + curr_extent.size);
		}
	}

	if (max_size > 0)
	{
		max_offset = (std::min)(max_size, max_offset);
	}

	if (!sparse_offsets.empty() && sparse_offsets[0].offset == 0)
	{
		backing_pos = sparse_offsets[0].backing_offset;
	}

	if (max_offset > backing_file->Size())
	{
		if (!backing_file->Resize(max_offset))
		{
			has_error = true;
			return;
		}
	}

	if (!backing_file->Seek(backing_pos))
	{
		has_error = true;
		return;
	}
}

SparseFile::SPosMap SparseFile::nextBackingOffset()
{
	std::vector<SPosMap>::iterator it = std::upper_bound(sparse_offsets.begin(), sparse_offsets.end(), SPosMap(seek_pos, -1));
	if (it != sparse_offsets.end())
	{
		return *it;
	}
	else
	{
		return SPosMap();
	}
}

SparseFile::SPosMap SparseFile::lastBackingOffset(int64 offset)
{
	std::vector<SPosMap>::iterator it = std::lower_bound(sparse_offsets.begin(), sparse_offsets.end(), SPosMap(offset, -1));
	if (it != sparse_offsets.end())
	{
		return *it;
	}
	else
	{
		return SPosMap();
	}
}

_u32 SparseFile::mappedOrigOp(IOrigOp * orig_op, _u32 op_size, bool * has_error)
{
	_u32 op_size_orig = op_size;
	while (op_size > 0)
	{
		SPosMap next = nextBackingOffset();
		_u32 max_size = static_cast<_u32>(op_size);
		if (next.offset != -1)
		{
			max_size = static_cast<_u32>((std::min)(static_cast<int64>(op_size), next.offset - seek_pos));
		}

		_u32 buffer_offset = op_size_orig - op_size;

		_u32 backing_op_size = orig_op->origOp(buffer_offset, max_size, has_error);

		seek_pos += backing_op_size;
		op_size -= backing_op_size;

		if (has_error != NULL && *has_error)
		{
			return op_size_orig - op_size;
		}

		if (seek_pos == next.offset)
		{
			backing_pos = next.backing_offset;
			if (!backing_file->Seek(backing_pos))
			{
				if (has_error != NULL)
				{
					*has_error = true;
				}
				return op_size_orig - op_size;
			}
		}
		else
		{
			break;
		}
	}
	return op_size_orig - op_size;
}

int64 SparseFile::mapToBackingOffset(int64 offset)
{
	SPosMap last = lastBackingOffset(offset);
	if (last.offset == -1)
	{
		return offset;
	}
	else
	{
		last.backing_offset += offset - last.offset;
		return last.backing_offset;
	}
}

void SparseFile::resetSparseExtentIter()
{
}

IFsFile::SSparseExtent SparseFile::nextSparseExtent()
{
	return SSparseExtent();
}

bool SparseFile::Resize(int64 new_size, bool set_sparse)
{
	return backing_file->Resize(mapToBackingOffset(new_size), set_sparse);
}

std::vector<IFsFile::SFileExtent> SparseFile::getFileExtents(int64 starting_offset, int64 block_size, bool & more_data)
{
	return std::vector<SFileExtent>();
}
