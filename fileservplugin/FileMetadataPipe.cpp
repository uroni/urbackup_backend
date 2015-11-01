/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#ifndef _WIN32
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#endif

const size_t metadata_id_size = 4+4+8+4;


FileMetadataPipe::FileMetadataPipe( IPipe* pipe, const std::wstring& cmd )
	: PipeFileBase(cmd), pipe(pipe),
#ifdef _WIN32
	hFile(INVALID_HANDLE_VALUE),
#else
	backup_state(BackupState_StatInit),
#endif
	metadata_state(MetadataState_Wait),
		errpipe(Server->createMemoryPipe())
{
	metadata_buffer.resize(4096);
	init();
}


bool FileMetadataPipe::getExitCode( int& exit_code )
{
	exit_code = 0;
	return true;
}

bool FileMetadataPipe::readStdoutIntoBuffer( char* buf, size_t buf_avail, size_t& read_bytes )
{
	if(token_callback.get()==NULL)
	{
		token_callback.reset(FileServ::newTokenCallback());
	}

    if(token_callback.get()==NULL)
    {
        return false;
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
		memcpy(buf, &fn_size + fn_off, read_bytes);
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
		fn_off+=read_bytes;

		if(fn_off==public_fn.size())
		{
			metadata_buffer_size = 0;
			metadata_buffer_off = 0;
			if(callback==NULL)
			{
				metadata_state = MetadataState_Common;
			}
			else
			{
				if(!metadata_file->Seek(metadata_file_off))
				{
					errpipe->Write(Server->ConvertToUTF8(L"Error seeking to metadata in \"" + metadata_file->getFilenameW()+L"\""));

					read_bytes=0;
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
			SFile file_meta = getFileMetadata(os_file_prefix(Server->ConvertToUnicode(local_fn)));
			if(file_meta.name.empty())
			{
				Server->Log("Error getting metadata (created and last modified time) of "+local_fn, LL_ERROR);
			}
			
			int64 created = little_endian(file_meta.created);
			int64 modified = little_endian(file_meta.last_modified);
			
			CWData meta_data;
			meta_data.addChar(1);
			meta_data.addInt64(created);
			meta_data.addInt64(modified);
			if(token_callback.get()!=NULL)
			{
				meta_data.addString(token_callback->getFileTokens(Server->ConvertToUnicode(local_fn)));
			}
			else
			{
				meta_data.addString("");
			}

			if(meta_data.getDataSize()+sizeof(unsigned int)<metadata_buffer.size())
			{
				unsigned int data_size = little_endian(static_cast<unsigned int>(meta_data.getDataSize()));
				memcpy(metadata_buffer.data(), &data_size, sizeof(data_size));
				memcpy(metadata_buffer.data()+sizeof(unsigned int), meta_data.getDataPtr(), meta_data.getDataSize());
				metadata_buffer_size = meta_data.getDataSize()+sizeof(unsigned int);
			}
			else
			{
				Server->Log("File metadata of "+local_fn+" too large ("+nconvert((size_t)meta_data.getDataSize())+")", LL_ERROR);
			}
			
		}

		if(metadata_buffer_size-metadata_buffer_off>0)
		{
			read_bytes = (std::min)(metadata_buffer_size-metadata_buffer_off, buf_avail);
			memcpy(buf, metadata_buffer.data()+metadata_buffer_off, read_bytes);
			metadata_buffer_off+=read_bytes;

			if(metadata_buffer_size-metadata_buffer_off == 0)
			{
				metadata_buffer_size = 0;
				metadata_buffer_off = 0;
				metadata_state=MetadataState_Os;
			}
		}
		return true;
	}
	else if(metadata_state == MetadataState_Os)
	{
		if(!transmitCurrMetadata(buf, buf_avail, read_bytes))
		{
			metadata_state = MetadataState_Wait;

#ifndef _WIN32
			backup_state=BackupState_StatInit;
#else
			hFile = INVALID_HANDLE_VALUE;
#endif

		}
		return true;
	}
	else if(metadata_state == MetadataState_File)
	{
		read_bytes = static_cast<size_t>((std::min)(static_cast<int64>(buf_avail), metadata_file_size));

		if(read_bytes==0)
		{
			metadata_state = MetadataState_Wait;
			return true;
		}

		_u32 read = metadata_file->Read(buf, static_cast<_u32>(read_bytes));
		if(read!=read_bytes)
		{
			errpipe->Write(Server->ConvertToUTF8(L"Error reading metadata stream from file \""+metadata_file->getFilenameW()+L"\"\n"));
			memset(buf + read, 0, read_bytes - read);
		}

		metadata_file_size-=read_bytes;

		if(read_bytes<buf_avail)
		{
			metadata_state = MetadataState_Wait;
		}
		return true;
	}


	while(true)
	{
		std::string msg;
		size_t r = pipe->Read(&msg, 100000);

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

			if(msg_data.getStr(&public_fn) &&
				msg_data.getStr(&local_fn))
			{
				if(public_fn.empty() &&
					local_fn.empty())
				{
					read_bytes = 0;
					return false;
				}


				int file_type_flags = os_get_file_type(os_file_prefix(Server->ConvertToUnicode(local_fn)));

				if(file_type_flags==0)
				{
					Server->Log("Error getting file type of "+local_fn, LL_ERROR);
					*buf = ID_METADATA_NOP;
					read_bytes = 1;
					return true;
				}

				std::string file_type;
				if( (file_type_flags & EFileType_Directory) 
					&& (file_type_flags & EFileType_Symlink) )
				{
					file_type="l";
				}
				else if(file_type_flags & EFileType_Directory)
				{
					file_type="d";
				}
				else
				{
					file_type="f";
				}

				public_fn = file_type + public_fn;

				metadata_state = MetadataState_FnSize;
				*buf = ID_METADATA_V1;
				read_bytes = 1;
				fn_off = 0;

				if(!msg_data.getVoidPtr(reinterpret_cast<void**>(&callback)))
				{
					callback=NULL;
				}
				else
				{
					std::string orig_path;
					metadata_file = callback->getMetadata(public_fn, orig_path, metadata_file_off, metadata_file_size);

					if(metadata_file==NULL)
					{
						errpipe->Write("Error opening metadata file for \""+public_fn+"\"");

						read_bytes=0;
						metadata_state = MetadataState_Wait;
						return false;
					}

					public_fn = file_type + orig_path;
				}

				return true;
			}
			else
			{
				assert(false);
				return false;
			}
		}
	}	
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
			if(pipe->hasError())
			{
				return false;
			}
		}
	}	
}


void FileMetadataPipe::forceExitWait()
{
	CWData data;
	data.addString(std::string());
	data.addString(std::string());
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

	if(hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFileW(os_file_prefix(Server->ConvertToUnicode(local_fn)).c_str(), GENERIC_READ|ACCESS_SYSTEM_SECURITY|READ_CONTROL, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT, NULL);

		if(hFile==INVALID_HANDLE_VALUE)
		{
			errpipe->Write("Error opening file \""+local_fn+"\" to read metadata. Last error: "+nconvert((int)GetLastError())+"\n");
			return false;
		}

		backup_read_state = 0;
		backup_read_context=NULL;
	}

	if(backup_read_state==0)
	{
		DWORD total_read = 0;
		DWORD read;
		BOOL b = BackupRead(hFile, reinterpret_cast<LPBYTE>(metadata_buffer.data()+1), metadata_id_size, &read, FALSE, TRUE, &backup_read_context);

		if(b==FALSE)
		{
			errpipe->Write("Error getting metadata of file \""+local_fn+"\". Last error: "+nconvert((int)GetLastError())+"\n");
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

			*buf=0;
			read_bytes = 1;
			return false;
		}

		if(read<metadata_id_size)
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
			b = BackupRead(hFile, reinterpret_cast<LPBYTE>(metadata_buffer.data()+1) + read, curr_stream->dwStreamNameSize, &read, FALSE, TRUE, &backup_read_context);

			if(b==FALSE)
			{
				errpipe->Write("Error getting metadata of file \""+local_fn+"\" (2). Last error: "+nconvert((int)GetLastError())+"\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			if(read<curr_stream->dwStreamNameSize)
			{
				errpipe->Write("Error getting metadata stream structure (name).\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			total_read += read;
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
				errpipe->Write("Error skipping data stream of file \""+local_fn+"\" (1). Last error: "+nconvert((int)GetLastError())+"\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			if(seeked.QuadPart!=curr_stream->Size.QuadPart)
			{
				errpipe->Write("Error skipping data stream of file \""+local_fn+"\" (2). Last error: "+nconvert((int)GetLastError())+"\n");
				*buf=0;
				read_bytes = 1;
				return false;
			}

			return transmitCurrMetadata(buf, buf_avail, read_bytes);
		}

		backup_read_state = 1;
		curr_stream_size = curr_stream->Size;
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
			errpipe->Write("Error reading metadata stream of file \""+local_fn+"\". Last error: "+nconvert((int)GetLastError())+"\n");
			memset(buf, 0, toread);
		}

		read_bytes = read;
		curr_pos+=read;

		if(curr_pos==curr_stream_size.QuadPart)
		{
			backup_read_state = 0;
		}

		return true;
	}

	return false;
}

#else //_WIN32

namespace
{
    void serialize_stat_buf(const struct stat64& buf, CWData& data)
	{
		data.addChar(1);
		data.addInt64(buf.st_dev);
		data.addInt64(buf.st_mode);
		data.addInt64(buf.st_uid);
		data.addInt64(buf.st_gid);
		data.addInt64(buf.st_rdev);
		data.addInt64(buf.st_atime);
        data.addUInt(buf.st_atim.tv_nsec);
		data.addInt64(buf.st_mtime);
        data.addUInt(buf.st_mtim.tv_nsec);
		data.addInt64(buf.st_ctime);
        data.addUInt(buf.st_ctim.tv_nsec);
	}

    bool get_xattr_keys(const std::string& fn, std::vector<std::string>& keys)
	{
		while(true)
		{
			ssize_t bufsize;
            bufsize = llistxattr(fn.c_str(), NULL, 0);

			if(bufsize==-1)
			{
				Server->Log("Error getting extended attribute list of file "+fn+" errno: "+nconvert(errno), LL_ERROR);
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
				Server->Log("Error getting extended attribute list of file "+fn+" errno: "+nconvert(errno)+" (2)", LL_ERROR);
				return false;
			}

			TokenizeMail(buf, keys, "\0");

            for(size_t i=0;i<keys.size();++i)
            {
                unsigned int ksize = keys[i].size();
                ksize = little_endian(ksize);

                keys[i].insert(0, reinterpret_cast<char*>(&ksize), sizeof(ksize));
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
				Server->Log("Error getting extended attribute "+key+" of file "+fn+" errno: "+nconvert(errno), LL_ERROR);
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
				Server->Log("Error getting extended attribute list of file "+fn+" errno: "+nconvert(errno)+" (2)", LL_ERROR);
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
            Server->Log("Error with lstat of "+local_fn+" errorcode: "+nconvert(errno), LL_ERROR);
			return false;
		}

        serialize_stat_buf(statbuf, data);

		if(data.getDataSize()>metadata_buffer.size())
		{
			Server->Log("File metadata of "+local_fn+" too large ("+nconvert((size_t)data.getDataSize())+")", LL_ERROR);
			return false;
		}

		memcpy(metadata_buffer.data(), data.getDataPtr(), data.getDataSize());
		metadata_buffer_size=data.getDataSize();
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
			return true;
		}
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
				return true;
			}
			else
			{
				return false;
			}
		}
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
			backup_state= BackupState_EAttr_Vals_Val;
			
            if(!get_xattr(local_fn, eattr_keys[eattr_idx], eattr_val))
			{
				return false;
			}
			eattr_val_off=0;
			return true;
		}
	}
	else if(backup_state==BackupState_EAttr_Vals_Val)
	{
		if(eattr_val.size()-eattr_val_off>0)
		{
			read_bytes = (std::min)(eattr_val.size()-eattr_val_off, buf_avail);
			memcpy(buf, eattr_val.data()+eattr_val_off, read_bytes);
			eattr_val_off+=read_bytes;
		}

		if(eattr_val.size()-eattr_val_off==0)
		{
			if(eattr_idx+1<eattr_keys.size())
			{
				++eattr_idx;
				backup_state=BackupState_EAttr_Vals_Key;
				eattr_key_off=0;

				return true;
			}
			else
			{
                //finished
                return false;
			}
		}
	}
	
}

#endif //_WIN32

