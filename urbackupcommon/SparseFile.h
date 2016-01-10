#pragma once

#include "../Interface/File.h"

/**
* Gives a non-sparse view of a sparse file
*/
class SparseFile : public IFsFile
{
public:

	SparseFile(IFsFile* backing_file, IFile* sparse_extends_f, bool read_only, int64 blocksize, bool take_file_ownership);
	SparseFile(IFsFile* backing_file, const std::vector<IFsFile::SSparseExtent>& sparse_extents, bool read_only, int64 blocksize, bool take_file_ownership);

	~SparseFile();

	bool hasError();

	// Inherited via IFile
	virtual std::string Read(_u32 tr, bool * has_error = NULL);
	virtual _u32 Read(char * buffer, _u32 bsize, bool * has_error = NULL);
	virtual _u32 Write(const std::string & tw, bool * has_error = NULL);
	virtual _u32 Write(const char * buffer, _u32 bsiz, bool * has_error = NULL);
	virtual bool Seek(_i64 spos);
	virtual _i64 Size(void);
	virtual _i64 RealSize();
	virtual bool PunchHole(_i64 spos, _i64 size);
	virtual bool Sync();
	virtual std::string getFilename(void);

	class IOrigOp
	{
	public:
		virtual _u32 origOp(_u32 buffer_offset, _u32 max_size, bool * has_error) = 0;
	};

	int64 mapToBackingOffset(int64 offset);

	virtual void resetSparseExtentIter();
	virtual SSparseExtent nextSparseExtent();
	virtual bool Resize(int64 new_size);

private:

	struct SPosMap
	{
		SPosMap()
			: offset(-1), backing_offset(-1)
		{

		}

		SPosMap(int64 offset, int64 backing_offset)
			: offset(offset), backing_offset(backing_offset)
		{}

		bool operator<(const SPosMap& other) const
		{
			return offset < other.offset;
		}

		int64 offset;
		int64 backing_offset;
	};

	void initWithSparseExtents(const std::vector<IFsFile::SSparseExtent>& sparse_extents, bool read_only, int64 blocksize);

	SPosMap nextBackingOffset();

	SPosMap lastBackingOffset(int64 offset);

	_u32 mappedOrigOp(IOrigOp* orig_op, _u32 op_size, bool * has_error);

	std::vector<SPosMap> sparse_offsets;

	IFsFile* backing_file;

	bool has_error;

	int64 seek_pos;
	int64 backing_pos;
	int64 sparse_extents_size;
	bool take_file_ownership;
};