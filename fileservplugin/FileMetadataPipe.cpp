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

#include "FileMetadataPipe.h"
#include "../common/data.h"
#include <assert.h>
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "IFileServ.h"
#include <cstring>
#include "FileServ.h"
#include "PipeSessions.h"
#include "../common/adler32.h"
#include <limits.h>

#ifndef _WIN32
#include <sys/types.h>

#ifndef __FreeBSD__
#include <sys/xattr.h>
#else
#include <sys/extattr.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#endif

#if defined(__APPLE__)
#define llistxattr(path, list, size) listxattr(path, list, size, XATTR_NOFOLLOW)
#define lgetxattr(path, name, value, size) getxattr(path, name, value, size, 0, XATTR_NOFOLLOW)
#define stat64 stat
#define lstat64 lstat
#endif

#if defined(__FreeBSD__)
#define llistxattr(path, list, size) extattr_list_link(path, EXTATTR_NAMESPACE_USER, list, size)
#define lgetxattr(path, name, value, size) extattr_get_link(path, EXTATTR_NAMESPACE_USER, name, value, size)
#define stat64 stat
#define lstat64 lstat
#endif

const size_t metadata_id_size = 4+4+8+4;


FileMetadataPipe::FileMetadataPipe( IPipe* pipe, const std::string& cmd )
	: PipeFileBase(cmd), pipe(pipe),
#ifdef _WIN32
	hFile(INVALID_HANDLE_VALUE),
	backup_read_state(-1),
#else
	backup_state(BackupState_StatInit),
#endif
	metadata_state(MetadataState_Wait),
		errpipe(Server->createMemoryPipe()),
	metadata_file(NULL)
{
	metadata_buffer.resize(4096);
	init();
}

FileMetadataPipe::~FileMetadataPipe()
{
	assert(token_callback.get() == NULL);
}


bool FileMetadataPipe::getExitCode( int& exit_code )
{
	exit_code = 0;
	return true;
}

bool FileMetadataPipe::readStdoutIntoBuffer( char* buf, size_t buf_avail, size_t& read_bytes )
{
	if (token_callback.get() == NULL)
	{
		token_callback.reset(FileServ::newTokenCallback());
	}

	if(buf_avail==0)
	{
		read_bytes = 0;
		return true;
	}

	if(metadata_state==MetadataState_FnSize)
	{
		read_bytes = (std::min)(buf_avail, sizeof(unsigned int) - fn_off);
		unsigned int fn_size = little_endian(static_cast<unsigned int>(public_fn.size()));
		fn_size = little_endian(fn_size);
		memcpy(buf, reinterpret_cast<char*>(&fn_size) + fn_off, read_bytes);
		curr_checksum = urb_adler32(curr_checksum, reinterpret_cast<char*>(&fn_size) + fn_off, static_cast<_u32>(read_bytes));
		fn_off+=read_bytes;

		if(fn_off==sizeof(unsigned int))
		{
			fn_off=0;
			metadata_state = MetadataState_Fn;
		}

		return true;
	}
	else if(metadata_state==MetadataState_Fn)
	{
		read_bytes = (std::min)(buf_avail, public_fn.size()- fn_off);
		memcpy(buf, public_fn.data()+fn_off, read_bytes);
		curr_checksum = urb_adler32(curr_checksum, public_fn.data()+fn_off, static_cast<_u32>(read_bytes));
		fn_off+=read_bytes;

		if(fn_off==public_fn.size())
		{
			fn_off=0;
			metadata_state = MetadataState_FnChecksum;
		}

		return true;
	}
	else if(metadata_state==MetadataState_FnChecksum)
	{
		read_bytes = (std::min)(buf_avail, sizeof(unsigned int) - fn_off);
		unsigned int endian_curr_checksum = little_endian(curr_checksum);
		memcpy(buf, reinterpret_cast<char*>(&endian_curr_checksum) + fn_off, read_bytes);
		fn_off+=read_bytes;

		if(fn_off==sizeof(unsigned int))
		{
			metadata_buffer_size = 0;
			metadata_buffer_off = 0;
			curr_checksum = urb_adler32(0, NULL, 0);
			if(callback==NULL)
			{
				metadata_state = MetadataState_Common;
			}
			else
			{
				if(!metadata_file->Seek(metadata_file_off))
				{
					std::string msg = "Error seeking to metadata in \"" + metadata_file->getFilename() + "\"";
					Server->Log(msg, LL_ERROR);
					errpipe->Write(msg);

					read_bytes=0;
					metadata_file.reset();
					PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
					metadata_state = MetadataState_Wait;
					return false;
				}

				metadata_state = MetadataState_File;
			}
		}

		return true;
	}
	else if(metadata_state == MetadataState_Common)
	{
		if(metadata_buffer_size==0)
		{
			SFile file_meta = getFileMetadata(os_file_prefix(local_fn));
			if(file_meta.name.empty())
			{
				Server->Log("Error getting metadata (created and last modified time) of "+local_fn, LL_ERROR);
			}
					
			CWData meta_data;
			meta_data.addChar(1);
#ifdef _WIN32
			meta_data.addVarInt(file_meta.created);
#else
			meta_data.addVarInt(0);
#endif
			meta_data.addVarInt(file_meta.last_modified);
			meta_data.addVarInt(file_meta.accessed);
			meta_data.addVarInt(folder_items);
			meta_data.addVarInt(metadata_id);
			if(token_callback.get()!=NULL)
			{
				meta_data.addString(token_callback->getFileTokens(local_fn));
			}
			else
			{
				meta_data.addString("");
			}

			if(meta_data.getDataSize()+2*sizeof(unsigned int)<metadata_buffer.size())
			{
				unsigned int data_size = little_endian(static_cast<unsigned int>(meta_data.getDataSize()));
				memcpy(metadata_buffer.data(), &data_size, sizeof(data_size));
				memcpy(metadata_buffer.data()+sizeof(unsigned int), meta_data.getDataPtr(), meta_data.getDataSize());
				metadata_buffer_size = meta_data.getDataSize()+sizeof(unsigned int);

				curr_checksum = urb_adler32(curr_checksum, metadata_buffer.data(), static_cast<_u32>(metadata_buffer_size));
				unsigned int endian_curr_checksum = little_endian(curr_checksum);
				memcpy(metadata_buffer.data()+metadata_buffer_size, &endian_curr_checksum, sizeof(endian_curr_checksum));
				metadata_buffer_size+=sizeof(endian_curr_checksum);
			}
			else
			{
				Server->Log("File metadata of "+local_fn+" too large ("+convert((size_t)meta_data.getDataSize())+")", LL_ERROR);
				return false;
			}
			
		}

		if (metadata_buffer_size - metadata_buffer_off>0)
		{
			read_bytes = (std::min)(metadata_buffer_size - metadata_buffer_off, buf_avail);
			memcpy(buf, metadata_buffer.data() + metadata_buffer_off, read_bytes);
			metadata_buffer_off += read_bytes;
		}
		if (metadata_buffer_size - metadata_buffer_off == 0)
		{
			metadata_buffer_size = 0;
			metadata_buffer_off = 0;
			curr_checksum = urb_adler32(0, NULL, 0);
			metadata_state = MetadataState_Os;
		}
		return true;
	}
	else if(metadata_state == MetadataState_Os)
	{
		bool b = transmitCurrMetadata(buf, buf_avail, read_bytes);

		if(read_bytes>0)
		{
			curr_checksum = urb_adler32(curr_checksum, buf, static_cast<_u32>(read_bytes));
		}

		if(!b)
		{
			fn_off=0;
			metadata_state = MetadataState_OsChecksum;

#ifndef _WIN32
			backup_state=BackupState_StatInit;
#else
			hFile = INVALID_HANDLE_VALUE;
			backup_read_state = -1;
#endif

		}
		return true;
	}
	else if(metadata_state == MetadataState_OsChecksum)
	{
		read_bytes = (std::min)(buf_avail, sizeof(unsigned int) - fn_off);
		unsigned int endian_curr_checksum = little_endian(curr_checksum);
		memcpy(buf, reinterpret_cast<char*>(&endian_curr_checksum) + fn_off, read_bytes);
		fn_off+=read_bytes;

		if(fn_off==sizeof(unsigned int))
		{
			PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
			metadata_state = MetadataState_Wait;
		}

		return true;
	}
	else if(metadata_state == MetadataState_File)
	{
		read_bytes = static_cast<size_t>((std::min)(static_cast<int64>(buf_avail), metadata_file_size));

		if(read_bytes==0)
		{
			metadata_state = MetadataState_FileChecksum;
			return true;
		}

		_u32 read = metadata_file->Read(buf, static_cast<_u32>(read_bytes));
		if(read!=read_bytes)
		{
			std::string msg;
			Server->Log(msg, LL_ERROR);
			errpipe->Write(msg);
			memset(buf + read, 0, read_bytes - read);
		}

		curr_checksum = urb_adler32(curr_checksum, buf, static_cast<_u32>(read_bytes));

		metadata_file_size-=read_bytes;

		if(read_bytes<buf_avail)
		{
			fn_off=0;
			metadata_state = MetadataState_File;
		}
		return true;
	}
	else if(metadata_state == MetadataState_FileChecksum)
	{
		read_bytes = (std::min)(buf_avail, sizeof(unsigned int) - fn_off);
		unsigned int endian_curr_checksum = little_endian(curr_checksum);
		memcpy(buf, reinterpret_cast<char*>(&endian_curr_checksum) + fn_off, read_bytes);
		fn_off+=read_bytes;

		if(fn_off==sizeof(unsigned int))
		{
			metadata_file.reset();
			PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
			metadata_state = MetadataState_Wait;
		}

		return true;
	}
	else if (metadata_state == MetadataState_Raw)
	{
		if (raw_metadata.size() - metadata_buffer_off > 0)
		{
			read_bytes = (std::min)(raw_metadata.size() - metadata_buffer_off, buf_avail);
			memcpy(buf, raw_metadata.data() + metadata_buffer_off, read_bytes);
			metadata_buffer_off += read_bytes;
		}
		if (raw_metadata.size() - metadata_buffer_off == 0)
		{
			PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
			metadata_state = MetadataState_Wait;
		}
		return true;
	}
	else if (metadata_state == MetadataState_RawFileFnSize)
	{
		read_bytes = (std::min)(buf_avail, sizeof(unsigned int) - fn_off);
		unsigned int fn_size = little_endian(static_cast<unsigned int>(public_fn.size()));
		fn_size = little_endian(fn_size);
		memcpy(buf, reinterpret_cast<char*>(&fn_size) + fn_off, read_bytes);
		curr_checksum = urb_adler32(curr_checksum, reinterpret_cast<char*>(&fn_size) + fn_off, static_cast<_u32>(read_bytes));
		fn_off += read_bytes;

		if (fn_off == sizeof(unsigned int))
		{
			fn_off = 0;
			metadata_state = MetadataState_RawFileFn;
		}

		return true;
	}
	else if (metadata_state == MetadataState_RawFileFn)
	{
		read_bytes = (std::min)(buf_avail, public_fn.size() - fn_off);
		memcpy(buf, public_fn.data() + fn_off, read_bytes);
		curr_checksum = urb_adler32(curr_checksum, public_fn.data() + fn_off, static_cast<_u32>(read_bytes));
		fn_off += read_bytes;

		if (fn_off == public_fn.size())
		{
			fn_off = 0;
			metadata_state = MetadataState_RawFileFnChecksum;
		}

		return true;
	}
	else if (metadata_state == MetadataState_RawFileFnChecksum)
	{
		read_bytes = (std::min)(buf_avail, sizeof(unsigned int) - fn_off);
		unsigned int endian_curr_checksum = little_endian(curr_checksum);
		memcpy(buf, reinterpret_cast<char*>(&endian_curr_checksum) + fn_off, read_bytes);
		fn_off += read_bytes;

		if (fn_off == sizeof(unsigned int))
		{
			int64* datasize = reinterpret_cast<int64*>(metadata_buffer.data());
			metadata_file_size = transmit_file->Size();
			metadata_file_off = 0;
			*datasize = little_endian(metadata_file_size);
			metadata_buffer_size = sizeof(int64);
			metadata_buffer_off = 0;
			metadata_state = MetadataState_RawFileDataSize;
		}

		return true;
	}
	else if (metadata_state == MetadataState_RawFileDataSize)
	{
		if (metadata_buffer_size - metadata_buffer_off>0)
		{
			read_bytes = (std::min)(metadata_buffer_size - metadata_buffer_off, buf_avail);
			memcpy(buf, metadata_buffer.data() + metadata_buffer_off, read_bytes);
			metadata_buffer_off += read_bytes;
		}
		if (metadata_buffer_size - metadata_buffer_off == 0)
		{
			metadata_buffer_size = 0;
			metadata_buffer_off = 0;
			curr_checksum = urb_adler32(0, NULL, 0);
			metadata_state = MetadataState_RawFileData;
			sha512_init(&transmit_file_ctx);
		}
		return true;
	}
	else if (metadata_state == MetadataState_RawFileData)
	{
		_u32 max_read = static_cast<_u32>((std::min)(metadata_file_size-metadata_file_off, (std::min)((int64)32768, static_cast<int64>(buf_avail))));

		bool has_read_error = false;
		read_bytes = transmit_file->Read(metadata_file_off, buf, max_read, &has_read_error);

		sha512_update(&transmit_file_ctx, reinterpret_cast<unsigned char*>(buf), static_cast<_u32>(read_bytes));

		if (has_read_error)
		{
			Server->Log("Error reading from file " + transmit_file->getFilename() + ". " + os_last_error_str(), LL_WARNING);
			return false;
		}
		
		metadata_file_off += read_bytes;

		if (metadata_file_off == metadata_file_size)
		{
			metadata_state = MetadataState_RawFileDataChecksum;
			sha512_final(&transmit_file_ctx, reinterpret_cast<unsigned char*>(metadata_buffer.data()));
			metadata_buffer_size = SHA512_DIGEST_SIZE;
			metadata_buffer_off = 0;
		}

		return true;
	}
	else if (metadata_state == MetadataState_RawFileDataChecksum)
	{
		if (metadata_buffer_size - metadata_buffer_off>0)
		{
			read_bytes = (std::min)(metadata_buffer_size - metadata_buffer_off, buf_avail);
			memcpy(buf, metadata_buffer.data() + metadata_buffer_off, read_bytes);
			metadata_buffer_off += read_bytes;
		}
		if (metadata_buffer_size - metadata_buffer_off == 0)
		{
			transmit_wait_pipe->Write(std::string());
			transmit_wait_pipe = NULL;
			transmit_file = NULL;
			PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
			metadata_state = MetadataState_Wait;
		}
		return true;
	}


	while(true)
	{
		std::string msg;
		size_t r = pipe->Read(&msg, 60000);

		if(r==0)
		{
			if(pipe->hasError())
			{
				read_bytes = 0;
				return false;
			}
			else
			{
				*buf = ID_METADATA_NOP;
				read_bytes = 1;
				return true;
			}
		}
		else
		{
			CRData msg_data(&msg);

			char id;
			if (msg_data.getChar(&id))
			{
				if (id == METADATA_PIPE_SEND_FILE &&
					msg_data.getStr(&public_fn) &&
					msg_data.getStr(&local_fn) &&
					msg_data.getInt64(&folder_items) &&
					msg_data.getInt64(&metadata_id) &&
					msg_data.getStr(&server_token))
				{
					assert(!public_fn.empty() || !local_fn.empty());

					if (std::find(last_public_fns.begin(), last_public_fns.end(), public_fn) != last_public_fns.end())
					{
						PipeSessions::fileMetadataDone(public_fn, server_token);
						*buf = ID_METADATA_NOP;
						read_bytes = 1;
						return true;
					}

					last_public_fns.push_back(public_fn);

					if (last_public_fns.size() > 10)
					{
						last_public_fns.pop_front();
					}

					int file_type_flags = os_get_file_type(os_file_prefix(local_fn));

					if (file_type_flags == 0)
					{
						Server->Log("Error getting file type of " + local_fn, LL_ERROR);
						*buf = ID_METADATA_NOP;
						read_bytes = 1;
						PipeSessions::fileMetadataDone(public_fn, server_token);
						return true;
					}

					if (!msg_data.getVoidPtr(reinterpret_cast<void**>(&callback)))
					{
						callback = NULL;
					}

					if (callback==NULL && !openFileHandle())
					{
						Server->Log("Error opening file handle to " + local_fn+". "+os_last_error_str(), LL_ERROR);
						*buf = ID_METADATA_NOP;
						read_bytes = 1;
						PipeSessions::fileMetadataDone(public_fn, server_token);
						return true;
					}

					std::string file_type;
					if ((file_type_flags & EFileType_Directory)
						&& (file_type_flags & EFileType_Symlink))
					{
						file_type = "l";
					}
					else if (file_type_flags & EFileType_Directory)
					{
						file_type = "d";
					}
					else
					{
						file_type = "f";
					}

					public_fn = file_type + public_fn;

					metadata_state = MetadataState_FnSize;
					*buf = ID_METADATA_V1;
					read_bytes = 1;
					fn_off = 0;
					curr_checksum = urb_adler32(0, NULL, 0);

					if(callback!=NULL)
					{
						std::string orig_path;
						_u32 version = 0;
						metadata_file.reset(callback->getMetadata(public_fn, &orig_path, &metadata_file_off, &metadata_file_size, &version, false));

						if (metadata_file.get() == NULL)
						{
							Server->Log("Error opening metadata file for \"" + public_fn + "\"", LL_ERROR);
							errpipe->Write("Error opening metadata file for \"" + public_fn + "\"");

							*buf = ID_METADATA_NOP;
							read_bytes = 1;
							metadata_state = MetadataState_Wait;
							PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
							return true;
						}

						if (version != 0)
						{
							*buf = version;
						}
						public_fn = file_type + orig_path;
					}

					return true;
				}
				else if (id == METADATA_PIPE_SEND_RAW
					&& msg_data.getStr(&public_fn)
					&& msg_data.getStr(&raw_metadata)
					&& msg_data.getStr(&server_token))
				{
					metadata_state = MetadataState_Raw;
					metadata_buffer_off = 0;
					return readStdoutIntoBuffer(buf, buf_avail, read_bytes);
				}
				else if (id == METADATA_PIPE_SEND_RAW_FILEDATA
					&& msg_data.getStr(&public_fn)
					&& msg_data.getVoidPtr(reinterpret_cast<void**>(&transmit_file))
					&& msg_data.getVoidPtr(reinterpret_cast<void**>(&transmit_wait_pipe))
					&& msg_data.getStr(&server_token))
				{
					metadata_state = MetadataState_RawFileFnSize;
					*buf = ID_RAW_FILE;
					read_bytes = 1;
					fn_off = 0;
					curr_checksum = urb_adler32(0, NULL, 0);

					return true;
				}
				else if (id == METADATA_PIPE_EXIT)
				{
					errpipe->shutdown();
					read_bytes = 0;
					return false;
				}
				else
				{
					Server->Log("Unknown metadata pipe id: " + convert((int)id), LL_ERROR);
					assert(false);
					return false;
				}
			}
			else
			{
				Server->Log("Cannot get metadata pipe id", LL_ERROR);
				assert(false);
				return false;
			}
		}
	}	
}

void FileMetadataPipe::finishStdout()
{
	token_callback.reset();
}

bool FileMetadataPipe::readStderrIntoBuffer( char* buf, size_t buf_avail, size_t& read_bytes )
{
	while(true)
	{
		if(stderr_buf.size()>0)
		{
			read_bytes = (std::min)(buf_avail, stderr_buf.size());
			memcpy(buf, stderr_buf.data(), read_bytes);
			stderr_buf.erase(0, read_bytes);
			return true;
		}

		if(errpipe->Read(&stderr_buf)==0)
		{
            if(errpipe->hasError())
			{
				return false;
			}
		}
	}	
}

void FileMetadataPipe::cleanupOnForceShutdown()
{
	if (metadata_state != MetadataState_Wait)
	{
		PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
	}

	metadata_file.reset();

#ifdef _WIN32
	if (hFile != INVALID_HANDLE_VALUE)
	{
		if (backup_read_context != NULL)
		{
			BackupRead(hFile, NULL, 0, NULL, TRUE, TRUE, &backup_read_context);
			backup_read_context = NULL;
		}

		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}
#endif

	while (true)
	{
		std::string msg;
		size_t r = pipe->Read(&msg, 0);

		if (r == 0)
		{
			break;
		}

		CRData msg_data(&msg);

		char id;
		if (msg_data.getChar(&id))
		{
			if (id == METADATA_PIPE_SEND_FILE &&
				msg_data.getStr(&public_fn) &&
				msg_data.getStr(&local_fn) &&
				msg_data.getInt64(&folder_items) &&
				msg_data.getInt64(&metadata_id) &&
				msg_data.getStr(&server_token))
			{
				PipeSessions::fileMetadataDone(public_fn, server_token);
			}
			else if (id == METADATA_PIPE_SEND_RAW
				&& msg_data.getStr(&public_fn)
				&& msg_data.getStr(&raw_metadata)
				&& msg_data.getStr(&server_token))
			{
				PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
			}
			else if (id == METADATA_PIPE_SEND_RAW_FILEDATA
				&& msg_data.getStr(&public_fn)
				&& msg_data.getVoidPtr(reinterpret_cast<void**>(&transmit_file))
				&& msg_data.getVoidPtr(reinterpret_cast<void**>(&transmit_wait_pipe))
				&& msg_data.getStr(&server_token))
			{
				PipeSessions::fileMetadataDone(public_fn.substr(1), server_token);
			}
		}
	}
}


void FileMetadataPipe::forceExitWait()
{
	CWData data;
	data.addChar(METADATA_PIPE_EXIT);
	pipe->Write(data.getDataPtr(), data.getDataSize());

	errpipe->shutdown();

	waitForExit();
}

#ifdef _WIN32
bool FileMetadataPipe::transmitCurrMetadata( char* buf, size_t buf_avail, size_t& read_bytes )
{
	if(metadata_buffer_size-metadata_buffer_off>0)
	{
		read_bytes = (std::min)(metadata_buffer_size-metadata_buffer_off, buf_avail);
		memcpy(buf, metadata_buffer.data()+metadata_buffer_off, read_bytes);
		metadata_buffer_off+=read_bytes;
		return true;
	}

	if(backup_read_state==-1)
	{
		if(hFile==INVALID_HANDLE_VALUE)
		{
			errpipe->Write("Error opening file \""+local_fn+"\" to read metadata. Last error: "+convert((int)GetLastError())+"\n");
			return false;
		}

		backup_read_state = 0;
		backup_read_context=NULL;

		FILE_BASIC_INFO file_information;
		if(GetFileInformationByHandleEx(hFile, FileBasicInfo, &file_information, sizeof(file_information))==FALSE)
		{
			errpipe->Write("Error getting file attributes of \""+local_fn+"\". Last error: "+convert((int)GetLastError())+"\n");
			return false;
		}

		CWData data;
		data.addChar(1);
		data.addUInt(file_information.FileAttributes);
		data.addVarInt(file_information.CreationTime.QuadPart);
		data.addVarInt(file_information.LastAccessTime.QuadPart);
		data.addVarInt(file_information.LastWriteTime.QuadPart);
		data.addVarInt(file_information.ChangeTime.QuadPart);

		_u32 data_size = little_endian(static_cast<_u32>(data.getDataSize()));
		memcpy(metadata_buffer.data(), &data_size, sizeof(data_size));
		memcpy(metadata_buffer.data()+sizeof(data_size), data.getDataPtr(), data.getDataSize());
		metadata_buffer_size = data.getDataSize()+sizeof(data_size);
		metadata_buffer_off=0;
		return transmitCurrMetadata(buf, buf_avail, read_bytes);
	}

	if(backup_read_state==0)
	{
		DWORD total_read = 0;
		DWORD read;
		BOOL b = BackupRead(hFile, reinterpret_cast<LPBYTE>(metadata_buffer.data()+1), metadata_id_size, &read, FALSE, TRUE, &backup_read_context);

		if(b==FALSE)
		{
			errpipe->Write("Error getting metadata of file \""+local_fn+"\". Last error: "+convert((int)GetLastError())+"\n");
			*buf = 0;
			read_bytes = 1;
			return false;
		}

		if(read==0)
		{
			BackupRead(hFile, NULL, 0, NULL, TRUE, TRUE, &backup_read_context);
			backup_read_context=NULL;
			
			CloseHandle(hFile);
			hFile = INVALID_HANDLE_VALUE;
			backup_read_state = -1;

			*buf=0;
			read_bytes = 1;
			return false;
		}

		if(read!=metadata_id_size)
		{
			errpipe->Write("Error getting metadata stream structure.\n");
			*buf=0;
			read_bytes = 1;
			return false;
		}

		total_read += read;

		metadata_buffer[0]=1;

		WIN32_STREAM_ID* curr_stream = reinterpret_cast<WIN32_STREAM_ID*>(metadata_buffer.data()+1);

		if(curr_stream->dwStreamNameSize>0)
		{
			if (3 + read + curr_stream->dwStreamNameSize > metadata_buffer.size())
			{
				metadata_buffer.resize(3 + read + curr_stream->dwStreamNameSize);
			}

			b = BackupRead(hFile, reinterpret_cast<LPBYTE>(metadata_buffer.data()+1) + read, curr_stream->dwStreamNameSize, &read, FALSE, TRUE, &backup_read_context);

			if(b==FALSE)
			{
				errpipe->Write("Error getting metadata of file \""+local_fn+"\" (2). Last error: "+convert((int)GetLastError())+"\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			if(read!=curr_stream->dwStreamNameSize)
			{
				errpipe->Write("Error getting metadata stream structure (name).\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			curr_stream->cStreamName[curr_stream->dwStreamNameSize/sizeof(WCHAR)] = 0;

			total_read += read;

			stream_name = Server->ConvertFromWchar(curr_stream->cStreamName);
		}
		else
		{
			stream_name.clear();
		}

		if(curr_stream->dwStreamId==BACKUP_DATA)
		{
			//skip
			LARGE_INTEGER seeked;
			DWORD high_seeked;
			b = BackupSeek(hFile, curr_stream->Size.LowPart, curr_stream->Size.HighPart, &seeked.LowPart, &high_seeked, &backup_read_context);
			seeked.HighPart = high_seeked;

			if(b==FALSE)
			{
				errpipe->Write("Error skipping data stream of file \""+local_fn+"\" (1). Last error: "+convert((int)GetLastError())+"\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			if(seeked.QuadPart!=curr_stream->Size.QuadPart)
			{
				errpipe->Write("Error skipping data stream of file \""+local_fn+"\" (2). Last error: "+convert((int)GetLastError())+"\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			return transmitCurrMetadata(buf, buf_avail, read_bytes);
		}

		if (curr_stream->Size.QuadPart > 0)
		{
			backup_read_state = 1;
			curr_stream_size = curr_stream->Size;
		}

		metadata_buffer_size = total_read + 1;
		metadata_buffer_off = 0;
		curr_pos = 0;

		return transmitCurrMetadata(buf, buf_avail, read_bytes);
	}
	else if(backup_read_state==1)
	{
		DWORD toread = static_cast<DWORD>((std::min)(curr_stream_size.QuadPart-curr_pos, static_cast<int64>(buf_avail)));

		DWORD read;
		BOOL b = BackupRead(hFile, reinterpret_cast<LPBYTE>(buf), toread, &read, FALSE, TRUE, &backup_read_context);

		if(b==FALSE)
		{
			errpipe->Write("Error reading metadata stream \""+stream_name+"\" of file \""+local_fn+"\". Last error: "+convert((int)GetLastError())+"\n");
			memset(buf, 0, toread);
		}

		read_bytes = read;
		curr_pos+=read;

		assert(curr_pos <= curr_stream_size.QuadPart);

		if(curr_pos==curr_stream_size.QuadPart)
		{
			backup_read_state = 0;
		}

		return true;
	}

	return false;
}

bool FileMetadataPipe::openFileHandle()
{
	backup_read_context = NULL;
	hFile = CreateFileW(Server->ConvertToWchar(os_file_prefix(local_fn)).c_str(), GENERIC_READ | ACCESS_SYSTEM_SECURITY | READ_CONTROL, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, NULL);

	backup_read_state = -1;

	return hFile != INVALID_HANDLE_VALUE;
}

#else //_WIN32

void serialize_stat_buf(const struct stat64& buf, const std::string& symlink_target, CWData& data)
{
	data.addChar(1);
	data.addVarInt(buf.st_dev);
	data.addVarInt(buf.st_mode);
	data.addVarInt(buf.st_uid);
	data.addVarInt(buf.st_gid);
	data.addVarInt(buf.st_rdev);
#if defined(__APPLE__) || defined(__FreeBSD__)
	data.addVarInt(buf.st_atimespec.tv_sec);
	data.addUInt(buf.st_atimespec.tv_nsec);
	data.addVarInt(buf.st_mtimespec.tv_sec);
	data.addUInt(buf.st_mtimespec.tv_nsec);
	data.addVarInt(buf.st_ctimespec.tv_sec);
	data.addUInt(buf.st_ctimespec.tv_nsec);
	data.addVarInt(buf.st_flags);
#else
	data.addVarInt(buf.st_atime);
	data.addUInt(buf.st_atim.tv_nsec);
	data.addVarInt(buf.st_mtime);
	data.addUInt(buf.st_mtim.tv_nsec);
	data.addVarInt(buf.st_ctime);
	data.addUInt(buf.st_ctim.tv_nsec);
	data.addVarInt(0);
#endif
	data.addString(symlink_target);
}

namespace
{
    bool get_xattr_keys(const std::string& fn, std::vector<std::string>& keys)
	{
		while(true)
		{
			ssize_t bufsize;
            bufsize = llistxattr(fn.c_str(), NULL, 0);

			if(bufsize==-1)
			{
				if (errno == EOPNOTSUPP)
				{
					return true;
				}
				Server->Log("Error getting extended attribute list of file "+fn+" errno: "+convert(errno), LL_ERROR);
				return false;
			}

			std::string buf;
			buf.resize(bufsize);

            bufsize = llistxattr(fn.c_str(), &buf[0], buf.size());

			if(bufsize==-1 && errno==ERANGE)
			{
				Server->Log("Extended attribute list size increased. Retrying...", LL_DEBUG);
				continue;
			}

			if(bufsize==-1)
			{
				Server->Log("Error getting extended attribute list of file "+fn+" errno: "+convert(errno)+" (2)", LL_ERROR);
				return false;
			}

			TokenizeMail(buf, keys, std::string(1, '\0'));

            for(size_t i=0;i<keys.size();)
            {
				if (keys[i].empty()
					|| keys[i].size()>=UINT_MAX)
				{
					keys.erase(keys.begin() + i);
					continue;
				}

                unsigned int ksize = static_cast<unsigned int>(keys[i].size());
                ksize = little_endian(ksize);

                keys[i].insert(0, reinterpret_cast<char*>(&ksize), sizeof(ksize));

				++i;
            }

			return true;
		}
	}

    bool get_xattr(const std::string& fn, const std::string& key, std::string& value)
	{
		while(true)
		{
			ssize_t bufsize;
            bufsize = lgetxattr(fn.c_str(), key.c_str()+sizeof(unsigned int), NULL, 0);

			if(bufsize==-1)
			{
				Server->Log("Error getting extended attribute \""+key+"\" of file \""+fn+"\" errno: "+convert(errno), LL_ERROR);
				return false;
			}

			value.resize(bufsize+sizeof(_u32));

            bufsize = lgetxattr(fn.c_str(), key.c_str()+sizeof(unsigned int), &value[sizeof(_u32)], value.size()-sizeof(_u32));

			if(bufsize==-1 && errno==ERANGE)
			{
				Server->Log("Extended attribute size increased. Retrying...", LL_DEBUG);
				continue;
			}

			if(bufsize==-1)
			{
				Server->Log("Error getting extended attribute \""+key+"\" of file \""+fn+"\" errno: "+convert(errno)+" (2)", LL_ERROR);
				return false;
			}

			if(bufsize<value.size()-sizeof(_u32))
			{
				value.resize(bufsize+sizeof(_u32));
			}

			_u32 vsize=static_cast<_u32>(bufsize);
			vsize=little_endian(vsize);

			memcpy(&value[0], &vsize, sizeof(vsize));

			return true;
		}
	}
}

bool FileMetadataPipe::transmitCurrMetadata(char* buf, size_t buf_avail, size_t& read_bytes)
{
	if(backup_state==BackupState_StatInit)
	{
		CWData data;
        struct stat64 statbuf;
        int rc = lstat64(local_fn.c_str(), &statbuf);

		if(rc!=0)
		{
            Server->Log("Error with lstat of "+local_fn+" errorcode: "+convert(errno), LL_ERROR);
			return false;
		}

		std::string symlink_target;
		if (S_ISLNK(statbuf.st_mode))
		{
			if (!os_get_symlink_target(local_fn, symlink_target))
			{
				Server->Log("Error getting symlink target of " + local_fn + " errorcode: " + convert(errno), LL_ERROR);
				return false;
			}
		}

        serialize_stat_buf(statbuf, symlink_target, data);

		if(data.getDataSize()+sizeof(_u32)>metadata_buffer.size())
		{
			Server->Log("File metadata of "+local_fn+" too large ("+convert((size_t)data.getDataSize()+sizeof(_u32))+")", LL_ERROR);
			return false;
		}


		_u32 metadata_size = little_endian(static_cast<_u32>(data.getDataSize()));
		memcpy(metadata_buffer.data(), &metadata_size, sizeof(_u32));
		memcpy(metadata_buffer.data()+sizeof(_u32), data.getDataPtr(), data.getDataSize());
		metadata_buffer_size=data.getDataSize()+sizeof(_u32);
		metadata_buffer_off=0;
		backup_state=BackupState_Stat;

		return transmitCurrMetadata(buf, buf_avail, read_bytes);
	}
    else if(backup_state==BackupState_Stat)
	{
		if(metadata_buffer_size-metadata_buffer_off>0)
		{
			read_bytes = (std::min)(metadata_buffer_size-metadata_buffer_off, buf_avail);
			memcpy(buf, metadata_buffer.data()+metadata_buffer_off, read_bytes);
			metadata_buffer_off+=read_bytes;
		}
		if(metadata_buffer_size-metadata_buffer_off==0)
		{
			backup_state=BackupState_EAttrInit;
		}
		return true;
	}
	else if(backup_state==BackupState_EAttrInit)
	{
		eattr_keys.clear();
        if(!get_xattr_keys(local_fn, eattr_keys))
		{
			return false;
		}

		CWData data;
		data.addInt64(eattr_keys.size());

		memcpy(metadata_buffer.data(), data.getDataPtr(), data.getDataSize());
		metadata_buffer_size=data.getDataSize();
		metadata_buffer_off=0;
		backup_state=BackupState_EAttr;

        return transmitCurrMetadata(buf, buf_avail, read_bytes);
	}
	else if(backup_state==BackupState_EAttr)
	{
		if(metadata_buffer_size-metadata_buffer_off>0)
		{
			read_bytes = (std::min)(metadata_buffer_size-metadata_buffer_off, buf_avail);
			memcpy(buf, metadata_buffer.data()+metadata_buffer_off, read_bytes);
			metadata_buffer_off+=read_bytes;
		}
		if(metadata_buffer_size-metadata_buffer_off==0)
		{
			if(!eattr_keys.empty())
			{
				eattr_idx=0;
				backup_state=BackupState_EAttr_Vals_Key;
				eattr_key_off=0;
			}
			else
			{
				return false;
			}
		}
		return true;
	}
	else if(backup_state==BackupState_EAttr_Vals_Key)
	{
        if(eattr_keys[eattr_idx].size()-eattr_key_off>0)
		{
            read_bytes = (std::min)(eattr_keys[eattr_idx].size()-eattr_key_off, buf_avail);
			memcpy(buf, eattr_keys[eattr_idx].c_str()+eattr_key_off, read_bytes);
			eattr_key_off+=read_bytes;
		}

        if(eattr_keys[eattr_idx].size()-eattr_key_off==0)
		{			
            if(!get_xattr(local_fn, eattr_keys[eattr_idx], eattr_val))
			{
				eattr_val.resize(sizeof(_u32));
				unsigned int umax = UINT_MAX;
				memcpy(&eattr_val[0], &umax, sizeof(umax));
			}
			backup_state = BackupState_EAttr_Vals_Val;
			eattr_val_off=0;
		}
		return true;
	}
	else if (backup_state == BackupState_EAttr_Vals_Val)
	{
		if (eattr_val.size() - eattr_val_off > 0)
		{
			read_bytes = (std::min)(eattr_val.size() - eattr_val_off, buf_avail);
			memcpy(buf, eattr_val.data() + eattr_val_off, read_bytes);
			eattr_val_off += read_bytes;
		}

		if (eattr_val.size() - eattr_val_off == 0)
		{
			if (eattr_idx + 1 < eattr_keys.size())
			{
				++eattr_idx;
				backup_state = BackupState_EAttr_Vals_Key;
				eattr_key_off = 0;
			}
			else
			{
				//finished
				return false;
			}
		}
		return true;
	}

	return false;
}

bool FileMetadataPipe::openFileHandle()
{
	return true;
}

#endif //_WIN32

