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

#include "Server.h"
#include "file.h"
#include "types.h"
#include "stringtools.h"
#include <sstream>
#include <assert.h>

#ifdef MODE_WIN

size_t File::tmp_file_index = 0;
IMutex* File::index_mutex = NULL;
std::string File::random_prefix;

File::File()
	: hfile(INVALID_HANDLE_VALUE), is_sparse(false), more_extents(true), curr_extent(0), last_sparse_pos(0)
{

}

bool File::Open(std::string pfn, int mode)
{
	if(mode==MODE_RW_DIRECT)
		mode = MODE_RW;
	if(mode==MODE_RW_CREATE_DIRECT)
		mode = MODE_RW_CREATE;
	
	fn=pfn;
	DWORD dwCreationDisposition;
	DWORD dwDesiredAccess;
	DWORD dwShareMode=FILE_SHARE_READ;
	if( mode==MODE_READ
		|| mode==MODE_READ_DEVICE
		|| mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode== MODE_READ_DEVICE_OVERLAPPED)
	{
		dwCreationDisposition=OPEN_EXISTING;
		dwDesiredAccess=GENERIC_READ;
	}
	else if( mode==MODE_WRITE )
	{
		DeleteFileInt(pfn);
		dwCreationDisposition=CREATE_NEW;
		dwDesiredAccess=GENERIC_WRITE;
	}
	else if( mode==MODE_APPEND )
	{
		dwCreationDisposition=OPEN_EXISTING;
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}
	else if( mode==MODE_TEMP )
	{
		dwCreationDisposition=CREATE_NEW;
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}
	else if( mode==MODE_RW 
		|| mode==MODE_RW_SEQUENTIAL
		|| mode==MODE_RW_CREATE
		|| mode==MODE_RW_READNONE
		|| mode== MODE_RW_DEVICE
		|| mode==MODE_RW_RESTORE
		|| mode==MODE_RW_CREATE_RESTORE
		|| mode== MODE_RW_CREATE_DEVICE
		|| mode== MODE_RW_CREATE_DELETE
		|| mode==MODE_RW_DELETE)
	{
		if(mode==MODE_RW
			|| mode==MODE_RW_SEQUENTIAL
			|| mode==MODE_RW_READNONE
			|| mode== MODE_RW_DEVICE
			|| mode==MODE_RW_RESTORE
			|| mode==MODE_RW_DELETE)
		{
			dwCreationDisposition=OPEN_EXISTING;
		}
		else
		{
			dwCreationDisposition=OPEN_ALWAYS;
		}
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}

	if(mode==MODE_READ_DEVICE
		|| mode== MODE_RW_DEVICE
		|| mode== MODE_RW_DELETE
		|| mode== MODE_RW_CREATE_DEVICE
		|| mode== MODE_RW_CREATE_DELETE
		|| mode== MODE_READ_DEVICE_OVERLAPPED)
	{
		dwShareMode|=FILE_SHARE_WRITE;
	}

	if (mode == MODE_RW_CREATE_DELETE
		|| mode == MODE_RW_DELETE)
	{
		dwShareMode |= FILE_SHARE_DELETE;
	}

	DWORD flags=FILE_ATTRIBUTE_NORMAL;
	if(mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode==MODE_RW_SEQUENTIAL)
	{
		flags|=FILE_FLAG_SEQUENTIAL_SCAN;
	}
	if(mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode==MODE_RW_RESTORE
		|| mode==MODE_RW_CREATE_RESTORE)
	{
		flags|=FILE_FLAG_BACKUP_SEMANTICS;
	}
	if (mode == MODE_READ_DEVICE_OVERLAPPED)
	{
		flags |= FILE_FLAG_OVERLAPPED| FILE_FLAG_NO_BUFFERING;
	}
	
	hfile=CreateFileW( Server->ConvertToWchar(fn).c_str(), dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, flags, NULL );

	if( hfile!=INVALID_HANDLE_VALUE )
	{
		if( mode==MODE_APPEND )
		{
			Seek( Size() );
		}
		return true;
	}
	else
	{
		DWORD err = GetLastError();
		return false;
	}
}

bool File::OpenTemporaryFile(const std::string &tmpdir, bool first_try)
{
	std::ostringstream filename;

	if(tmpdir.empty())
	{
		wchar_t tmpp[MAX_PATH];
		DWORD l;
		if((l=GetTempPathW(MAX_PATH, tmpp))==0 || l>MAX_PATH )
		{
			wcscpy_s(tmpp, L"C:\\");
		}
		
		filename << Server->ConvertFromWchar(tmpp);
	}
	else
	{
		filename << tmpdir;

		if(tmpdir[tmpdir.size()-1]!='\\')
		{
			filename << "\\";
		}
	}

	filename << "urb" << random_prefix << L"-" << std::hex;

	{
		IScopedLock lock(index_mutex);
		filename << ++tmp_file_index;
	}

	filename << ".tmp";

	if(!Open(filename.str(), MODE_TEMP))
	{
		if(first_try)
		{
			Server->Log("Creating temporary file at \"" + filename.str()+"\" failed. Creating directory \""+tmpdir+"\"...", LL_WARNING);
			BOOL b = CreateDirectoryW(Server->ConvertToWchar(tmpdir).c_str(), NULL);

			if(b)
			{
				return OpenTemporaryFile(tmpdir, false);
			}
			else
			{
				Server->Log("Creating directory \""+tmpdir+"\" failed.", LL_WARNING);
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	else
	{
		return true;
	}
}

bool File::Open(void *handle, const std::string& pFilename)
{
	hfile=(HANDLE)handle;
	fn = pFilename;
	if( hfile!=INVALID_HANDLE_VALUE )
	{
		return true;
	}
	else
	{
		return false;
	}
}

std::string File::Read(_u32 tr, bool *has_error)
{
	std::string ret;
	ret.resize(tr);
	_u32 gc=Read((char*)ret.c_str(), tr, has_error);
	if( gc<tr )
		ret.resize( gc );
	
	return ret;
}

std::string File::Read(int64 spos, _u32 tr, bool *has_error)
{
	std::string ret;
	ret.resize(tr);
	_u32 gc=Read(spos, (char*)ret.c_str(), tr, has_error);
	if( gc<tr )
		ret.resize( gc );

	return ret;
}

_u32 File::Read(char* buffer, _u32 bsize, bool *has_error)
{
	DWORD read;
	BOOL b=ReadFile(hfile, buffer, bsize, &read, NULL );
	if(b==FALSE)
	{
#ifdef _DEBUG
		int err=GetLastError();
		Server->Log("Read error: "+convert(err));
#endif
		if(has_error)
		{
			*has_error=true;
		}
	}
	return (_u32)read;
}

_u32 File::Read(int64 spos, char* buffer, _u32 bsize, bool *has_error)
{
	OVERLAPPED overlapped = {};
	LARGE_INTEGER li;
	li.QuadPart = spos;
	overlapped.Offset = li.LowPart;
	overlapped.OffsetHigh = li.HighPart;

	DWORD read;
	BOOL b=ReadFile(hfile, buffer, bsize, &read, &overlapped );
	if(b==FALSE)
	{
#ifdef _DEBUG
		int err=GetLastError();
		Server->Log("Read error: "+convert(err));
#endif
		if (has_error)
		{
			*has_error = true;
		}
	}
	return (_u32)read;
}

_u32 File::Write(const std::string &tw, bool *has_error)
{
	return Write( tw.c_str(), (_u32)tw.size(), has_error );
}

_u32 File::Write(int64 spos, const std::string &tw, bool *has_error)
{
	return Write(spos, tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 File::Write(const char* buffer, _u32 bsize, bool *has_error)
{
	DWORD written;
	if (WriteFile(hfile, buffer, bsize, &written, NULL) == FALSE)
	{
		if (has_error)
		{
			*has_error = true;
		}
	}
	return written;
}

_u32 File::Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error)
{
	OVERLAPPED overlapped = {};
	LARGE_INTEGER li;
	li.QuadPart = spos;
	overlapped.Offset = li.LowPart;
	overlapped.OffsetHigh = li.HighPart;

	DWORD written;
	if (WriteFile(hfile, buffer, bsize, &written, &overlapped) == FALSE)
	{
		if (has_error)
		{
			*has_error = true;
		}
	}
	return written;
}

bool File::Seek(_i64 spos)
{
	LARGE_INTEGER tmp;
	tmp.QuadPart=spos;
	if( SetFilePointerEx(hfile, tmp, NULL, FILE_BEGIN) == FALSE )
	{
		int err=GetLastError();
		return false;
	}
	else
		return true;
}

_i64 File::Size(void)
{
	LARGE_INTEGER fs;
	GetFileSizeEx(hfile, &fs);

	return fs.QuadPart;
}

_i64 File::RealSize()
{
	return Size();
}

void File::Close()
{
	if( hfile!=INVALID_HANDLE_VALUE )
	{
		BOOL b=CloseHandle( hfile );
		hfile=INVALID_HANDLE_VALUE;
	}
}

IFsFile::os_file_handle File::getOsHandle(bool release_handle)
{
	HANDLE ret = hfile;
	if (release_handle)
	{
		hfile = INVALID_HANDLE_VALUE;
	}
	return ret;
}

IVdlVolCache* File::createVdlVolCache()
{
	return new VdlVolCache;
}

namespace
{
	std::string os_file_prefix(std::string path)
	{
		if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\')
		{
			if (path.size() >= 3 && path[2] == '?')
			{
				return path;
			}
			else
			{
				return "\\\\?\\UNC" + path.substr(1);
			}
		}
		else
			return "\\\\?\\" + path;
	}

#pragma pack(push)
#pragma pack(1)
	struct NTFSFileRecord
	{
		char magic[4];
		unsigned short sequence_offset;
		unsigned short sequence_size;
		uint64 lsn;
		unsigned short squence_number;
		unsigned short hardlink_count;
		unsigned short attribute_offset;
		unsigned short flags;
		unsigned int real_size;
		unsigned int allocated_size;
		uint64 base_record;
		unsigned short next_id;
		//char padding[470];
	};

	struct MFTAttribute
	{
		unsigned int type;
		unsigned int length;
		unsigned char nonresident;
		unsigned char name_lenght;
		unsigned short name_offset;
		unsigned short flags;
		unsigned short attribute_id;
		unsigned int attribute_length;
		unsigned short attribute_offset;
		unsigned char indexed_flag;
		unsigned char padding1;
		//char padding2[488];
	};

	struct MFTAttributeNonResident
	{
		unsigned int type;
		unsigned int lenght;
		unsigned char nonresident;
		unsigned char name_length;
		unsigned short name_offset;
		unsigned short flags;
		unsigned short attribute_id;
		uint64 starting_vnc;
		uint64 last_vnc;
		unsigned short run_offset;
		unsigned short compression_size;
		unsigned int padding;
		uint64 allocated_size;
		uint64 real_size;
		uint64 initial_size;
	};

	struct MFTAttributeListItem
	{
		unsigned int type;
		unsigned short length;
		unsigned char name_length;
		unsigned char name_offset;
		uint64 attr_vcn;
		uint64 attr_frn;
		unsigned short attr_id;
	};
#pragma pack(pop)

	HANDLE GetVolumeData(const std::wstring& volfn, NTFS_VOLUME_DATA_BUFFER& vol_data)
	{
		HANDLE vol = CreateFileW(volfn.c_str(), GENERIC_WRITE | GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (vol == INVALID_HANDLE_VALUE)
			return vol;

		DWORD ret_bytes;
		BOOL b = DeviceIoControl(vol, FSCTL_GET_NTFS_VOLUME_DATA,
			NULL, 0, &vol_data, sizeof(vol_data), &ret_bytes, NULL);

		if (!b)
		{
			CloseHandle(vol);
			return INVALID_HANDLE_VALUE;
		}

		return vol;
	}

	int64 getMftIndex(int64 frn)
	{
		return (frn & 0xFFFFFFFFFFFFLL);
	}

	struct RunlistItem
	{
		uint64 length;
		int64 offset;
	};

#define UD_UINT64 0xFFFFFFFFFFFFFFFFULL

	class Runlist
	{
	public:
		Runlist(char* pData, char* pDataEnd)
			: data(pData), data_end(pDataEnd)
		{
			reset();
		}

		void reset(void)
		{
			pos = data;
		}

		bool getNext(RunlistItem& item)
		{
			if (pos >= data_end)
				return false;

			char f = *pos;
			if (f == 0)
				return false;

			if (pos + 1 >= data_end)
				return false;

			char offset_size = f >> 4;
			char length_size = f & 0x0F;
			item.length = 0;
			item.offset = 0;
			memcpy(&item.length, pos + 1, length_size);

			if (pos + 1 + length_size + offset_size -1 >= data_end)
				return false;

			bool is_signed = (*(pos + 1 + length_size + offset_size - 1) & 0x80) > 0;

			memcpy(&item.offset, pos + 1 + length_size, offset_size);

			if (is_signed)
			{
				char* ar = (char*)&item.offset;
				ar[offset_size - 1] = ar[offset_size - 1] & 0x7F;
				item.offset *= -1;
			}

			pos += 1 + offset_size + length_size;
			return true;
		}

		uint64 getSizeInClusters(void)
		{
			reset();
			RunlistItem item;
			uint64 size = 0;
			while (getNext(item))
			{
				size += item.length;
			}
			return size;
		}

		uint64 getLCN(uint64 vcn)
		{
			reset();
			RunlistItem item;
			uint64 lcn = 0;
			uint64 coffset = 0;
			while (getNext(item))
			{
				lcn += item.offset;

				if (coffset <= vcn && coffset + item.length > vcn)
				{
					return lcn + (vcn - coffset);
				}

				coffset += item.length;
			}
			return UD_UINT64;
		}

	private:
		char* data;
		char* data_end;
		char* pos;
	};

	int64 GetFileValidData(int64 frn, HANDLE vol, const NTFS_VOLUME_DATA_BUFFER& vol_data);

	int64 GetFileValidData(int64 frn, NTFSFileRecord* record, BYTE* record_end, HANDLE vol, const NTFS_VOLUME_DATA_BUFFER& vol_data)
	{
		int64 mft_index_nr = getMftIndex(frn);

		MFTAttributeListItem* attr_list_item = reinterpret_cast<MFTAttributeListItem*>(record);

		BYTE* record_pos = reinterpret_cast<BYTE*>(record);
		_u32 currpos = record->attribute_offset;
		MFTAttribute* attr = nullptr;
		while ((attr == nullptr ||
			attr->type != 0xFFFFFFFF)
			&& record_pos + currpos + sizeof(MFTAttribute) < record_end)
		{
			attr = reinterpret_cast<MFTAttribute*>(record_pos + currpos);
			BYTE* attr_end = record_pos + currpos + attr->length;
			if (attr->type == 0x80
				&& record_pos + currpos + attr->attribute_offset + sizeof(MFTAttributeNonResident)
				< record_end)
			{
				if (attr->nonresident == 0)
				{
#ifndef NDEBUG
					Server->Log("nonresident=0 frn=" + convert(frn), LL_ERROR);
#endif
					assert(false);
					return -1;
				}

				MFTAttributeNonResident* dataattr = reinterpret_cast<MFTAttributeNonResident*>(record_pos
					+ currpos + attr->attribute_offset);
				return dataattr->initial_size;
			}
			else if (attr->type == 0x20)
			{
				size_t curr_attr_pos = attr->attribute_offset;
				std::vector<char> attr_buf;

				if (attr->nonresident != 0)
				{
					MFTAttributeNonResident* attrlist = reinterpret_cast<MFTAttributeNonResident*>(record_pos
						+ currpos + attr->attribute_offset);

					Runlist runlist(reinterpret_cast<char*>(record_pos
						+ currpos + attr->attribute_offset + attrlist->run_offset),
						reinterpret_cast<char*>(record_pos
							+ currpos + attr->attribute_offset + attrlist->lenght));

					attr_buf.resize((attrlist->last_vnc - attrlist->starting_vnc + 1) * vol_data.BytesPerCluster);

					std::unique_ptr<IFsFile> dev(Server->openFileFromHandle(vol, "vol"));
					if (dev.get() == NULL)
						return -1;

					for (uint64 i = attrlist->starting_vnc; i <= attrlist->last_vnc; ++i)
					{
						uint64 lcn = runlist.getLCN(i);
						if (lcn == UD_UINT64)
						{
#ifndef NDEBUG
							Server->Log("Error getting data run " + convert(i), LL_ERROR);
#endif
							assert(false);
							dev->getOsHandle(true);
							return -1;
						}

						if (dev->Read(lcn * vol_data.BytesPerCluster, &attr_buf[i * vol_data.BytesPerCluster], vol_data.BytesPerCluster) != vol_data.BytesPerCluster)
						{
#ifndef NDEBUG
							Server->Log("Error reading data from vol at pos " + convert(lcn * vol_data.BytesPerCluster), LL_ERROR);
#endif
							assert(false);
							dev->getOsHandle(true);
							return -1;
						}
					}

					dev->getOsHandle(true);

					currpos += attr->length;

					attr = reinterpret_cast<MFTAttribute*>(attr_buf.data());
					attr_end = reinterpret_cast<BYTE*>(attr_buf.data() + attr_buf.size());
					curr_attr_pos = 0;
				}				

				while (reinterpret_cast<BYTE*>(attr) + curr_attr_pos + sizeof(MFTAttributeListItem) < attr_end)
				{
					MFTAttributeListItem* attr_list_item = reinterpret_cast<MFTAttributeListItem*>(reinterpret_cast<BYTE*>(attr) + curr_attr_pos);

					if (attr_list_item->type == 0x80 &&
						getMftIndex(attr_list_item->attr_frn) != mft_index_nr)
					{
						if (attr_list_item->attr_vcn != 0)
						{
#ifndef NDEBUG
							Server->Log("attr_vcn!=0 frn=" + convert(frn) + " attr_frn=" + convert(attr_list_item->attr_vcn), LL_ERROR);
#endif
							assert(false);
						}
						else
						{
							int64 ret = GetFileValidData(attr_list_item->attr_frn, vol, vol_data);
							if (ret >= 0)
								return ret;
						}
					}

					curr_attr_pos += attr_list_item->length;
				}

				if (!attr_buf.empty())
					continue;
			}
			currpos += attr->length;
		}

		return -1;
	}

	int64 GetFileValidData(int64 frn, HANDLE vol, const NTFS_VOLUME_DATA_BUFFER& vol_data)
	{
		NTFS_FILE_RECORD_INPUT_BUFFER record_in;
		record_in.FileReferenceNumber.QuadPart = frn;
		std::vector<BYTE> buf;
		buf.resize(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) + vol_data.BytesPerFileRecordSegment - 1);
		NTFS_FILE_RECORD_OUTPUT_BUFFER* record_out = reinterpret_cast<NTFS_FILE_RECORD_OUTPUT_BUFFER*>(buf.data());
		DWORD bout;
		BOOL b = DeviceIoControl(vol, FSCTL_GET_NTFS_FILE_RECORD, &record_in,
			sizeof(record_in), record_out, static_cast<DWORD>(buf.size()), &bout, NULL);

		if (!b)
			return -1;

		int64 mft_index_nr = getMftIndex(frn);
		if (record_out->FileReferenceNumber.QuadPart != mft_index_nr)
			return -1;

		NTFSFileRecord* record = reinterpret_cast<NTFSFileRecord*>(record_out->FileRecordBuffer);

		return GetFileValidData(frn, record, buf.data() + bout, vol, vol_data);
	}


	int64 GetFileValidData(HANDLE file, HANDLE vol, const NTFS_VOLUME_DATA_BUFFER& vol_data)
	{
		BY_HANDLE_FILE_INFORMATION hfi;
		BOOL b = GetFileInformationByHandle(file, &hfi);
		if (!b)
			return -1;

		LARGE_INTEGER frn;
		frn.HighPart = hfi.nFileIndexHigh;
		frn.LowPart = hfi.nFileIndexLow;

		return GetFileValidData(frn.QuadPart, vol, vol_data);
	}
}

File::VdlVolCache::~VdlVolCache()
{
	if (!volfn.empty())
	{
		CloseHandle(vol);
	}
}


int64 File::getValidDataLength(IVdlVolCache* p_vol_cache)
{
	VdlVolCache* vol_cache = static_cast<VdlVolCache*>(p_vol_cache);

	if (!vol_cache->volfn.empty()
		&& !next(fn, 0, vol_cache->volfn))
	{
		CloseHandle(vol_cache->vol);
		vol_cache->volfn.clear();
	}

	if (vol_cache->volfn.empty())
	{
		std::string prefixedbpath = os_file_prefix(fn);
		std::wstring tvolume;
		tvolume.resize(prefixedbpath.size() + 100);
		DWORD cchBufferLength = static_cast<DWORD>(tvolume.size());
		BOOL b = GetVolumePathNameW(Server->ConvertToWchar(prefixedbpath).c_str(), &tvolume[0], cchBufferLength);
		if (!b)
		{
			return -1;
		}

		std::string volfn = Server->ConvertFromWchar(tvolume).c_str();
		std::string volume_lower = strlower(volfn);

		if (volume_lower.find("\\\\?\\") == 0
			&& volume_lower.find("\\\\?\\globalroot") != 0
			&& volume_lower.find("\\\\?\\volume") != 0)
		{
			volfn.erase(0, 4);
			tvolume.erase(0, 4);
		}

		size_t tvolume_len = wcslen(tvolume.c_str());
		if (tvolume_len > 0
			&& tvolume[tvolume_len - 1] == '\\')
		{
			tvolume[tvolume_len - 1] = 0;
			--tvolume_len;
		}

		if (!tvolume.empty()
			&& tvolume_len>0)
		{
			HANDLE vol = GetVolumeData(tvolume, vol_cache->vol_data);

			if (vol == INVALID_HANDLE_VALUE)
				return -1;

			vol_cache->vol = vol;
			vol_cache->volfn = volfn;
		}
	}

	return GetFileValidData(hfile, vol_cache->vol, vol_cache->vol_data);
}

void File::init_mutex()
{
	index_mutex = Server->createMutex();

	std::string rnd;
	rnd.resize(8);
	unsigned int timesec = static_cast<unsigned int>(Server->getTimeSeconds());
	memcpy(&rnd[0], &timesec, sizeof(timesec));
	Server->randomFill(&rnd[4], 4);

	random_prefix = bytesToHex(reinterpret_cast<unsigned char*>(&rnd[0]), rnd.size());
}

void File::destroy_mutex()
{
	Server->destroy(index_mutex);
}

bool File::setSparse()
{
	if (!is_sparse)
	{
		FILE_SET_SPARSE_BUFFER buf = { TRUE };
		DWORD ret_bytes;
		BOOL b = DeviceIoControl(hfile, FSCTL_SET_SPARSE, &buf,
			static_cast<DWORD>(sizeof(buf)), NULL, 0, &ret_bytes, NULL);

		if (!b)
		{
			return false;
		}

		is_sparse = true;
	}

	return true;
}

bool File::PunchHole( _i64 spos, _i64 size )
{
	if (!setSparse())
	{
		return false;
	}
	
	FILE_ZERO_DATA_INFORMATION zdi;

	zdi.FileOffset.QuadPart = spos;
	zdi.BeyondFinalZero.QuadPart = spos + size;

	DWORD ret_bytes;
	BOOL b = DeviceIoControl(hfile, FSCTL_SET_ZERO_DATA, &zdi,
		static_cast<DWORD>(sizeof(zdi)), NULL, 0, &ret_bytes, 0);

	if(!b)
	{
		return false;
	}
	else
	{
		return true;
	}
}

bool File::Sync()
{
	return FlushFileBuffers(hfile)!=0;
}

bool File::Resize(int64 new_size, bool set_sparse)
{
	int64 fsize = Size();

	if (new_size > fsize
		&& set_sparse)
	{
		if (!setSparse())
		{
			return false;
		}
	}

	LARGE_INTEGER tmp;
	tmp.QuadPart = 0;
	LARGE_INTEGER curr_pos;
	if (SetFilePointerEx(hfile, tmp, &curr_pos, FILE_CURRENT) == FALSE)
	{
		return false;
	}

	tmp.QuadPart = new_size;
	if (SetFilePointerEx(hfile, tmp, NULL, FILE_BEGIN) == FALSE)
	{
		return false;
	}

	BOOL ret = SetEndOfFile(hfile);

	SetFilePointerEx(hfile, curr_pos, NULL, FILE_BEGIN);

	return ret == TRUE;
}

void File::resetSparseExtentIter()
{
	res_extent_buffer.clear();
	more_extents = true;
	curr_extent = 0;
}

IFsFile::SSparseExtent File::nextSparseExtent()
{
	while (!res_extent_buffer.empty()
		&& curr_extent<res_extent_buffer.size())
	{
		if (res_extent_buffer[curr_extent].FileOffset.QuadPart != last_sparse_pos)
		{
			IFsFile::SSparseExtent ret(last_sparse_pos, res_extent_buffer[curr_extent].FileOffset.QuadPart - last_sparse_pos);
			last_sparse_pos = res_extent_buffer[curr_extent].FileOffset.QuadPart + res_extent_buffer[curr_extent].Length.QuadPart;
			++curr_extent;
			return ret;
		}
		
		last_sparse_pos = res_extent_buffer[curr_extent].FileOffset.QuadPart + res_extent_buffer[curr_extent].Length.QuadPart;
		++curr_extent;
	}

	if (!more_extents)
	{
		int64 fsize = Size();
		if (last_sparse_pos!=-1 && last_sparse_pos != fsize)
		{
			IFsFile::SSparseExtent ret = IFsFile::SSparseExtent(last_sparse_pos, fsize - last_sparse_pos);
			last_sparse_pos = fsize;
			return ret;
		}

		return IFsFile::SSparseExtent();
	}

	int64 fsize = Size();

	FILE_ALLOCATED_RANGE_BUFFER query_range;
	query_range.FileOffset.QuadPart = last_sparse_pos;
	query_range.Length.QuadPart = fsize- last_sparse_pos;
	
	if (res_extent_buffer.empty())
	{
		res_extent_buffer.resize(10);
	}
	else
	{
		res_extent_buffer.resize(100);
	}

	DWORD output_bytes;
	BOOL b = DeviceIoControl(hfile, FSCTL_QUERY_ALLOCATED_RANGES,
		&query_range, sizeof(query_range), res_extent_buffer.data(), static_cast<DWORD>(res_extent_buffer.size()*sizeof(FILE_ALLOCATED_RANGE_BUFFER)),
		&output_bytes, NULL);

	more_extents = (!b && GetLastError() == ERROR_MORE_DATA);

	if (more_extents || b)
	{
		res_extent_buffer.resize(output_bytes / sizeof(FILE_ALLOCATED_RANGE_BUFFER));
		curr_extent = 0;
	}
	else
	{
		res_extent_buffer.clear();
		more_extents = false;
		curr_extent = 0;
		last_sparse_pos = -1;
	}

	return nextSparseExtent();
}

std::vector<IFsFile::SFileExtent> File::getFileExtents(int64 starting_offset, int64 block_size, bool& more_data, unsigned int flags)
{
	std::vector<IFsFile::SFileExtent> ret;

	STARTING_VCN_INPUT_BUFFER starting_vcn;
	starting_vcn.StartingVcn.QuadPart = starting_offset / block_size;

	std::vector<char> buf;
	buf.resize(4096);

	DWORD retBytes;
	BOOL b = DeviceIoControl(hfile, FSCTL_GET_RETRIEVAL_POINTERS,
		&starting_vcn, sizeof(starting_vcn),
		buf.data(), static_cast<DWORD>(buf.size()), &retBytes, NULL);

	more_data = (!b && GetLastError() == ERROR_MORE_DATA);

	if (more_data || b)
	{
		PRETRIEVAL_POINTERS_BUFFER pbuf = reinterpret_cast<PRETRIEVAL_POINTERS_BUFFER>(buf.data());
		LARGE_INTEGER last_vcn = pbuf->StartingVcn;
		for (DWORD i = 0; i < pbuf->ExtentCount; ++i)
		{
			if (pbuf->Extents[i].Lcn.QuadPart != -1)
			{
				int64 count = pbuf->Extents[i].NextVcn.QuadPart - last_vcn.QuadPart;

				IFsFile::SFileExtent ext;
				ext.offset = last_vcn.QuadPart * block_size;
				ext.size = count * block_size;
				ext.volume_offset = pbuf->Extents[i].Lcn.QuadPart * block_size;

				ret.push_back(ext);
			}

			last_vcn = pbuf->Extents[i].NextVcn;
		}
	}

	return ret;
}

#endif
