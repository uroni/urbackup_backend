#include "ParallelHash.h"
#include "../Interface/Server.h"
#include "ClientHash.h"
#include <algorithm>
#include "database.h"

namespace
{
	const size_t max_modify_file_buffer_size = 2 * 1024 * 1024;
	const int64 file_buffer_commit_interval = 120 * 1000;
}

ParallelHash::ParallelHash(IFile * phash_queue, int sha_version)
	: do_quit(false), phash_queue(phash_queue), phash_queue_pos(0),
	stdout_buf_size(0), stdout_buf_pos(0), mutex(Server->createMutex()),
	last_file_buffer_commit_time(0), sha_version(sha_version)
{
	stdout_buf.resize(4090);
}

bool ParallelHash::getExitCode(int & exit_code)
{
	exit_code = 0;
	return true;
}

void ParallelHash::forceExit()
{
	do_quit = true;
}

bool ParallelHash::readStdoutIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes)
{
	while(!do_quit)
	{
		IScopedLock lock(mutex.get());
		if (stdout_buf_size > stdout_buf_pos)
		{
			read_bytes = (std::min)(stdout_buf_size - stdout_buf_pos, buf_avail);
			memcpy(buf, &stdout_buf[stdout_buf_pos], read_bytes);
			stdout_buf_pos += read_bytes;

			if (stdout_buf_pos == stdout_buf_size)
			{
				stdout_buf_pos = 0;
				stdout_buf_size = 0;
			}

			return true;
		}
		else
		{
			Server->wait(1000);
		}
	}

	return false;
}

void ParallelHash::finishStdout()
{
}

bool ParallelHash::readStderrIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes)
{
	while (!do_quit)
	{
		Server->wait(1000);
	}
	
	return true;
}

void ParallelHash::operator()()
{
	ClientDAO clientdao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT));

	while (!do_quit)
	{
		bool had_msg = false;
		if (phash_queue->Size() >= phash_queue_pos + sizeof(_u32))
		{
			_u32 msg_size;
			if (phash_queue->Read(phash_queue_pos, reinterpret_cast<char*>(&msg_size), sizeof(msg_size))
				== sizeof(msg_size))
			{
				if (phash_queue->Size() >= phash_queue_pos + sizeof(_u32) + msg_size)
				{
					had_msg = true;

					std::string msg = phash_queue->Read(phash_queue_pos + sizeof(_u32), msg_size);
					CRData data(msg.data(), msg.size());
					phash_queue_pos += sizeof(_u32) + msg_size;

					if (!hashFile(data, clientdao))
					{
						Server->Log("Error hashing file. Data: " + msg, LL_ERROR);
					}
				}
			}
		}
		if (!had_msg)
		{
			Server->wait(1000);
			CWData data;
			data.addUShort(1);
			data.addChar(0);
			addToStdoutBuf(data.getDataPtr(), data.getDataSize());
		}
	}
}

bool ParallelHash::hashFile(CRData & data, ClientDAO& clientdao)
{
	char id;
	if (!data.getChar(&id))
		return false;

	if (id == ID_SET_CURR_DIRS)
	{
		if (!data.getStr2(&curr_dir)
			|| !data.getInt(&curr_tgroup)
			|| !data.getStr2(&curr_snapshot_dir))
		{
			return false;
		}

		return true;
	}
	else if (id == ID_FINISH_CURR_DIR)
	{
		int64 target_generation;
		if (!data.getVarInt(&target_generation))
			return false;

		std::vector<SFileAndHash> files;
		int64 generation = -1;
		if (clientdao.getFiles(curr_dir, curr_tgroup, files, generation))
		{
			if (generation != target_generation)
				return true;

			bool added_hash = false;
			for (size_t i = 0; i < files.size(); ++i)
			{
				if (files[i].hash.empty())
				{
					std::vector<SFileAndHash>::iterator it =
						std::find(curr_files.begin(), curr_files.end(), files[i]);
					if (it != curr_files.end())
					{
						files[i].hash = it->hash;
						added_hash = true;
					}
				}
			}

			if (added_hash)
			{
				addModifyFileBuffer(clientdao, curr_dir, curr_tgroup, files, target_generation);
			}

			return true;
		}

		return false;
	}
	else if (id == ID_INIT_HASH)
	{
		client_hash.reset(new ClientHash(NULL, false, 0, NULL, 0));
	}
	else if (id == ID_CBT_DATA)
	{
		IFile* index_hdat_file;
		int64 index_hdat_fs_block_size;
		size_t* snapshot_sequence_id;
		int64 snapshot_sequence_id_reference;
		if (!data.getVoidPtr(reinterpret_cast<void**>(&index_hdat_file))
			|| !data.getVarInt(&index_hdat_fs_block_size)
			|| !data.getVoidPtr(reinterpret_cast<void**>(&snapshot_sequence_id))
			|| !data.getVarInt(&snapshot_sequence_id_reference))
		{
			return false;
		}

		client_hash.reset(new ClientHash(index_hdat_file, true, index_hdat_fs_block_size,
			snapshot_sequence_id, static_cast<size_t>(snapshot_sequence_id_reference)));
	}

	if (id != ID_HASH_FILE)
	{
		return false;
	}

	std::string fn;
	if (!data.getStr2(&fn))
	{
		return false;
	}

	int64 file_id;
	if (!data.getVarInt(&file_id))
	{
		return false;
	}

	std::string full_path = curr_snapshot_dir + os_file_sep() + fn;

	SFileAndHash fandhash;
	CWData wdata;
	wdata.addChar(1);
	if (sha_version == 256)
	{
		HashSha256 hash_256;
		if (!client_hash->getShaBinary(fn, hash_256, false))
		{
			return false;
		}

		fandhash.hash = hash_256.finalize();
	}
	else if (sha_version == 528)
	{
		TreeHash treehash(client_hash->hasCbtFile() ? client_hash.get() : NULL);
		if (!client_hash->getShaBinary(fn, treehash, client_hash->hasCbtFile()))
		{
			return false;
		}

		fandhash.hash = treehash.finalize();
	}
	else
	{
		HashSha512 hash_512;
		if (!client_hash->getShaBinary(fn, hash_512, false))
		{
			return false;
		}

		fandhash.hash = hash_512.finalize();
	}

	wdata.addVarInt(file_id);
	wdata.addString2(fandhash.hash);
	fandhash.name = fn;
	curr_files.push_back(fandhash);

	addToStdoutBuf(wdata.getDataPtr(), wdata.getDataSize());

	return true;
}

void ParallelHash::addToStdoutBuf(const char * ptr, size_t size)
{
	IScopedLock lock(mutex.get());

	while (stdout_buf_size + size > 32 * 1024)
	{
		lock.relock(NULL);
		Server->wait(1000);
		lock.relock(mutex.get());
	}

	if (stdout_buf_size + size > stdout_buf.size())
	{
		stdout_buf.resize(stdout_buf_size + size);
	}

	memcpy(&stdout_buf[stdout_buf_size], ptr, size);
	stdout_buf_size += size;
}

size_t ParallelHash::calcBufferSize(const std::string &path, const std::vector<SFileAndHash> &data)
{
	size_t add_size = path.size() + sizeof(std::string) + sizeof(int) + sizeof(int64);
	for (size_t i = 0; i<data.size(); ++i)
	{
		add_size += data[i].name.size();
		add_size += sizeof(SFileAndHash);
		add_size += data[i].hash.size();
	}
	add_size += sizeof(std::vector<SFile>);

	return add_size;
}

void ParallelHash::addModifyFileBuffer(ClientDAO& clientdao, const std::string & path, int tgroup,
	const std::vector<SFileAndHash>& files, int64 target_generation)
{
	modify_file_buffer_size += calcBufferSize(path, files);

	modify_file_buffer.push_back(SBufferItem(path, tgroup, files));

	if (last_file_buffer_commit_time == 0)
	{
		last_file_buffer_commit_time = Server->getTimeMS();
	}

	if (modify_file_buffer_size>max_modify_file_buffer_size
		|| Server->getTimeMS() - last_file_buffer_commit_time>file_buffer_commit_interval)
	{
		commitModifyFileBuffer(clientdao);
	}
}

void ParallelHash::commitModifyFileBuffer(ClientDAO& clientdao)
{
	DBScopedWriteTransaction trans(clientdao.getDatabase());
	for (size_t i = 0; i<modify_file_buffer.size(); ++i)
	{
		clientdao.modifyFiles(modify_file_buffer[i].path, modify_file_buffer[i].tgroup,
			modify_file_buffer[i].files, modify_file_buffer[i].target_generation);
	}

	modify_file_buffer.clear();
	modify_file_buffer_size = 0;
	last_file_buffer_commit_time = Server->getTimeMS();
}
