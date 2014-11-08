/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
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

#include "os_functions.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <algorithm>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#if defined(__FreeBSD__)
#define lstat64 lstat
#define stat64 stat
#define statvfs64 statvfs
#define open64 open
#endif

void getMousePos(int &x, int &y)
{
	x=0;
	y=0;
}

std::vector<SFile> getFiles(const std::wstring &path, bool *has_error, bool follow_symlinks, bool exact_filesize)
{
	if(has_error!=NULL)
	{
		*has_error=false;
	}
	std::string upath=Server->ConvertToUTF8(path);
	std::vector<SFile> tmp;
	DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(upath.c_str())) == NULL)
	{
		if(has_error!=NULL)
		{
			*has_error=true;
		}
		Server->Log("No permission to access \""+upath+"\"", LL_ERROR);
        return tmp;
    }
	
	upath+=os_file_sepn();

    while ((dirp = readdir(dp)) != NULL)
	{
		SFile f;
		f.name=Server->ConvertToUnicode(dirp->d_name);
		if(f.name==L"." || f.name==L".." )
			continue;
		
#ifndef sun
		f.isdir=(dirp->d_type==DT_DIR);
		if(!f.isdir || dirp->d_type==DT_UNKNOWN 
				|| (dirp->d_type!=DT_REG && dirp->d_type!=DT_DIR)
				|| dirp->d_type==DT_LNK )
		{
#endif
			struct stat64 f_info;
			int rc=stat64((upath+dirp->d_name).c_str(), &f_info);
			if(rc==0)
			{
#ifndef sun
				if(dirp->d_type==DT_UNKNOWN
					|| (dirp->d_type!=DT_REG && dirp->d_type!=DT_DIR)
					|| dirp->d_type==DT_LNK)
				{
#endif
					f.isdir=S_ISDIR(f_info.st_mode);
					if(!f.isdir)
					{
						if(!S_ISREG(f_info.st_mode) )
						{
							continue;
						}
						f.last_modified=f_info.st_mtime;
						if(f.last_modified<0) f.last_modified*=-1;
						f.size=f_info.st_size;
					}
					else
					{
						if(!follow_symlinks && S_ISLNK(f_info.st_mode) )
						{
							continue;
						}
					}
#ifndef sun
				}
				else
				{
					f.last_modified=f_info.st_mtime;
					if(f.last_modified<0) f.last_modified*=-1;
					f.size=f_info.st_size;
				}
#endif
			}
			else
			{
				Server->Log("No permission to stat \""+upath+dirp->d_name+"\"", LL_ERROR);
				continue;
			}
#ifndef sun
		}
		else
		{
			f.last_modified=0;
			f.size=0;
		}
#endif
		tmp.push_back(f);
    }
    closedir(dp);
	
	std::sort(tmp.begin(), tmp.end());
	
    return tmp;
}

void removeFile(const std::wstring &path)
{
	unlink(Server->ConvertToUTF8(path).c_str());
}

void moveFile(const std::wstring &src, const std::wstring &dst)
{
	rename(Server->ConvertToUTF8(src).c_str(), Server->ConvertToUTF8(dst).c_str() );
}

bool os_remove_symlink_dir(const std::wstring &path)
{
	return unlink(Server->ConvertToUTF8(path).c_str())==0;	
}

bool os_remove_dir(const std::string &path)
{
	return rmdir(path.c_str())==0;
}

bool os_remove_dir(const std::wstring &path)
{
	return rmdir(Server->ConvertToUTF8(path).c_str())==0;
}

bool isDirectory(const std::wstring &path)
{
        struct stat64 f_info;
		int rc=stat64(Server->ConvertToUTF8(path).c_str(), &f_info);
		if(rc!=0)
		{
			return false;
		}

        if ( S_ISDIR(f_info.st_mode) )
        {
                return true;
        }
        else
        {
                return false;
        }
}

int64 os_atoi64(const std::string &str)
{
	return strtoll(str.c_str(), NULL, 10);
}

bool os_create_dir(const std::wstring &dir)
{
	int rc=mkdir(Server->ConvertToUTF8(dir).c_str(), S_IRWXU | S_IRWXG );
	return rc==0;
}

bool os_create_dir(const std::string &path)
{
	return mkdir(path.c_str(), S_IRWXU | S_IRWXG)==0;
}

bool os_create_reflink(const std::wstring &linkname, const std::wstring &fname)
{
#ifndef sun
	int src_desc=open64(Server->ConvertToUTF8(fname).c_str(), O_RDONLY);
	if( src_desc<0)
	{
		Server->Log("Error opening source file. errno="+nconvert(errno));
	    return false;
	}

	int dst_desc=open64(Server->ConvertToUTF8(linkname).c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG);
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
	
	if(rc)
	{
		if(unlink(Server->ConvertToUTF8(linkname).c_str())
		{
			Server->Log("Removing destination file failed. errno="+nconvert(errno));
		}
	}
	
	return rc==0;
#else
	return false;
#endif
}

bool os_create_hardlink(const std::wstring &linkname, const std::wstring &fname, bool use_ioref, bool* too_many_links)
{
	if(too_many_links!=NULL)
		*too_many_links=false;
		
	if( use_ioref )
		return os_create_reflink(linkname, fname);
		
	int rc=link(Server->ConvertToUTF8(fname).c_str(), Server->ConvertToUTF8(linkname).c_str());
	return rc==0;
}

int64 os_free_space(const std::wstring &path)
{
	std::wstring cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='/')
		cp+='/';

	struct statvfs64 buf;
	int rc=statvfs64(Server->ConvertToUTF8(path).c_str(), &buf);
	if(rc==0)
		return buf.f_bsize*buf.f_bavail;
	else
		return -1;
}

int64 os_total_space(const std::wstring &path)
{
	std::wstring cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='/')
		cp+='/';

	struct statvfs64 buf;
	int rc=statvfs64(Server->ConvertToUTF8(path).c_str(), &buf);
	if(rc==0)
	{
		fsblkcnt_t used=buf.f_blocks-buf.f_bfree;
		return buf.f_bsize*(used+buf.f_bavail);
	}
	else
		return -1;
}

bool os_directory_exists(const std::wstring &path)
{
	//std::string upath=Server->ConvertToUTF8(path);
	//DIR *dp=opendir(upath.c_str());
	//closedir(dp);
	//return dp!=NULL;
	return isDirectory(path);
}

bool os_remove_nonempty_dir(const std::wstring &path, os_symlink_callback_t symlink_callback, void* userdata, bool delete_root)
{
	std::string upath=Server->ConvertToUTF8(path);
	std::vector<SFile> tmp;
	DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(upath.c_str())) == NULL)
	{
		Server->Log("No permission to access \""+upath+"\"", LL_ERROR);
        return false;
    }
	
	bool ok=true;
	std::vector<std::wstring> subdirs;
	while ((dirp = readdir(dp)) != NULL)
	{
		if( (std::string)dirp->d_name!="." && (std::string)dirp->d_name!=".." )
		{
#ifndef sun
			if(dirp->d_type==DT_UNKNOWN)
			{
#endif
				struct stat64 f_info;
				int rc=lstat64((upath+"/"+(std::string)dirp->d_name).c_str(), &f_info);
				if(rc==0)
				{
					if(S_ISDIR(f_info.st_mode) )
					{
						subdirs.push_back(Server->ConvertToUnicode(dirp->d_name));
					}
					else if(S_ISLNK(f_info.st_mode))
					{
						if(symlink_callback!=NULL)
						{
							symlink_callback(Server->ConvertToUnicode(upath+"/"+(std::string)dirp->d_name), userdata);
						}
						else
						{
							if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
							{
								Server->Log("Error deleting symlink \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
							}
						}
					}
					else
					{
						if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
						{
							Server->Log("Error deleting file \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
						}
					}
				}
				else
				{
					std::string e=nconvert(errno);
					switch(errno)
					{
					    case EACCES: e="EACCES"; break;
					    case EBADF: e="EBADF"; break;
					    case EFAULT: e="EFAULT"; break;
					    case ELOOP: e="ELOOP"; break;
					    case ENAMETOOLONG: e="ENAMETOOLONG"; break;
					    case ENOENT: e="ENOENT"; break;
					    case ENOMEM: e="ENOMEM"; break;
					    case ENOTDIR: e="ENOTDIR"; break;
					}
					Server->Log("No permission to stat \""+upath+"/"+dirp->d_name+"\" error: "+e, LL_ERROR);
				}
#ifndef sun
			}
			else if(dirp->d_type==DT_DIR )
			{
				subdirs.push_back(Server->ConvertToUnicode(dirp->d_name));
			}
			else if(dirp->d_type==DT_LNK )
			{
				if(symlink_callback!=NULL)
				{
					symlink_callback(Server->ConvertToUnicode(upath+"/"+(std::string)dirp->d_name), userdata);
				}
				else
				{
					if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
					{
						Server->Log("Error deleting symlink \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
					}
				}
			}
			else
			{
				if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
				{
					Server->Log("Error deleting file \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
				}
			}
#endif
		}
    }
    closedir(dp);
    for(size_t i=0;i<subdirs.size();++i)
    {
		bool b=os_remove_nonempty_dir(path+L"/"+subdirs[i], symlink_callback, userdata);
		if(!b)
		    ok=false;
    }
	if(delete_root)
	{
		if(rmdir(upath.c_str())!=0)
		{
			Server->Log("Error deleting directory \""+upath+"\"", LL_ERROR);
		}
	}
	return ok;
}

std::wstring os_file_sep(void)
{
	return L"/";
}

std::string os_file_sepn(void)
{
	return "/";
}

bool os_link_symbolic(const std::wstring &target, const std::wstring &lname, void* transaction)
{
	return symlink(Server->ConvertToUTF8(target).c_str(), Server->ConvertToUTF8(lname).c_str())==0;
}

bool os_lookuphostname(std::string pServer, unsigned int *dest)
{
	const char* host=pServer.c_str();
    unsigned int addr = inet_addr(host);
    if (addr != INADDR_NONE)
	{
        *dest = addr;
    }
	else
	{
		hostent* hp = gethostbyname(host);
        if (hp != 0)
		{
			in_addr tmp;
			memcpy(&tmp, hp->h_addr,  hp->h_length );
			*dest=tmp.s_addr;
		}
		else
		{
			return false;
		}
	}
	return true;
}

std::wstring os_file_prefix(std::wstring path)
{
	return path;
}

bool os_file_truncate(const std::wstring &fn, int64 fsize)
{
	if( truncate(Server->ConvertToUTF8(fn).c_str(), (off_t)fsize) !=0 )
	{
		return false;
	}
	return true;
}

std::string os_strftime(std::string fs)
{
	time_t rawtime;		
	char buffer [100];
	time ( &rawtime );
	struct tm *timeinfo;
	timeinfo = localtime ( &rawtime );
	strftime (buffer,100,fs.c_str(),timeinfo);
	std::string r(buffer);
	return r;
}

bool os_create_dir_recursive(std::wstring fn)
{
	if(fn.empty())
		return false;
		
	bool b=os_create_dir(fn);
	if(!b)
	{
		b=os_create_dir_recursive(ExtractFilePath(fn));
		if(!b)
			return false;

		return os_create_dir(fn);
	}
	else
	{
		return true;
	}
}

bool os_rename_file(std::wstring src, std::wstring dst, void* transaction)
{
	int rc=rename(Server->ConvertToUTF8(src).c_str(), Server->ConvertToUTF8(dst).c_str());
	return rc==0;
}

bool os_get_symlink_target(const std::wstring &lname, std::wstring &target)
{
	std::string lname_utf8 = Server->ConvertToUTF8(lname);
	struct stat sb;
	if(lstat(lname_utf8.c_str(), &sb)==-1)
	{
		return false;
	}
	
	std::string target_buf;
	
	target_buf.resize(sb.st_size);
	
	ssize_t rc = readlink(lname_utf8.c_str(), &target_buf[0], sb.st_size);
	
	if(rc<0)
	{
		return false;
	}
	
	if(rc > sb.st_size)
	{
		return false;
	}
	else if(rc<sb.st_size)
	{
		target_buf.resize(rc);
	}
	
	target = Server->ConvertToUnicode(target_buf);
	
	return true;
}

bool os_is_symlink(const std::wstring &lname)
{
	struct stat sb;
	if(lstat(Server->ConvertToUTF8(lname).c_str(), &sb)==-1)
	{
		return false;
	}
	
	return S_ISLNK(sb.st_mode);
}

void* os_start_transaction()
{
	return NULL;
}

bool os_finish_transaction(void* transaction)
{
	return false;
}

int64 os_last_error()
{
	return errno;
}
