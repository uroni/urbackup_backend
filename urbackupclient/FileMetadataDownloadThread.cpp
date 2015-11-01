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
#include "../common/data.h"
#include <assert.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/xattr.h>
#endif
#include <memory>

namespace client {

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

			bool is_dir = (curr_fn[0]=='d' || curr_fn[0]=='l');

#ifdef _WIN32
			std::wstring os_path;
#else
            std::wstring os_path=L"/";
#endif
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
	const int64 win32_meta_magic = little_endian(0x320FAB3D119DCB4A);
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

	int64 win32_magic_and_size[2];

	if(metadata_f->Read(reinterpret_cast<char*>(win32_magic_and_size), sizeof(win32_magic_and_size))!=sizeof(win32_magic_and_size))
	{
		restore.log(L"Error reading  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
		has_error=true;
		return false;
	}

	if(win32_magic_and_size[1]!=win32_meta_magic)
	{
		restore.log(L"Win32 metadata magic wrong in  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
		has_error=true;
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

namespace
{
    const int64 unix_meta_magic =  little_endian(0xFE4378A3467647F0ULL);

    void unserialize_stat_buf(CRData& data, struct stat64& statbuf)
    {
#define SET_STAT_MEM(x)  {int64 tmp; assert(data.getInt64(&tmp)); statbuf.x = tmp;}
#define SET_STAT_MEM32(x)  {_u32 tmp; assert(data.getUInt(&tmp)); statbuf.x = tmp;}

        SET_STAT_MEM(st_dev);
        SET_STAT_MEM(st_mode);
        SET_STAT_MEM(st_uid);
        SET_STAT_MEM(st_gid);
        SET_STAT_MEM(st_rdev);
        SET_STAT_MEM(st_atime);
        SET_STAT_MEM32(st_atim.tv_nsec);
        SET_STAT_MEM(st_mtime);
        SET_STAT_MEM32(st_mtim.tv_nsec);
        SET_STAT_MEM(st_ctime);
        SET_STAT_MEM32(st_ctim.tv_nsec);

#undef SET_STAT_MEM
#undef SET_STAT_MEM32
    }

    bool restore_stat_buf(RestoreFiles& restore, struct stat64& statbuf, const std::string& fn)
    {
        bool ret=true;
        if(S_ISLNK(statbuf.st_mode))
        {
            if(lchown(fn.c_str(), statbuf.st_uid, statbuf.st_gid)!=0)
            {
                restore.log("Error setting owner of symlink \""+fn+"\" errno: "+nconvert(errno), LL_ERROR);
                ret = false;
            }

            struct timeval tvs[2];
            TIMESPEC_TO_TIMEVAL(&tvs[0], &(statbuf.st_atim));
            TIMESPEC_TO_TIMEVAL(&tvs[1], &(statbuf.st_mtim));

            if(lutimes(fn.c_str(), tvs)!=0)
            {
                restore.log("Error setting access and modification time of symlink \""+fn+"\" errno: "+nconvert(errno), LL_ERROR);
                ret = false;
            }

            return ret;
        }
        else if(S_ISREG(statbuf.st_mode) || S_ISDIR(statbuf.st_mode) )
        {
            if(chmod(fn.c_str(), statbuf.st_mode)!=0)
            {
                restore.log("Error changing permissions of file \""+fn+"\" errno: "+nconvert(errno), LL_ERROR);
                ret = false;
            }
        }
        else if(S_ISFIFO(statbuf.st_mode) )
        {
            unlink(fn.c_str());

            if(mkfifo(fn.c_str(), statbuf.st_mode)!=0)
            {
                restore.log("Error creating FIFO \""+fn+"\" errno: "+nconvert(errno), LL_ERROR);
                ret = false;
            }
        }
        else
        {
            unlink(fn.c_str());

            if(mknod(fn.c_str(), statbuf.st_mode, statbuf.st_dev)!=0)
            {
                restore.log("Error creating file system node \""+fn+"\" errno: "+nconvert(errno), LL_ERROR);
                ret = false;
            }
        }

        if(chown(fn.c_str(), statbuf.st_uid, statbuf.st_gid)!=0)
        {
            restore.log("Error setting owner of file \""+fn+"\" errno: "+nconvert(errno), LL_ERROR);
            ret = false;
        }

        struct timeval tvs[2];
        TIMESPEC_TO_TIMEVAL(&tvs[0], &(statbuf.st_atim));
        TIMESPEC_TO_TIMEVAL(&tvs[1], &(statbuf.st_mtim));

        if(utimes(fn.c_str(), tvs)!=0)
        {
            restore.log("Error setting access and modification time of file \""+fn+"\" errno: "+nconvert(errno), LL_ERROR);
            ret = false;
        }

        return ret;
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

            return true;
        }
    }

    bool remove_all_xattr(const std::string& fn)
    {
        std::vector<std::string> keys;
        if(!get_xattr_keys(fn, keys))
        {
            return false;
        }

        for(size_t i=0;i<keys.size();++i)
        {
            if(lremovexattr(fn.c_str(), keys[i].c_str())!=0)
            {
                Server->Log("Error removing xattr "+keys[i]+" from "+fn+" errno: "+nconvert(errno), LL_ERROR);
                return false;
            }
        }

        return true;
    }
}

bool FileMetadataDownloadThread::applyOsMetadata( IFile* metadata_f, const std::wstring& output_fn)
{
    int64 unix_magic_and_size[2];

    if(metadata_f->Read(reinterpret_cast<char*>(unix_magic_and_size), sizeof(unix_magic_and_size))!=sizeof(unix_magic_and_size))
    {
        restore.log(L"Error reading  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
        has_error=true;
        return false;
    }

    if(unix_magic_and_size[1]!=unix_meta_magic)
    {
        restore.log(L"Unix metadata magic wrong in  \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
        has_error=true;
        return false;
    }

    char version;
    if(metadata_f->Read(&version, 1)!=1)
    {
        restore.log(L"Error reading unix metadata version from \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
        return false;
    }

    if(version!=1)
    {
        restore.log(L"Unknown unix metadata version +"+convert((int)version)+L" in \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
        return false;
    }

    char stat_data[sizeof(int64)*8+sizeof(_u32)*3];
    if(metadata_f->Read(stat_data, sizeof(stat_data))!=sizeof(stat_data))
    {
        restore.log(L"Error reading unix metadata from \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
        return false;
    }

    CRData read_stat(stat_data, sizeof(stat_data));
    struct stat64 statbuf;
    unserialize_stat_buf(read_stat, statbuf);

    std::string utf8fn = Server->ConvertToUTF8(output_fn);

    if(!restore_stat_buf(restore, statbuf, utf8fn))
    {
        restore.log(L"Error setting unix metadata of "+output_fn, LL_ERROR);
    }

    if(!remove_all_xattr(utf8fn))
    {
        restore.log("Error removing xattrs of "+utf8fn, LL_ERROR);
    }

    int64 num_eattr_keys;
    if(metadata_f->Read(reinterpret_cast<char*>(&num_eattr_keys), sizeof(num_eattr_keys))!=sizeof(num_eattr_keys))
    {
        restore.log(L"Error reading eattr num from \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
        return false;
    }

    num_eattr_keys = little_endian(num_eattr_keys);

    for(int64 i=0;i<num_eattr_keys;++i)
    {
        unsigned int key_size;
        if(metadata_f->Read(reinterpret_cast<char*>(&key_size), sizeof(key_size))!=sizeof(key_size))
        {
            restore.log(L"Error reading eattr key size from \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
            return false;
        }

        key_size = little_endian(key_size);

        std::string eattr_key;
        eattr_key.resize(key_size);

        if(metadata_f->Read(&eattr_key[0], eattr_key.size())!=eattr_key.size())
        {
            restore.log(L"Error reading eattr key from \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
            return false;
        }

        unsigned int val_size;
        if(metadata_f->Read(reinterpret_cast<char*>(&val_size), sizeof(val_size))!=sizeof(val_size))
        {
            restore.log(L"Error reading eattr value size from \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
            return false;
        }

        val_size = little_endian(val_size);

        std::string eattr_val;
        eattr_val.resize(val_size);

        if(metadata_f->Read(&eattr_val[0], eattr_val.size())!=eattr_val.size())
        {
            restore.log(L"Error reading eattr value from \"" + metadata_f->getFilenameW() + L"\"", LL_ERROR);
            return false;
        }

        if(lsetxattr(utf8fn.c_str(), eattr_key.c_str(), eattr_val.data(), eattr_val.size(), 0)!=0)
        {
            restore.log("Error setting xattr "+eattr_key+" of "+utf8fn+" errno: "+nconvert(errno), LL_ERROR);
        }
    }

    return true;
}

#endif //_WIN32

} //namespace client

