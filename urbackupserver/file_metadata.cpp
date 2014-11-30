/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "file_metadata.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#include "server_prepare_hash.h"
#include "../common/data.h"
#include "../fileservplugin/chunk_settings.h"
#include <assert.h>
#include <algorithm>
#include <memory>

namespace
{
	const char ID_GRANT_ACCESS=0;
	const char ID_DENY_ACCESS=1;
	
	const unsigned int METADATA_MAGIC=0xA4F04E41;

	int64 get_hashdata_size(int64 hashfilesize)
	{
		int64 num_chunks = hashfilesize/c_checkpoint_dist;
		int64 size = chunkhash_file_off+num_chunks*chunkhash_single_size;
		if(hashfilesize%c_checkpoint_dist!=0)
		{
			size+=big_hash_size + ((hashfilesize%c_checkpoint_dist)/c_chunk_size)*small_hash_size
					+ ((((hashfilesize%c_checkpoint_dist)%c_chunk_size)!=0)?small_hash_size:0);
		}
		return size;
	}

	bool write_metadata(IFile* out, INotEnoughSpaceCallback *cb, const FileMetadata& metadata)
	{
		CWData data;
		data.addUInt(METADATA_MAGIC);
		data.addChar(0);
		metadata.serialize(data);

		_u32 metadata_size = static_cast<_u32>(data.getDataSize());

		metadata_size = little_endian(metadata_size);

		if(!BackupServerPrepareHash::writeRepeatFreeSpace(out, reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size), cb))
		{
			Server->Log(L"Error writing file metadata to file \""+out->getFilenameW()+L"\" -1", LL_ERROR);
			return false;
		}

		if(!BackupServerPrepareHash::writeRepeatFreeSpace(out, data.getDataPtr(), data.getDataSize(), cb))
		{
			Server->Log(L"Error writing file metadata to file \""+out->getFilenameW()+L"\"", LL_ERROR);
			return false;
		}

		return true;
	}

	bool read_metadata_values(IFile* in, FileMetadata& metadata)
	{
		_u32 metadata_size_and_magic[2];
		if(in->Read(reinterpret_cast<char*>(&metadata_size_and_magic), sizeof(metadata_size_and_magic))!=sizeof(metadata_size_and_magic) ||
			metadata_size_and_magic[0]==0)
		{
			Server->Log(L"Error reading file metadata hashfilesize from \""+in->getFilenameW()+L"\" -2", LL_DEBUG);
			return false;
		}
		
		if(metadata_size_and_magic[1]!=METADATA_MAGIC)
		{
			Server->Log(L"Metadata magic wrong in file \""+in->getFilenameW(), LL_DEBUG);
			return false;
		}		

		std::vector<char> buffer;
		buffer.resize(metadata_size_and_magic[0]);

		if(in->Read(&buffer[0], metadata_size_and_magic[0]-sizeof(unsigned int))!=metadata_size_and_magic[0]-sizeof(unsigned int))
		{
			Server->Log(L"Error reading file metadata hashfilesize from \""+in->getFilenameW()+L"\" -3", LL_ERROR);
			return false;
		}

		CRData data(&buffer[0], buffer.size());

		bool ok=true;
		char version = 0;
		ok &= data.getChar(&version);
		ok &= metadata.read(data);

		if(version!=0)
		{
			Server->Log(L"Unknown metadata version at \""+in->getFilenameW()+L"\"", LL_ERROR);
			return false;
		}

		if(!ok)
		{
			Server->Log(L"Malformed metadata at \""+in->getFilenameW()+L"\"", LL_ERROR);
			return false;
		}

		return true;
	}

	bool read_metadata(IFile* in, FileMetadata& metadata)
	{
		int64 hashfilesize;

		in->Seek(0);
		if(in->Read(reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize))!=sizeof(hashfilesize))
		{
			Server->Log(L"Error reading file metadata hashfilesize from \""+in->getFilenameW()+L"\"", LL_DEBUG);
			return false;
		}

		hashfilesize=little_endian(hashfilesize);

		if(hashfilesize!=-1)
		{
			in->Seek(get_hashdata_size(hashfilesize));
		}

		return read_metadata_values(in, metadata);
	}

	
}

bool write_file_metadata(IFile* out, INotEnoughSpaceCallback *cb, const FileMetadata& metadata)
{
	int64 hashfilesize;

	out->Seek(0);
	if(out->Read(reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize))!=sizeof(hashfilesize))
	{
		hashfilesize=little_endian((int64)-1);
		out->Seek(0);
	}
	else
	{
		hashfilesize=little_endian(hashfilesize);

		int64 hashdata_size = get_hashdata_size(hashfilesize);

		if(out->Size()!=hashdata_size)
		{
			Server->Log(L"File \""+out->getFilenameW()+L"\" has wrong size. Should="+convert(hashdata_size)+L" is="+convert(out->Size())+L". Error writing metadata to file.", LL_WARNING);
			return false;
		}

		out->Seek(hashdata_size);
	}

	if(!BackupServerPrepareHash::writeRepeatFreeSpace(out, reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize), cb))
	{
		Server->Log(L"Error writing file metadata to file \""+out->getFilenameW()+L"\"", LL_ERROR);
		return false;
	}

	return write_metadata(out, cb, metadata);
}

bool write_file_metadata(const std::wstring& out_fn, INotEnoughSpaceCallback *cb, const FileMetadata& metadata)
{
	std::auto_ptr<IFile> out(Server->openFile(os_file_prefix(out_fn), MODE_RW_CREATE));

	if(!out.get())
	{
		Server->Log(L"Error writing file metadata to file \""+out_fn+L"\"", LL_ERROR);
		return false;
	}

	return write_file_metadata(out.get(), cb, metadata);
}

bool is_metadata_only(IFile* hash_file)
{
	int64 hashfilesize;

	hash_file->Seek(0);
	if(hash_file->Read(reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize))!=sizeof(hashfilesize))
	{
		return false;
	}

	return hashfilesize==-1;
}

bool read_metadata(const std::wstring& in_fn, FileMetadata& metadata)
{
	std::auto_ptr<IFile> in(Server->openFile(os_file_prefix(in_fn)));

	if(!in.get())
	{
		Server->Log(L"Error reading file metadata from file \""+in_fn+L"\"", LL_DEBUG);
		return false;
	}

	return read_metadata(in.get(), metadata);
}

bool has_metadata( const std::wstring& in_fn, const FileMetadata& metadata )
{
	FileMetadata r_metadata;
	if(!read_metadata(in_fn, r_metadata))
	{
		return false;
	}

	return r_metadata==metadata;
}


std::wstring escape_metadata_fn( const std::wstring& fn )
{
	if(fn.find(metadata_dir_fn)==0)
	{
		std::wstring num_str = fn.substr(sizeof(metadata_dir_fn)/sizeof(metadata_dir_fn[0])-1);
		if(num_str.empty())
		{
			return fn+L"0";
		}

		size_t num_num = std::count_if(num_str.begin(), num_str.end(), static_cast<bool(*)(wchar_t)>(str_isnumber));
		if(num_num!=num_str.size())
		{
			return fn;
		}

		return std::wstring(metadata_dir_fn)+convert(watoi64(num_str)+1);
	}
	else
	{
		return fn;
	}
}

std::wstring unescape_metadata_fn( const std::wstring& fn )
{
	if(fn.find(metadata_dir_fn)==0)
	{
		std::wstring num_str = fn.substr(sizeof(metadata_dir_fn)/sizeof(metadata_dir_fn[0])-1);
		if(num_str.empty())
		{
			return fn;
		}

		size_t num_num = std::count_if(num_str.begin(), num_str.end(), static_cast<bool(*)(wchar_t)>(str_isnumber));
		if(num_num!=num_str.size())
		{
			return fn;
		}

		int64 c = watoi64(num_str)-1;

		if(c==0)
		{
			return metadata_dir_fn;
		}
		else
		{
			return std::wstring(metadata_dir_fn)+convert(c);
		}
	}
	else
	{
		return fn;
	}
}

void FileMetadata::serialize( CWData& data ) const
{
	data.addString(file_permissions);
	data.addInt64(last_modified);
	data.addInt64(created);
	data.addString(shahash);
}

bool FileMetadata::read( CRData& data )
{
	bool ok=true;
	ok &= data.getStr(&file_permissions);
	ok &= data.getInt64(&last_modified);
	ok &= data.getInt64(&created);
	ok &= data.getStr(&shahash);

	if(ok && last_modified>0 && created>0)
	{
		exist=true;
	}

	return ok;
}

bool FileMetadata::read( str_map& extra_params )
{
	file_permissions = base64_decode_dash(wnarrow(extra_params[L"dacl"]));
	last_modified = watoi64(extra_params[L"mod"]);
	created = watoi64(extra_params[L"creat"]);

	if(last_modified>0 && created>0)
	{
		exist=true;
	}

	return true;
}

bool FileMetadata::hasPermission(int id, bool& denied) const
{
	CRData perm(file_permissions.data(),
		file_permissions.size());

	char action;
	while(perm.getChar(&action))
	{
		int pid;
		if(!perm.getInt(&pid))
		{
			return false;
		}

		switch(action)
		{
		case ID_GRANT_ACCESS:
			if(pid==-1 || pid==id)
			{
				return true;
			}
			break;
		case ID_DENY_ACCESS:
			if(pid==-1 || pid==id)
			{
				denied=true;
				return false;
			}
			break;
		}
	}

	return false;
}

void FileMetadata::set_shahash( const std::string& the_shahash )
{
	shahash=the_shahash;
}
