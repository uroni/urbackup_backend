#include "FileClientChunked.h"
#include "../../common/data.h"
#include "../../Interface/Server.h"
#include "../../Interface/File.h"
#include "../../stringtools.h"

#include <memory.h>
#include <assert.h>
#include <queue>
#include <memory>

#define VLOG(x) x

const unsigned int chunkhash_file_off=sizeof(_i64);
const unsigned int chunkhash_single_size=big_hash_size+small_hash_size*(c_checkpoint_dist/c_small_hash_dist);
const unsigned int c_reconnection_tries=30;

unsigned int adler32(unsigned int adler, const char *buf, unsigned int len);

FileClientChunked::FileClientChunked(IPipe *pipe, bool del_pipe, CTCPStack *stack,
	FileClientChunked::ReconnectionCallback *reconnection_callback, FileClientChunked::NoFreeSpaceCallback *nofreespace_callback
	, std::string identity, FileClientChunked* prev)
	: pipe(pipe), destroy_pipe(del_pipe), stack(stack), transferred_bytes(0), reconnection_callback(reconnection_callback),
	  nofreespace_callback(nofreespace_callback), reconnection_timeout(300000), identity(identity), received_data_bytes(0),
	  parent(prev), queue_only(false), queue_callback(NULL), remote_filesize(-1), ofb_pipe(NULL), hashfilesize(-1), did_queue_fc(false), queued_chunks(0)
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
	  parent(NULL), remote_filesize(-1), ofb_pipe(NULL), hashfilesize(-1), did_queue_fc(false), queued_chunks(0)
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

_u32 FileClientChunked::GetFilePatch(std::string remotefn, IFile *orig_file, IFile *patchfile, IFile *chunkhashes, IFile *hashoutput, _i64 predicted_filesize)
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

	return GetFile(remotefn);
}

_u32 FileClientChunked::GetFileChunked(std::string remotefn, IFile *file, IFile *chunkhashes, IFile *hashoutput, _i64 predicted_filesize)
{
	patch_mode=false;
	m_file=file;
	m_chunkhashes=chunkhashes;
	m_hashoutput=hashoutput;
	remote_filesize = predicted_filesize;
	
	return GetFile(remotefn);
}

_u32 FileClientChunked::GetFile(std::string remotefn)
{
	bool was_prepared = false;

	if(!queued_fcs.empty())
	{
		std::auto_ptr<FileClientChunked> next(queued_fcs.front());

		queued_fcs.pop();

		assert(next->remote_filename==remotefn);

		return next->GetFile(remotefn);
	}
	else if(parent!=NULL && !queue_only)
	{
		was_prepared = true;
	}


	getfile_done=false;
	retval=ERR_SUCCESS;
	remote_filename=remotefn;

	if(pipe==NULL)
		return ERR_ERROR;

	_i64 fileoffset=0;

	m_chunkhashes->Seek(0);
	hashfilesize=0;
	if(m_chunkhashes->Read((char*)&hashfilesize, sizeof(_i64))!=sizeof(_i64) )
	{
		Server->Log("Cannot read hashfilesize in FileClientChunked::GetFile", LL_ERROR);
		return ERR_INT_ERROR;
	}

	if(patch_mode)
	{
		if(hashfilesize!=m_file->Size())
		{
			Server->Log("Hashfile size wrong in FileClientChunked::GetFile "+nconvert(hashfilesize)+"!="+nconvert(m_file->Size()), LL_WARNING);
		}
		else
		{
			VLOG(Server->Log("Old filesize="+nconvert(hashfilesize), LL_DEBUG));
		}
	}

	if(!was_prepared)
	{
		CWData data;
		data.addUChar( ID_GET_FILE_BLOCKDIFF );
		data.addString( remotefn );
		data.addString( identity );
		data.addInt64( fileoffset );
		data.addInt64( hashfilesize );

		if(remote_filesize!=-1)
		{
			data.addInt64(remote_filesize);
		}

		stack->Send( pipe, data.getDataPtr(), data.getDataSize() );

		next_chunk=0;
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

	do
	{
		if(queuedChunks()<c_max_queued_chunks && remote_filesize!=-1 && next_chunk<num_total_chunks)
		{			
			while(queuedChunks()<c_max_queued_chunks && next_chunk<num_total_chunks)
			{
				if(next_chunk<num_chunks)
				{
					m_chunkhashes->Seek(chunkhash_file_off+next_chunk*chunkhash_single_size);

					char buf[chunkhash_single_size+2*sizeof(char)+sizeof(_i64)];
					buf[0]=ID_BLOCK_REQUEST;
					*((_i64*)(buf+1))=little_endian(next_chunk*c_checkpoint_dist);
					buf[1+sizeof(_i64)]=0;
					_u32 r=m_chunkhashes->Read(&buf[2*sizeof(char)+sizeof(_i64)], chunkhash_single_size);
					if(r<chunkhash_single_size)
					{
						memset(&buf[2*sizeof(char)+sizeof(_i64)+r], 0, chunkhash_single_size-r);
					}
					stack->Send( pipe, buf, chunkhash_single_size+2*sizeof(char)+sizeof(_i64));

					char *sptr=&buf[2*sizeof(char)+sizeof(_i64)];
					SChunkHashes chhash;
					memcpy(chhash.big_hash, sptr, big_hash_size);
					memcpy(chhash.small_hash, sptr+big_hash_size, chunkhash_single_size-big_hash_size);
					pending_chunks.insert(std::pair<_i64, SChunkHashes>(next_chunk*c_checkpoint_dist, chhash));
				}
				else
				{
					CWData data;
					data.addUChar(ID_BLOCK_REQUEST);
					data.addInt64(next_chunk*c_checkpoint_dist);
					data.addChar(1);
					stack->Send( pipe, data.getDataPtr(), data.getDataSize());

					pending_chunks.insert(std::pair<_i64, SChunkHashes>(next_chunk*c_checkpoint_dist, SChunkHashes() ));
				}
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
					pipe->isReadable(100);
				}
			}
		}

		if( ( ( parent==NULL && queued_fcs.empty() ) || !did_queue_fc )
			&& queuedChunks()<c_max_queued_chunks && next_chunk==num_total_chunks)
		{
			std::string remotefn;
			IFile* orig_file;
			IFile* patchfile;
			IFile* chunkhashes;
			IFile* hashoutput;
			_i64 predicted_filesize;

			if(queue_callback && queue_callback->getQueuedFileChunked(remotefn, orig_file, patchfile, chunkhashes, hashoutput, predicted_filesize))
			{
				did_queue_fc=true;

				FileClientChunked* next = new FileClientChunked(pipe, false, stack, reconnection_callback,
					nofreespace_callback, identity, parent?parent:this);

				if(parent)
				{
					parent->queued_fcs.push(next);
				}
				else
				{
					queued_fcs.push(next);
				}

				next->setQueueCallback(queue_callback);

				next->setQueueOnly(true);
				next->GetFilePatch(remotefn, orig_file, patchfile, chunkhashes, hashoutput, predicted_filesize);
				next->setQueueOnly(false);				
			}
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
			rc = pipe->Read(buf, BUFFERSIZE, 0);
		}

		initial_read = false;

		
		if(rc==0)
		{
			if(pipe->hasError())
			{
				Server->Log("Pipe has error. Reconnecting...", LL_DEBUG);
				if(!Reconnect())
				{
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

			_u32 err = handle_data(buf, rc, false);

			if(err!=ERR_CONTINUE)
			{
				return err;
			}
		}

		unsigned int ctime = Server->getTimeMS();
		if(ctime>starttime && ctime-starttime>=SERVER_TIMEOUT)
		{
			Server->Log("Connection timeout. Reconnecting...", LL_DEBUG);
			if(!Reconnect())
			{
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
	}
	while(true);

	return ERR_TIMEOUT;
}


_u32 FileClientChunked::handle_data( char* buf, size_t bsize, bool ignore_filesize )
{
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
				State_Acc(ignore_filesize);
			}break;
		case CS_BLOCK:
			{
				State_Block();
			}break;
		case CS_CHUNK:
			{
				State_Chunk();
			}break;
		}

		bufptr+=bufptr_bytes_done;

		if( (remote_filesize!=-1 && 
			next_chunk==num_total_chunks
			&& pending_chunks.empty() )
			|| getfile_done  )
		{

			if(!getfile_done ||
				(retval==ERR_BASE_DIR_LOST
				 || retval==ERR_FILE_DOESNT_EXIST
				 || retval==ERR_SUCCESS) )
			{
				FileClientChunked* next = getNextFileClient();
				if( next				
					&& remaining_bufptr_bytes>0)
				{
					next->setInitialBytes(bufptr, remaining_bufptr_bytes);
				}
			}
			

			if(!getfile_done)
			{
				return ERR_SUCCESS;
			}
		}

		if(getfile_done)
		{
			return retval;				
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
	case ID_BASE_DIR_LOST: need_bytes=0; break;
	case ID_COULDNT_OPEN: need_bytes=0; break;
	case ID_WHOLE_BLOCK: need_bytes=sizeof(_i64)+sizeof(_u32); break;
	case ID_UPDATE_CHUNK: need_bytes=sizeof(_i64)+sizeof(_u32); break;
	case ID_NO_CHANGE: need_bytes=sizeof(_i64); break;
	case ID_BLOCK_HASH: need_bytes=sizeof(_i64)+big_hash_size; break;
	default:
		Server->Log("Unknown Packet ID in State_First", LL_ERROR);
		need_bytes = 0;
		getfile_done = true;
		retval = ERR_ERROR;
		break;
	}
	packet_buf_off=0;
	total_need_bytes=need_bytes;
}

void FileClientChunked::State_Acc(bool ignore_filesize)
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
			VLOG(Server->Log("Finalizing info packet... packet_buf_off="+nconvert(packet_buf_off)+" remaining_bufptr_bytes="+nconvert(remaining_bufptr_bytes)+" need_bytes="+nconvert(need_bytes), LL_DEBUG));
			memcpy(&packet_buf[packet_buf_off], bufptr, need_bytes);
			msg.set(packet_buf, total_need_bytes);
		}

		bufptr_bytes_done+=need_bytes;
		remaining_bufptr_bytes-=need_bytes;

		switch(curr_id)
		{
		case ID_FILESIZE:
			{
				if(!pending_chunks.empty())
				{
					int a4=4;
				}
				if(!ignore_filesize)
				{
					VLOG(Server->Log("Receiving filesize...", LL_DEBUG));
					_i64 new_remote_filesize;
					msg.getInt64(&new_remote_filesize);

					if(new_remote_filesize!=remote_filesize)
					{
						int a4=4;
					}

					if(remote_filesize!=-1 && new_remote_filesize>remote_filesize)
					{
						Server->Log("Filesize increase from predicted filesize. Loading file out of band...", LL_WARNING);
						assert(false);
						if(!Reconnect())
						{
							getfile_done=true;
							retval=ERR_CONN_LOST;
						}
						return;
					}

					state=CS_ID_FIRST;
					calcTotalChunks();
					if(patch_mode)
					{
						writePatchSize(remote_filesize);
					}
					if(remote_filesize==0)
					{
						getfile_done=true;
						retval=ERR_SUCCESS;
						return;
					}

					m_hashoutput->Seek(0);
					writeFileRepeat(m_hashoutput, (char*)&remote_filesize, sizeof(_i64));
				}				
			}break;
		case ID_BASE_DIR_LOST:
			{
				getfile_done=true;
				retval=ERR_BASE_DIR_LOST;
				return;
			}
		case ID_COULDNT_OPEN:
			{
				getfile_done=true;
				retval=ERR_FILE_DOESNT_EXIST;
				return;
			}
		case ID_WHOLE_BLOCK:
			{
				_i64 block_start;
				msg.getInt64(&block_start);
				chunk_start=block_start;

				VLOG(Server->Log("FileClientChunked: Whole block start="+nconvert(block_start), LL_DEBUG));

				if(pending_chunks.find(block_start)==pending_chunks.end())
				{
					Server->Log("Block not requested.", LL_ERROR);
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
				adler_hash=adler32(0, NULL, 0);
				adler_remaining=c_chunk_size;
				block_pos=0;

				m_hashoutput->Seek(chunkhash_file_off+(block_start/c_checkpoint_dist)*chunkhash_single_size);
				char tmp[big_hash_size]={};
				writeFileRepeat(m_hashoutput, tmp, big_hash_size);
			}break;
		case ID_UPDATE_CHUNK:
			{
				_i64 new_chunk_start;
				msg.getInt64(&new_chunk_start);
				bool new_block;
				Hash_upto(new_chunk_start, new_block);
				msg.getUInt(&adler_remaining);

				VLOG(Server->Log("FileClientChunked: Chunk start="+nconvert(chunk_start)+" remaining="+nconvert(adler_remaining), LL_DEBUG));

				file_pos=chunk_start;
				_i64 block=chunk_start/c_checkpoint_dist;

				std::map<_i64, SChunkHashes>::iterator it=pending_chunks.find(block*c_checkpoint_dist);
				if(it==pending_chunks.end())
				{
					Server->Log("Chunk not requested.", LL_ERROR);
					assert(false);
					retval=ERR_ERROR;
					getfile_done=true;
					return;
				}
				else if(new_block)
				{
					m_hashoutput->Seek(chunkhash_file_off+(chunk_start/c_checkpoint_dist)*chunkhash_single_size);
					writeFileRepeat(m_hashoutput, it->second.big_hash, chunkhash_single_size);
				}

				m_file->Seek(chunk_start);
				
				unsigned int chunknum=(chunk_start%c_checkpoint_dist)/c_chunk_size;
				m_hashoutput->Seek(chunkhash_file_off+block*chunkhash_single_size
					+big_hash_size+chunknum*small_hash_size);

				state=CS_CHUNK;
				adler_hash=adler32(0, NULL, 0);

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
		}
	}
	else
	{
		VLOG(Server->Log("Accumulating data for info packet... packet_buf_off="+nconvert(packet_buf_off)+" remaining_bufptr_bytes="+nconvert(remaining_bufptr_bytes), LL_DEBUG));
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
		VLOG(Server->Log("Chunk is in new block", LL_DEBUG));
	}
	else
	{
		new_block=false;
	}
	
	if(chunk_start!=new_chunk_start)
	{
		m_file->Seek(chunk_start);
		char buf2[BUFFERSIZE];
		do
		{
			_u32 toread=(std::min)((_u32)BUFFERSIZE, (_u32)(new_chunk_start-chunk_start));
			size_t r=m_file->Read(buf2,  toread);
			VLOG(Server->Log("Read for hash at chunk_start="+nconvert(chunk_start)+" toread="+nconvert(toread)+" n="+nconvert(r), LL_DEBUG));
			if(r<toread)
			{
				retval=ERR_INT_ERROR;
				getfile_done=true;
				Server->Log("Read error in File chunked - 1", LL_ERROR);
				break;
			}
			chunk_start+=r;
			md5_hash.update((unsigned char*)buf2, (unsigned int)r);
		}while(chunk_start<new_chunk_start);
		file_pos=new_chunk_start;
	}
}

void FileClientChunked::Hash_finalize(_i64 curr_pos, const char *hash_from_client)
{
	if(!hash_for_whole_block)
	{
		VLOG(Server->Log("Not a whole block. currpos="+nconvert(curr_pos)+" block_for_chunk_start="+nconvert(block_for_chunk_start), LL_DEBUG));
		if(curr_pos==block_for_chunk_start && block_for_chunk_start!=-1)
		{
			_i64 dest_pos=curr_pos+c_checkpoint_dist;

			if(dest_pos>remote_filesize)
				dest_pos=remote_filesize;

			VLOG(Server->Log("dest_pos="+nconvert(dest_pos), LL_DEBUG));
		
			char buf2[BUFFERSIZE];
			m_file->Seek(chunk_start);
			while(chunk_start<dest_pos)
			{
				size_t r=m_file->Read(buf2, (std::min)((_u32)BUFFERSIZE, (_u32)(dest_pos-chunk_start)) );
				VLOG(Server->Log("Read for hash finalize at block_start="+nconvert(chunk_start)+" n="+nconvert(r), LL_DEBUG));
				if(r==0)
				{
					Server->Log("Read err in Hash_finalize", LL_WARNING);
					retval=ERR_INT_ERROR;
					getfile_done=true;
					break;
				}
				file_pos+=r;
				chunk_start+=r;
				md5_hash.update((unsigned char*)buf2, (unsigned int)r);
			}
		}

		block_for_chunk_start=-1;
		md5_hash.finalize();
	}

	if(memcmp(hash_from_client, md5_hash.raw_digest_int(), big_hash_size)!=0)
	{
		if(!hash_for_whole_block)
		{
			Server->Log("Block hash wrong. Getting whole block. currpos="+nconvert(curr_pos), LL_DEBUG);
			//system("pause");
			invalidateLastPatches();
			if(getNextFileClient())
			{
				size_t backup_remaining_bufptr_bytes=remaining_bufptr_bytes;
				char* backup_bufptr = bufptr;
				loadChunkOutOfBand(curr_pos);
				remaining_bufptr_bytes = backup_remaining_bufptr_bytes;
				bufptr = backup_bufptr;
			}
			else
			{
				CWData data;
				data.addUChar(ID_BLOCK_REQUEST);
				data.addInt64(curr_pos);
				data.addChar(1);
				stack->Send( pipe, data.getDataPtr(), data.getDataSize());
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
		m_hashoutput->Seek(chunkhash_file_off+(curr_pos/c_checkpoint_dist)*chunkhash_single_size);
		writeFileRepeat(m_hashoutput, hash_from_client, big_hash_size);

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
		addReceivedBlock(curr_pos);
		m_hashoutput->Seek(chunkhash_file_off+(curr_pos/c_checkpoint_dist)*chunkhash_single_size);
		writeFileRepeat(m_hashoutput, it->second.big_hash, chunkhash_single_size);
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
		adler_hash=adler32(adler_hash, alder_bufptr, (unsigned int)adler_bytes);
		alder_bufptr+=adler_bytes;
		rbytes-=adler_bytes;
		adler_remaining-=(unsigned int)adler_bytes;
		if(adler_remaining==0 || whole_block_remaining==0)
		{
			writeFileRepeat(m_hashoutput, (char*)&adler_hash, small_hash_size);
			adler_hash=adler32(0, NULL, 0);
			adler_remaining=c_chunk_size;
		}

		block_pos+=(unsigned int)adler_bytes;
	}

	if(whole_block_remaining==0)
	{
		md5_hash.finalize();
		hash_for_whole_block=true;
		m_hashoutput->Seek(chunkhash_file_off+(block_for_chunk_start/c_checkpoint_dist)*chunkhash_single_size);
		writeFileRepeat(m_hashoutput, (char*)md5_hash.raw_digest_int(), big_hash_size);

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
			if(nofreespace_callback!=NULL && !nofreespace_callback->handle_not_enough_space(f->getFilenameW()) )
			{
				break;
			}
			Server->Log("Failed to write to file... waiting... in Chunked File transfer", LL_WARNING);
			Server->wait(10000);
			--tries;
		}
	}
	while(written<bsize && (rc>0 || tries>0) );

	if(rc==0)
	{
		Server->Log("Fatal error writing to file in writeFileRepeat. Write error in Chunked File transfer.", LL_ERROR);
	}
}

void FileClientChunked::State_Chunk(void)
{
	size_t rbytes=(std::min)(remaining_bufptr_bytes, (size_t)adler_remaining);
	adler_remaining-=(unsigned int)rbytes;

	chunk_start+=rbytes;

	if(rbytes>0)
	{
		adler_hash=adler32(adler_hash, bufptr, (unsigned int)rbytes);
		md5_hash.update((unsigned char*)bufptr, (unsigned int)rbytes);

		if(!patch_mode)
		{
			writeFileRepeat(m_file, bufptr, rbytes);
			file_pos+=rbytes;
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
		writeFileRepeat(m_hashoutput, (char*)&adler_hash, small_hash_size);
		state=CS_ID_FIRST;
	}
}

_i64 FileClientChunked::getSize(void)
{
	return remote_filesize;
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
	last_chunk_patches.push_back(patchfile_pos);
	patchfile_pos+=plen+length;
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
	if(pipe!=NULL)
	{
		transferred_bytes+=pipe->getTransferedBytes();
		pipe->resetTransferedBytes();
	}
	return transferred_bytes;
}

bool FileClientChunked::Reconnect(void)
{
	if(queue_callback!=NULL)
	{
		queue_callback->resetQueueChunked();

		clearFileClientQueue();
	}

	if(reconnection_callback==NULL)
		return false;

	unsigned int reconnect_starttime=Server->getTimeMS();
	while(Server->getTimeMS()-reconnect_starttime<reconnection_timeout)
	{
		IPipe *nc=reconnection_callback->new_fileclient_connection();
		if(nc!=NULL)
		{
			if(pipe!=NULL && destroy_pipe)
			{
				Server->destroy(pipe);
			}
			pipe=nc;
			for(size_t i=0;i<throttlers.size();++i)
			{
				pipe->addThrottler(throttlers[i]);
			}
			Server->Log("Reconnected successfully.", LL_DEBUG);
			remote_filesize=-1;
			num_total_chunks=0;
			starttime=Server->getTimeMS();
			resetQueuedChunks();
			block_for_chunk_start=-1;
			state=CS_ID_FIRST;
			patch_buf_pos=0;

			_i64 fileoffset=0;

			_i64 hashfilesize=0;
			m_chunkhashes->Seek(0);
			if(m_chunkhashes->Read((char*)&hashfilesize, sizeof(_i64))!=sizeof(_i64) )
				return false;

			CWData data;
			data.addUChar( ID_GET_FILE_BLOCKDIFF );
			data.addString( remote_filename );
			data.addString( identity );
			data.addInt64( fileoffset );
			data.addInt64( hashfilesize );			

			size_t rc=stack->Send( pipe, data.getDataPtr(), data.getDataSize() );
			if(rc==0)
			{
				Server->Log("Failed anyways. has_error="+nconvert(pipe->hasError()), LL_DEBUG);
				Server->wait(2000);
				continue;
			}

			Server->Log("pending_chunks="+nconvert(pending_chunks.size())+" next_chunk="+nconvert(next_chunk), LL_DEBUG);
			for(std::map<_i64, SChunkHashes>::iterator it=pending_chunks.begin();it!=pending_chunks.end();++it)
			{
				if( it->first/c_checkpoint_dist<next_chunk)
				{
					next_chunk=it->first/c_checkpoint_dist;
				}
			}
			VLOG(Server->Log("next_chunk="+nconvert(next_chunk), LL_DEBUG));

			if(patch_mode)
			{
				Server->Log("Invalidating "+nconvert(last_chunk_patches.size())+" chunks in patch file", LL_DEBUG);
			}
			invalidateLastPatches();
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
	if(pipe!=NULL)
	{
		pipe->addThrottler(throttler);
	}
}

IPipe *FileClientChunked::getPipe()
{
	return pipe;
}

void FileClientChunked::setReconnectionTimeout(unsigned int t)
{
	reconnection_timeout=t;
}

_i64 FileClientChunked::getReceivedDataBytes( void )
{
	IScopedLock lock(mutex);
	return received_data_bytes;
}

void FileClientChunked::resetReceivedDataBytes( void )
{
	IScopedLock lock(mutex);
	received_data_bytes=0;
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

_u32 FileClientChunked::loadFileOutOfBand()
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
		return tmp_fc.GetFilePatch(remote_filename, m_file, m_patchfile, m_chunkhashes, m_hashoutput, -1);
	}
	else
	{
		return tmp_fc.GetFileChunked(remote_filename, m_file, m_chunkhashes, m_hashoutput, -1);
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
		Server->destroy(ofbPipe());
	}

	unsigned int reconnect_starttime=Server->getTimeMS();
	while(Server->getTimeMS()-reconnect_starttime<reconnection_timeout)
	{
		setOfbPipe(reconnection_callback->new_fileclient_connection());

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

_u32 FileClientChunked::loadChunkOutOfBand(_i64 chunk_pos)
{
	if(ofbPipe()==NULL)
	{
		if(!constructOutOfBandPipe())
		{
			return ERR_CONN_LOST;
		}
	}

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

	char stack_buf[BUFFERSIZE];

	while(pending_chunks.find(chunk_pos)!=pending_chunks.end())
	{
		size_t rc = ofbPipe()->Read(stack_buf, BUFFERSIZE, 0);

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
					starttime=Server->getTimeMS();
				}
			}
		}
		else
		{
			starttime=Server->getTimeMS();

			_u32 err = handle_data(stack_buf, rc, true);

			if(err!=ERR_CONTINUE)
			{
				return err;
			}
		}

		unsigned int ctime = Server->getTimeMS();
		if(ctime>starttime && ctime-starttime>=SERVER_TIMEOUT)
		{
			Server->Log("OFB-Connection timeout. Reconnecting...", LL_DEBUG);
			if(!constructOutOfBandPipe())
			{
				return ERR_TIMEOUT;
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
	}

	return ERR_SUCCESS;
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
			queued_fcs.pop();
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

