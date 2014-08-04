#include "file_metadata.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#include "server_prepare_hash.h"
#include "../common/data.h"
#include "../fileservplugin/chunk_settings.h"
extern "C"
{
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.c"
};
#include <assert.h>
#include <algorithm>

namespace
{
	int64 get_hashdata_size(int64 hashfilesize)
	{
		int64 num_chunks = hashfilesize/c_checkpoint_dist+((hashfilesize%c_checkpoint_dist!=0)?1:0);
		return chunkhash_file_off+num_chunks*chunkhash_single_size;
	}

	bool write_metadata(IFile* out, INotEnoughSpaceCallback *cb, const FileMetadata& metadata)
	{
		CWData data;
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
		_u32 metadata_size;
		if(in->Read(reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size))!=sizeof(metadata_size) ||
			metadata_size==0)
		{
			Server->Log(L"Error reading file metadata hashfilesize from \""+in->getFilenameW()+L"\" -2", LL_DEBUG);
			return false;
		}

		metadata_size = little_endian(metadata_size);

		std::vector<char> buffer;
		buffer.resize(metadata_size);

		if(in->Read(&buffer[0], metadata_size)!=metadata_size)
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
			Server->Log(L"File \""+out_fn+L"\" has wrong size. Should="+convert(hashdata_size)+L" is="+convert(out->Size()), LL_ERROR);
			assert(false);
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
	IFile* in = Server->openFile(os_file_prefix(in_fn));

	if(!in)
	{
		Server->Log(L"Error reading file metadata from file \""+in_fn+L"\"", LL_DEBUG);
		return false;
	}

	return read_metadata(in, metadata);
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
	char is_compressed=0;

	if(file_permission_bits.size()>10)
	{
		std::string comp_out;
		comp_out.resize(mz_compressBound(static_cast<mz_ulong>(file_permission_bits.size())));
		mz_ulong dest_len = static_cast<mz_ulong>(comp_out.size());
		int rc = mz_compress2(reinterpret_cast<unsigned char*>(&comp_out[0]),
			&dest_len, reinterpret_cast<const unsigned char*>(file_permission_bits.c_str()),
			static_cast<mz_ulong>(file_permission_bits.size()), MZ_BEST_COMPRESSION);
		if(rc!=MZ_OK)
		{
			Server->Log("Error compressing metadata file permission bits: "+nconvert(rc), LL_WARNING);
		}
		else
		{
			comp_out.resize(dest_len);
			is_compressed=1;
			data.addChar(is_compressed);
			data.addUInt(static_cast<unsigned int>(comp_out.size()));
			data.addString(comp_out);
		}
	}

	if(is_compressed==0)
	{
		data.addChar(is_compressed);
		data.addString(file_permission_bits);
	}
	data.addInt64(last_modified);
	data.addInt64(created);
	data.addString(shahash);
}

bool FileMetadata::read( CRData& data )
{
	bool ok=true;
	ok &= data.getChar(&permissions_compressed);
	unsigned int uncomp_size;
	if(permissions_compressed)
	{
		ok &= data.getUInt(&uncomp_size);
	}
	ok &= data.getStr(&file_permission_bits);
	ok &= data.getInt64(&last_modified);
	ok &= data.getInt64(&created);
	ok &= data.getStr(&shahash);

	if(ok && permissions_compressed)
	{
		std::string input_data = file_permission_bits;
		file_permission_bits.resize(uncomp_size);
		mz_ulong dest_len = static_cast<mz_ulong>(file_permission_bits.size());
		int rc = mz_uncompress(reinterpret_cast<unsigned char*>(&file_permission_bits[0]),
			&dest_len,
			reinterpret_cast<const unsigned char*>(input_data.c_str()),
			static_cast<mz_ulong>(input_data.size()));
		if(rc!=MZ_OK)
		{
			Server->Log("Error decompressing file metadata permission bits: "+nconvert(rc), LL_ERROR);
			ok=false;
		}
	}

	return ok;
}

bool FileMetadata::read( str_map& extra_params )
{
	file_permission_bits = base64_decode_dash(wnarrow(extra_params[L"rpb"]));
	last_modified = watoi64(extra_params[L"mod"]);
	created = watoi64(extra_params[L"creat"]);

	return true;
}

bool FileMetadata::bitSet( size_t id ) const
{
	size_t bitmap_byte=(size_t)(id/8);
	size_t bitmap_bit=id%8;

	if(file_permission_bits.size()<bitmap_byte)
	{
		return false;
	}

	unsigned char b=static_cast<unsigned char>(file_permission_bits[bitmap_byte]);

	return (b & (1<<(7-bitmap_bit)))>0;
}

void FileMetadata::set_shahash( const std::string& the_shahash )
{
	shahash=the_shahash;
}
