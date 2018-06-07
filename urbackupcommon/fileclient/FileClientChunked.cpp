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

#include "FileClientChunked.h"
#include "../../common/data.h"
#include "../../Interface/Server.h"
#include "../../Interface/File.h"
#include "../../stringtools.h"

#include <memory.h>
#include <assert.h>
#include <queue>
#include <memory>
#include <algorithm>
#include <limits.h>
#include "../../common/adler32.h"
#include "../chunk_hasher.h"
#include "../../urbackupcommon/os_functions.h"

#define VLOG(x) x


namespace
{
	const char* fc_err_strs[] = {
		"Seeking in file failed",
		"Reading from file failed"
	};
}

int64 get_hashdata_size(int64 hashfilesize)
{
	if(hashfilesize==-1)
	{
		return chunkhash_file_off;
	}

	int64 num_chunks = hashfilesize/c_checkpoint_dist;
	int64 size = chunkhash_file_off+num_chunks*chunkhash_single_size;
	if(hashfilesize%c_checkpoint_dist!=0)
	{
		size+=big_hash_size + ((hashfilesize%c_checkpoint_dist)/c_chunk_size)*small_hash_size
			+ ((((hashfilesize%c_checkpoint_dist)%c_chunk_size)!=0)?small_hash_size:0);
	}
	return size;
}

FileClientChunked::FileClientChunked(IPipe *pipe, bool del_pipe, CTCPStack *stack,
	FileClientChunked::ReconnectionCallback *reconnection_callback, FileClientChunked::NoFreeSpaceCallback *nofreespace_callback
	, std::string identity, FileClientChunked* prev)
	: pipe(pipe), destroy_pipe(del_pipe), stack(stack), transferred_bytes(0), reconnection_callback(reconnection_callback),
	  nofreespace_callback(nofreespace_callback), reconnection_timeout(300000), identity(identity), received_data_bytes(0),
	  parent(prev), queue_only(false), queue_callback(NULL), remote_filesize(-1), ofb_pipe(NULL), hashfilesize(-1), did_queue_fc(false), queued_chunks(0),
	  last_transferred_bytes(0), last_progress_log(0), progress_log_callback(NULL), reconnected(false), needs_flush(false),
	  real_transferred_bytes(0), queue_next(false), sparse_bytes(0)
{
	has_error=false;
	if(parent==NULL)
	{
		mutex = Server->createMutex();
	}
	else
	{
		mutex=NULL;
	}
}

FileClientChunked::FileClientChunked(void)
	: pipe(NULL), stack(NULL), destroy_pipe(false), transferred_bytes(0), reconnection_callback(NULL), reconnection_timeout(300000), received_data_bytes(0),
	  parent(NULL), remote_filesize(-1), ofb_pipe(NULL), hashfilesize(-1), did_queue_fc(false), queued_chunks(0), last_transferred_bytes(0), last_progress_log(0),
	  progress_log_callback(NULL), reconnected(false), real_transferred_bytes(0), queue_next(false), sparse_bytes(0)
{
	has_error=true;
	mutex=NULL;
}

FileClientChunked::~FileClientChunked(void)
{
	if(pipe!=NULL && destroy_pipe)
	{
		Server->destroy(pipe);
		pipe=NULL;
	}
	Server->destroy(mutex);
	Server->destroy(ofb_pipe);
}

_u32 FileClientChunked::GetFilePatch(std::string remotefn, IFile *orig_file, IFile *patchfile, IFile *chunkhashes, IFsFile *hashoutput, _i64& predicted_filesize, int64 file_id, bool is_script, IFile** sparse_extents_f)
{
	m_file=NULL;
	patch_mode=true;

	m_chunkhashes=chunkhashes;
	m_hashoutput=hashoutput;
	m_patchfile=patchfile;
	m_file=orig_file;
	patchfile_pos=0;
	patch_buf_pos=0;
	remote_filesize = predicted_filesize;
	last_transferred_bytes=0;
	curr_output_fsize=0;
	curr_is_script = is_script;
	file_pos = 0;
	extent_iterator.reset();
	curr_sparse_extent.offset = -1;

	_u32 rc = GetFile(remotefn, predicted_filesize, file_id, sparse_extents_f);

	if (has_error)
		return ERR_ERROR;

	return rc;
}

_u32 FileClientChunked::GetFileChunked(std::string remotefn, IFile *file, IFile *chunkhashes, IFsFile *hashoutput, _i64& predicted_filesize, int64 file_id, bool is_script, IFile** sparse_extents_f)
{
	patch_mode=false;
	m_file=file;
	m_chunkhashes=chunkhashes;
	m_hashoutput=hashoutput;
	m_patchfile = NULL;
	remote_filesize = predicted_filesize;
	last_transferred_bytes=0;
	curr_output_fsize=0;
	curr_is_script = is_script;
	file_pos = 0;
	extent_iterator.reset();
	curr_sparse_extent.offset = -1;
	
	_u32 rc = GetFile(remotefn, predicted_filesize, file_id, sparse_extents_f);

	if (has_error)
		return ERR_ERROR;

	return rc;
}

_u32 FileClientChunked::GetFile(std::string remotefn, _i64& filesize_out, int64 file_id, IFile** sparse_extents_f)
{
	bool was_prepared = false;

	if(!queued_fcs.empty())
	{
		std::auto_ptr<FileClientChunked> next(queued_fcs.front());

		queued_fcs.pop_front();

		assert(next->remote_filename==remotefn);
		assert(next->curr_file_id == file_id);

		return next->GetFile(remotefn, filesize_out, file_id, sparse_extents_f);
	}
	else if(parent!=NULL && !queue_only)
	{
		was_prepared = true;
	}


	getfile_done=false;
	retval=ERR_SUCCESS;
	remote_filename=remotefn;
	curr_file_id = file_id;

	if(getPipe()==NULL)
		return ERR_ERROR;

	if (!queue_only)
	{
		setReconnectTries(50);
	}

	_i64 fileoffset=0;

	m_chunkhashes->Seek(0);
	hashfilesize=0;
	if(m_chunkhashes->Read((char*)&hashfilesize, sizeof(_i64))!=sizeof(_i64) )
	{
		Server->Log("Cannot read hashfilesize in FileClientChunked::GetFile", LL_ERROR);
		return ERR_INT_ERROR;
	}

	hashfilesize = little_endian(hashfilesize);

	if(hashfilesize<0)
	{
		Server->Log("Hashfile size wrong. Hashfile is damaged. Size is "+convert(hashfilesize), LL_ERROR);
		return ERR_INT_ERROR;
	}

	if(hashfilesize!=m_file->Size())
	{
		Server->Log("Hashfile size differs in FileClientChunked::GetFile "+convert(hashfilesize)+"!="+convert(m_file->Size()), LL_DEBUG);
		if(m_file->Size()<hashfilesize)
		{
			//partial file
			hashfilesize=m_file->Size();
		}
	}
	else
	{
		VLOG(Server->Log("Old filesize="+convert(hashfilesize), LL_DEBUG));
	}

	if(!was_prepared)
	{
		CWData data;
		data.addUChar( file_id!=0 ? ID_GET_FILE_BLOCKDIFF_WITH_METADATA : ID_GET_FILE_BLOCKDIFF );
		data.addString( remotefn );
		data.addString( identity );

		if(file_id!=0)
		{
			data.addChar(0);
			data.addVarInt(file_id);
			data.addChar(1);
		}

		data.addInt64( fileoffset );
		data.addInt64( hashfilesize );

		if(remote_filesize!=-1)
		{
			data.addInt64(remote_filesize);
		}

		needs_flush = true;
		next_chunk = 0;

		if (queue_only)
		{
			if (stack->Send(getPipe(), data.getDataPtr(), data.getDataSize(), c_default_timeout, false) != data.getDataSize())
			{
				Server->Log("Timeout during file queue request (3)", LL_ERROR);
				return ERR_TIMEOUT;
			}
		}
		else
		{
			int tries = 10;
			while (stack->Send(getPipe(), data.getDataPtr(), data.getDataSize(), c_default_timeout, false) != data.getDataSize())
			{
				Server->Log("Timeout during file request (3). Reconnecting...", LL_DEBUG);

				--tries;

				if (tries == 0
					|| !Reconnect(false))
				{
					Server->Log("Timeout during file request (3)", LL_ERROR);
					return ERR_TIMEOUT;
				}
			}
		}
	}
	
	num_chunks=hashfilesize/c_checkpoint_dist+((hashfilesize%c_checkpoint_dist!=0)?1:0);
	
	starttime=Server->getTimeMS();
	
	block_for_chunk_start=-1;
	bool initial_read = true;
	md5_hash.init();

	char stack_buf[BUFFERSIZE];
	state=CS_ID_FIRST;

	if(remote_filesize!=-1)
	{
		calcTotalChunks();
	}
	else
	{
		num_total_chunks=0;
	}

	size_t queued_chunks_low = c_queued_chunks_low;

	if(queue_only)
	{
		queued_chunks_low = c_max_queued_chunks;
	}

	do
	{
		if(queuedChunks()<queued_chunks_low && remote_filesize!=-1 && next_chunk<num_total_chunks)
		{		
			while(queuedChunks()<c_max_queued_chunks && next_chunk<num_total_chunks)
			{
				if(!getPipe()->isWritable())
				{
					break;
				}

				while (curr_sparse_extent.offset!=-1
					&& next_chunk*c_checkpoint_dist < curr_sparse_extent.offset)
				{
					curr_sparse_extent = extent_iterator->nextExtent();
				}

				if (curr_sparse_extent.offset != -1
					&& next_chunk*c_checkpoint_dist >= curr_sparse_extent.offset
					&& (next_chunk + 1)*c_checkpoint_dist <= curr_sparse_extent.offset + curr_sparse_extent.size)
				{
					_i64 num_chunks = ((curr_sparse_extent.offset + curr_sparse_extent.size) - next_chunk*c_checkpoint_dist)/c_checkpoint_dist;

					if (m_hashoutput != NULL)
					{
						m_hashoutput->Seek(chunkhash_file_off + next_chunk*chunkhash_single_size);
						for (_i64 i = 0; i < num_chunks; ++i)
						{
							std::string sparse_hashdata = get_sparse_extent_content();
							writeFileRepeat(m_hashoutput, sparse_hashdata.data(), sparse_hashdata.size());
						}
					}

					if (!patch_mode)
					{
						m_file->PunchHole(next_chunk*c_checkpoint_dist, num_chunks*c_checkpoint_dist);
					}

					curr_output_fsize = (std::max)(curr_output_fsize, curr_sparse_extent.offset + curr_sparse_extent.size);

					next_chunk+= num_chunks;

					addSparseBytes(num_chunks*c_checkpoint_dist);

					continue;
				}

				bool get_whole_block = false;
				char buf[chunkhash_single_size + 2 * sizeof(char) + sizeof(_i64)];
				size_t buf_size = sizeof(buf);

				if(next_chunk<num_chunks
					&& m_chunkhashes->Seek(chunkhash_file_off+next_chunk*chunkhash_single_size))
				{					
					buf[0]=ID_BLOCK_REQUEST;
					*((_i64*)(buf+1))=little_endian(next_chunk*c_checkpoint_dist);
					buf[1+sizeof(_i64)]=0;
					_u32 r=m_chunkhashes->Read(&buf[2*sizeof(char)+sizeof(_i64)], chunkhash_single_size);
					if(r==0)
					{
						get_whole_block=true;
					}
					else
					{
						if(r<chunkhash_single_size)
						{
							memset(&buf[2*sizeof(char)+sizeof(_i64)+r], 0, chunkhash_single_size-r);
						}
						char *sptr=&buf[2*sizeof(char)+sizeof(_i64)];
						SChunkHashes chhash;
						memcpy(chhash.big_hash, sptr, big_hash_size);
						memcpy(chhash.small_hash, sptr+big_hash_size, chunkhash_single_size-big_hash_size);					
						pending_chunks.insert(std::pair<_i64, SChunkHashes>(next_chunk*c_checkpoint_dist, chhash));
					}					
				}
				else
				{
					get_whole_block=true;
				}
				
				if(get_whole_block)
				{
					buf[0] = ID_BLOCK_REQUEST;
					*((_i64*)(buf + 1)) = little_endian(next_chunk*c_checkpoint_dist);
					buf[1 + sizeof(_i64)] = 1;
					buf_size = sizeof(char) * 2 + sizeof(_i64);

					pending_chunks.insert(std::pair<_i64, SChunkHashes>(next_chunk*c_checkpoint_dist, SChunkHashes() ));
				}

				if (stack->Send(getPipe(), buf, buf_size, c_default_timeout, false) != buf_size)
				{
					Server->Log("Timeout during chunk request of chunk "+convert(next_chunk*c_checkpoint_dist)+". Reconnecting...", LL_DEBUG);

					if (queue_only)
					{
						return ERR_TIMEOUT;
					}

					if (!Reconnect(true))
					{
						Server->Log("Timeout during chunk request of chunk "+convert(next_chunk*c_checkpoint_dist), LL_ERROR);
						adjustOutputFilesizeOnFailure(filesize_out);
						return ERR_TIMEOUT;
					}
					else
					{
						break;
					}
				}

				needs_flush = true;

				incrQueuedChunks();
				++next_chunk;
			}
		}
		else
		{
			if(queuedChunks()>0 || remote_filesize==-1)
			{
				if(!queue_only && (!was_prepared || !initial_read) )
				{
					getPipe()->isReadable(100);
				}
			}
		}

		if( ( ( parent==NULL && queued_fcs.empty() ) || !did_queue_fc )
			&& queuedChunks()<queued_chunks_low && next_chunk>=num_total_chunks
			&& remote_filesize!=-1)
		{
			if(queue_only)
			{
				queue_next=true;
			}
			else
			{
				int64 queue_start_time = Server->getTimeMS();
				FileClientChunked* prev=NULL;
				bool has_next=true;
				while(has_next && Server->getTimeMS()-queue_start_time<10000)
				{
					has_next=false;

					std::string remotefn;
					IFile* orig_file;
					IFile* patchfile;
					IFile* chunkhashes;
					IFsFile* hashoutput;
					_i64 predicted_filesize;
					int64 file_id;
					bool is_script;

					if(queue_callback && 
						getPipe()->isWritable() &&
						queue_callback->getQueuedFileChunked(remotefn, orig_file, patchfile, chunkhashes, hashoutput, predicted_filesize, file_id, is_script) )
					{
						if(prev==NULL)
						{
							did_queue_fc=true;
						}
						else
						{
							prev->did_queue_fc=true;
						}
						

						FileClientChunked* next = new FileClientChunked(NULL, false, stack, reconnection_callback,
							nofreespace_callback, identity, parent?parent:this);

						if(parent)
						{
							parent->queued_fcs.push_back(next);
						}
						else
						{
							queued_fcs.push_back(next);
						}

						next->setQueueCallback(queue_callback);
						next->setProgressLogCallback(progress_log_callback);

						next->setQueueOnly(true);

						_u32 rc;
						if (patch_mode)
						{
							rc = next->GetFilePatch(remotefn, orig_file, patchfile, chunkhashes, hashoutput, predicted_filesize, file_id, is_script, NULL);
						}
						else
						{
							rc = next->GetFileChunked(remotefn, orig_file, chunkhashes, hashoutput, predicted_filesize, file_id, is_script, NULL);
						}

						if(rc!=ERR_SUCCESS)
						{
							std::deque<FileClientChunked*>::iterator iter;
							if(parent)
							{
								iter = std::find(parent->queued_fcs.begin(), parent->queued_fcs.end(), next);
								if(iter!=parent->queued_fcs.end())
								{
									parent->queued_fcs.erase(iter);
								}
							}
							else
							{
								iter = std::find(queued_fcs.begin(), queued_fcs.end(), next);
								if(iter!=queued_fcs.end())
								{
									queued_fcs.erase(iter);
								}
							}
							delete next;
							if(prev==NULL)
							{
								did_queue_fc=false;
							}
							else
							{
								prev->did_queue_fc=false;
							}

							queue_callback->unqueueFileChunked(remotefn);

							Server->Log("Reconnecting after pipeline queuing failure", LL_DEBUG);

							if (!Reconnect(true))
							{
								Server->Log("Timeout after queueing next file", LL_ERROR);
								adjustOutputFilesizeOnFailure(filesize_out);
								return ERR_TIMEOUT;
							}
						}
						else
						{
							next->setQueueOnly(false);

							if(prev==NULL)
							{
								needs_flush=false;
							}
							else
							{
								prev->needs_flush=false;
							}
							

							if(next->queue_next)
							{
								has_next=true;
								prev=next;
							}
						}
					}
				}
			}
			
		}

		if(needs_flush)
		{
			Flush(getPipe());
			needs_flush=false;
		}

		if(queue_only)
		{
			return ERR_SUCCESS;
		}

		size_t rc;
		char* buf;
		if(initial_read && !initial_bytes.empty())
		{
			rc = initial_bytes.size();
			buf = &initial_bytes[0];
		}
		else
		{
			buf = stack_buf;
			rc = getPipe()->Read(buf, BUFFERSIZE, 0);
		}

		initial_read = false;

		
		if(rc==0)
		{
			if(getPipe()->hasError())
			{
				Server->Log("Pipe has error. Reconnecting...", LL_DEBUG);
				if(!Reconnect(true))
				{
					adjustOutputFilesizeOnFailure(filesize_out);
					
					return ERR_CONN_LOST;
				}
				else
				{
					starttime=Server->getTimeMS();
				}
			}
		}
		else
		{
			starttime=Server->getTimeMS();

			_u32 err = handle_data(buf, rc, false, true, sparse_extents_f);

			if(err!=ERR_CONTINUE)
			{
				if(err==ERR_SUCCESS)
				{
					if (curr_is_script)
					{
						err = freeFile();
					}
					
					if (err == ERR_SUCCESS)
					{
						Server->Log("Successful. Returning filesize " + convert(remote_filesize), LL_DEBUG);
						filesize_out = remote_filesize;
					}
				}
				else
				{
					adjustOutputFilesizeOnFailure(filesize_out);
				}
				return err;
			}
		}

		int64 ctime = Server->getTimeMS();
		if(ctime>starttime && ctime-starttime>=SERVER_TIMEOUT)
		{
			Server->Log("Connection timeout. Reconnecting...", LL_DEBUG);
			if(!Reconnect(true))
			{
				adjustOutputFilesizeOnFailure(filesize_out);

				break;
			}
			else
			{
				starttime=Server->getTimeMS();
			}
		}
		else if(ctime<starttime)
		{
			starttime=ctime;
		}

		logTransferProgress();
	}
	while(true);

	return ERR_TIMEOUT;
}


_u32 FileClientChunked::handle_data( char* buf, size_t bsize, bool ignore_filesize, bool allow_reconnect, IFile** sparse_extents_f)
{
	reconnected=false;
	bufptr=buf;
	remaining_bufptr_bytes=bsize;
	while(bufptr<buf+bsize)
	{
		bufptr_bytes_done=0;

		switch(state)
		{
		case CS_ID_FIRST:
			{
				State_First();						
			} //fallthrough
		case CS_ID_ACC:
			{
				State_Acc(ignore_filesize, allow_reconnect, sparse_extents_f);
			}break;
		case CS_BLOCK:
			{
				State_Block();
			}break;
		case CS_CHUNK:
			{
				State_Chunk();
			}break;
		case CS_SPARSE_EXTENTS:
			{
				State_SparseExtents(sparse_extents_f);
			} break;
		}

		bufptr+=bufptr_bytes_done;

		if (!ignore_filesize)
		{
			if ((remote_filesize != -1 &&
				remote_filesize > 0 &&
				next_chunk >= num_total_chunks
				&& pending_chunks.empty())
				|| getfile_done)
			{

				if (!getfile_done ||
					(retval == ERR_BASE_DIR_LOST
						|| retval == ERR_CANNOT_OPEN_FILE
						|| retval == ERR_SUCCESS))
				{
					FileClientChunked* next = getNextFileClient();
					if (next
						&& remaining_bufptr_bytes > 0)
					{
						next->setInitialBytes(bufptr, remaining_bufptr_bytes);
					}
				}


				if (!getfile_done)
				{
					return ERR_SUCCESS;
				}
			}
		}

		if(getfile_done)
		{
			return retval;				
		}
		
		if(reconnected)
		{
			break;
		}
	}

	return ERR_CONTINUE;
}


void FileClientChunked::State_First(void)
{
	curr_id=*bufptr;
	++bufptr;
	--remaining_bufptr_bytes;

	switch(curr_id)
	{
	case ID_FILESIZE: need_bytes=sizeof(_i64); break;
	case ID_FILESIZE_AND_EXTENTS: need_bytes=2*sizeof(_i64); break;
	case ID_BASE_DIR_LOST: need_bytes=0; break;
	case ID_READ_ERROR: need_bytes = 0; break;
	case ID_COULDNT_OPEN: need_bytes=0; break;
	case ID_WHOLE_BLOCK: need_bytes=sizeof(_i64)+sizeof(_u32); break;
	case ID_UPDATE_CHUNK: need_bytes=sizeof(_i64)+sizeof(_u32); break;
	case ID_NO_CHANGE: need_bytes=sizeof(_i64); break;
	case ID_BLOCK_HASH: need_bytes=sizeof(_i64)+big_hash_size; break;
	case ID_BLOCK_ERROR: need_bytes=sizeof(_u32)*2; break;
	default:
		Server->Log("Unknown Packet ID "+convert(static_cast<int>(curr_id))+" in State_First"
			" while loading file "+remote_filename+" with size "+convert(remote_filesize)
			+" at pos "+convert(next_chunk)+"/"+convert(num_total_chunks)+
			" remaining_bufptr_bytes="+convert(remaining_bufptr_bytes), LL_ERROR);
		need_bytes = 0;
		getfile_done = true;
		retval = ERR_ERROR;
		break;
	}
	packet_buf_off=0;
	total_need_bytes=need_bytes;
}

void FileClientChunked::State_Acc(bool ignore_filesize, bool allow_reconnect, IFile** sparse_extents_f)
{
	if(need_bytes<=remaining_bufptr_bytes)
	{
		CRData msg;
		if(state==CS_ID_FIRST)
		{
			msg.set(bufptr, need_bytes);
		}
		else
		{
			VLOG(Server->Log("Finalizing info packet... packet_buf_off="+convert(packet_buf_off)+" remaining_bufptr_bytes="+convert(remaining_bufptr_bytes)+" need_bytes="+convert(need_bytes), LL_DEBUG));
			assert(packet_buf_off + need_bytes <= sizeof(packet_buf));
			memcpy(&packet_buf[packet_buf_off], bufptr, need_bytes);
			msg.set(packet_buf, total_need_bytes);
		}

		bufptr_bytes_done+=need_bytes;
		remaining_bufptr_bytes-=need_bytes;

		switch(curr_id)
		{
		case ID_FILESIZE:
		case ID_FILESIZE_AND_EXTENTS:
			{
				_i64 new_remote_filesize;
				msg.getInt64(&new_remote_filesize);

				_i64 num_sparse_extents = 0;
				if (curr_id == ID_FILESIZE_AND_EXTENTS)
				{
					msg.getInt64(&num_sparse_extents);
				}

				if(!ignore_filesize)
				{					
					if(new_remote_filesize>=0)
					{
						if(remote_filesize!=-1 && new_remote_filesize!=remote_filesize)
						{
							Server->Log("Filesize change from expected filesize. Expected="+convert(remote_filesize)+" Got="+convert(new_remote_filesize), LL_WARNING);
						}

						VLOG(Server->Log("Receiving filesize... Filesize="+convert(new_remote_filesize)+" Predicted="+convert(remote_filesize), LL_DEBUG));

						if(remote_filesize!=-1 && new_remote_filesize>remote_filesize && getNextFileClient())
						{
							Server->Log("Filesize increase from predicted filesize and next file is queued. Reconnecting...", LL_WARNING);
							if(!allow_reconnect || !Reconnect(true))
							{
								getfile_done=true;
								retval=ERR_CONN_LOST;
							}
							return;
						}
						else
						{
							if(remote_filesize!=-1 && new_remote_filesize<remote_filesize)
							{
								remote_filesize = new_remote_filesize;

								_i64 old_num_total_chunks = num_total_chunks;

								calcTotalChunks();

								if(next_chunk>=num_total_chunks)
								{
									Server->Log("Filesize decrease from predicted filesize and problematic chunks are already queued (old_num_total_chunks="+convert(old_num_total_chunks)+
										" num_total_chunks="+convert(num_total_chunks)+" next_chunk="+convert(next_chunk)+"). Reconnecting...", LL_WARNING);

									if(!allow_reconnect || !Reconnect(true))
									{
										getfile_done=true;
										retval=ERR_CONN_LOST;
									}
									return;
								}
							}
							else
							{
								remote_filesize = new_remote_filesize;

								calcTotalChunks();

								if(m_hashoutput != NULL)
								{
									int64 max_hashoutput_size = get_hashdata_size(new_remote_filesize);

									if (max_hashoutput_size < m_hashoutput->Size())
									{
										m_hashoutput->Resize(max_hashoutput_size, false);
									}
								}
							}
						}
					}
					else
					{
						Server->Log("Script output download. Filesize unknown.", LL_INFO);
						remote_filesize = LLONG_MAX;

						calcTotalChunks();
					}
					

					state=CS_ID_FIRST;
					calcTotalChunks();
					if(patch_mode)
					{
						writePatchSize(remote_filesize);
					}
					if(remote_filesize==0 && num_sparse_extents==0)
					{
						getfile_done=true;
						retval=ERR_SUCCESS;
						return;
					}

					if(m_hashoutput!=NULL)
					{
						m_hashoutput->Seek(0);
						_i64 endian_remote_filesize = little_endian(remote_filesize);
						writeFileRepeat(m_hashoutput, (char*)&endian_remote_filesize, sizeof(_i64));
					}					
				}		

				if (num_sparse_extents > 0)
				{
					Server->Log("Downloading \"" + remote_filename + "\" with sparse extents (chunked)...", LL_DEBUG);

					state = CS_SPARSE_EXTENTS;
					whole_block_remaining = static_cast<_u32>(num_sparse_extents*sizeof(IFsFile::SSparseExtent) + big_hash_size);
					md5_hash.init();

					if (sparse_extents_f != NULL && *sparse_extents_f != NULL)
					{
						std::string tmp_fn = (*sparse_extents_f)->getFilename();
						Server->destroy(*sparse_extents_f);
						Server->deleteFile(tmp_fn);
						*sparse_extents_f = NULL;
						extent_iterator.reset();
						curr_sparse_extent.offset = -1;
					}

					if (sparse_extents_f != NULL && *sparse_extents_f == NULL)
					{
						*sparse_extents_f = FileClient::temporaryFileRetry();
					}

					if (sparse_extents_f != NULL && *sparse_extents_f == NULL)
					{
						Server->Log("Got sparse extents but have no file to write them to", LL_ERROR);
						retval = ERR_ERROR;
						getfile_done = true;
						return;
					}

					if (sparse_extents_f != NULL
						&& !FileClient::writeFileRetry(*sparse_extents_f, reinterpret_cast<char*>(&num_sparse_extents), sizeof(num_sparse_extents)))
					{
						Server->Log("Error writing number of sparse extentd (blockdiff)", LL_ERROR);
						retval = ERR_ERROR;
						getfile_done = true;
						return;
					}
				}

			}break;
		case ID_BASE_DIR_LOST:
			{
				getfile_done=true;
				retval=ERR_BASE_DIR_LOST;
				if(remote_filesize!=-1)
				{
					Server->Log("Did expect file to exist (1). Reconnecting...", LL_WARNING);
					if(!allow_reconnect || !Reconnect(false))
					{
						getfile_done=true;
						retval=ERR_CONN_LOST;
					}
				}
				return;
			}
		case ID_COULDNT_OPEN:
			{
				getfile_done=true;
				retval= ERR_CANNOT_OPEN_FILE;
				if(remote_filesize!=-1)
				{
					Server->Log("Did expect file to exist (2). Reconnecting...", LL_WARNING);
					if(!allow_reconnect || !Reconnect(false))
					{
						getfile_done=true;
						retval=ERR_CONN_LOST;
					}
				}
				return;
			}
		case ID_READ_ERROR:
			{
				getfile_done = true;
				retval = ERR_READ_ERROR;
				if (remote_filesize != -1)
				{
					Server->Log("Did expect file to exist (3). Reconnecting...", LL_WARNING);
					if (!allow_reconnect || !Reconnect(false))
					{
						getfile_done = true;
						retval = ERR_CONN_LOST;
					}
				}
				return;
			}
		case ID_WHOLE_BLOCK:
			{
				_i64 block_start;
				msg.getInt64(&block_start);
				chunk_start=block_start;

				VLOG(Server->Log("FileClientChunked: Whole block start="+convert(block_start), LL_DEBUG));

				if(pending_chunks.find(block_start)==pending_chunks.end())
				{
					Server->Log("Block not requested. ("+convert(block_start)+")", LL_ERROR);
					logPendingChunks();
					assert(false);
					retval=ERR_ERROR;
					getfile_done=true;
					return;
				}

				file_pos=block_start;
				if(!m_file->Seek(block_start))
				{
					Server->Log("Chunked Transfer: Seeking failed", LL_ERROR);
					assert(false);
				}

				block_for_chunk_start=block_start;

				msg.getUInt(&whole_block_remaining);
				state=CS_BLOCK;
				md5_hash.init();
				hash_for_whole_block=false;
				adler_hash=urb_adler32(0, NULL, 0);
				adler_remaining=c_chunk_size;
				block_pos=0;

				if(m_hashoutput!=NULL)
				{
					m_hashoutput->Seek(chunkhash_file_off+(block_start/c_checkpoint_dist)*chunkhash_single_size);
					char tmp[big_hash_size]={};
					writeFileRepeat(m_hashoutput, tmp, big_hash_size);
				}				
			}break;
		case ID_UPDATE_CHUNK:
			{
				_i64 new_chunk_start;
				msg.getInt64(&new_chunk_start);
				bool new_block;
				Hash_upto(new_chunk_start, new_block);
				msg.getUInt(&adler_remaining);

				VLOG(Server->Log("FileClientChunked: Chunk start="+convert(chunk_start)+" remaining="+convert(adler_remaining), LL_DEBUG));

				file_pos=chunk_start;
				_i64 block=chunk_start/c_checkpoint_dist;

				std::map<_i64, SChunkHashes>::iterator it=pending_chunks.find(block*c_checkpoint_dist);
				if(it==pending_chunks.end())
				{
					Server->Log("Chunk not requested. ("+convert(block*c_checkpoint_dist)+")", LL_ERROR);
					logPendingChunks();
					assert(false);
					retval=ERR_ERROR;
					getfile_done=true;
					return;
				}
				else if(new_block && m_hashoutput!=NULL)
				{
					m_hashoutput->Seek(chunkhash_file_off+(chunk_start/c_checkpoint_dist)*chunkhash_single_size);
					_i64 block_start = block*c_checkpoint_dist;
					if(block_start+c_checkpoint_dist>remote_filesize)
					{
						size_t missing_chunks = static_cast<size_t>((block_start + c_checkpoint_dist - remote_filesize)/c_chunk_size);
						writeFileRepeat(m_hashoutput, it->second.big_hash, chunkhash_single_size - missing_chunks*small_hash_size);
					}
					else
					{
						writeFileRepeat(m_hashoutput, it->second.big_hash, chunkhash_single_size);
					}
				}

				m_file->Seek(chunk_start);
				
				unsigned int chunknum=(chunk_start%c_checkpoint_dist)/c_chunk_size;
				if(m_hashoutput!=NULL)
				{
					m_hashoutput->Seek(chunkhash_file_off+block*chunkhash_single_size
						+big_hash_size+chunknum*small_hash_size);
				}				

				if (adler_remaining > 0)
				{
					state = CS_CHUNK;
				}
				else
				{
					state = CS_ID_FIRST;
				}
				adler_hash=urb_adler32(0, NULL, 0);

			}break;
		case ID_NO_CHANGE:
			{
				_i64 block_start;
				msg.getInt64(&block_start);
				Hash_nochange(block_start);
				state=CS_ID_FIRST;
			}break;
		case ID_BLOCK_HASH:
			{
				_i64 block_start;
				msg.getInt64(&block_start);
				const char *blockhash=msg.getCurrDataPtr();
				Hash_finalize(block_start, blockhash);
				state=CS_ID_FIRST;
			}break;
		case ID_BLOCK_ERROR:
			{
				_u32 ec1 = -1;
				_u32 ec2 = -1;
				msg.getUInt(&ec1);
				msg.getUInt(&ec2);

				Server->Log("Received error codes (ID_BLOCK_ERROR) ec1=" + convert(ec1) + " ec2=" + convert(ec2), LL_DEBUG);

				setErrorCodes(ec1, ec2);

				retval=ERR_ERRORCODES;
				getfile_done=true;

				if (remote_filesize != -1)
				{
					Server->Log("Did expect file to exist (4). Reconnecting...", LL_WARNING);
					if (!allow_reconnect || !Reconnect(false))
					{
						getfile_done = true;
						retval = ERR_CONN_LOST;
					}
				}

				return;
			} break;
		}
	}
	else
	{
		VLOG(Server->Log("Accumulating data for info packet... packet_buf_off="+convert(packet_buf_off)+" remaining_bufptr_bytes="+convert(remaining_bufptr_bytes), LL_DEBUG));
		assert(packet_buf_off + remaining_bufptr_bytes <= sizeof(packet_buf));
		if(remaining_bufptr_bytes>0)
		{
			memcpy(&packet_buf[packet_buf_off], bufptr, remaining_bufptr_bytes);
			packet_buf_off+=remaining_bufptr_bytes;
			need_bytes-=(unsigned int)remaining_bufptr_bytes;
		}
		state=CS_ID_ACC;

		bufptr_bytes_done+=remaining_bufptr_bytes;
		remaining_bufptr_bytes=0;
	}
}

void FileClientChunked::Hash_upto(_i64 new_chunk_start, bool &new_block)
{
	_i64 block_start=(new_chunk_start/c_checkpoint_dist)*c_checkpoint_dist;
	if(block_start!=block_for_chunk_start)
	{
		new_block=true;
		block_for_chunk_start=block_start;
		md5_hash.init();
		last_chunk_patches.clear();
		patch_buf_pos=0;
		hash_for_whole_block=false;
		chunk_start=block_start;
		VLOG(Server->Log("Chunk is in new block. block_start="+convert(block_start)+" block_for_chunk_start="+convert(block_for_chunk_start), LL_DEBUG));
	}
	else
	{
		new_block=false;
	}
	
	if(chunk_start!=new_chunk_start)
	{
		if(m_file->Seek(chunk_start))
		{
			char buf2[BUFFERSIZE];
			do
			{
				_u32 toread=(std::min)((_u32)BUFFERSIZE, (_u32)(new_chunk_start-chunk_start));
				size_t r=m_file->Read(buf2,  toread);
				VLOG(Server->Log("Read for hash at chunk_start="+convert(chunk_start)+" toread="+convert(toread)+" n="+convert(r), LL_DEBUG));
				if(r<toread)
				{
					Server->Log("Read error in hash calculation at position "+convert(chunk_start)+" toread="+convert(toread)+" read="+convert(r)+". This will cause the whole block to be loaded. "+os_last_error_str(), LL_WARNING);
					chunk_start=new_chunk_start;
					break;
				}
				chunk_start+=r;
				md5_hash.update((unsigned char*)buf2, (unsigned int)r);
			}while(chunk_start<new_chunk_start);
		}
		else
		{
			Server->Log("Error seeking in base file (to position "+convert(chunk_start)+"). Whole block will be loaded (1). "+os_last_error_str(), LL_WARNING);
			chunk_start=new_chunk_start;
		}
		
		file_pos=new_chunk_start;
	}
}

void FileClientChunked::Hash_finalize(_i64 curr_pos, const char *hash_from_client)
{
	bool load_whole_block = false;

	if(!hash_for_whole_block)
	{
		VLOG(Server->Log("Not a whole block. currpos="+convert(curr_pos)+" block_for_chunk_start="+convert(block_for_chunk_start), LL_DEBUG));
		if(curr_pos==block_for_chunk_start && block_for_chunk_start!=-1)
		{
			_i64 dest_pos=curr_pos+c_checkpoint_dist;

			if(dest_pos>remote_filesize)
				dest_pos=remote_filesize;

			VLOG(Server->Log("dest_pos="+convert(dest_pos)+" chunk_start="+convert(chunk_start), LL_DEBUG));
		
			char buf2[BUFFERSIZE];
			if(m_file->Seek(chunk_start))
			{
				while(chunk_start<dest_pos)
				{
					_u32 toread = (std::min)((_u32)BUFFERSIZE, (_u32)(dest_pos-chunk_start));
					size_t r=m_file->Read(buf2, toread);
					VLOG(Server->Log("Read for hash finalize at block_start="+convert(chunk_start)+" n="+convert(r), LL_DEBUG));
					if(r==0)
					{
						Server->Log("Read error in hash finalization at position "+convert(chunk_start)+" toread="+convert(toread)+" read="+convert(r)+". This will cause the whole block to be loaded. "+os_last_error_str(), LL_WARNING);
						file_pos+=dest_pos-chunk_start;
						chunk_start=dest_pos;
						load_whole_block = true;
						break;
					}
					file_pos+=r;
					chunk_start+=r;
					md5_hash.update((unsigned char*)buf2, (unsigned int)r);
				}
			}
			else
			{
				Server->Log("Error seeking in base file (to position "+convert(chunk_start)+"). Whole block will be loaded (2). "+os_last_error_str(), LL_WARNING);
				file_pos+=dest_pos-chunk_start;
				chunk_start=dest_pos;
				load_whole_block = true;
			}
		}

		block_for_chunk_start=-1;
		md5_hash.finalize();
	}
	else
	{
		VLOG(Server->Log("Whole block. currpos="+convert(curr_pos)+" block_for_chunk_start="+convert(block_for_chunk_start)+" chunk_start="+convert(chunk_start), LL_DEBUG));
	}

	if(load_whole_block 
		|| memcmp(hash_from_client, md5_hash.raw_digest_int(), big_hash_size)!=0)
	{
		if(!hash_for_whole_block)
		{
			Server->Log("Block hash wrong. Getting whole block. currpos="+convert(curr_pos), LL_WARNING);
			//system("pause");
			invalidateLastPatches();

			size_t backup_remaining_bufptr_bytes=remaining_bufptr_bytes;
			size_t backup_bufptr_bytes_done = bufptr_bytes_done;
			Server->Log("remaining_bufptr_bytes="+convert(remaining_bufptr_bytes), LL_DEBUG);
			remaining_bufptr_bytes = 0;
			char* backup_bufptr = bufptr;
			size_t backup_packet_buf_off = packet_buf_off;
			packet_buf_off = 0;
			char backup_packet_buf[24];
			memcpy(backup_packet_buf, packet_buf, sizeof(backup_packet_buf));
			state = CS_ID_FIRST;
			_u32 rc = loadChunkOutOfBand(curr_pos);
			remaining_bufptr_bytes = backup_remaining_bufptr_bytes;
			bufptr = backup_bufptr;
			packet_buf_off = backup_packet_buf_off;
			bufptr_bytes_done = backup_bufptr_bytes_done;
			memcpy(packet_buf, backup_packet_buf, sizeof(backup_packet_buf));

			if (rc != ERR_SUCCESS)
			{
				retval = rc;
				Server->Log("OFB-Block load failed with rc=" + convert(rc), LL_WARNING);
				getfile_done = true;
			}
		}
		else
		{
			retval=ERR_HASH;
			getfile_done=true;
		}
	}
	else
	{
		if(m_hashoutput!=NULL)
		{
			m_hashoutput->Seek(chunkhash_file_off+(curr_pos/c_checkpoint_dist)*chunkhash_single_size);
			writeFileRepeat(m_hashoutput, hash_from_client, big_hash_size);
		}

		int64 dest_pos = curr_pos + c_checkpoint_dist;
		if (dest_pos>remote_filesize)
			dest_pos = remote_filesize;

		curr_output_fsize = (std::max)(curr_output_fsize, dest_pos);

		std::map<_i64, SChunkHashes>::iterator it=pending_chunks.find(curr_pos);
		if(it!=pending_chunks.end())
		{
			addReceivedBlock(curr_pos);
			pending_chunks.erase(it);
			decrQueuedChunks();
		}
		else
		{
			Server->Log("Pending chunk not found -1", LL_ERROR);
			assert(false);
		}
	}

	last_chunk_patches.clear();
}

void FileClientChunked::Hash_nochange(_i64 curr_pos)
{
	std::map<_i64, SChunkHashes>::iterator it=pending_chunks.find(curr_pos);
	if(it!=pending_chunks.end())
	{
		Server->Log("Block without change. currpos="+convert(curr_pos), LL_DEBUG);
		addReceivedBlock(curr_pos);

		if(m_hashoutput!=NULL)
		{
			m_hashoutput->Seek(chunkhash_file_off+(curr_pos/c_checkpoint_dist)*chunkhash_single_size);
		}
		
		if(curr_pos+c_checkpoint_dist<=remote_filesize)
		{
			if(m_hashoutput!=NULL)
			{
				writeFileRepeat(m_hashoutput, it->second.big_hash, chunkhash_single_size);
			}
			curr_output_fsize = (std::max)(curr_output_fsize, curr_pos+c_checkpoint_dist);
		}
		else
		{
			size_t missing_chunks = static_cast<size_t>((curr_pos + c_checkpoint_dist - remote_filesize)/c_chunk_size);
			if(m_hashoutput!=NULL)
			{
				writeFileRepeat(m_hashoutput, it->second.big_hash, chunkhash_single_size-missing_chunks*small_hash_size);
			}
			curr_output_fsize = (std::max)(curr_output_fsize, remote_filesize);
		}
		pending_chunks.erase(it);
		decrQueuedChunks();
	}
	else
	{
		Server->Log("Pending chunk not found -1", LL_ERROR);
		retval=ERR_ERROR;
		getfile_done=true;
	}
}

void FileClientChunked::State_Block(void)
{
	size_t rbytes=(std::min)(remaining_bufptr_bytes, (size_t)whole_block_remaining);

	remaining_bufptr_bytes-=rbytes;
	bufptr_bytes_done+=rbytes;
	whole_block_remaining-=(unsigned int)rbytes;
	
	md5_hash.update((unsigned char*)bufptr, (unsigned int)rbytes);
	if(!patch_mode)
	{
		writeFileRepeat(m_file, bufptr, rbytes);
		file_pos+=rbytes;
		curr_output_fsize = (std::max)(curr_output_fsize, file_pos);
	}
	else
	{
		writePatch(file_pos, (unsigned int)rbytes, bufptr, whole_block_remaining==0);
		file_pos+=rbytes;
	}
	
	chunk_start+=(unsigned int)rbytes;

	char *alder_bufptr=bufptr;
	while(rbytes>0)
	{
		size_t adler_bytes=(std::min)((size_t)adler_remaining, rbytes);
		adler_hash=urb_adler32(adler_hash, alder_bufptr, (unsigned int)adler_bytes);
		alder_bufptr+=adler_bytes;
		rbytes-=adler_bytes;
		adler_remaining-=(unsigned int)adler_bytes;
		if(adler_remaining==0 || whole_block_remaining==0)
		{
			_u32 endian_adler_hash = little_endian(adler_hash);
			if(m_hashoutput!=NULL)
			{
				writeFileRepeat(m_hashoutput, (char*)&endian_adler_hash, small_hash_size);
			}
			adler_hash=urb_adler32(0, NULL, 0);
			adler_remaining=c_chunk_size;
		}

		block_pos+=(unsigned int)adler_bytes;
	}

	if(whole_block_remaining==0)
	{
		md5_hash.finalize();
		hash_for_whole_block=true;
		if(m_hashoutput!=NULL)
		{
			m_hashoutput->Seek(chunkhash_file_off+(block_for_chunk_start/c_checkpoint_dist)*chunkhash_single_size);
			writeFileRepeat(m_hashoutput, (char*)md5_hash.raw_digest_int(), big_hash_size);
		}

		state=CS_ID_FIRST;
	}
}

void FileClientChunked::writeFileRepeat(IFile *f, const char *buf, size_t bsize)
{
	_u32 written=0;
	_u32 rc;
	int tries=50;
	do
	{
		rc=f->Write(buf+written, (_u32)(bsize-written));
		written+=rc;
		if(rc==0)
		{
			if(nofreespace_callback!=NULL && !nofreespace_callback->handle_not_enough_space(f->getFilename()) )
			{
				break;
			}
			Server->Log("Failed to write to file... waiting... in Chunked File transfer. "+os_last_error_str(), LL_WARNING);
			Server->wait(10000);
			--tries;
		}
	}
	while(written<bsize && (rc>0 || tries>0) );

	if(rc==0)
	{
		Server->Log("Fatal error writing to file in writeFileRepeat. Write error in Chunked File transfer. "+os_last_error_str(), LL_ERROR);
		has_error = true;
		getPipe()->shutdown();
	}
}

void FileClientChunked::State_Chunk(void)
{
	size_t rbytes=(std::min)(remaining_bufptr_bytes, (size_t)adler_remaining);
	adler_remaining-=(unsigned int)rbytes;

	chunk_start+=rbytes;

	if(rbytes>0)
	{
		adler_hash=urb_adler32(adler_hash, bufptr, (unsigned int)rbytes);
		md5_hash.update((unsigned char*)bufptr, (unsigned int)rbytes);

		if(!patch_mode)
		{
			writeFileRepeat(m_file, bufptr, rbytes);
			file_pos+=rbytes;
			curr_output_fsize = (std::max)(curr_output_fsize, file_pos);
		}
		else
		{
			writePatch(file_pos, (unsigned int)rbytes, bufptr, adler_remaining==0);
			file_pos+=rbytes;
		}

		remaining_bufptr_bytes-=rbytes;
		bufptr_bytes_done+=rbytes;
	}

	if(adler_remaining==0)
	{
		_u32 endian_adler_hash = little_endian(adler_hash);
		if(m_hashoutput!=NULL)
		{
			writeFileRepeat(m_hashoutput, (char*)&endian_adler_hash, small_hash_size);
		}
		state=CS_ID_FIRST;
	}
}

void FileClientChunked::State_SparseExtents(IFile** sparse_extents_f)
{
	if (whole_block_remaining > big_hash_size)
	{
		size_t hash_rbytes = (std::min)(remaining_bufptr_bytes, (size_t)(whole_block_remaining - big_hash_size));
		md5_hash.update((unsigned char*)bufptr, (unsigned int)hash_rbytes);
	}
	
	_u32 rbytes = (std::min)(static_cast<_u32>(remaining_bufptr_bytes), whole_block_remaining);

	if (sparse_extents_f!=NULL
		&& !FileClient::writeFileRetry(*sparse_extents_f, bufptr, rbytes))
	{
		Server->Log("Error writing to sparse extents file. "+os_last_error_str(), LL_ERROR);
		retval = ERR_HASH;
		getfile_done = true;
	}

	remaining_bufptr_bytes -= rbytes;
	bufptr_bytes_done += rbytes;
	whole_block_remaining -= (unsigned int)rbytes;
	
	if (whole_block_remaining == 0)
	{
		md5_hash.finalize();

		if (sparse_extents_f != NULL)
		{
			(*sparse_extents_f)->Seek((*sparse_extents_f)->Size() - 16);
			std::string received_hash = (*sparse_extents_f)->Read(16);

			if (memcmp(md5_hash.raw_digest_int(), received_hash.data(), 16) != 0)
			{
				Server->Log("Sparse extent hash wrong", LL_ERROR);
				retval = ERR_HASH;
				getfile_done = true;
			}

			extent_iterator.reset(new ExtentIterator(*sparse_extents_f, false));
			curr_sparse_extent = extent_iterator->nextExtent();
		}		

		state = CS_ID_FIRST;
	}
}

void FileClientChunked::writePatch(_i64 pos, unsigned int length, char *buf, bool last)
{
	if(length<=c_chunk_size-patch_buf_pos && (patch_buf_pos==0 || pos==patch_buf_start+patch_buf_pos) )
	{
		if(buf!=NULL)
		{
			memcpy(&patch_buf[patch_buf_pos], buf, length);
		}
		if(patch_buf_pos==0)
		{
			patch_buf_start=pos;
		}
		patch_buf_pos+=length;

		if(last || patch_buf_pos==c_chunk_size || length==0)
		{
			writePatchInt(patch_buf_start, patch_buf_pos,  patch_buf);
			patch_buf_pos=0;
		}
	}
	else
	{
		if(patch_buf_pos>0)
		{
			writePatchInt(patch_buf_start, patch_buf_pos, patch_buf);
			patch_buf_pos=0;
		}

		if(buf!=NULL)
		{
			if(!last && length>0 && length<c_chunk_size)
			{
				memcpy(&patch_buf[patch_buf_pos], buf, length);
				patch_buf_start=pos;
				patch_buf_pos+=length;
			}
			else
			{
				writePatchInt(pos, length, buf);
			}
		}
	}
}

void FileClientChunked::writePatchInt(_i64 pos, unsigned int length, char *buf)
{
	const unsigned int plen=sizeof(_i64)+sizeof(unsigned int);
	char pd[plen];
	_i64 pos_tmp = little_endian(pos);
	memcpy(pd, &pos_tmp, sizeof(_i64));
	unsigned int length_tmp = little_endian(length);
	memcpy(pd+sizeof(_i64), &length_tmp, sizeof(unsigned int));
	writeFileRepeat(m_patchfile, pd, plen);
	writeFileRepeat(m_patchfile, buf, length);
	if (last_chunk_patches.empty())
	{
		last_patch_output_fsize = curr_output_fsize;
	}
	last_chunk_patches.push_back(patchfile_pos);
	patchfile_pos+=plen+length;
	curr_output_fsize = (std::max)(curr_output_fsize, pos + length);
}

void FileClientChunked::writePatchSize(_i64 remote_fs)
{
	m_patchfile->Seek(0);
	_i64 remote_fs_tmp=little_endian(remote_fs);
	writeFileRepeat(m_patchfile, (char*)&remote_fs_tmp, sizeof(_i64));
	if(patchfile_pos==0)
	{
		patchfile_pos=sizeof(_i64);
	}
	else
	{
		m_patchfile->Seek(patchfile_pos);
	}
}

bool FileClientChunked::hasError(void)
{
	return has_error;
}

void FileClientChunked::invalidateLastPatches(void)
{
	if(patch_mode)
	{
		if (!last_chunk_patches.empty())
		{
			curr_output_fsize = last_patch_output_fsize;
		}

		_i64 invalid_pos=little_endian(-1);
		for(size_t i=0;i<last_chunk_patches.size();++i)
		{
			m_patchfile->Seek(last_chunk_patches[i]);
			writeFileRepeat(m_patchfile, (char*)&invalid_pos, sizeof(_i64));
		}
		m_patchfile->Seek(patchfile_pos);
		patch_buf_pos=0;
	}
	last_chunk_patches.clear();	
}

void FileClientChunked::setDestroyPipe(bool b)
{
	destroy_pipe=b;
}

_i64 FileClientChunked::getTransferredBytes(void)
{
	IScopedLock lock(mutex);
	if(getPipe()!=NULL)
	{
		transferred_bytes+=getPipe()->getTransferedBytes();
		getPipe()->resetTransferedBytes();
	}
	if(ofbPipe()!=NULL)
	{
		transferred_bytes+=ofbPipe()->getTransferedBytes();
		ofbPipe()->resetTransferedBytes();
	}
	return transferred_bytes;
}

bool FileClientChunked::Reconnect(bool rerequest)
{
	if (has_error)
		return false;

	if(queue_callback!=NULL)
	{
		queue_callback->resetQueueChunked();

		clearFileClientQueue();
	}

	if(reconnection_callback==NULL)
		return false;

	if (decrReconnectTries() <= 0)
	{
		return false;
	}

	int64 reconnect_starttime=Server->getTimeMS();
	while(Server->getTimeMS()-reconnect_starttime<reconnection_timeout)
	{
		IPipe *nc=reconnection_callback->new_fileclient_connection();
		if(nc!=NULL)
		{
			if(getPipe()!=NULL &&
				( destroy_pipe || (parent && parent->destroy_pipe) ) )
			{
				IScopedLock lock(getMutex());
				transferred_bytes+=getPipe()->getTransferedBytes();
				real_transferred_bytes+=getPipe()->getRealTransferredBytes();
				Server->destroy(getPipe());
				setPipe(nc);
			}
			else
			{
				IScopedLock lock(getMutex());
				setPipe(nc);
			}
			for(size_t i=0;i<throttlers.size();++i)
			{
				getPipe()->addThrottler(throttlers[i]);
			}
			Server->Log("Reconnected successfully.", LL_DEBUG);
			remote_filesize=-1;
			num_total_chunks=0;
			starttime=Server->getTimeMS();
			resetQueuedChunks();
			block_for_chunk_start=-1;
			state=CS_ID_FIRST;
			patch_buf_pos=0;
			did_queue_fc=false;
			md5_hash.init();
			reconnected=true;
			initial_bytes.clear();

			_i64 fileoffset=0;

			_i64 hashfilesize=0;
			m_chunkhashes->Seek(0);
			if(m_chunkhashes->Read((char*)&hashfilesize, sizeof(_i64))!=sizeof(_i64) )
				return false;

			hashfilesize = little_endian(hashfilesize);

			if(m_file->Size()<hashfilesize)
			{
				hashfilesize=m_file->Size();
			}

			if(rerequest)
			{
				CWData data;
				data.addUChar( curr_file_id!=0 ? ID_GET_FILE_BLOCKDIFF_WITH_METADATA : ID_GET_FILE_BLOCKDIFF );
				data.addString( remote_filename );
				data.addString( identity );

				if(curr_file_id!=0)
				{
					data.addChar(0); //version
					data.addVarInt(curr_file_id);
					data.addChar(1); // with sparse
				}

				data.addInt64( fileoffset );
				data.addInt64( hashfilesize );
				if (file_pos > 0)
				{
					data.addUChar(1); //resume flag
				}

				size_t rc=stack->Send( getPipe(), data.getDataPtr(), data.getDataSize() );
				if(rc==0)
				{
					Server->Log("Failed anyways. has_error="+convert(getPipe()->hasError()), LL_DEBUG);
					Server->wait(2000);
					continue;
				}

				needs_flush=true;

				Server->Log("pending_chunks="+convert(pending_chunks.size())+" next_chunk="+convert(next_chunk), LL_DEBUG);
				for(std::map<_i64, SChunkHashes>::iterator it=pending_chunks.begin();it!=pending_chunks.end();++it)
				{
					if( it->first/c_checkpoint_dist<next_chunk)
					{
						next_chunk=it->first/c_checkpoint_dist;
					}
				}
				VLOG(Server->Log("next_chunk="+convert(next_chunk), LL_DEBUG));

				if(patch_mode)
				{
					Server->Log("Invalidating "+convert(last_chunk_patches.size())+" chunks in patch file", LL_DEBUG);
				}
				invalidateLastPatches();
			}
			
			pending_chunks.clear();

			return true;
		}
		else
		{
			Server->wait(2000);
		}
	}
	return false;
}

void FileClientChunked::addThrottler(IPipeThrottler *throttler)
{
	throttlers.push_back(throttler);
	if(getPipe()!=NULL)
	{
		getPipe()->addThrottler(throttler);
	}
}

IPipe *FileClientChunked::getPipe()
{
	if(parent)
	{
		return parent->getPipe();
	}
	else
	{
		return pipe;
	}
}

void FileClientChunked::setReconnectionTimeout(unsigned int t)
{
	reconnection_timeout=t;
}

_i64 FileClientChunked::getReceivedDataBytes(bool with_sparse)
{
	IScopedLock lock(mutex);
	if (with_sparse)
	{
		return received_data_bytes + sparse_bytes;
	}
	else
	{
		return received_data_bytes;
	}
}

void FileClientChunked::resetReceivedDataBytes(bool with_sparse)
{
	IScopedLock lock(mutex);
	received_data_bytes=0;

	if (with_sparse)
	{
		sparse_bytes = 0;
	}
}

void FileClientChunked::setQueueCallback( QueueCallback* cb )
{
	queue_callback = cb;
}

void FileClientChunked::setQueueOnly( bool b )
{
	queue_only = b;
}

void FileClientChunked::setInitialBytes( const char* buf, size_t bsize )
{
	initial_bytes.assign(buf, buf+bsize);
}

void FileClientChunked::calcTotalChunks()
{
	num_total_chunks=remote_filesize/c_checkpoint_dist+((remote_filesize%c_checkpoint_dist!=0)?1:0);
}

_u32 FileClientChunked::loadFileOutOfBand(IFile** sparse_extents_f)
{
	if(ofbPipe()==NULL)
	{
		if(!constructOutOfBandPipe())
		{
			return false;
		}
	}

	FileClientChunked tmp_fc(ofbPipe(), false, stack, reconnection_callback, nofreespace_callback, identity, NULL);

	if(patch_mode)
	{
		int64 filesize_out=-1;
		return tmp_fc.GetFilePatch(remote_filename, m_file, m_patchfile, m_chunkhashes, m_hashoutput, filesize_out, curr_file_id, curr_is_script, sparse_extents_f);
	}
	else
	{
		int64 filesize_out=-1;
		return tmp_fc.GetFileChunked(remote_filename, m_file, m_chunkhashes, m_hashoutput, filesize_out, curr_file_id, curr_is_script, sparse_extents_f);
	}
}

bool FileClientChunked::constructOutOfBandPipe()
{
	if(!reconnection_callback)
	{
		return false;
	}

	if(ofbPipe())
	{
		IScopedLock lock(getMutex());
		transferred_bytes+=ofbPipe()->getTransferedBytes();
		real_transferred_bytes+=ofbPipe()->getRealTransferredBytes();
		Server->destroy(ofbPipe());
		setOfbPipe(NULL);
	}

	int64 reconnect_starttime=Server->getTimeMS();
	while(Server->getTimeMS()-reconnect_starttime<reconnection_timeout)
	{
		{
			IPipe* np = reconnection_callback->new_fileclient_connection();
			IScopedLock lock(getMutex());
			setOfbPipe(np);
		}

		if(ofbPipe())
		{
			for(size_t i=0;i<throttlers.size();++i)
			{
				ofbPipe()->addThrottler(throttlers[i]);
			}

			return true;
		}
		else
		{
			Server->wait(2000);
		}
	}

	return false;
}

void FileClientChunked::requestOfbChunk(_i64 chunk_pos)
{
	{
		CWData data;
		data.addUChar( ID_GET_FILE_BLOCKDIFF );
		data.addString( remote_filename );
		data.addString( identity );
		data.addInt64( 0 );
		data.addInt64( hashfilesize );

		stack->Send( ofbPipe(), data.getDataPtr(), data.getDataSize() );
	}
	
	{
		CWData data;
		data.addUChar(ID_BLOCK_REQUEST);
		data.addInt64(chunk_pos);
		data.addChar(1);

		stack->Send( ofbPipe(), data.getDataPtr(), data.getDataSize());
	}

	state = CS_ID_FIRST;
	remaining_bufptr_bytes = 0;
	packet_buf_off = 0;
	invalidateLastPatches();
}

_u32 FileClientChunked::loadChunkOutOfBand(_i64 chunk_pos)
{
	if(ofbPipe()==NULL)
	{
		if(!constructOutOfBandPipe())
		{
			return ERR_CONN_LOST;
		}
	}

	requestOfbChunk(chunk_pos);

	Flush(ofbPipe());

	std::vector<char> ofb_buf;
	ofb_buf.resize(BUFFERSIZE);

	int64 total_starttime = Server->getTimeMS();

	while(pending_chunks.find(chunk_pos)!=pending_chunks.end()
		&& Server->getTimeMS()-total_starttime<20*60*1000)
	{
		size_t rc = ofbPipe()->Read(ofb_buf.data(), BUFFERSIZE, 100);

		if(rc==0)
		{
			if(ofbPipe()->hasError())
			{
				Server->Log("OFB-Pipe has error. Reconnecting...", LL_DEBUG);
				if(!constructOutOfBandPipe())
				{
					return ERR_CONN_LOST;
				}
				else
				{
					requestOfbChunk(chunk_pos);
					starttime=Server->getTimeMS();
				}
			}
		}
		else
		{
			starttime=Server->getTimeMS();

			_u32 err = handle_data(ofb_buf.data(), rc, true, false, NULL);

			if(err!=ERR_CONTINUE)
			{
				return err;
			}
		}

		int64 ctime = Server->getTimeMS();
		if(ctime>starttime && ctime-starttime>=SERVER_TIMEOUT)
		{
			Server->Log("OFB-Connection timeout. Reconnecting...", LL_DEBUG);
			if(!constructOutOfBandPipe())
			{
				return ERR_TIMEOUT;
			}
			else
			{
				requestOfbChunk(chunk_pos);
				starttime=Server->getTimeMS();
			}
		}
		else if(ctime<starttime)
		{
			starttime=ctime;
		}
	}

	if(pending_chunks.find(chunk_pos)==pending_chunks.end())
	{
		return ERR_SUCCESS;
	}
	else
	{
		Server->Log("OFB-Block download timed out", LL_WARNING);
		return ERR_TIMEOUT;
	}
}

FileClientChunked* FileClientChunked::getNextFileClient()
{
	if(parent)
	{
		return parent->getNextFileClient();
	}
	else
	{
		if(queued_fcs.empty())
		{
			return NULL;
		}
		else
		{
			return queued_fcs.front();
		}
	}
}

void FileClientChunked::clearFileClientQueue()
{
	if(parent)
	{
		parent->clearFileClientQueue();
	}
	else
	{
		while(!queued_fcs.empty())
		{
			delete queued_fcs.front();
			queued_fcs.pop_front();
		}
	}
}

unsigned int FileClientChunked::queuedChunks()
{
	if(parent)
	{
		return parent->queuedChunks();
	}
	else
	{
		return queued_chunks;
	}
}

void FileClientChunked::incrQueuedChunks()
{
	if(parent)
	{
		return parent->incrQueuedChunks();
	}
	else
	{
		++queued_chunks;
	}
}

void FileClientChunked::decrQueuedChunks()
{
	if(parent)
	{
		return parent->decrQueuedChunks();
	}
	else
	{
		--queued_chunks;
	}
}

void FileClientChunked::resetQueuedChunks()
{
	if(parent)
	{
		return parent->resetQueuedChunks();
	}
	else
	{
		queued_chunks = 0;
	}
}

IPipe* FileClientChunked::ofbPipe()
{
	if(parent)
	{
		return parent->ofbPipe();
	}
	else
	{
		return ofb_pipe;
	}
}

void FileClientChunked::setOfbPipe( IPipe* p )
{
	if(parent)
	{
		parent->setOfbPipe(p);
	}
	else
	{
		ofb_pipe = p;
	}
}

void FileClientChunked::addReceivedBytes( size_t bytes )
{
	if(parent)
	{
		parent->addReceivedBytes(bytes);
	}
	else
	{
		IScopedLock lock(mutex);
		received_data_bytes += bytes;
	}
}

void FileClientChunked::addSparseBytes(_i64 bytes)
{
	if (parent)
	{
		parent->addSparseBytes(bytes);
	}
	else
	{
		IScopedLock lock(mutex);
		sparse_bytes += bytes;
	}
}

void FileClientChunked::addReceivedBlock( _i64 block_start )
{
	if(remote_filesize-block_start<c_checkpoint_dist)
	{
		addReceivedBytes(static_cast<size_t>(remote_filesize-block_start));
	}
	else
	{
		addReceivedBytes(c_checkpoint_dist);
	}
}

void FileClientChunked::logPendingChunks()
{
	for(std::map<_i64, SChunkHashes>::iterator iter=pending_chunks.begin();
		iter!=pending_chunks.end();++iter)
	{
		Server->Log("Pending chunk: "+convert(iter->first), LL_ERROR);
	}
}

void FileClientChunked::logTransferProgress()
{
	int64 ct = Server->getTimeMS();
	if(remote_filesize>0 && (last_progress_log==0 ||
		ct-last_progress_log>60000) )
	{
		int64 newTransferred=getTransferredBytes();

		if( last_transferred_bytes!=0 &&
			last_progress_log!=0 )
		{			
			int64 tranferred = newTransferred - last_transferred_bytes;
			int64 speed_bps = tranferred*1000 / (ct-last_progress_log);

			if(tranferred>0 && progress_log_callback)
			{
				progress_log_callback->log_progress(remote_filename,
					remote_filesize, file_pos, speed_bps);
			}
		}

		last_transferred_bytes = newTransferred;
		last_progress_log = ct;
	}	
}

void FileClientChunked::setProgressLogCallback( FileClient::ProgressLogCallback* cb )
{
	progress_log_callback=cb;
}

void FileClientChunked::setPipe(IPipe* p)
{
	if(parent)
	{
		parent->setPipe(p);
	}
	else
	{
		pipe = p;
	}
}

_u32 FileClientChunked::getErrorcode1()
{
	return errorcode1;
}

_u32 FileClientChunked::getErrorcode2()
{
	return errorcode2;
}

std::string FileClientChunked::getErrorcodeString()
{
	std::string err;
	if(errorcode1<sizeof(fc_err_strs)/sizeof(fc_err_strs[0]))
	{
		err=fc_err_strs[errorcode1];
	}
	else
	{
		err="Errorcode: "+ convert(errorcode1);
	}

	err+=". System error code: "+convert(errorcode2);

	return err;
}

void FileClientChunked::adjustOutputFilesizeOnFailure( _i64& filesize_out )
{
	filesize_out = curr_output_fsize;

	if(hashfilesize>filesize_out)
	{
		Server->Log("Hashfilesize greater than currently downloaded filesize. Using old filesize (of base file) and copying hash data.", LL_DEBUG);
		filesize_out = hashfilesize;

		if(m_hashoutput!=NULL)
		{
			if(m_hashoutput->Seek(m_hashoutput->Size()) &&
				m_chunkhashes->Seek(m_hashoutput->Size()))
			{
				std::vector<char> buffer;
				buffer.resize(4096);

				_u32 read;
				do 
				{
					bool has_error;
					read = m_chunkhashes->Read(&buffer[0], 4096, &has_error);

					if(has_error)
					{
						Server->Log("Error reading from chunkhashes file. Copying hashdata failed. "+os_last_error_str(), LL_ERROR);
					}

					writeFileRepeat(m_hashoutput, buffer.data(), read);	

				} while (read==4096);

				assert(m_hashoutput->Size() == m_chunkhashes->Size());
			}
			else
			{
				Server->Log("Error seeking to hashoutput end. Copying hashdata failed. "+os_last_error_str(), LL_ERROR);
			}
		}		
	}

	if(patch_mode)
	{
		writePatchSize(filesize_out);
	}

	if(m_hashoutput!=NULL)
	{
		m_hashoutput->Seek(0);
		_i64 endian_filesize_out = little_endian(filesize_out);
		writeFileRepeat(m_hashoutput, (char*)&endian_filesize_out, sizeof(_i64));
	}
	

	Server->Log("Not successful. Returning filesize "+convert(filesize_out), LL_DEBUG);
}

_u32 FileClientChunked::Flush(IPipe* fpipe)
{
	CWData data;
	data.addUChar( ID_FLUSH_SOCKET );

	if(stack->Send( fpipe, data.getDataPtr(), data.getDataSize() )!=data.getDataSize())
	{
		Server->Log("Error sending flush request", LL_ERROR);
		return ERR_TIMEOUT;
	}

	return ERR_SUCCESS;
}

int FileClientChunked::getReconnectTries()
{
	if (parent)
	{
		return parent->getReconnectTries();
	}
	else
	{
		return reconnect_tries;
	}
}

int FileClientChunked::decrReconnectTries()
{
	if (parent)
	{
		return parent->decrReconnectTries();
	}
	else
	{
		return reconnect_tries--;
	}
}

void FileClientChunked::setReconnectTries(int tries)
{
	if (parent)
	{
		parent->setReconnectTries(tries);
	}
	else
	{
		reconnect_tries=tries;
	}
}

void FileClientChunked::setErrorCodes(_u32 ec1, _u32 ec2)
{
	if (parent)
	{
		parent->setErrorCodes(ec1, ec2);
	}
	else
	{
		errorcode1 = ec1;
		errorcode2 = ec2;
	}
}

IMutex * FileClientChunked::getMutex()
{
	if (parent != NULL)
	{
		return parent->getMutex();
	}
	else
	{
		return mutex;
	}
}

_u32 FileClientChunked::freeFile()
{
	if ( (parent!=NULL && !parent->queued_fcs.empty())
		|| (parent==NULL && !queued_fcs.empty()) )
	{
		return ERR_SUCCESS;
	}

	CWData data;
	data.addUChar(ID_FREE_SERVER_FILE);

	if (stack->Send(getPipe(), data.getDataPtr(), data.getDataSize()) != data.getDataSize())
	{
		Server->Log("Timout during free file request (4)", LL_ERROR);
		return ERR_TIMEOUT;
	}

	return ERR_SUCCESS;
}

_i64 FileClientChunked::getRealTransferredBytes()
{
	IScopedLock lock(mutex);
	_i64 tbytes=real_transferred_bytes;
	if(getPipe()!=NULL)
	{
		tbytes+=getPipe()->getRealTransferredBytes();
	}
	if(ofbPipe()!=NULL)
	{
		tbytes+=ofbPipe()->getRealTransferredBytes();
	}
	return tbytes;
}
