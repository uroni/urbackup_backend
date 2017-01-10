#include "patch.h"
#include "../ChunkPatcher.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../urbackupcommon/ExtentIterator.h"
#include <assert.h>

namespace
{
	const int64 hash_bsize = 512 * 1024;

	class PatcherCallback : public IChunkPatcherCallback
	{
	public:
		PatcherCallback(ChunkPatcher& chunk_patcher)
			:file_pos(0), chunk_patcher(chunk_patcher) {}
		// Inherited via IChunkPatcherCallback
		virtual void next_chunk_patcher_bytes(const char * buf, size_t bsize, bool changed, bool * is_sparse = NULL)
		{
			if (buf != NULL)
			{
			}
			else if (is_sparse == NULL || *is_sparse == false)
			{
				assert(file_pos%hash_bsize == 0);
				assert(bsize == hash_bsize || file_pos + bsize == chunk_patcher.getFilesize());
			}

			file_pos += bsize;
		}
		virtual void next_sparse_extent_bytes(const char * buf, size_t bsize)
		{
		}
		virtual int64 chunk_patcher_pos()
		{
			return file_pos;
		}

	private:
		int64 file_pos;
		ChunkPatcher& chunk_patcher;
	};
}

int patch_hash()
{
	ChunkPatcher chunk_patcher;
	chunk_patcher.setRequireUnchanged(false);
	chunk_patcher.setUnchangedAlign(hash_bsize);

	PatcherCallback patcher_callback(chunk_patcher);

	std::string sparse_extents_file = Server->getServerParameter("sparse_extents_file");

	if (!sparse_extents_file.empty())
	{
		chunk_patcher.setWithSparse(true);
	}

	chunk_patcher.setCallback(&patcher_callback);

	std::string source_file = Server->getServerParameter("source_file");

	if (source_file.empty())
	{
		Server->Log("Parameter source_file not present", LL_ERROR);
		return 2;
	}

	std::auto_ptr<IFile> f(Server->openFile(source_file, MODE_READ));
	if (f.get() == NULL)
	{
		Server->Log("Cannot open source file " + source_file + ". " + os_last_error_str(), LL_ERROR);
		return 2;
	}

	std::string patch_file_fn = Server->getServerParameter("patch_file");
	std::auto_ptr<IFile> patch_file(Server->openFile(patch_file_fn, MODE_READ));
	if (patch_file.get() == NULL)
	{
		Server->Log("Cannot open patch file " + patch_file_fn + ". " + os_last_error_str(), LL_ERROR);
		return 2;
	}

	std::auto_ptr<ExtentIterator> extent_iterator;

	if (!sparse_extents_file.empty())
	{
		IFile* extents_f(Server->openFile(sparse_extents_file, MODE_READ));
		if (extents_f == NULL)
		{
			Server->Log("Cannot open patch file " + sparse_extents_file + ". " + os_last_error_str(), LL_ERROR);
			return 2;
		}

		extent_iterator.reset(new ExtentIterator(extents_f, true, hash_bsize));
	}

	bool b = chunk_patcher.ApplyPatch(f.get(), patch_file.get(), extent_iterator.get());
	return b ? 0 : 1;
}
