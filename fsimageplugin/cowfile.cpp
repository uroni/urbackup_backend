#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cowfile.h"
#include "../Interface/Server.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../stringtools.h"
#include <fcntl.h>
#include "fs/ntfs.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <memory>

const unsigned int blocksize = 4096;

namespace
{

bool create_reflink(const std::string &linkname, const std::string &fname)
{
	int src_desc=open64(fname.c_str(), O_RDONLY);
	if( src_desc<0)
	{
		Server->Log("Error opening source file. errno="+nconvert(errno));
	    return false;
	}

	int dst_desc=open64(linkname.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG);
	if( dst_desc<0 )
	{
		Server->Log("Error opening destination file. errno="+nconvert(errno));
	    close(src_desc);
	    return false;
	}

#define BTRFS_IOCTL_MAGIC 0x94
#define BTRFS_IOC_CLONE _IOW (BTRFS_IOCTL_MAGIC, 9, int)

	int rc=ioctl(dst_desc, BTRFS_IOC_CLONE, src_desc);

	if(rc)
	{
		Server->Log("Reflink ioctl failed. errno="+nconvert(errno));
	}

	close(src_desc);
	close(dst_desc);

	return rc==0;
}


class FileWrapper : public IFile
{
public:
	FileWrapper(CowFile* cowfile, int64 offset)
		: cowfile(cowfile), offset(offset)
	{

	}

	virtual std::string Read(_u32 tr)
	{
		std::string ret;
		ret.resize(tr);
		_u32 gc=Read((char*)ret.c_str(), tr);
		if( gc<tr )
			ret.resize( gc );

		return ret;
	}

	virtual _u32 Read(char* buffer, _u32 bsize)
	{
		size_t read;
		bool rc = cowfile->Read(buffer, bsize, read);
		if(!rc)
		{
			read=0;
		}
		return static_cast<_u32>(read);
	}

	virtual _u32 Write(const std::string &tw)
	{
		return Write( tw.c_str(), (_u32)tw.size() );
	}

	virtual _u32 Write(const char* buffer, _u32 bsize)
	{
		return cowfile->Write(buffer, bsize);
	}

	virtual bool Seek(_i64 spos)
	{
		return cowfile->Seek(offset+spos);
	}

	virtual _i64 Size(void)
	{
		return static_cast<_i64>(cowfile->getSize());
	}

	virtual _i64 RealSize()
	{
		return static_cast<_i64>(cowfile->usedSize());
	}

	virtual std::string getFilename(void)
	{
		return cowfile->getFilename();
	}

	virtual std::wstring getFilenameW(void)
	{
		return cowfile->getFilenameW();
	}

private:
	int64 offset;
	CowFile* cowfile;
};


}


CowFile::CowFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize)
 : bitmap_dirty(false), finished(false), curr_offset(0)
{
	filename = Server->ConvertToUTF8(fn);
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
					Server->Log("Truncating cow file to size "+nconvert(filesize)+" failed", LL_ERROR);
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

CowFile::CowFile(const std::wstring &fn, const std::wstring &parent_fn, bool pRead_only)
	: bitmap_dirty(false), finished(false), curr_offset(0)
{
	filename = Server->ConvertToUTF8(fn);
	read_only = pRead_only;


	struct stat64 statbuf;
	int rc = stat64(Server->ConvertToUTF8(parent_fn).c_str(), &statbuf);
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
			is_open = loadBitmap(Server->ConvertToUTF8(parent_fn)+".bitmap");
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
			is_open = create_reflink(filename, Server->ConvertToUTF8(parent_fn));
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
						Server->Log("Truncating cow file to parent size "+nconvert(filesize)+" failed", LL_ERROR);
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
		Server->Log("Seeking in file failed. errno="+nconvert(errno), LL_ERROR);
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

_u32 CowFile::Write(const char* buffer, _u32 bsize)
{
	if(!is_open) return 0;

	ssize_t w=write(fd, buffer, bsize);
	if( w<0 )
	{
		Server->Log("Write to CowFile failed. errno="+nconvert(errno), LL_DEBUG);
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

std::wstring CowFile::getFilenameW(void)
{
	return Server->ConvertToUnicode(filename);
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
	return true;
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

bool CowFile::setUnused(_i64 unused_start, _i64 unused_end)
{
	int rc = fallocate64(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, unused_start, unused_end-unused_start);
	if(rc==0)
	{
		return true;
	}
	else
	{
		Server->Log("fallocate failed (setting unused image range) with errno "+nconvert((int)errno), LL_WARNING);
		return false;
	}
}

bool CowFile::trimUnused(_i64 fs_offset, ITrimCallback* trim_callback)
{
	FileWrapper devfile(this, fs_offset);
	FSNTFS ntfs(&devfile);

	if(ntfs.hasError())
	{
		Server->Log("Error opening NTFS bitmap. Cannot trim.", LL_WARNING);
		return false;
	}

	int64 unused_start_block = -1;

	for(int64 ntfs_block=0, n_ntfs_blocks = ntfs.getSize()/ntfs.getBlocksize();
			ntfs_block<n_ntfs_blocks; ++ntfs_block)
	{
		if(!ntfs.readBlock(ntfs_block, NULL))
		{
			if(unused_start_block==-1)
			{
				unused_start_block=ntfs_block;
			}
		}
		else if(unused_start_block!=-1)
		{
			int64 unused_start = fs_offset + unused_start_block*ntfs.getBlocksize();
			int64 unused_end = fs_offset + ntfs_block*ntfs.getBlocksize();
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
			unused_start_block=-1;
		}
	}

	return true;
}

bool CowFile::syncBitmap(_i64 fs_offset)
{
	FileWrapper devfile(this, fs_offset);
	FSNTFS ntfs(&devfile);

	if(ntfs.hasError())
	{
		Server->Log("Error opening NTFS bitmap. Cannot synchronize bitmap.", LL_WARNING);
		return false;
	}

	int64 used_start_block=-1;

	for(int64 ntfs_block=0, n_ntfs_blocks = ntfs.getSize()/ntfs.getBlocksize();
				ntfs_block<n_ntfs_blocks; ++ntfs_block)
	{
		if(ntfs.readBlock(ntfs_block, NULL))
		{
			used_start_block=ntfs_block;
		}
		else if(used_start_block!=-1)
		{
			int64 used_start = fs_offset + used_start_block*ntfs.getBlocksize();
			int64 used_end = fs_offset + ntfs_block*ntfs.getBlocksize();
			setBitmapRange(used_start, used_end, true);
		}
	}
}
