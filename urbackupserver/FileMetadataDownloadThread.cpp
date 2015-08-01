#include "FileMetadataDownloadThread.h"
#include "ClientMain.h"
#include "server_log.h"
#include "../urbackupcommon/file_metadata.h"
#include "../common/data.h"

const _u32 ID_METADATA_OS_WIN = 1<<0;
const _u32 ID_METADATA_OS_UNIX = 1<<2;
const _u32 ID_METADATA_NOP = 0;
const _u32 ID_METADATA_V1 = 1<<3;

FileMetadataDownloadThread::FileMetadataDownloadThread( FileClient& fc, const std::string& server_token, logid_t logid)
	: fc(fc), server_token(server_token), logid(logid), has_error(false)
{

}

void FileMetadataDownloadThread::operator()()
{
	std::auto_ptr<IFile> tmp_f(ClientMain::getTemporaryFileRetry(true, std::wstring(), logid));
	
	std::string remote_fn = "SCRIPT|urbackup/FILE_METADATA|"+server_token;

	_u32 rc = fc.GetFile(remote_fn, tmp_f.get(), true, false);

	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, L"Error getting file metadata. Errorcode: "+widen(FileClient::getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
		has_error=true;
	}
	else
	{
		has_error=false;
	}

	metadata_tmp_fn = tmp_f->getFilenameW();
}

bool FileMetadataDownloadThread::applyMetadata( const std::wstring& backupdir, INotEnoughSpaceCallback *cb)
{
	buffer.resize(32768);
	std::auto_ptr<IFile> metadata_f(Server->openFile(metadata_tmp_fn, MODE_READ_SEQUENTIAL));

	if(metadata_f.get()==NULL)
	{
		ServerLogger::Log(logid, L"Error opening metadata file. Cannot save file metadata.", LL_ERROR);
		return false;
	}

	ServerLogger::Log(logid, L"Saving file metadata...", LL_INFO);

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
				ServerLogger::Log(logid, L"Error saving metadata. Filename size could not be read.", LL_ERROR);
				return false;
			}

			std::string curr_fn;
			curr_fn.resize(little_endian(curr_fn_size));

			if(metadata_f->Read(&curr_fn[0], static_cast<_u32>(curr_fn.size()))!=curr_fn.size())
			{
				ServerLogger::Log(logid, L"Error saving metadata. Filename could not be read.", LL_ERROR);
				return false;
			}

			if(curr_fn.empty())
			{
				ServerLogger::Log(logid, L"Error saving metadata. Filename is empty.", LL_ERROR);
				return false;
			}

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

					if(i==fs_toks.size()-1)
					{
						os_path += escape_metadata_fn(Server->ConvertToUnicode(fs_toks[i]));

						if(is_dir)
						{
							os_path+=os_file_sep()+metadata_dir_fn;
						}
					}
					else
					{
						os_path += Server->ConvertToUnicode(fs_toks[i]);
					}					
				}
			}

			std::auto_ptr<IFile> output_f(Server->openFile(os_file_prefix(backupdir+os_file_sep()+os_path), MODE_RW));

			if(output_f.get()==NULL)
			{
				ServerLogger::Log(logid, L"Error saving metadata. Filename could not open output file at \"" + backupdir+os_file_sep()+os_path + L"\"", LL_ERROR);
				return false;
			}

			unsigned int common_metadata_size =0;
			if(metadata_f->Read(reinterpret_cast<char*>(&common_metadata_size), sizeof(common_metadata_size))!=sizeof(common_metadata_size))
			{
				ServerLogger::Log(logid, L"Error saving metadata. Common metadata size could not be read.", LL_ERROR);
				return false;
			}

			std::vector<char> common_metadata;
			common_metadata.resize(common_metadata_size);

			if(metadata_f->Read(&common_metadata[0], static_cast<_u32>(common_metadata.size()))!=common_metadata.size())
			{
				ServerLogger::Log(logid, L"Error saving metadata. Common metadata could not be read.", LL_ERROR);
				return false;
			}

			CRData common_data(common_metadata.data(), common_metadata.size());
			char common_version;
			common_data.getChar(&common_version);
			int64 created;
			int64 modified;
			std::string permissions;
			if(common_version!=1
				|| !common_data.getInt64(&created)
				|| !common_data.getInt64(&modified)
				|| !common_data.getStr(&permissions) )
			{
				ServerLogger::Log(logid, L"Error saving metadata. Cannot parse common metadata.", LL_ERROR);
				return false;
			}

			FileMetadata curr_metadata;
			if(!read_metadata(output_f.get(), curr_metadata))
			{
				ServerLogger::Log(logid, L"Error reading current metadata", LL_WARNING);
			}

			curr_metadata.created=created;
			curr_metadata.last_modified = modified;
			curr_metadata.file_permissions = permissions;

			if(!write_file_metadata(output_f.get(), cb, curr_metadata))
			{
				ServerLogger::Log(logid, L"Error saving metadata. Cannot write common metadata.", LL_ERROR);
				return false;
			}

			int64 offset = os_metadata_offset(output_f.get());

			if(offset==-1)
			{
				ServerLogger::Log(logid, L"Error saving metadata. Metadata offset cannot be calculated at \"" + backupdir+os_file_sep()+os_path + L"\"", LL_ERROR);
				return false;
			}

			if(!output_f->Seek(offset))
			{
				ServerLogger::Log(logid, L"Error saving metadata. Could not seek to end of file \"" + backupdir+os_file_sep()+os_path + L"\"", LL_ERROR);
				return false;
			}

			int64 metadata_size=0;
			bool ok=false;
			if(ch & ID_METADATA_OS_WIN)
			{
				ok = applyWindowsMetadata(metadata_f.get(), output_f.get(), metadata_size, cb);
			}

			if(!ok)
			{
				ServerLogger::Log(logid, L"Error saving metadata. Could not save OS specific metadata to \"" + backupdir+os_file_sep()+os_path + L"\"", LL_ERROR);
				return false;
			}
		}

	} while (true);

	assert(false);
	return true;
}

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

bool FileMetadataDownloadThread::applyWindowsMetadata( IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb)
{
	metadata_size=0;
	while(true) 
	{
		char cont = 0;
		if(metadata_f->Read(reinterpret_cast<char*>(&cont), sizeof(cont))!=sizeof(cont))
		{
			ServerLogger::Log(logid, L"Error reading  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
			return false;
		}

		if(!writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&cont), sizeof(cont), cb))
		{
			ServerLogger::Log(logid, L"Error writing to  \"" + output_f->getFilenameW() + L"\" (cont)", LL_ERROR);
			return false;
		}
		++metadata_size;

		if(cont==0)
		{
			return true;
		}
	
		WIN32_STREAM_ID_INT stream_id;

		if(metadata_f->Read(reinterpret_cast<char*>(&stream_id), metadata_id_size)!=metadata_id_size)
		{
			ServerLogger::Log(logid, L"Error reading  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
			return false;
		}

		if(!writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&stream_id), metadata_id_size, cb))
		{
			ServerLogger::Log(logid, L"Error writing to  \"" + output_f->getFilenameW() + L"\"", LL_ERROR);
			return false;
		}

		metadata_size+=metadata_id_size;

		if(stream_id.dwStreamNameSize>0)
		{
			std::vector<char> stream_name;
			stream_name.resize(stream_id.dwStreamNameSize);

			if(metadata_f->Read(stream_name.data(), static_cast<_u32>(stream_name.size()))!=stream_name.size())
			{
				ServerLogger::Log(logid, L"Error reading  \"" + metadata_f->getFilenameW() + L"\" -2", LL_ERROR);
				return false;
			}

			if(!writeRepeatFreeSpace(output_f, stream_name.data(), stream_name.size(), cb))
			{
				ServerLogger::Log(logid, L"Error writing to  \"" + output_f->getFilenameW() + L"\" -2", LL_ERROR);
				return false;
			}

			metadata_size+=stream_name.size();
		}	

		int64 curr_pos=0;

		while(curr_pos<stream_id.Size)
		{
			_u32 toread = static_cast<_u32>((std::min)(static_cast<int64>(buffer.size()), stream_id.Size-curr_pos));

			if(metadata_f->Read(buffer.data(), toread)!=toread)
			{
				ServerLogger::Log(logid, L"Error reading  \"" + metadata_f->getFilenameW() + L"\" -3", LL_ERROR);
				return false;
			}

			if(!writeRepeatFreeSpace(output_f, buffer.data(), toread, cb))
			{
				ServerLogger::Log(logid, L"Error writing to  \"" + output_f->getFilenameW() + L"\" -3", LL_ERROR);
				return false;
			}

			metadata_size+=toread;

			curr_pos+=toread;
		}
	}

	return true;
}

