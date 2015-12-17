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

#include "file_metadata.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#include "../common/data.h"
#include "../fileservplugin/chunk_settings.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include <assert.h>
#include <algorithm>
#include <memory>
#include "chunk_hasher.h"

namespace
{
	const char ID_GRANT_ACCESS=0;
	const char ID_DENY_ACCESS=1;
	
	const unsigned int METADATA_MAGIC=0xA4F04E41;

	

	bool write_metadata(IFile* out, INotEnoughSpaceCallback *cb, const FileMetadata& metadata, int64& written)
	{
		CWData data;
		data.addUInt(METADATA_MAGIC);
		data.addChar(0);
		metadata.serialize(data);
		written=0;

		_u32 metadata_size = static_cast<_u32>(data.getDataSize());

		metadata_size = little_endian(metadata_size);

		if(!writeRepeatFreeSpace(out, reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size), cb))
		{
			Server->Log("Error writing file metadata to file \""+out->getFilename()+"\" -1", LL_ERROR);
			return false;
		}
		written+=sizeof(metadata_size);

		if(!writeRepeatFreeSpace(out, data.getDataPtr(), data.getDataSize(), cb))
		{
			Server->Log("Error writing file metadata to file \""+out->getFilename()+"\"", LL_ERROR);
			return false;
		}

		written+=data.getDataSize();

		return true;
	}

	_u32 get_metadata_size(IFile* in)
	{
		_u32 metadata_size_and_magic[2];
		if(in->Read(reinterpret_cast<char*>(&metadata_size_and_magic), sizeof(metadata_size_and_magic))!=sizeof(metadata_size_and_magic) ||
			metadata_size_and_magic[0]==0)
		{
			Server->Log("Error reading file metadata hashfilesize from \""+in->getFilename()+"\" -2", LL_DEBUG);
			return 0;
		}

		if(little_endian(metadata_size_and_magic[1])!=METADATA_MAGIC)
		{
			Server->Log("Metadata magic wrong in file \""+in->getFilename(), LL_DEBUG);
			return 0;
		}

		return little_endian(metadata_size_and_magic[0]);
	}

	bool read_metadata_values(IFile* in, FileMetadata& metadata)
	{
		_u32 metadata_size_and_magic[2];
		if(in->Read(reinterpret_cast<char*>(&metadata_size_and_magic), sizeof(metadata_size_and_magic))!=sizeof(metadata_size_and_magic) ||
			metadata_size_and_magic[0]==0)
		{
			Server->Log("Error reading file metadata hashfilesize from \""+in->getFilename()+"\" -2", LL_DEBUG);
			return false;
		}
		
		if(little_endian(metadata_size_and_magic[1])!=METADATA_MAGIC)
		{
			Server->Log("Metadata magic wrong in file \""+in->getFilename(), LL_DEBUG);
			return false;
		}

		metadata_size_and_magic[0] = little_endian(metadata_size_and_magic[0]);

		std::vector<char> buffer;
		buffer.resize(metadata_size_and_magic[0]);

		if(in->Read(&buffer[0], metadata_size_and_magic[0]-sizeof(unsigned int))!=metadata_size_and_magic[0]-sizeof(unsigned int))
		{
			Server->Log("Error reading file metadata hashfilesize from \""+in->getFilename()+"\" -3", LL_ERROR);
			return false;
		}

		CRData data(&buffer[0], buffer.size());

		bool ok=true;
		char version = 0;
		ok &= data.getChar(&version);
		ok &= metadata.read(data);

		if(version!=0)
		{
			Server->Log("Unknown metadata version at \""+in->getFilename()+"\"", LL_ERROR);
			return false;
		}

		if(!ok)
		{
			Server->Log("Malformed metadata at \""+in->getFilename()+"\"", LL_ERROR);
			return false;
		}

		return true;
	}	
}

bool write_file_metadata(IFile* out, INotEnoughSpaceCallback *cb, const FileMetadata& metadata, bool overwrite_existing, int64& truncate_to_bytes)
{
	int64 hashfilesize;
	int64 hashdata_size;
	bool needs_truncate=false;
	truncate_to_bytes=-1;

	if(out->Size()==0)
	{
		hashfilesize=little_endian((int64)-1);
		out->Seek(0);

		if(!writeRepeatFreeSpace(out, reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize), cb))
		{
			Server->Log("Error writing file metadata to file \""+out->getFilename()+"\"", LL_ERROR);
			return false;
		}

		hashdata_size = get_hashdata_size(hashfilesize);
	}
	else
	{
		if(!out->Seek(0))
		{
			Server->Log("Error seeking in metadata file \""+out->getFilename()+"\"", LL_ERROR);
			return false;
		}

		if(out->Read(reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize))!=sizeof(hashfilesize))
		{
			Server->Log("Error reading hashfilesize from metadata file \""+out->getFilename()+"\"", LL_ERROR);
			return false;
		}

		hashfilesize=little_endian(hashfilesize);

		hashdata_size = get_hashdata_size(hashfilesize);

		int64 size_should=hashdata_size;

		if(out->Size()!=size_should)
		{
			if(!overwrite_existing)
			{
				Server->Log("File \""+out->getFilename()+"\" has wrong size. Should="+convert(size_should)+" is="+convert(out->Size())+". Error writing metadata to file. -1", LL_WARNING);
				return false;
			}
			else
			{
				if(!out->Seek(hashdata_size))
				{
					Server->Log("Cannot seek to "+convert(hashdata_size)+"in \""+out->getFilename()+"\". Error writing metadata to file.", LL_WARNING);
					return false;
				}
				_u32 metadata_size = get_metadata_size(out);

				size_should+=metadata_size+sizeof(_u32);

				if(out->Size()>size_should)
				{
					if(!out->Seek(size_should))
					{
						Server->Log("Cannot seek to "+convert(size_should)+" (2) in \""+out->getFilename()+"\". Error writing metadata to file.", LL_WARNING);
						return false;
					}

					int64 os_metadata_size;
					if(out->Read(reinterpret_cast<char*>(&os_metadata_size), sizeof(os_metadata_size))!=sizeof(os_metadata_size))
					{
						Server->Log("Error reading os metadata size. Error writing metadata to file.", LL_ERROR);
						return false;
					}

					size_should+=little_endian(os_metadata_size);

					if(out->Size()!=size_should)
					{
						Server->Log("File \""+out->getFilename()+"\" has wrong size. Should="+convert(size_should)+" is="+convert(out->Size())+". Error writing metadata to file. -3", LL_WARNING);
						return false;
					}

					needs_truncate=true;
				}
				else if(out->Size()!=size_should)
				{
					Server->Log("File \""+out->getFilename()+"\" has wrong size. Should="+convert(size_should)+" is="+convert(out->Size())+". Error writing metadata to file. -2", LL_WARNING);
					return false;
				}
			}
		}

		if(!out->Seek(hashdata_size))
		{
			Server->Log("Cannot seek to "+convert(hashdata_size)+"in \""+out->getFilename()+"\". Error writing metadata to file. -2", LL_WARNING);
			return false;
		}
	}
		

	int64 written;
	bool ret=write_metadata(out, cb, metadata, written);

	if(needs_truncate)
	{
		truncate_to_bytes = hashdata_size+written;
	}

	return ret;
}

bool write_file_metadata(const std::string& out_fn, INotEnoughSpaceCallback *cb, const FileMetadata& metadata, bool overwrite_existing)
{
	std::auto_ptr<IFile> out(Server->openFile(os_file_prefix(out_fn), MODE_RW_CREATE));

	if(!out.get())
	{
		Server->Log("Error writing file metadata to file \""+out_fn+"\"", LL_ERROR);
		return false;
	}

	int64 truncate_to_bytes;
	bool ret = write_file_metadata(out.get(), cb, metadata, overwrite_existing, truncate_to_bytes);

	if(ret && truncate_to_bytes>0)
	{
		out.reset();
		return os_file_truncate(out_fn, truncate_to_bytes);
	}

	return ret;
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

bool read_metadata(const std::string& in_fn, FileMetadata& metadata)
{
	std::auto_ptr<IFile> in(Server->openFile(os_file_prefix(in_fn)));

	if(!in.get())
	{
		Server->Log("Error reading file metadata from file \""+in_fn+"\"", LL_DEBUG);
		return false;
	}

	return read_metadata(in.get(), metadata);
}

bool read_metadata(IFile* in, FileMetadata& metadata)
{
	int64 hashfilesize;

	in->Seek(0);
	if(in->Read(reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize))!=sizeof(hashfilesize))
	{
		Server->Log("Error reading file metadata hashfilesize from \""+in->getFilename()+"\"", LL_DEBUG);
		return false;
	}

	hashfilesize=little_endian(hashfilesize);

	if(hashfilesize!=-1)
	{
		in->Seek(get_hashdata_size(hashfilesize));
	}

	return read_metadata_values(in, metadata);
}

bool has_metadata( const std::string& in_fn, const FileMetadata& metadata )
{
	FileMetadata r_metadata;
	if(!read_metadata(in_fn, r_metadata))
	{
		return false;
	}

	return r_metadata==metadata;
}


std::string escape_metadata_fn( const std::string& fn )
{
	if(fn.find(metadata_dir_fn)==0)
	{
		std::string num_str = fn.substr(sizeof(metadata_dir_fn)/sizeof(metadata_dir_fn[0])-1);
		if(num_str.empty())
		{
			return fn+"0";
		}

		size_t num_num = std::count_if(num_str.begin(), num_str.end(), str_isnumber);
		if(num_num!=num_str.size())
		{
			return fn;
		}

		return std::string(metadata_dir_fn)+convert(watoi64(num_str)+1);
	}
	else
	{
		return fn;
	}
}

std::string unescape_metadata_fn( const std::string& fn )
{
	if(fn.find(metadata_dir_fn)==0)
	{
		std::string num_str = fn.substr(sizeof(metadata_dir_fn)/sizeof(metadata_dir_fn[0])-1);
		if(num_str.empty())
		{
			return fn;
		}

		size_t num_num = std::count_if(num_str.begin(), num_str.end(), str_isnumber);
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
			return std::string(metadata_dir_fn)+convert(c);
		}
	}
	else
	{
		return fn;
	}
}

int64 os_metadata_offset( IFile* meta_file )
{
	int64 hashfilesize;

	meta_file->Seek(0);
	if(meta_file->Read(reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize))!=sizeof(hashfilesize))
	{
		Server->Log("Error reading file metadata hashfilesize from \""+meta_file->getFilename()+"\"", LL_DEBUG);
		return -1;
	}

	hashfilesize=little_endian(hashfilesize);

	int64 meta_offset = 0;

	if(hashfilesize!=-1)
	{
		meta_offset = get_hashdata_size(hashfilesize);
		meta_file->Seek(meta_offset);
	}
	else
	{
		meta_offset=sizeof(int64);
	}

	if(!meta_file->Seek(meta_offset))
	{
		Server->Log("Error seeking to metadata in \""+meta_file->getFilename()+"\"", LL_DEBUG);
		return -1;
	}

	_u32 metadata_size_and_magic[2];
	if(meta_file->Read(reinterpret_cast<char*>(&metadata_size_and_magic), sizeof(metadata_size_and_magic))!=sizeof(metadata_size_and_magic) ||
		metadata_size_and_magic[0]==0)
	{
		Server->Log("Error reading file metadata hashfilesize from \""+meta_file->getFilename()+"\" -2", LL_DEBUG);
		return -1;
	}

	if(little_endian(metadata_size_and_magic[1])!=METADATA_MAGIC)
	{
		Server->Log("Metadata magic wrong in file \""+meta_file->getFilename(), LL_DEBUG);
		return -1;
	}

	return meta_offset + sizeof(_u32) + little_endian(metadata_size_and_magic[0]);
}

bool copy_os_metadata( const std::string& in_fn, const std::string& out_fn, INotEnoughSpaceCallback *cb)
{
	std::auto_ptr<IFile> in_f(Server->openFile(os_file_prefix(in_fn), MODE_READ));
    std::auto_ptr<IFile> out_f(Server->openFile(os_file_prefix(out_fn), MODE_RW));

	if(in_f.get()==NULL)
	{
		Server->Log("Error opening metadata file \""+in_fn+"\" (input)", LL_ERROR);
		return false;
	}

	if(out_f.get()==NULL)
	{
		Server->Log("Error opening metadata file \""+out_fn+"\" (output)", LL_ERROR);
		return false;
	}

	int64 offset = os_metadata_offset(in_f.get());

	if(offset==-1)
	{
		return false;
	}

	if(!in_f->Seek(offset))
	{
		Server->Log("Error seeking to os metadata in \""+in_fn+"\" (input)", LL_ERROR);
		return false;
	}

	if(!out_f->Seek(out_f->Size()))
	{
		Server->Log("Error seeking to os metadata in \""+out_fn+"\" (output)", LL_ERROR);
		return false;
	}

	std::vector<char> buffer;
	buffer.resize(32768);

	_u32 read;
	do 
	{
		read = in_f->Read(buffer.data(), static_cast<_u32>(buffer.size()));

		if(!writeRepeatFreeSpace(out_f.get(), buffer.data(), read, cb))
		{
			Server->Log("Error while writing os metadata to \""+out_fn+"\" (output)", LL_ERROR);
			return false;
		}

	} while (read>0);

	return true;
}

int64 read_hashdata_size( IFile* meta_file )
{
	int64 hashfilesize;

	meta_file->Seek(0);
	if(meta_file->Read(reinterpret_cast<char*>(&hashfilesize), sizeof(hashfilesize))!=sizeof(hashfilesize))
	{
		Server->Log("Error reading file metadata hashfilesize from \""+meta_file->getFilename()+"\"", LL_DEBUG);
		return -1;
	}

	return little_endian(hashfilesize);
}

void FileMetadata::serialize( CWData& data ) const
{
	data.addString(shahash);
	data.addString(orig_path);
    data.addString(file_permissions);
	data.addVarInt(last_modified);
	data.addVarInt(created);
	data.addVarInt(accessed);
}

bool FileMetadata::read( CRData& data )
{
	bool ok=true;
	ok &= data.getStr(&shahash);
	ok &= data.getStr(&orig_path);
    ok &= data.getStr(&file_permissions);
	ok &= data.getVarInt(&last_modified);
	ok &= data.getVarInt(&created);
	ok &= data.getVarInt(&accessed);

	if(ok)
	{
		exist=true;
	}

	return ok;
}

bool FileMetadata::read( str_map& extra_params )
{
	str_map::iterator it_orig_path = extra_params.find("orig_path");
	if(it_orig_path!=extra_params.end())
	{
		orig_path = (it_orig_path->second);
		has_orig_path=true;
	}

    str_map::iterator it_shahash = extra_params.find("sha512");
    if(it_shahash!=extra_params.end())
    {
        shahash = base64_decode_dash(it_shahash->second);
    }

	if(has_orig_path || !shahash.empty())
	{
		exist=true;
	}

	return true;
}

bool FileMetadata::hasPermission(int64 id, bool& denied) const
{
	return hasPermission(file_permissions, id, denied);
}

bool FileMetadata::hasPermission( const std::string& permissions, int64 id, bool& denied )
{
	CRData perm(permissions.data(),
		permissions.size());

	char action;
	while(perm.getChar(&action))
	{
		int64 pid;
		if(!perm.getVarInt(&pid))
		{
			return false;
		}

		switch(action)
		{
		case ID_GRANT_ACCESS:
			if(pid==0 || pid==id)
			{
				return true;
			}
			break;
		case ID_DENY_ACCESS:
			if(pid==0 || pid==id)
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

void FileMetadata::set_orig_path( const std::string& the_orig_path )
{
	orig_path = the_orig_path;
}
