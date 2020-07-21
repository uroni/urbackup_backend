#include "ParallelHash.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../Interface/Condition.h"
#include "ClientHash.h"
#include <algorithm>
#include "database.h"
#include "../stringtools.h"
#include <assert.h>

//#define HASH_CBT_CHECK

namespace
{
	const size_t max_modify_file_buffer_size = 2 * 1024 * 1024;
	const int64 file_buffer_commit_interval = 120 * 1000;
	const int64 link_file_min_size = 2048;
}

ParallelHash::ParallelHash(SQueueRef* phash_queue, int sha_version, size_t extra_n_threads)
	: do_quit(false), phash_queue(phash_queue), phash_queue_pos(0),
	stdout_buf_size(0), stdout_buf_pos(0), mutex(Server->createMutex()),
	last_file_buffer_commit_time(0), sha_version(sha_version), eof(false),
	extra_n_threads(extra_n_threads), extra_thread(false),
	extra_mutex(Server->createMutex()), extra_cond(Server->createCondition()),
	modify_file_buffer_mutex(Server->createMutex())
{
	stdout_buf.resize(4090);
	ticket = Server->getThreadPool()->execute(this, extra_n_threads>0 ? "phash master": "phash");
}

bool ParallelHash::getExitCode(int & exit_code)
{
	Server->getThreadPool()->waitFor(ticket);
	exit_code = 0;
	return true;
}

void ParallelHash::forceExit()
{
	do_quit = true;
	//Server->getThreadPool()->waitFor(ticket);
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

				if (eof)
				{
					return false;
				}
			}

			return true;
		}
		else
		{
			if (stdout_buf_pos == stdout_buf_size
				&& eof)
			{
				return false;
			}

			lock.relock(NULL);
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
	while (!do_quit
		&& !eof)
	{
		Server->wait(1000);
	}
	
	return false;
}

void ParallelHash::operator()()
{
	if (extra_thread)
	{
		runExtraThread();
		return;
	}

	extra_thread = true;
	for (size_t i = 0; i < extra_n_threads; ++i)
	{
		extra_tickets.push_back(Server->getThreadPool()->execute(this, "phash e"+convert(i)));
	}

	ClientDAO clientdao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT));

	int mode = MODE_READ_SEQUENTIAL;
#ifdef _WIN32
	mode = MODE_READ_DEVICE;
#endif
	std::auto_ptr<IFile> phashf(Server->openFile(phash_queue->phash_queue->getFilename(), mode));

	while (!do_quit
		&& phashf.get()!=NULL)
	{
		bool had_msg = false;
		if (phashf->Size() >= phash_queue_pos + static_cast<int64>(sizeof(_u32)))
		{
			_u32 msg_size;
			if (phashf->Read(phash_queue_pos, reinterpret_cast<char*>(&msg_size), sizeof(msg_size))
				== sizeof(msg_size))
			{
				if (phashf->Size() >= phash_queue_pos + static_cast<int64>(sizeof(_u32)) + msg_size)
				{
					had_msg = true;

					std::string msg = phashf->Read(phash_queue_pos + sizeof(_u32), msg_size);
					
					int64 working_file_id = phash_queue_pos;
					phash_queue_pos += sizeof(_u32) + msg_size;

					IScopedLock lock(extra_mutex.get());
					working_file_ids.insert(working_file_id);	

					if (extra_n_threads > 0)
					{
						extra_queue.push_back(std::make_pair(working_file_id, msg));
						extra_cond->notify_one();
					}
					else
					{
						lock.relock(NULL);

						CRData data(msg.data(), msg.size());
						hashFile(working_file_id, data, clientdao);

						lock.relock(extra_mutex.get());
						working_file_ids.erase(working_file_id);
						addQueuedStdoutMsgs();
					}

					if (eof)
					{
						break;
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
			if (!addToStdoutBuf(data.getDataPtr(), data.getDataSize()))
				break;
		}
	}

	commitModifyFileBuffer(clientdao);

	{
		IScopedLock lock(extra_mutex.get());
		do_quit = true;
		extra_cond->notify_all();
	}

	Server->getThreadPool()->waitFor(extra_tickets);

	if (phash_queue->deref())
	{
		delete phash_queue;
		phash_queue = NULL;
	}
}

void ParallelHash::addQueuedStdoutMsgs()
{
	std::map<int64, CWData>::iterator it_msg;
	while (!working_file_ids.empty() &&
		(it_msg = stdout_msg_buf.find(*working_file_ids.begin())) != stdout_msg_buf.end())
	{
		addToStdoutBuf(it_msg->second.getDataPtr(), it_msg->second.getDataSize());
		stdout_msg_buf.erase(it_msg);
	}
}

bool ParallelHash::hashFile(int64 working_file_id, CRData & data, ClientDAO& clientdao)
{
	char id;
	if (!data.getChar(&id))
		return false;

	if (id == ID_SET_CURR_DIRS)
	{
		int64 dir_id;
		SCurrDir dir;
		if (!data.getVarInt(&dir_id)
			|| !data.getStr2(&dir.dir)
			|| !data.getInt(&dir.tgroup)
			|| !data.getStr2(&dir.snapshot_dir))
		{
			assert(false);
			return false;
		}

		dir.finish = false;
		IScopedLock lock(mutex.get());
		curr_dirs[dir_id] = dir;
		return true;
	}
	else if (id == ID_FINISH_CURR_DIR)
	{
		int64 target_generation;
		int64 id;
		int64 dir_files;
		if (!data.getVarInt(&id)
			|| !data.getVarInt(&target_generation)
			|| !data.getVarInt(&dir_files))
		{
			assert(false);
			return false;
		}

		SCurrDir* dir;
		{
			IScopedLock lock(mutex.get());
			dir = &curr_dirs[id];

			if (dir->files.size() != dir_files)
			{
				dir->finish = true;
				dir->target_generation = target_generation;
				dir->dir_target_nfiles = dir_files;
				return true;
			}
		}

		return finishDir(dir, clientdao, target_generation, id);
	}
	else if (id == ID_INIT_HASH)
	{
		client_hash.reset(new ClientHash(NULL, false, 0, NULL, 0));
		return true;
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
		return true;
	}
	else if (id == ID_PHASH_FINISH)
	{
		eof = true;
		return true;
	}

	if (id != ID_HASH_FILE)
	{
		assert(false);
		return false;
	}

	int64 file_id;
	if (!data.getVarInt(&file_id))
	{
		assert(false);
		return false;
	}

	std::string fn;
	if (!data.getStr2(&fn))
	{
		assert(false);
		return false;
	}

	int64 dir_id;
	if (!data.getVarInt(&dir_id))
	{
		assert(false);
		return false;
	}

	SCurrDir* dir;
	{
		IScopedLock lock(mutex.get());
		dir = &curr_dirs[dir_id];
	}

	std::string full_path = dir->snapshot_dir + os_file_sep() + fn;

	std::auto_ptr<IFsFile>  f(Server->openFile(os_file_prefix(full_path), MODE_READ_SEQUENTIAL_BACKUP));

	SFileAndHash fandhash;
	std::string ph_action;
	if (f.get() != NULL && f->Size() < link_file_min_size)
	{
		f.reset();
		ph_action = " (Not hashing. File too small)";
	}
	else if (sha_version == 256)
	{
		f.reset();

		HashSha256 hash_256;
		if (!client_hash->getShaBinary(full_path, hash_256, false))
		{
			Server->Log("Error hashing file (0) " + full_path + ". " + os_last_error_str(), LL_DEBUG);
		}
		else
		{
			fandhash.hash = hash_256.finalize();
		}
		ph_action = " (Calculated sha256 hash)";
	}
	else if (sha_version == 528)
	{
		f.reset();

		TreeHash treehash(client_hash->hasCbtFile() ? client_hash.get() : NULL);
		if (!client_hash->getShaBinary(full_path, treehash, client_hash->hasCbtFile()))
		{
			Server->Log("Error hashing file (1) " + full_path+". "+os_last_error_str(), LL_DEBUG);
		}
		else
		{
			fandhash.hash = treehash.finalize();
		}

#ifdef HASH_CBT_CHECK
		TreeHash treehash2(client_hash->hasCbtFile() ? client_hash.get() : NULL);
		client_hash->getShaBinary(full_path, treehash2, false);
		
		std::string other_hash = treehash2.finalize();
		if (other_hash != fandhash.hash)
		{
			Server->Log("Treehash compare without CBT failed at file \"" + full_path 
				+ "\". Real hash: "+ base64_encode_dash(other_hash), LL_ERROR);
		}
#endif
		ph_action = " (Calculated tree hash v1)";
	}
	else
	{
		f.reset();

		HashSha512 hash_512;
		if (!client_hash->getShaBinary(full_path, hash_512, false))
		{
			Server->Log("Error hashing file (2) " + full_path + ". " + os_last_error_str(), LL_DEBUG);
		}
		else
		{
			fandhash.hash = hash_512.finalize();
		}
		ph_action = " (Calculated sha512 hash)";
	}

	CWData wdata;
	wdata.addUShort(0);
	wdata.addChar(1);
	wdata.addVarInt(file_id);
	wdata.addString2(fandhash.hash);
	fandhash.name = fn;
	*reinterpret_cast<_u16*>(wdata.getDataPtr()) = little_endian(static_cast<_u16>(wdata.getDataSize() - sizeof(_u16)));

	bool finish_dir = false;
	{
		IScopedLock lock(mutex.get());
		dir->files.push_back(fandhash);

		if (dir->finish &&
			dir->files.size() == dir->dir_target_nfiles)
		{
			finish_dir = true;
		}
	}

	if (finish_dir)
	{
		finishDir(dir, clientdao, dir->target_generation, dir_id);
	}

	Server->Log("Parallel hash \"" + full_path + "\" id=" + convert(file_id) + " hash=" + base64_encode_dash(fandhash.hash)+ ph_action, LL_DEBUG);

	{
		IScopedLock lock(mutex.get());
		if (!working_file_ids.empty()
			&& *working_file_ids.begin() != working_file_id)
		{
			stdout_msg_buf[working_file_id] = wdata;
			return true;
		}
	}

	return addToStdoutBuf(wdata.getDataPtr(), wdata.getDataSize());
}

bool ParallelHash::finishDir(ParallelHash::SCurrDir* dir, ClientDAO& clientdao, const int64& target_generation, int64& id)
{
#ifndef _WIN32
	std::string path_lower = dir->dir + os_file_sep();
#else
	std::string path_lower = strlower(dir->dir + os_file_sep());
#endif

	std::vector<SFileAndHash> files;
	int64 generation = -1;
	if (clientdao.getFiles(path_lower, dir->tgroup, files, generation))
	{
		if (generation != target_generation)
		{
			IScopedLock lock(mutex.get());
			curr_dirs.erase(id);
			return true;
		}

		std::sort(dir->files.begin(), dir->files.end());

		bool added_hash = false;
		for (size_t i = 0; i < files.size(); ++i)
		{
			if (files[i].hash.empty())
			{
				std::vector<SFileAndHash>::iterator it =
					std::lower_bound(dir->files.begin(), dir->files.end(), files[i]);
				if (it != dir->files.end()
					&& it->name == files[i].name)
				{
					files[i].hash = it->hash;
					added_hash = true;
				}
			}
		}

		if (added_hash)
		{
			addModifyFileBuffer(clientdao, path_lower, dir->tgroup, files, target_generation);
		}

		IScopedLock lock(mutex.get());
		curr_dirs.erase(id);
		return true;
	}

	IScopedLock lock(mutex.get());
	curr_dirs.erase(id);
	return false;
}

bool ParallelHash::addToStdoutBuf(const char * ptr, size_t size)
{
	IScopedLock lock(mutex.get());

	while (stdout_buf_size + size > 32 * 1024
		&& !do_quit)
	{
		lock.relock(NULL);
		Server->wait(1000);
		lock.relock(mutex.get());
	}

	if (do_quit)
		return false;

	if (stdout_buf_size + size > stdout_buf.size())
	{
		stdout_buf.resize(stdout_buf_size + size);
	}

	memcpy(&stdout_buf[stdout_buf_size], ptr, size);
	stdout_buf_size += size;

	return true;
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

void ParallelHash::runExtraThread()
{
	IScopedLock lock(extra_mutex.get());
	while (!do_quit)
	{
		while (extra_queue.empty() &&
			do_quit)
		{
			extra_cond->wait(&lock);
		}

		std::pair<int64, std::string> msg = extra_queue.front();
		extra_queue.pop_front();

		lock.relock(NULL);


		lock.relock(extra_mutex.get());
		working_file_ids.erase(msg.first);

		addQueuedStdoutMsgs();
	}
}

void ParallelHash::addModifyFileBuffer(ClientDAO& clientdao, const std::string & path, int tgroup,
	const std::vector<SFileAndHash>& files, int64 target_generation)
{
	IScopedLock lock(modify_file_buffer_mutex.get());

	modify_file_buffer_size += calcBufferSize(path, files);

	modify_file_buffer.push_back(SBufferItem(path, tgroup, files, target_generation));

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
