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

#include "FileMetadataDownloadThread.h"
#include "../urbackupcommon/file_metadata.h"
#include "../urbackupcommon/os_functions.h"
#include <assert.h>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <memory>

const _u32 ID_METADATA_OS_WIN = 1<<0;
const _u32 ID_METADATA_OS_UNIX = 1<<2;

const _u32 ID_METADATA_NOP = 0;
const _u32 ID_METADATA_V1 = 1<<3;
#ifdef _WIN32
const _u32 ID_METADATA_OS = ID_METADATA_OS_WIN;
#else
const _u32 ID_METADATA_OS = ID_METADATA_OS_UNIX;
#endif

FileMetadataDownloadThread::FileMetadataDownloadThread(RestoreFiles& restore, FileClient& fc, const std::string& client_token)
	: restore(restore), fc(fc), client_token(client_token), has_error(false)
{

}

void FileMetadataDownloadThread::operator()()
{
	std::auto_ptr<IFile> tmp_f(Server->openTemporaryFile());

	if(tmp_f.get()==NULL)
	{
		restore.log("Error creating temporary file for metadata", LL_ERROR);
	}
	
	std::string remote_fn = "SCRIPT|urbackup/FILE_METADATA|"+client_token;

	_u32 rc = fc.GetFile(remote_fn, tmp_f.get(), true, false);

	if(rc!=ERR_SUCCESS)
	{
		restore.log(L"Error getting file metadata. Errorcode: "+widen(FileClient::getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
		has_error=true;
	}
	else
	{
		has_error=false;
	}

	metadata_tmp_fn = tmp_f->getFilenameW();
}

bool FileMetadataDownloadThread::applyMetadata()
{
	buffer.resize(32768);
	std::auto_ptr<IFile> metadata_f(Server->openFile(metadata_tmp_fn, MODE_READ_SEQUENTIAL));

	if(metadata_f.get()==NULL)
	{
		restore.log(L"Error opening metadata file. Cannot save file metadata.", LL_ERROR);
		return false;
	}

	restore.log(L"Applying file metadata...", LL_INFO);

	do 
	{
		char ch;
		if(metadata_f->Read(reinterpret_cast<char*>(&ch), sizeof(ch))!=sizeof(ch))
		{
			return true;
		}

		if(ch==ID_METADATA_NOP)
		{
			continue;
		}
		
		if(ch & ID_METADATA_V1)
		{
			unsigned int curr_fn_size =0;
			if(metadata_f->Read(reinterpret_cast<char*>(&curr_fn_size), sizeof(curr_fn_size))!=sizeof(curr_fn_size))
			{
				restore.log(L"Error saving metadata. Filename size could not be read.", LL_ERROR);
				return false;
			}

			std::string curr_fn;

			if(curr_fn_size>0)
			{			
				curr_fn.resize(little_endian(curr_fn_size));

				if(metadata_f->Read(&curr_fn[0], static_cast<_u32>(curr_fn.size()))!=curr_fn.size())
				{
					restore.log(L"Error saving metadata. Filename could not be read.", LL_ERROR);
					return false;
				}
			}

			if(curr_fn.empty())
			{
				restore.log(L"Error saving metadata. Filename is empty.", LL_ERROR);
				return false;
			}

			restore.log("Applying metadata of file \"" + curr_fn + "\"", LL_DEBUG);

			bool is_dir = curr_fn[0]=='d';

			std::wstring os_path;
			std::vector<std::string> fs_toks;
			TokenizeMail(curr_fn.substr(1), fs_toks, "/");

			for(size_t i=0;i<fs_toks.size();++i)
			{
				if(fs_toks[i]!="." && fs_toks[i]!="..")
				{
					if(!os_path.empty())
						os_path+=os_file_sep();

					os_path += Server->ConvertToUnicode(fs_toks[i]);				
				}
			}

			bool ok=false;
			if(ch & ID_METADATA_OS)
			{
				ok = applyOsMetadata(metadata_f.get(), os_path);
			}
			else
			{
				restore.log("Wrong metadata. This metadata is not for this operating system! (id=" + nconvert(ch)+")", LL_ERROR);
			}

			if(!ok)
			{
				restore.log(L"Error saving metadata. Could not save OS specific metadata to \"" + os_path + L"\"", LL_ERROR);
				return false;
			}			
		}

	} while (true);

	assert(false);
	return true;
}

#ifdef _WIN32
namespace
{
	struct WIN32_STREAM_ID_INT
	{
		_u32 dwStreamId;
		_u32 dwStreamAttributes;
		int64 Size;
		_u32 dwStreamNameSize;
	};

	const size_t metadata_id_size = 4+4+8+4;
}

bool FileMetadataDownloadThread::applyOsMetadata( IFile* metadata_f, const std::wstring& output_fn)
{
	HANDLE hFile = CreateFileW(os_file_prefix(output_fn).c_str(), GENERIC_WRITE|ACCESS_SYSTEM_SECURITY|WRITE_OWNER|WRITE_DAC, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN, NULL);

	if(hFile==INVALID_HANDLE_VALUE)
	{
		restore.log(L"Cannot open handle to restore file metadata of file \""+output_fn+L"\"", LL_ERROR);
		return false;
	}

	bool has_error=false;
	void* context = NULL;

	while(true) 
	{
		char cont = 0;
		if(metadata_f->Read(&cont, sizeof(cont))!=sizeof(cont))
		{
			restore.log(L"Error reading  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
			has_error=true;
			break;
		}

		if(cont==0)
		{
			break;
		}

		std::vector<char> stream_id;
		stream_id.resize(metadata_id_size);

		if(metadata_f->Read(stream_id.data(), metadata_id_size)!=metadata_id_size)
		{
			restore.log(L"Error reading  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
			has_error=true;
			break;
		}

		WIN32_STREAM_ID_INT* curr_stream_id =
			reinterpret_cast<WIN32_STREAM_ID_INT*>(stream_id.data());

		if(curr_stream_id->dwStreamNameSize>0)
		{
			stream_id.resize(stream_id.size()+curr_stream_id->dwStreamNameSize);

			if(metadata_f->Read(stream_id.data() + metadata_id_size, static_cast<_u32>(curr_stream_id->dwStreamNameSize))!=curr_stream_id->dwStreamNameSize)
			{
				restore.log(L"Error reading  \"" + metadata_f->getFilenameW() + L"\" -2", LL_ERROR);
				has_error=true;
				break;
			}
		}	

		DWORD written=0;
		BOOL b=BackupWrite(hFile, reinterpret_cast<LPBYTE>(stream_id.data()), static_cast<DWORD>(stream_id.size()), &written, FALSE, TRUE, &context);

		if(!b || written!=stream_id.size())
		{
			restore.log(L"Error writting metadata to file \""+output_fn+L"\". Last error: "+convert((int)GetLastError()), LL_ERROR);
			has_error=true;
			break;
		}

		int64 curr_pos=0;

		while(curr_pos<curr_stream_id->Size)
		{
			_u32 toread = static_cast<_u32>((std::min)(static_cast<int64>(buffer.size()), curr_stream_id->Size-curr_pos));

			if(metadata_f->Read(buffer.data(), toread)!=toread)
			{
				restore.log(L"Error reading  \"" + metadata_f->getFilenameW() + L"\" -3", LL_ERROR);
				has_error=true;
				break;
			}

			DWORD written=0;
			BOOL b=BackupWrite(hFile, reinterpret_cast<LPBYTE>(buffer.data()), toread, &written, FALSE, TRUE, &context);

			if(!b || written!=toread)
			{
				restore.log(L"Error writting metadata to file \""+output_fn+L"\". Last error: "+convert((int)GetLastError()), LL_ERROR);
				has_error=true;
				break;
			}

			curr_pos+=toread;
		}
	}

	if(context!=NULL)
	{
		DWORD written;
		BackupWrite(hFile, NULL, 0, &written, TRUE, TRUE, &context);
	}

	CloseHandle(hFile);
	return !has_error;
}

#else //_WIN32

bool FileMetadataDownloadThread::applyOsMetadata( IFile* metadata_f, const std::wstring& output_fn)
{
	//TODO: Implement
	assert(false);
	return false;
}

#endif //_WIN32

