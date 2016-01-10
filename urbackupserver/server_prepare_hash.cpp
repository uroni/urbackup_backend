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

			std::auto_ptr<ExtentIterator> extent_iterator;
			if (!sparse_extents_fn.empty())
			{
				IFile* sparse_extents_f = Server->openFile(sparse_extents_fn, MODE_READ);

				if (sparse_extents_f != NULL)
				{
					extent_iterator.reset(new ExtentIterator(sparse_extents_f));
				}
			}
			
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
				ServerLogger::Log(logid, "PT: Hashing file \""+ExtractFileName(tfn)+"\"", LL_DEBUG);
				std::string h;
				if(!diff_file)
				{

					h=hash_sha(tf, extent_iterator.get());
				}
				else
				{
					h=hash_with_patch(old_file, tf, extent_iterator.get());
				}

				if(!client_sha_dig.empty() && h!=client_sha_dig)
				{
					ServerLogger::Log(logid, "Client calculated hash of \""+tfn+"\" differs from server calculated hash. "
						"This may be caused by a bug, when a file changes while it is being backed up (backing up without snapshots) or by random bit flips on hard disks. "
						"This message will most likely disappear if you run a full backup.", LL_WARNING);
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

std::string BackupServerPrepareHash::hash_sha(IFile *f, ExtentIterator* extent_iterator)
{
	const size_t bsize = 32768;
	f->Seek(0);
	unsigned char buf[bsize];
	_u32 rc;

	sha_def_ctx local_ctx;
	sha_def_init(&local_ctx);
	int64 fpos = 0;
	IFsFile::SSparseExtent curr_extent;


	sha_def_ctx sparse_ctx;
	sha_def_init(&sparse_ctx);

	if (extent_iterator != NULL)
	{
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
			&& curr_extent.offset <= fpos && curr_extent.offset + curr_extent.size>=fpos + static_cast<int64>(bsize))
		{
			if (skip_start == -1)
			{
				skip_start = fpos;
			}
			fpos += bsize;
			rc = bsize;
			continue;
		}

		if (skip_start != -1)
		{
			f->Seek(fpos);
		}

		rc=f->Read((char*)buf, bsize);

		if (rc == bsize && buf_is_zero((char*)buf, bsize))
		{
			if (skip_start == -1)
			{
				skip_start = fpos;
			}
			fpos += bsize;
			rc = bsize;
			continue;
		}

		if (skip_start != -1)
		{
			++skip_count;
			int64 skip[2];
			skip[0] = skip_start;
			skip[1] = fpos - skip_start;
			sha_def_update(&sparse_ctx, reinterpret_cast<unsigned char*>(&skip), sizeof(int64) * 2);
			skip_start = -1;
		}

		if (rc > 0)
		{
			sha_def_update(&local_ctx, buf, rc);
			fpos += rc;
		}
	}
	while(rc>0);

	if (skip_count > 0)
	{
		std::string skip_hash;
		skip_hash.resize(SHA_DEF_DIGEST_SIZE);
		sha_def_final(&sparse_ctx, (unsigned char*)&skip_hash[0]);

		sha_def_update(&local_ctx, reinterpret_cast<unsigned char*>(&skip_hash[0]), SHA_DEF_DIGEST_SIZE);
	}
	
	std::string ret;
	ret.resize(SHA_DEF_DIGEST_SIZE);
	sha_def_final(&local_ctx, (unsigned char*)&ret[0]);
	return ret;
}

std::string BackupServerPrepareHash::hash_with_patch(IFile *f, IFile *patch, ExtentIterator* extent_iterator)
{
	sha_def_init(&ctx);
	sha_def_init(&sparse_ctx);

	has_sparse_extents = false;
	
	chunk_patcher.ApplyPatch(f, patch, extent_iterator);

	std::string ret;
	ret.resize(SHA_DEF_DIGEST_SIZE);

	if (has_sparse_extents)
	{
		sha512_final(&sparse_ctx, (unsigned char*)&ret[0]);
		sha512_update(&ctx, reinterpret_cast<unsigned char*>(&ret[0]), SHA_DEF_DIGEST_SIZE);
	}

	sha_def_final(&ctx, (unsigned char*)&ret[0]);
	return ret;
}

void BackupServerPrepareHash::next_sparse_extent_bytes(const char * buf, size_t bsize)
{
	has_sparse_extents = true;
	sha_def_update(&sparse_ctx, (const unsigned char*)buf, (unsigned int)bsize);
}

void BackupServerPrepareHash::next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed)
{
	sha_def_update(&ctx, (const unsigned char*)buf, (unsigned int)bsize);
}

bool BackupServerPrepareHash::isWorking(void)
{
	return working;
}

bool BackupServerPrepareHash::hasError(void)
{
	volatile bool r=has_error;
	has_error=false;
	return r;
}

#endif //CLIENT_ONLY
