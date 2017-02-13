#pragma once
#include <string>
#include "../Interface/Types.h"
#include "../urbackupcommon/TreeHash.h"

class IHashFunc;
class IFile;

class ClientHash : public IHashOutput
{
public:
	ClientHash(IFile* index_hdat_file, bool own_hdat_file, int64 index_hdat_fs_block_size,
		size_t* snapshot_sequence_id, size_t snapshot_sequence_id_reference);
	~ClientHash();

	bool getShaBinary(const std::string& fn, IHashFunc& hf, bool with_cbt);

	void hash_output_all_adlers(int64 pos, const char * hash, size_t hsize);

	bool hasCbtFile() {
		return index_hdat_file != NULL;
	}

private:
	IFile* index_hdat_file;
	bool own_hdat_file;
	int64 index_hdat_fs_block_size;
	int64 index_chunkhash_pos;
	_u16 index_chunkhash_pos_offset;
	std::string sparse_extent_content;
	volatile size_t* snapshot_sequence_id;
	size_t snapshot_sequence_id_reference;
};