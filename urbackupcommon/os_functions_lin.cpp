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

#include "os_functions.h"
#include "../stringtools.h"
#include "server_compat.h"
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <utime.h>
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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include "../config.h"
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
#include <stack>

#if defined(__FreeBSD__) || defined(__APPLE__)
#define lstat64 lstat
#define stat64 stat
#define statvfs64 statvfs
#define open64 open
#define readdir64 readdir
#define dirent64 dirent
#define fsblkcnt64_t fsblkcnt_t
#endif

#if defined(__ANDROID__)
#define fsblkcnt64_t fsblkcnt_t
#include "android_popen.h"
#endif


void getMousePos(int &x, int &y)
{
	x=0;
	y=0;
}

std::vector<SFile> getFilesWin(const std::string &path, bool *has_error, bool exact_filesize, bool with_usn, bool ignore_other_fs)
{
	return getFiles(path, has_error, ignore_other_fs);
}

std::vector<SFile> getFiles(const std::string &path, bool *has_error, bool ignore_other_fs)
{
	if(has_error!=NULL)
	{
		*has_error=false;
	}
    std::string upath=(path);
	std::vector<SFile> tmp;
	DIR *dp;
    struct dirent64 *dirp;
    if((dp  = opendir(upath.c_str())) == NULL)
	{
		if(has_error!=NULL)
		{
			*has_error=true;
		}
		std::string errmsg;
		int err = os_last_error(errmsg);
		Log("Cannot open \""+path+"\": "+errmsg+" ("+convert(err)+")", LL_ERROR);
        return tmp;
    }
	
	dev_t parent_dev_id;
	bool has_parent_dev_id=false;
	if(ignore_other_fs)
	{
		struct stat64 f_info;
		int rc=lstat64(upath.c_str(), &f_info);
		if(rc==0)
		{
			has_parent_dev_id = true;
			parent_dev_id = f_info.st_dev; 
		}
	}
	
	upath+=os_file_sep();

    errno=0;
	std::string last_err_fn;
    while ((dirp = readdir64(dp)) != NULL)
	{
		SFile f;
        f.name=(dirp->d_name);
		if(f.name=="." || f.name==".." )
			continue;		

		f.isdir=(dirp->d_type==DT_DIR);
		
		struct stat64 f_info;
		int rc=lstat64((upath+dirp->d_name).c_str(), &f_info);
		if(rc==0)
		{	
			f.isdir = S_ISDIR(f_info.st_mode);
			
			if(ignore_other_fs && S_ISDIR(f_info.st_mode)
				&& has_parent_dev_id && parent_dev_id!=f_info.st_dev)
			{
				continue;
			}
			
			if(S_ISLNK(f_info.st_mode))
			{
				f.issym=true;
				f.isspecialf=true;
				struct stat64 l_info;
				int rc2 = stat64((upath+dirp->d_name).c_str(), &l_info);
				
				if(rc2==0)
				{
					f.isdir=S_ISDIR(l_info.st_mode);
				}
				else
				{
					f.isdir=false;
				}
			}
			
			f.usn = (uint64)f_info.st_mtime | ((uint64)f_info.st_ctime<<32);
			
			if(!f.isdir)
			{
				if(!S_ISREG(f_info.st_mode) )
				{
					f.isspecialf=true;
				}			
				
				f.size=f_info.st_size;
			}
			
			f.last_modified=f_info.st_mtime;
			f.created = f_info.st_ctime;
			f.accessed = f_info.st_atime;
		}
		else
		{
			std::string errmsg;
			int err = os_last_error(errmsg);
			Log("Cannot stat \""+upath+dirp->d_name+"\": "+(errmsg)+" ("+convert(err)+")", LL_ERROR);
			if(has_error!=NULL)
			{
				*has_error=true;
			}
			if(!last_err_fn.empty() &&
				last_err_fn==dirp->d_name)
			{
				Log("Returned again after error \""+upath+dirp->d_name+"\". Stopping directory iteration.", LL_ERROR);
				break;
			}
			else
			{
				errno=0;
				last_err_fn = dirp->d_name;
				continue;
			}
		}
		tmp.push_back(f);
		errno=0;
    }
    
    if(errno!=0)
    {
		std::string errmsg;
		int err = os_last_error(errmsg);
	    Log("Error listing files in directory \""+path+"\": "+errmsg+" ("+convert(err)+")", LL_ERROR);
		if(has_error!=NULL)
			*has_error=true;
    }
    
    closedir(dp);
	
	std::sort(tmp.begin(), tmp.end());
	
    return tmp;
}

bool removeFile(const std::string &path)
{
    return unlink((path).c_str())==0;
}

bool moveFile(const std::string &src, const std::string &dst)
{
    return rename((src).c_str(), (dst).c_str() )==0;
}

bool os_remove_symlink_dir(const std::string &path)
{
    return unlink((path).c_str())==0;
}

bool os_remove_dir(const std::string &path)
{
	return rmdir(path.c_str())==0;
}

bool isDirectory(const std::string &path, void* transaction)
{
        struct stat64 f_info;
        int rc=stat64(path.c_str(), &f_info);
		if(rc!=0)
		{
            rc = lstat64(path.c_str(), &f_info);
			if(rc!=0)
			{
				return false;
			}
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

int os_get_file_type(const std::string &path)
{
	int ret = 0;
	struct stat64 f_info;
    int rc1=stat64((path).c_str(), &f_info);
	if(rc1==0)
	{
		if ( S_ISDIR(f_info.st_mode) )
        {
			ret |= EFileType_Directory;
		}
		else
		{
			ret |= EFileType_File;
		}
	}

    int rc2 = lstat64((path).c_str(), &f_info);
	if(rc2==0)
	{
		if(S_ISLNK(f_info.st_mode))
		{
			ret |= EFileType_Symlink;
		}
		
		if(!S_ISDIR(f_info.st_mode)
			&& !S_ISREG(f_info.st_mode) )
		{
			ret |= EFileType_Special;
		}
		
		if(rc1!=0)
		{
			ret |= EFileType_File;
		}
	}
	
	return ret;
}

int64 os_atoi64(const std::string &str)
{
	return strtoll(str.c_str(), NULL, 10);
}

bool os_create_dir(const std::string &path)
{
	return mkdir(path.c_str(), S_IRWXU | S_IRWXG)==0;
}

bool os_create_reflink(const std::string &linkname, const std::string &fname)
{
#ifndef sun
    int src_desc=open64((fname).c_str(), O_RDONLY);
	if( src_desc<0)
	{
        Log("Error opening source file. errno="+convert(errno), LL_INFO);
	    return false;
	}

    int dst_desc=open64((linkname).c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG);
	if( dst_desc<0 )
	{
        Log("Error opening destination file. errno="+convert(errno), LL_INFO);
	    close(src_desc);
	    return false;
	}

#define BTRFS_IOCTL_MAGIC 0x94
#define BTRFS_IOC_CLONE _IOW (BTRFS_IOCTL_MAGIC, 9, int)
	
	int rc=ioctl(dst_desc, BTRFS_IOC_CLONE, src_desc);
	
	if(rc)
	{
        Log("Reflink ioctl failed. errno="+convert(errno), LL_INFO);
	}

	close(src_desc);
	close(dst_desc);
	
	if(rc)
	{
        if(unlink((linkname).c_str()))
		{
            Log("Removing destination file failed. errno="+convert(errno), LL_INFO);
		}
	}
	
	return rc==0;
#else
	return false;
#endif
}

bool os_create_hardlink(const std::string &linkname, const std::string &fname, bool use_ioref, bool* too_many_links)
{
	if(too_many_links!=NULL)
		*too_many_links=false;
		
	if( use_ioref )
		return os_create_reflink(linkname, fname);
		
    int rc=link((fname).c_str(), (linkname).c_str());
	return rc==0;
}

int64 os_free_space(const std::string &path)
{
	std::string cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='/')
		cp+='/';

	struct statvfs64 buf = {};
    int rc=statvfs64((path).c_str(), &buf);
	if(rc==0)
	{
		fsblkcnt64_t blocksize = buf.f_frsize ? buf.f_frsize : buf.f_bsize;
		fsblkcnt64_t free = blocksize*buf.f_bavail;
		if(free>LLONG_MAX)
		{
			return LLONG_MAX;
		}
		return free;
	}
	else
	{
		return -1;
	}
}

int64 os_total_space(const std::string &path)
{
	std::string cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='/')
		cp+='/';

	struct statvfs64 buf;
    int rc=statvfs64((path).c_str(), &buf);
	if(rc==0)
	{
		fsblkcnt64_t used=buf.f_blocks-buf.f_bfree;
		fsblkcnt64_t total = (used+buf.f_bavail)*buf.f_bsize;
		if(total>LLONG_MAX)
		{
			return LLONG_MAX;
		}
		return total;
	}
	else
		return -1;
}

bool os_directory_exists(const std::string &path)
{
    //std::string upath=(path);
	//DIR *dp=opendir(upath.c_str());
	//closedir(dp);
	//return dp!=NULL;
	return isDirectory(path);
}

//TODO: fix returning error and check users
bool os_remove_nonempty_dir(const std::string &root_path, os_symlink_callback_t symlink_callback, void* userdata, bool delete_root)
{
	if(delete_root)
	{
		struct stat64 f_info;
		int rc = lstat64(root_path.c_str(), &f_info);
		if(rc==0 && S_ISLNK(f_info.st_mode))
		{
			if(unlink(root_path.c_str())!=0)
			{
				Log("Error deleting symlink \""+root_path+"\" (root)", LL_ERROR);
			}
			return true;
		}
	}

	std::stack<std::pair<std::string, bool> > paths;

	paths.push(std::make_pair(root_path, false));
	bool ok=true;

	while(!paths.empty())
	{
		std::string upath=paths.top().first;
		std::string path = upath;

		if(paths.top().second)
		{
			if(paths.size()>1
				|| delete_root)
			{
				if(rmdir(upath.c_str())!=0)
				{
						Log("Error deleting directory \""+upath+"\"", LL_ERROR);
				}
			}
			paths.pop();
			continue;
		}
		
		paths.top().second=true;

		DIR *dp;
		struct dirent *dirp;
		if((dp  = opendir(upath.c_str())) == NULL)
		{
			Log("No permission to access \""+upath+"\"", LL_ERROR);
			paths.pop();
			ok=false;
			continue;
		}
	
		bool ok=true;
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
						if(S_ISLNK(f_info.st_mode))
						{
							if(symlink_callback!=NULL)
							{
								symlink_callback((upath+"/"+(std::string)dirp->d_name), NULL, userdata);
							}
							else
							{
								if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
								{
									Log("Error deleting symlink \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
								}
							}
						}
						else if(S_ISDIR(f_info.st_mode) )
						{
							paths.push(std::make_pair(upath+"/"+std::string(dirp->d_name), false));
						}
						else
						{
							if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
							{
								Log("Error deleting file \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
							}
						}
					}
					else
					{
						std::string e=convert(errno);
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
						Log("No permission to stat \""+upath+"/"+dirp->d_name+"\" error: "+e, LL_ERROR);
					}
	#ifndef sun
				}
				else if(dirp->d_type==DT_DIR )
				{
					paths.push(std::make_pair(upath+"/"+std::string(dirp->d_name), false));
				}
				else if(dirp->d_type==DT_LNK )
				{
					if(symlink_callback!=NULL)
					{
						symlink_callback((upath+"/"+(std::string)dirp->d_name), NULL, userdata);
					}
					else
					{
						if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
						{
							Log("Error deleting symlink \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
						}
					}
				}
				else
				{
					if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
					{
						Log("Error deleting file \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
					}
				}
	#endif
			}
		}
		closedir(dp);
	}

	return ok;
}

std::string os_file_sep(void)
{
	return "/";
}

bool os_link_symbolic(const std::string &target, const std::string &lname, void* transaction, bool* isdir)
{
    return symlink(target.c_str(), lname.c_str())==0;
}

#ifndef OS_FUNC_NO_NET
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
		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addrinfo* h;
		if(getaddrinfo(pServer.c_str(), NULL, &hints, &h)==0)
		{
			if(h!=NULL)
			{
				in_addr tmp;
				if(h->ai_addrlen>=sizeof(sockaddr_in))
				{
					*dest=reinterpret_cast<sockaddr_in*>(h->ai_addr)->sin_addr.s_addr;
					freeaddrinfo(h);
					return true;
				}				
				else
				{
					freeaddrinfo(h);
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
			return false;
		}
	}
	return true;
}

bool os_lookuphostname(std::string pServer, SLookupResult & dest)
{
	const char* host = pServer.c_str();
	unsigned int addr = inet_addr(host);
	if (addr != INADDR_NONE)
	{
		dest.is_ipv6 = false;
		dest.addr_v4 = addr;
		return true;
	}
	int rc = inet_pton(AF_INET6, host, dest.addr_v6);
	if (rc == 1)
	{
		dest.is_ipv6 = true;
		return true;
	}

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* h;
	if (getaddrinfo(pServer.c_str(), NULL, &hints, &h) == 0)
	{
		addrinfo* orig_h = h;
		while (h != NULL)
		{
			if (h->ai_family != AF_INET6
				&& h->ai_family != AF_INET)
			{
				h = h->ai_next;
				continue;
			}

			if (h->ai_family == AF_INET6)
			{
				if (h->ai_addrlen >= sizeof(sockaddr_in6))
				{
					dest.is_ipv6 = true;
					memcpy(dest.addr_v6, &reinterpret_cast<sockaddr_in6*>(h->ai_addr)->sin6_addr, 16);
					freeaddrinfo(orig_h);
					return true;
				}
				else
				{
					freeaddrinfo(orig_h);
					return false;
				}
			}
			else
			{
				if (h->ai_addrlen >= sizeof(sockaddr_in))
				{
					dest.is_ipv6 = false;
					dest.addr_v4 = reinterpret_cast<sockaddr_in*>(h->ai_addr)->sin_addr.s_addr;
					freeaddrinfo(orig_h);
					return true;
				}
				else
				{
					freeaddrinfo(orig_h);
					return false;
				}
			}

			h = h->ai_next;
		}
		freeaddrinfo(orig_h);
		return false;
	}
	else
	{
		return false;
	}
}
#endif //OS_FUNC_NO_NET

std::string os_file_prefix(std::string path)
{
	return path;
}

bool os_file_truncate(const std::string &fn, int64 fsize)
{
    if( truncate((fn).c_str(), (off_t)fsize) !=0 )
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

bool os_create_dir_recursive(std::string fn)
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

bool os_rename_file(std::string src, std::string dst, void* transaction)
{
    int rc=rename(src.c_str(), dst.c_str());
	return rc==0;
}

bool os_get_symlink_target(const std::string &lname, std::string &target)
{
	struct stat sb;
	if(lstat(lname.c_str(), &sb)==-1)
	{
		return false;
	}
	
	target.resize(sb.st_size);
	
	ssize_t rc = readlink(lname.c_str(), &target[0], sb.st_size);
	
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
		target.resize(rc);
	}
	
	return true;
}

bool os_is_symlink(const std::string &lname)
{
	struct stat sb;
    if(lstat(lname.c_str(), &sb)==-1)
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

std::string os_format_errcode(int64 errcode)
{
	int err = static_cast<int>(errcode);
	char* str = strerror(err);
	if(str!=NULL)
	{
		return str;
	}
	return std::string();
}

int64 os_last_error(std::string& message)
{
	int err = errno;
	message = os_format_errcode(err);
	return err;
}

const int64 WINDOWS_TICK=10000000;
const int64 SEC_TO_UNIX_EPOCH=11644473600LL;

int64 os_windows_to_unix_time(int64 windows_filetime)
{	
	return windows_filetime / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;
}

int64 os_to_windows_filetime(int64 unix_time)
{
	return (unix_time+SEC_TO_UNIX_EPOCH)*WINDOWS_TICK;
}

bool os_set_file_time(const std::string& fn, int64 created, int64 last_modified, int64 accessed)
{
	time_t atime = static_cast<time_t>(accessed);
	time_t mtime = static_cast<time_t>(last_modified);

#if !defined(__APPLE__) && !defined(__FreeBSD__)	
#if defined(HAVE_UTIMENSAT)
	timespec tss[2];
	tss[0].tv_sec = atime;
	tss[0].tv_nsec = 0;
	tss[1].tv_sec = mtime;
	tss[1].tv_nsec = 0;
	
	int rc = utimensat(0, fn.c_str(), tss, AT_SYMLINK_NOFOLLOW);
#else
	struct timeval tv[2];
    tv[0].tv_sec = atime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = mtime;
    tv[1].tv_usec = 0;

	int rc = utimes(fn.c_str(), tv);
#endif
	return rc==0;
#else
	struct timeval tv[2];
    tv[0].tv_sec = atime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = mtime;
    tv[1].tv_usec = 0;
	
	int rc = lutimes(fn.c_str(), tv);
	return rc==0;
#endif
}

#ifndef OS_FUNC_NO_SERVER
bool copy_file(const std::string &src, const std::string &dst, bool flush, std::string* error_str)
{
	IFile *fsrc=Server->openFile(src, MODE_READ);
	if (fsrc == NULL)
	{
		if (error_str != NULL)
		{
			*error_str = os_last_error_str();
		}
		return false;
	}
	IFile *fdst=Server->openFile(dst, MODE_WRITE);
	if(fdst==NULL)
	{
		if (error_str != NULL)
		{
			*error_str = os_last_error_str();
		}
		Server->destroy(fsrc);
		return false;
	}

	bool copy_ok = copy_file(fsrc, fdst, error_str);

	if (copy_ok && flush)
	{
		copy_ok = fdst->Sync();

		if (!copy_ok
			&& error_str!=NULL)
		{
			*error_str = os_last_error_str();
		}
	}

	Server->destroy(fsrc);
	Server->destroy(fdst);

	return copy_ok;
}

bool copy_file(IFile *fsrc, IFile *fdst, std::string* error_str)
{
	if(fsrc==NULL || fdst==NULL)
	{
		return false;
	}

	if(!fsrc->Seek(0))
	{
		return false;
	}

	if(!fdst->Seek(0))
	{
		return false;
	}

	char buf[4096];
	size_t rc;
	bool has_error=false;
	while( (rc=(_u32)fsrc->Read(buf, 4096, &has_error))>0)
	{
		if(has_error)
		{
			if (error_str != NULL)
			{
				*error_str = os_last_error_str();
			}
			break;
		}
		
		if(rc>0)
		{
			fdst->Write(buf, (_u32)rc, &has_error);

			if(has_error)
			{
				if (error_str != NULL)
				{
					*error_str = os_last_error_str();
				}
				break;
			}
		}
	}

	if(has_error)
	{
		return false;
	}
	else
	{
		return true;
	}
}
#endif //OS_FUNC_NO_SERVER

SFile getFileMetadataWin( const std::string &path, bool with_usn)
{
	return getFileMetadata(path);
}

SFile getFileMetadata( const std::string &path )
{
	SFile ret;
	ret.name=path;

	struct stat64 f_info;
    int rc=lstat64((path).c_str(), &f_info);

	if(rc==0)
	{
		if(S_ISDIR(f_info.st_mode) )
		{
			ret.isdir = true;
		}

		if (S_ISLNK(f_info.st_mode))
		{
			ret.issym = true;
			ret.isspecialf = true;
		}

		if (!ret.isdir && !S_ISREG(f_info.st_mode))
		{
			ret.isspecialf = true;
		}

		ret.size = f_info.st_size;
		ret.last_modified = f_info.st_mtime;
		ret.created = f_info.st_ctime;
		ret.accessed = f_info.st_atime;

		return ret;
	}
	else
	{
		return SFile();
	}
}

std::string os_get_final_path(std::string path)
{
    char* retptr = realpath((path).c_str(), NULL);
	if(retptr==NULL)
	{
		return path;
	}
    std::string ret = (retptr);
	free(retptr);
	return ret;
}

bool os_path_absolute(const std::string& path)
{
    if(!path.empty() && path[0]=='/')
    {
        return true;
    }
    else
    {
        return false;
    }
}

int os_popen(const std::string& cmd, std::string& ret)
{
	ret.clear();

#ifdef __ANDROID__
    POFILE* pin = NULL;
#endif

	FILE* in = NULL;

#ifndef _WIN32
#define _popen popen
#define _pclose pclose
#endif

#ifdef __ANDROID__
    pin = and_popen(cmd.c_str(), "r");
    if(pin!=NULL) in=pin->fp;
#elif __linux__
	in = _popen(cmd.c_str(), "re");
	if(!in) in = _popen(cmd.c_str(), "r");
#else
	in = _popen(cmd.c_str(), "r");
#endif

	if(in==NULL)
	{
		return -1;
	}

	char buf[4096];
	size_t read;
	do
	{
		read=fread(buf, 1, sizeof(buf), in);
		if(read>0)
		{
			ret.append(buf, buf+read);
		}
	}
	while(read==sizeof(buf));

#ifdef __ANDROID__
    return and_pclose(pin);
#else
    return _pclose(in);
#endif
}

std::string os_last_error_str()
{
	std::string msg;
	int64 code = os_last_error(msg);
	
	if(code==0)
	{
		return "Code 0";
	}
	
	return trim(msg) + " (code: " + convert(code) + ")";
}

#ifdef __linux__
#if defined(__i386__)
#define __NR_ioprio_set		289
#define __NR_ioprio_get		290
#elif defined(__ppc__)
#define __NR_ioprio_set		273
#define __NR_ioprio_get		274
#elif defined(__x86_64__)
#define __NR_ioprio_set		251
#define __NR_ioprio_get		252
#elif defined(__ia64__)
#define __NR_ioprio_set		1274
#define __NR_ioprio_get		1275
#endif
#endif //__linux__

#ifdef __NR_ioprio_set

static inline int ioprio_set(int which, int who, int ioprio)
{
	return syscall(__NR_ioprio_set, which, who, ioprio);
}

static inline int ioprio_get(int which, int who)
{
	return syscall(__NR_ioprio_get, which, who);
}

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

#define IOPRIO_CLASS_SHIFT	13

struct SPrioInfoInt
{
	int io_prio;
	int cpu_prio;
};


SPrioInfo::SPrioInfo()
 : prio_info(new SPrioInfoInt)
{
}

SPrioInfo::~SPrioInfo()
{
	delete prio_info;
}

bool os_enable_background_priority(SPrioInfo& prio_info)
{	
	if(getpid() == syscall(SYS_gettid))
	{
		//This would set it for the whole process
		return false;
	}
	
	prio_info.prio_info->io_prio = ioprio_get(IOPRIO_WHO_PROCESS, 0);
	prio_info.prio_info->cpu_prio = getpriority(PRIO_PROCESS, 0);
	
	int ioprio = 7;
	int ioprio_class = IOPRIO_CLASS_IDLE;
	
	if(ioprio_set(IOPRIO_WHO_PROCESS, 0, ioprio | ioprio_class << IOPRIO_CLASS_SHIFT)==-1)
	{
		return false;
	}
	int cpuprio = 19;
	if(setpriority(PRIO_PROCESS, 0, cpuprio)==-1)
	{
		os_disable_background_priority(prio_info);
		return false;
	}
	
	return true;
}

bool os_disable_background_priority(SPrioInfo& prio_info)
{
	if(prio_info.prio_info==NULL)
	{
		return false;
	}
		
	int io_prio =  prio_info.prio_info->io_prio;
	
	//Setting a IO prio with IOPRIO_CLASS_NONE fails
	if(io_prio>>IOPRIO_CLASS_SHIFT == IOPRIO_CLASS_NONE)
	{
		io_prio|=IOPRIO_CLASS_BE<<IOPRIO_CLASS_SHIFT;
	}
	
	if(ioprio_set(IOPRIO_WHO_PROCESS, 0, io_prio)==-1)
	{
		return false;
	}
	
	if(setpriority(PRIO_PROCESS, 0, prio_info.prio_info->cpu_prio)==-1)
	{
		return false;
	}
	
	return true;
}

bool os_enable_prioritize(SPrioInfo& prio_info, EPrio prio)
{	
	if(getpid() == syscall(SYS_gettid))
	{
		//This would set it for the whole process
		return false;
	}

	prio_info.prio_info->io_prio = ioprio_get(IOPRIO_WHO_PROCESS, 0);
	prio_info.prio_info->cpu_prio = getpriority(PRIO_PROCESS, 0);
	
	int ioprio = 0;
	int ioprio_class = IOPRIO_CLASS_BE;
	int cpuprio = -10;
	
	if(prio==Prio_SlightPrioritize)
	{
		ioprio=2;
		cpuprio=-3;
	}
	else if(prio==Prio_SlightBackground)
	{
		ioprio=7;
		cpuprio=5;
	}
	
	if(ioprio_set(IOPRIO_WHO_PROCESS, 0, ioprio | ioprio_class << IOPRIO_CLASS_SHIFT)==-1)
	{
		return false;
	}	
	if(setpriority(PRIO_PROCESS, 0, cpuprio)==-1)
	{
		os_disable_prioritize(prio_info);
		return false;
	}
	
	return true;
}

void assert_process_priority()
{
	int io_prio = ioprio_get(IOPRIO_WHO_PROCESS, 0);
	int cpu_prio = getpriority(PRIO_PROCESS, 0);
	
	assert( io_prio == (4 | IOPRIO_CLASS_BE<<IOPRIO_CLASS_SHIFT)
               || io_prio== (4 | IOPRIO_CLASS_NONE<<IOPRIO_CLASS_SHIFT));
	assert( cpu_prio==0 );
}

bool os_disable_prioritize(SPrioInfo& prio_info)
{
	return os_disable_background_priority(prio_info);
}

void os_reset_priority()
{
	ioprio_set(IOPRIO_WHO_PROCESS, 0, 4 | IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT);
	setpriority(PRIO_PROCESS, 0, 0);
}

#else //__NR_ioprio_set

SPrioInfo::SPrioInfo()
{
}

SPrioInfo::~SPrioInfo()
{
}

bool os_enable_background_priority(SPrioInfo& prio_info)
{
	return false;
}

bool os_disable_background_priority(SPrioInfo& prio_info)
{
	return false;
}

bool os_enable_prioritize(SPrioInfo& prio_info, EPrio prio)
{
	return false;
}

bool os_disable_prioritize(SPrioInfo& prio_info)
{
	return false;
}

void assert_process_priority()
{
}

void os_reset_priority()
{
}

#endif //__NR_ioprio_set

#define BTRFS_IOC_SYNC _IO(BTRFS_IOCTL_MAGIC, 8)

bool os_sync(const std::string & path)
{
#if defined(__linux__)
	int fd = open(path.c_str(), O_RDONLY|O_CLOEXEC);
	
	if(fd!=-1)
	{
		if(ioctl(fd, BTRFS_IOC_SYNC, NULL)==-1)
		{
			if(errno!=ENOTTY && errno!=ENOSYS 
				&& errno!=EINVAL)
			{
				close(fd);
				return false;
			}
		}
		else
		{
			close(fd);
			return true;
		}

#if defined(HAVE_SYNCFS)
		if(syncfs(fd)!=0)
		{
			if(errno==ENOSYS)
			{
				close(fd);
				sync();
				return true;
			}
			close(fd);
			return false;
		}
		else
		{
			close(fd);
			return true;
		}
#else
		close(fd);
		sync();
		return true;
#endif
	}
	else
	{
		sync();
		return true;
	}
#else
	sync();
	return true;
#endif
}

size_t os_get_num_cpus()
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}

int os_system(const std::string& cmd)
{
#ifdef __ANDROID__
	return and_system(cmd.c_str());
#else
	return system(cmd.c_str());
#endif
}
