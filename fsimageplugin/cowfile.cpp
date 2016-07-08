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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __APPLE__
#include "cowfile.h"
#include "../Interface/Server.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../stringtools.h"
#include <fcntl.h>
#include "fs/ntfs.h"
#include <errno.h>
#include <memory>
#include "FileWrapper.h"
#include "ClientBitmap.h"

#ifdef __FreeBSD__
#define open64 open
#define O_LARGEFILE 0
#define ftruncate64 ftruncate
#define lseek64 lseek
#define stat64 stat
#define fstat64 fstat
#endif

#include "../config.h"

#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE    0x1
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE   0x2
#endif

const unsigned int blocksize = 4096;

CowFile::CowFile(const std::string &fn, bool pRead_only, uint64 pDstsize)
 : bitmap_dirty(false), finished(false), curr_offset(0)
{
	filename = fn;
	read_only = pRead_only;
	filesize = pDstsize;

	if(FileExists(filename+".bitmap"))
	{
		is_open = loadBitmap(filename+".bitmap");
	}
	else
	{
		is_open = !read_only;
		setupBitmap();
	}

	if(is_open)
	{
		mode_t imode=S_IRWXU|S_IRWXG;
		int flags=0;

		if(read_only)
		{
			flags=O_RDONLY;
		}
		else
		{
			flags=O_RDWR|O_CREAT;
		}

		fd = open64(filename.c_str(), flags|O_LARGEFILE, imode);

		if(fd==-1)
		{
			is_open=false;
		}
		else
		{
			is_open=true;

			if(!read_only)
			{
				int rc = ftruncate64(fd, filesize);

				if(rc!=0)
				{
					Server->Log("Truncating cow file to size "+convert(filesize)+" failed", LL_ERROR);
					is_open=false;
					close(fd);
				}
			}
			else
			{
				struct stat64 statbuf;
				int rc = stat64(filename.c_str(), &statbuf);
				if(rc==0)
				{
					filesize = statbuf.st_size;
					is_open=true;
				}
				else
				{
					is_open=false;
				}
			}
		}
	}
}

CowFile::CowFile(const std::string &fn, const std::string &parent_fn, bool pRead_only)
	: bitmap_dirty(false), finished(false), curr_offset(0)
{
	filename = fn;
	read_only = pRead_only;


	struct stat64 statbuf;
	int rc = stat64(parent_fn.c_str(), &statbuf);
	if(rc==0)
	{
		filesize = statbuf.st_size;
		is_open=true;
	}
	else
	{
		is_open=false;
	}


	if(is_open)
	{
		if(FileExists(filename+".bitmap"))
		{
			is_open = loadBitmap(filename+".bitmap");
		}
		else if(!read_only)
		{
			is_open = loadBitmap(parent_fn+".bitmap");
		}
		else
		{
			is_open=false;
		}
	}

	if(is_open)
	{
		if(!FileExists(filename))
		{
			is_open=false;
		}

		if(is_open)
		{
			mode_t imode=S_IRWXU|S_IRWXG;
			int flags=0;

			if(read_only)
			{
				flags=O_RDONLY;
			}
			else
			{
				flags=O_RDWR|O_CREAT;
			}

			fd = open64(filename.c_str(), flags|O_LARGEFILE, imode);

			if(fd==-1)
			{
				is_open=false;
			}
			else
			{
				is_open=true;
			}

			struct stat64 statbuf;
			if(fstat64(fd, &statbuf)==0)
			{
				if(filesize!=statbuf.st_size)
				{
					rc = ftruncate64(fd, filesize);

					if(rc!=0)
					{
						Server->Log("Truncating cow file to parent size "+convert(filesize)+" failed", LL_ERROR);
						is_open=false;
						close(fd);
					}
				}
			}
			else
			{
				is_open=false;
				close(fd);
			}
		}
	}
}

CowFile::~CowFile()
{
	if(is_open)
	{
		fsync(fd);
		close(fd);
	}

	if(!finished)
	{
		finish();
	}
}

bool CowFile::Seek(_i64 offset)
{
	if(!is_open) return false;

	curr_offset = offset;

	off_t off = lseek64(fd, curr_offset, SEEK_SET);

	if(off!=curr_offset)
	{
		Server->Log("Seeking in file failed. errno="+convert(errno), LL_ERROR);
		return false;
	}

	return true;
}

bool CowFile::Read(char* buffer, size_t bsize, size_t& read_bytes)
{
	if(!is_open) return false;

	ssize_t r=read(fd, buffer, bsize);
	if( r<0 )
	{
		read_bytes=0;
		return false;
	}
	else
	{
		curr_offset+=r;
		read_bytes=r;
		return true;
	}
}

_u32 CowFile::Write(const char* buffer, _u32 bsize, bool *has_error)
{
	if(!is_open) return 0;

	ssize_t w=write(fd, buffer, bsize);
	if( w<0 )
	{
		if(has_error) *has_error=true;
		Server->Log("Write to CowFile failed. errno="+convert(errno), LL_DEBUG);
		return 0;
	}

	setBitmapRange(curr_offset, curr_offset+w, true);
	curr_offset+=w;

	return (_u32)w;
}

bool CowFile::isOpen(void)
{
	return is_open;
}

uint64 CowFile::getSize(void)
{
	/*stat buf;
	int rc = fstat64(fd, &buf);
	if(rc!=0)
	{
		return 0;
	}
	else
	{
		return buf.st_size;
	}*/
	return filesize;
}

uint64 CowFile::usedSize(void)
{
	uint64 ret = 0;

	for(uint64 i=0;i<filesize;i+=blocksize)
	{
		if(isBitmapSet(i))
		{
			ret+=blocksize;
		}
	}

	return ret;
}

std::string CowFile::getFilename(void)
{
	return filename;
}

bool CowFile::has_sector(_i64 sector_size)
{
	if(sector_size<0)
	{
		return isBitmapSet(curr_offset);
	}
	else
	{
		for(_i64 off=curr_offset;off<curr_offset+sector_size;off+=blocksize)
		{
			if(isBitmapSet(off))
			{
				return true;
			}
		}

		return false;
	}
}

bool CowFile::this_has_sector(_i64 sector_size)
{
	return has_sector(sector_size);
}

unsigned int CowFile::getBlocksize()
{
	return blocksize;
}

bool CowFile::finish()
{
	finished=true;
	if(bitmap_dirty)
	{
		return saveBitmap();
	}
	return true;
}

void CowFile::setupBitmap()
{
	uint64 n_blocks = filesize/blocksize+(filesize%blocksize>0?1:0);
	size_t n_bits = n_blocks/8 + (n_blocks%8>0?1:0);
	bitmap.resize(n_bits);
}

void CowFile::resizeBitmap()
{
	uint64 n_blocks = filesize/blocksize+(filesize%blocksize>0?1:0);
	size_t n_bits = n_blocks/8 + (n_blocks%8>0?1:0);
	if(n_bits>bitmap.size())
	{
		bitmap.insert(bitmap.end(), n_bits-bitmap.size(), 0);
	}
}

bool CowFile::isBitmapSet(uint64 offset)
{
	uint64 block=offset/blocksize;
	size_t bitmap_byte=(size_t)(block/8);
	size_t bitmap_bit=block%8;

	unsigned char b=bitmap[bitmap_byte];

	bool has_bit=((b & (1<<(7-bitmap_bit)))>0);

	return has_bit;
}

void CowFile::setBitmapBit(uint64 offset, bool v)
{
	uint64 block=offset/blocksize;
	size_t bitmap_byte=(size_t)(block/8);
	size_t bitmap_bit=block%8;

	unsigned char b=bitmap[bitmap_byte];

	if(v==true)
		b=b|(1<<(7-bitmap_bit));
	else
		b=b&(~(1<<(7-bitmap_bit)));

	bitmap[bitmap_byte]=b;
	bitmap_dirty=true;
}

bool CowFile::saveBitmap()
{
	std::auto_ptr<IFile> bitmap_file(Server->openFile(filename+".bitmap", MODE_WRITE));

	if(!bitmap_file.get())
	{
		Server->Log("Error opening Bitmap file \"" + filename+".bitmap\" for writing", LL_ERROR);
		return false;
	}

	if(bitmap_file->Write(reinterpret_cast<const char*>(bitmap.data()), bitmap.size())!=bitmap.size())
	{
		return false;
	}

	bitmap_dirty=false;
	return bitmap_file->Sync();
}

bool CowFile::loadBitmap(const std::string& bitmap_fn)
{
	std::auto_ptr<IFile> bitmap_file(Server->openFile(bitmap_fn, MODE_READ));

	if(!bitmap_file.get())
	{
		Server->Log("Error opening Bitmap file \"" + bitmap_fn+"\" for reading", LL_ERROR);
		return false;
	}

	bitmap.resize(bitmap_file->Size());

	if(bitmap_file->Read(reinterpret_cast<char*>(&bitmap[0]), bitmap.size())!=bitmap.size())
	{
		return false;
	}
	
	resizeBitmap();

	return true;
}

void CowFile::setBitmapRange(uint64 offset_start, uint64 offset_end, bool v)
{
	uint64 block_start = offset_start/blocksize;
	uint64 block_end = offset_end/blocksize;
	for(;block_start<block_end;++block_start)
	{
		setBitmapBit(block_start*blocksize, v);
	}
}

bool CowFile::hasBitmapRange(uint64 offset_start, uint64 offset_end)
{
	uint64 block_start = offset_start/blocksize;
	uint64 block_end = offset_end/blocksize;
	for(;block_start<block_end;++block_start)
	{
		if(isBitmapSet(block_start*blocksize))
		{
			return true;
		}
	}
	return false;
}

bool CowFile::setUnused(_i64 unused_start, _i64 unused_end)
{
#if !defined(__FreeBSD__) && defined(HAVE_FALLOCATE64)
	int rc = fallocate64(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, unused_start, unused_end-unused_start);
	if(rc==0)
	{
		return true;
	}
	else
	{
		int err=errno;
		Server->Log("fallocate failed (setting unused image range) with errno "+convert(err), LL_WARNING);
		errno=err;
		return false;
	}
#else
	return false;
#endif
}

bool CowFile::trimUnused(_i64 fs_offset, _i64 trim_blocksize, ITrimCallback* trim_callback)
{
	FileWrapper devfile(this, fs_offset);
	std::auto_ptr<IReadOnlyBitmap> bitmap_source;

	bitmap_source.reset(new ClientBitmap(filename + ".cbitmap"));

	if (bitmap_source->hasError())
	{
		Server->Log("Error reading client bitmap. Falling back to reading bitmap from NTFS", LL_WARNING);

		bitmap_source.reset(new FSNTFS(&devfile, false, false));
	}

	if (bitmap_source->hasError())
	{
		Server->Log("Error opening NTFS bitmap. Cannot trim.", LL_WARNING);
		return false;
	}

	unsigned int bitmap_blocksize = static_cast<unsigned int>(bitmap_source->getBlocksize());
	
	if(trim_blocksize<bitmap_blocksize)
	{
		trim_blocksize = bitmap_blocksize;
	}
	
	if(trim_blocksize%bitmap_blocksize!=0)
	{
		Server->Log("Trim block size (" + convert(trim_blocksize)+") is not a multiple of the bitmap block size ("+convert(bitmap_blocksize)+")", LL_WARNING);
		return false;
	}
	
	trim_blocksize=trim_blocksize/bitmap_blocksize;
	
	int64 unused_start_block = -1;

	for(int64 ntfs_block=0, n_ntfs_blocks = devfile.Size()/bitmap_blocksize;
			ntfs_block<n_ntfs_blocks; ntfs_block+=trim_blocksize)
	{
		bool has_block=false;
		for(int64 i=ntfs_block;i<ntfs_block+trim_blocksize && i<n_ntfs_blocks;++i)
		{
			if(bitmap_source->hasBlock(i))
			{
				has_block=true;
				break;
			}
		}
	
		if(!has_block)
		{
			if(unused_start_block==-1)
			{
				unused_start_block=ntfs_block;
			}
		}
		else if(unused_start_block!=-1)
		{
			int64 unused_start = fs_offset + unused_start_block*bitmap_blocksize;
			int64 unused_end = fs_offset + ntfs_block*bitmap_blocksize;
			if(hasBitmapRange(unused_start, unused_end))
			{
				setBitmapRange(unused_start, unused_end, false);
				if(!setUnused(unused_start, unused_end))
				{
					Server->Log("Trimming syscall failed. Stopping trimming.", LL_WARNING);
					return false;
				}
				if(trim_callback!=NULL)
				{
					trim_callback->trimmed(unused_start - fs_offset, unused_end - fs_offset);
				}
			}
			unused_start_block=-1;
		}
	}

	return true;
}

bool CowFile::syncBitmap(_i64 fs_offset)
{
	FileWrapper devfile(this, fs_offset);
	std::auto_ptr<IReadOnlyBitmap> bitmap_source;

	bitmap_source.reset(new ClientBitmap(filename + ".cbitmap"));

	if (bitmap_source->hasError())
	{
		Server->Log("Error reading client bitmap. Falling back to reading bitmap from NTFS", LL_WARNING);

		bitmap_source.reset(new FSNTFS(&devfile, false, false));
	}

	if (bitmap_source->hasError())
	{
		Server->Log("Error opening NTFS bitmap. Cannot synchronize bitmap.", LL_WARNING);
		return false;
	}

	unsigned int bitmap_blocksize = static_cast<unsigned int>(bitmap_source->getBlocksize());

	int64 used_start_block=-1;

	for(int64 ntfs_block=0, n_ntfs_blocks = devfile.Size()/bitmap_blocksize;
				ntfs_block<n_ntfs_blocks; ++ntfs_block)
	{
		if(bitmap_source->hasBlock(ntfs_block))
		{
			used_start_block=ntfs_block;
		}
		else if(used_start_block!=-1)
		{
			int64 used_start = fs_offset + used_start_block*bitmap_blocksize;
			int64 used_end = fs_offset + ntfs_block*bitmap_blocksize;
			setBitmapRange(used_start, used_end, true);
		}
	}
}

#endif //__APPLE__