#include "ClientHash.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"
#include <memory>
#include <assert.h>
#include "../urbackupcommon/ExtentIterator.h"
#include "../fileservplugin/chunk_settings.h"
#include "../stringtools.h"
#include "../common/adler32.h"
#include "../md5.h"
#include "../urbackupcommon/TreeHash.h"

namespace
{
	bool buf_is_zero(const char* buf, size_t bsize)
	{
		for (size_t i = 0; i < bsize; ++i)
		{
			if (buf[i] != 0)
			{
				return false;
			}
		}

		return true;
	}

	std::string build_sparse_extent_content()
	{
		char buf[c_small_hash_dist] = {};
		_u32 small_hash = urb_adler32(urb_adler32(0, NULL, 0), buf, c_small_hash_dist);
		small_hash = little_endian(small_hash);

		MD5 big_hash;
		for (int64 i = 0; i<c_checkpoint_dist; i += c_small_hash_dist)
		{
			big_hash.update(reinterpret_cast<unsigned char*>(buf), c_small_hash_dist);
		}
		big_hash.finalize();

		std::string ret;
		ret.resize(chunkhash_single_size);
		char* ptr = &ret[0];
		memcpy(ptr, big_hash.raw_digest_int(), big_hash_size);
		ptr += big_hash_size;
		for (int64 i = 0; i < c_checkpoint_dist; i += c_small_hash_dist)
		{
			memcpy(ptr, &small_hash, sizeof(small_hash));
			ptr += sizeof(small_hash);
		}
		return ret;
	}
}

ClientHash::ClientHash(IFile * index_hdat_file,
	bool own_hdat_file,
	int64 index_hdat_fs_block_size,
	size_t* snapshot_sequence_id, size_t snapshot_sequence_id_reference)
	: index_hdat_file(index_hdat_file),
	own_hdat_file(own_hdat_file),
	index_hdat_fs_block_size(index_hdat_fs_block_size),
	index_chunkhash_pos(-1),
	snapshot_sequence_id(snapshot_sequence_id),
	snapshot_sequence_id_reference(snapshot_sequence_id_reference)
{
}

ClientHash::~ClientHash()
{
	if (own_hdat_file)
	{
		Server->destroy(index_hdat_file);
	}
}

bool ClientHash::getShaBinary(const std::string & fn, IHashFunc & hf, bool with_cbt)
{
	std::auto_ptr<IFsFile>  f(Server->openFile(os_file_prefix(fn), MODE_READ_SEQUENTIAL_BACKUP));

	if (f.get() == NULL)
	{
		return false;
	}

	int64 skip_start = -1;
	bool needs_seek = false;
	const size_t bsize = 512 * 1024;
	int64 fpos = 0;
	_u32 rc = 1;
	std::vector<char> buf;
	buf.resize(bsize);

	FsExtentIterator extent_iterator(f.get(), bsize);

	IFsFile::SSparseExtent curr_sparse_extent = extent_iterator.nextExtent();

	int64 fsize = f->Size();

	bool has_more_extents = false;
	std::vector<IFsFile::SFileExtent> extents;
	size_t curr_extent_idx = 0;
	if (with_cbt
		&& fsize>c_checkpoint_dist)
	{
		extents = f->getFileExtents(0, index_hdat_fs_block_size, has_more_extents);
	}
	else
	{
		with_cbt = false;
	}

	while (fpos <= fsize && rc>0)
	{
		while (curr_sparse_extent.offset != -1
			&& curr_sparse_extent.offset + curr_sparse_extent.size<fpos)
		{
			curr_sparse_extent = extent_iterator.nextExtent();
		}

		size_t curr_bsize = bsize;
		if (fpos + static_cast<int64>(curr_bsize) > fsize)
		{
			curr_bsize = static_cast<size_t>(fsize - fpos);
		}

		if (curr_sparse_extent.offset != -1
			&& curr_sparse_extent.offset <= fpos
			&& curr_sparse_extent.offset + curr_sparse_extent.size >= fpos + static_cast<int64>(bsize))
		{
			if (skip_start == -1)
			{
				skip_start = fpos;
			}
			fpos += bsize;
			rc = static_cast<_u32>(bsize);
			continue;
		}

		index_chunkhash_pos = -1;

		if (!extents.empty()
			&& index_hdat_file != NULL
			&& fpos%c_checkpoint_dist == 0
			&& curr_bsize == bsize)
		{
			assert(bsize == c_checkpoint_dist);
			while (curr_extent_idx<extents.size()
				&& extents[curr_extent_idx].offset + extents[curr_extent_idx].size < fpos)
			{
				++curr_extent_idx;

				if (curr_extent_idx >= extents.size()
					&& has_more_extents)
				{
					extents = f->getFileExtents(fpos, index_hdat_fs_block_size, has_more_extents);
					curr_extent_idx = 0;
				}
			}

			if (curr_extent_idx<extents.size()
				&& extents[curr_extent_idx].offset <= fpos
				&& extents[curr_extent_idx].offset + extents[curr_extent_idx].size >= fpos + static_cast<int64>(bsize))
			{
				int64 volume_pos = extents[curr_extent_idx].volume_offset + (fpos - extents[curr_extent_idx].offset);
				index_chunkhash_pos = (volume_pos / c_checkpoint_dist)*(sizeof(_u16) + chunkhash_single_size);
				index_chunkhash_pos_offset = static_cast<_u16>((volume_pos%c_checkpoint_dist) / 512);

				char chunkhash[sizeof(_u16) + chunkhash_single_size];
				if (snapshot_sequence_id!=NULL
					&& *snapshot_sequence_id == snapshot_sequence_id_reference
					&& index_hdat_file->Read(index_chunkhash_pos, chunkhash, sizeof(chunkhash)) == sizeof(chunkhash))
				{
					_u16 chunkhash_offset;
					memcpy(&chunkhash_offset, chunkhash, sizeof(chunkhash_offset));
					if (index_chunkhash_pos_offset == chunkhash_offset
						&& !buf_is_zero(chunkhash, sizeof(chunkhash)))
					{
						if (sparse_extent_content.empty())
						{
							sparse_extent_content = build_sparse_extent_content();
							assert(sparse_extent_content.size() == chunkhash_single_size);
						}

						if (memcmp(chunkhash + sizeof(_u16), sparse_extent_content.data(), chunkhash_single_size) == 0)
						{
							if (skip_start == -1)
							{
								skip_start = fpos;
							}
						}
						else
						{
							if (skip_start != -1)
							{
								int64 skip[2];
								skip[0] = skip_start;
								skip[1] = fpos - skip_start;
								hf.sparse_hash(reinterpret_cast<char*>(&skip), sizeof(int64) * 2);
								skip_start = -1;
							}

							hf.addHashAllAdler(chunkhash + sizeof(_u16), chunkhash_single_size, bsize);
						}

						fpos += bsize;
						rc = bsize;

						needs_seek = true;
						continue;
					}
				}
				else
				{
					index_chunkhash_pos = -1;
				}
			}
		}

		if (skip_start != -1
			|| needs_seek)
		{
			f->Seek(fpos);
			needs_seek = false;
		}

		if (curr_bsize > 0)
		{
			bool has_read_error = false;
			rc = f->Read(buf.data(), static_cast<_u32>(curr_bsize), &has_read_error);

			if (has_read_error)
			{
				std::string msg;
				int64 code = os_last_error(msg);
				Server->Log("Read error while hashing \"" + fn + "\". " + msg + " (code: " + convert(code) + ")", LL_ERROR);
				return false;
			}
		}
		else
		{
			rc = 0;
		}

		if (rc == bsize && buf_is_zero(buf.data(), bsize))
		{
			if (skip_start == -1)
			{
				skip_start = fpos;
			}
			fpos += bsize;
			rc = bsize;

			if (index_chunkhash_pos != -1)
			{
				if (sparse_extent_content.empty())
				{
					sparse_extent_content = build_sparse_extent_content();
					assert(sparse_extent_content.size() == chunkhash_single_size);
				}

				char chunkhash[sizeof(_u16) + chunkhash_single_size];
				memcpy(chunkhash, &index_chunkhash_pos_offset, sizeof(index_chunkhash_pos_offset));
				memcpy(chunkhash + sizeof(_u16), sparse_extent_content.data(), chunkhash_single_size);
				if (snapshot_sequence_id!=NULL
					&& *snapshot_sequence_id == snapshot_sequence_id_reference)
				{
					index_hdat_file->Write(index_chunkhash_pos, chunkhash, sizeof(chunkhash));
				}
			}

			continue;
		}

		if (skip_start != -1)
		{
			int64 skip[2];
			skip[0] = skip_start;
			skip[1] = fpos - skip_start;
			hf.sparse_hash(reinterpret_cast<char*>(&skip), sizeof(int64) * 2);
			skip_start = -1;
		}

		if (rc > 0)
		{
			hf.hash(buf.data(), rc);
			fpos += rc;
		}
	}

	return true;
}

void ClientHash::hash_output_all_adlers(int64 pos, const char * hash, size_t hsize)
{
	if (index_chunkhash_pos != -1
		&& index_hdat_file != NULL)
	{
		assert(hsize == chunkhash_single_size);
		char chunkhash[sizeof(_u16) + chunkhash_single_size];
		memcpy(chunkhash, &index_chunkhash_pos_offset, sizeof(index_chunkhash_pos_offset));
		memcpy(chunkhash + sizeof(_u16), hash, chunkhash_single_size);

		if (snapshot_sequence_id!=NULL
			&& *snapshot_sequence_id == snapshot_sequence_id_reference)
		{
			index_hdat_file->Write(index_chunkhash_pos, chunkhash, sizeof(chunkhash));
		}
	}
}
