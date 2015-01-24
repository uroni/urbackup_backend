#include "FileMetadataPipe.h"
#include "../common/data.h"
#include <assert.h>
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "IFileServ.h"

const size_t metadata_id_size = 4+4+8+4;


FileMetadataPipe::FileMetadataPipe( IPipe* pipe, const std::wstring& cmd )
	: PipeFileBase(cmd), pipe(pipe), hFile(INVALID_HANDLE_VALUE), metadata_state(MetadataState_Wait),
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
	if(buf_avail==0)
	{
		read_bytes = 0;
		return true;
	}

	if(metadata_state==MetadataState_FnSize)
	{
		read_bytes = (std::min)(buf_avail, sizeof(unsigned int) - fn_off);
		unsigned int fn_size = little_endian(static_cast<unsigned int>(public_fn.size()));
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
		memcpy(buf, public_fn.data(), read_bytes);
		fn_off+=read_bytes;

		if(fn_off==public_fn.size())
		{
			metadata_buffer_size = 0;
			metadata_buffer_off = 0;
			if(callback==NULL)
			{
				metadata_state = MetadataState_Os;
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
	else if(metadata_state == MetadataState_Os)
	{
		if(!transmitCurrMetadata(buf, buf_avail, read_bytes))
		{
			metadata_state = MetadataState_Wait;
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

		_u32 read = metadata_file->Read(buf, read_bytes);
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

				bool isdir=false;
				if(isDirectory(os_file_prefix(Server->ConvertToUnicode(local_fn))))
				{
					isdir=true;
					public_fn = "d" + public_fn;
				}
				else
				{
					public_fn = "f" + public_fn;
				}

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

					public_fn = (isdir?"d":"f") + orig_path;
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
		hFile = CreateFileW(Server->ConvertToUnicode(local_fn).c_str(), GENERIC_READ|ACCESS_SYSTEM_SECURITY|READ_CONTROL, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN, NULL);

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

