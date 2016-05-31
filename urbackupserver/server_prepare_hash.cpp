/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifndef CLIENT_ONLY

#include "server_prepare_hash.h"
#include "server_hash.h"
#include "../common/data.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "server_log.h"
#include "../urbackupcommon/os_functions.h"
#include "../fileservplugin/chunk_settings.h"
#include "../md5.h"
#include <memory.h>
#include "../common/adler32.h"
#include "../urbackupcommon/file_metadata.h"

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

	const size_t hash_bsize = 512*1024;
}

BackupServerPrepareHash::BackupServerPrepareHash(IPipe *pPipe, IPipe *pOutput, int pClientid, logid_t logid)
	: logid(logid)
{
	pipe=pPipe;
	output=pOutput;
	clientid=pClientid;
	working=false;
	chunk_patcher.setCallback(this);
	chunk_patcher.setWithSparse(true);
	has_error=false;
}

BackupServerPrepareHash::~BackupServerPrepareHash(void)
{
	Server->destroy(pipe);
}

void BackupServerPrepareHash::operator()(void)
{
	while(true)
	{
		working=false;
		std::string data;
		size_t rc=pipe->Read(&data);
		if(data=="exit")
		{
			output->Write("exit");
			Server->Log("server_prepare_hash Thread finished (exit)");
			delete this;
			return;
		}
		else if(data=="flush")
		{
			continue;
		}

		if(rc>0)
		{
			working=true;
			
			CRData rd(&data);

			std::string temp_fn;
			rd.getStr(&temp_fn);

			int backupid;
			rd.getInt(&backupid);

			int incremental;
			rd.getInt(&incremental);

			char with_hashes;
			rd.getChar(&with_hashes);

			std::string tfn;
			rd.getStr(&tfn);

			std::string hashpath;
			rd.getStr(&hashpath);

			std::string hashoutput_fn;
			rd.getStr(&hashoutput_fn);

			bool diff_file=!hashoutput_fn.empty();

			std::string old_file_fn;
			rd.getStr(&old_file_fn);

			int64 t_filesize;
			rd.getInt64(&t_filesize);

			std::string client_sha_dig;
			rd.getStr(&client_sha_dig);

			std::string sparse_extents_fn;
			rd.getStr(&sparse_extents_fn);

			char c_hash_func;
			rd.getChar(&c_hash_func);

			char c_has_snapshot;
			rd.getChar(&c_has_snapshot);

			bool has_snapshot = c_has_snapshot == 1;
			
			FileMetadata metadata;
			metadata.read(rd);

			IFile *tf=Server->openFile(os_file_prefix((temp_fn)), MODE_READ);
			IFile *old_file=NULL;
			if(diff_file)
			{
				old_file=Server->openFile(os_file_prefix((old_file_fn)), MODE_READ);
				if(old_file==NULL)
				{
					ServerLogger::Log(logid, "Error opening file \""+old_file_fn+"\" from pipe for reading. File: old_file ec="+convert(os_last_error()), LL_ERROR);
					has_error=true;
					if(tf!=NULL) Server->destroy(tf);
					continue;
				}
			}

			if(tf==NULL)
			{
				ServerLogger::Log(logid, "Error opening file \""+temp_fn+"\" from pipe for reading file. File: temp_fn ec="+convert(os_last_error()), LL_ERROR);
				has_error=true;
				if(old_file!=NULL)
				{
					Server->destroy(old_file);
				}
			}
			else
			{
				std::auto_ptr<ExtentIterator> extent_iterator;
				if (!sparse_extents_fn.empty())
				{
					IFile* sparse_extents_f = Server->openFile(sparse_extents_fn, MODE_READ);

					if (sparse_extents_f != NULL)
					{
						extent_iterator.reset(new ExtentIterator(sparse_extents_f, true, hash_bsize));
					}
				}

				ServerLogger::Log(logid, "PT: Hashing file \""+ExtractFileName(tfn)+"\"", LL_DEBUG);
				std::string h;
				if(!diff_file)
				{
					if (c_hash_func == HASH_FUNC_SHA512_NO_SPARSE
						|| c_hash_func == HASH_FUNC_SHA512)
					{
						HashSha512 hashsha;
						if (hash_sha(tf, extent_iterator.get(), c_hash_func != HASH_FUNC_SHA512_NO_SPARSE, hashsha))
						{
							h = hashsha.finalize();
						}
					}
					else
					{
						TreeHash treehash;
						if (hash_sha(tf, extent_iterator.get(), true, treehash))
						{
							h = treehash.finalize();
						}
					}
					
				}
				else
				{
					if (c_hash_func == HASH_FUNC_SHA512_NO_SPARSE
						|| c_hash_func == HASH_FUNC_SHA512)
					{
						hashoutput_f = NULL;
						HashSha512 hashsha;
						hashf = &hashsha;
						if (hash_with_patch(old_file, tf, extent_iterator.get(), c_hash_func != HASH_FUNC_SHA512_NO_SPARSE))
						{
							h = hashsha.finalize();
						}
					}
					else
					{
						std::auto_ptr<IFile> l_hashoutput_f(Server->openFile(os_file_prefix(hashoutput_fn), MODE_READ));
						hashoutput_f = l_hashoutput_f.get();
						TreeHash treehash;
						hashf = &treehash;
						if (hash_with_patch(old_file, tf, extent_iterator.get(), true))
						{
							h = treehash.finalize();
						}
					}
				}

				if (h.empty())
				{
					ServerLogger::Log(logid, "Error while hashing file \"" + tf->getFilename() + "\" (destination: \""+ tfn+"\"). Failing backup.", LL_ERROR);
					has_error = true;
				}
				else if(!client_sha_dig.empty() && h!=client_sha_dig)
				{
					if (has_snapshot)
					{
						ServerLogger::Log(logid, "Client calculated hash of \"" + tfn + "\" differs from server calculated hash. "
							"This may be caused by a bug or by random bit flips on the client or server hard disk. Failing backup. "
							"(Hash: "+ print_hash_func(c_hash_func)+
							", client hash: "+base64_encode(reinterpret_cast<const unsigned char*>(client_sha_dig.data()), static_cast<unsigned int>(client_sha_dig.size()))+
							", server hash: "+ base64_encode(reinterpret_cast<const unsigned char*>(h.data()), static_cast<unsigned int>(h.size()))+")", LL_ERROR);
						has_error = true;
					}
					else
					{
						ServerLogger::Log(logid, "Client calculated hash of \"" + tfn + "\" differs from server calculated hash. "
							"The file is being backed up without a snapshot so this is most likely caused by the file changing during the backup. "
							"The backed up file may be corrupt and not a valid, consistent backup. "
							"(Hash: "+print_hash_func(c_hash_func) + ")", LL_WARNING);
					}
				}

				Server->destroy(tf);
				if(old_file!=NULL)
				{
					Server->destroy(old_file);
				}
				
				CWData data;
				data.addInt(BackupServerHash::EAction_LinkOrCopy);
				data.addString(temp_fn);
				data.addInt(backupid);
				data.addInt(incremental);
				data.addChar(with_hashes);
				data.addString(tfn);
				data.addString(hashpath);
				data.addString(h);
				data.addString(hashoutput_fn);
				data.addString(old_file_fn);
				data.addInt64(t_filesize);
				data.addString(sparse_extents_fn);
				metadata.serialize(data);

				output->Write(data.getDataPtr(), data.getDataSize() );
			}
		}
	}
}

std::string BackupServerPrepareHash::calc_hash(IFsFile * f, std::string method)
{
	FsExtentIterator extent_iterator(f, hash_bsize);
	if (method=="sha512-nosparse" 
		|| method=="sha512-sparse")
	{
		HashSha512 hashsha;
		if (hash_sha(f, &extent_iterator, method == "sha512-sparse", hashsha))
		{
			return hashsha.finalize();
		}
	}
	else
	{
		TreeHash treehash;
		if (hash_sha(f, &extent_iterator, true, treehash))
		{
			return treehash.finalize();
		}
	}

	return std::string();
}

bool BackupServerPrepareHash::hash_sha(IFile *f, IExtentIterator* extent_iterator, bool hash_with_sparse, IHashFunc& hashf, IHashProgressCallback* progress_callback)
{
	f->Seek(0);
	std::vector<char> buf;
	buf.resize(hash_bsize);
	_u32 rc;

	int64 fpos = 0;
	IFsFile::SSparseExtent curr_extent;

	if (extent_iterator != NULL)
	{
		extent_iterator->reset();
		curr_extent = extent_iterator->nextExtent();
	}

	int64 skip_start = -1;
	int64 skip_count = 0;

	do
	{
		while (curr_extent.offset != -1
			&& curr_extent.offset + curr_extent.size<fpos)
		{
			curr_extent = extent_iterator->nextExtent();
		}

		if (curr_extent.offset != -1
			&& curr_extent.offset <= fpos
			&& curr_extent.offset + curr_extent.size>=fpos + static_cast<int64>(hash_bsize))
		{
			if (skip_start == -1)
			{
				skip_start = fpos;
			}
			fpos += hash_bsize;
			rc = hash_bsize;
			continue;
		}

		if (skip_start != -1)
		{
			f->Seek(fpos);
		}

		bool has_read_error = false;
		rc=f->Read(buf.data(), hash_bsize, &has_read_error);

		if (has_read_error)
		{
			Server->Log("Error reading from file \"" + f->getFilename() + "\" while hashing", LL_ERROR);
			return false;
		}

		if (hash_with_sparse
			&& rc == hash_bsize
			&& buf_is_zero(buf.data(), hash_bsize))
		{
			if (skip_start == -1)
			{
				skip_start = fpos;
			}
			fpos += hash_bsize;
			rc = hash_bsize;

			if (progress_callback != NULL)
			{
				progress_callback->hash_progress(fpos);
			}

			continue;
		}

		if (skip_start != -1)
		{
			++skip_count;
			int64 skip[2];
			skip[0] = skip_start;
			skip[1] = fpos - skip_start;
			hashf.sparse_hash(reinterpret_cast<char*>(&skip), sizeof(int64) * 2);
			skip_start = -1;
		}

		if (rc > 0)
		{
			hashf.hash(buf.data(), rc);

			fpos += rc;

			if (progress_callback != NULL)
			{
				progress_callback->hash_progress(fpos);
			}

		}
	}
	while(rc>0);

	if (progress_callback != NULL)
	{
		progress_callback->hash_progress(fpos);
	}

	return true;
}

bool BackupServerPrepareHash::hash_with_patch(IFile *f, IFile *patch, ExtentIterator* extent_iterator, bool hash_with_sparse)
{
	has_sparse_extents = false;

	if (hashoutput_f == NULL)
	{
		chunk_patcher.setRequireUnchanged(true);
		chunk_patcher.setUnchangedAlign(0);
	}
	else
	{
		chunk_patcher.setRequireUnchanged(false);
		chunk_patcher.setUnchangedAlign(hash_bsize);
	}

	file_pos = 0;
	chunk_patcher.setWithSparse(hash_with_sparse);
	bool b = chunk_patcher.ApplyPatch(f, patch, extent_iterator);
	return b;
}

void BackupServerPrepareHash::addUnchangedHashes(int64 start, size_t size, bool* is_sparse)
{
	assert(start%hash_bsize == 0);
	assert(size==hash_bsize || start+size==chunk_patcher.getFilesize());

	if (!hashoutput_f->Seek(sizeof(_i64) + (start / hash_bsize)*chunkhash_single_size))
	{
		Server->Log("Error seeking in hashoutput file " + hashoutput_f->getFilename(), LL_ERROR);
		has_error = true;
	}

	bool has_read_error = false;
	char chunkhashes[chunkhash_single_size];
	_u32 r = hashoutput_f->Read(chunkhashes, chunkhash_single_size, &has_read_error);

	if (has_read_error)
	{
		Server->Log("Error reading from " + hashoutput_f->getFilename(), LL_ERROR);
		has_error = true;
	}

	assert(r == chunkhash_single_size || start + size == chunk_patcher.getFilesize());

	size_t chunkhash_size = big_hash_size + small_hash_size*(size / c_small_hash_dist + (size%c_small_hash_dist == 0 ? 0 : 1));
	
	assert(chunkhash_size==chunkhash_single_size || start + size == chunk_patcher.getFilesize());

	std::string sparse_hashes = get_sparse_extent_content();

	if (is_sparse!=NULL && memcmp(sparse_hashes.data(), chunkhashes, (std::min)(chunkhash_size, static_cast<size_t>(r))) == 0)
	{
		*is_sparse = true;
		return;
	}

	reinterpret_cast<TreeHash*>(hashf)->addHashAllAdler(chunkhashes, r, size);
}

void BackupServerPrepareHash::next_sparse_extent_bytes(const char * buf, size_t bsize)
{
	hashf->sparse_hash(buf, (unsigned int)bsize);
}

void BackupServerPrepareHash::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed, bool* is_sparse)
{
	if (buf != NULL)
	{
		hashf->hash(buf, (unsigned int)bsize);
	}
	else if(hashoutput_f!=NULL
		&& (is_sparse==NULL || *is_sparse==false) )
	{
		addUnchangedHashes(file_pos, bsize, is_sparse);
	}

	file_pos += bsize;
}

bool BackupServerPrepareHash::isWorking(void)
{
	return working;
}

bool BackupServerPrepareHash::hasError(void)
{
	return has_error;
}

#endif //CLIENT_ONLY
