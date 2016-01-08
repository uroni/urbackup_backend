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

#if defined(__FreeBSD__) || defined(__APPLE__)
#define lstat64 lstat
#define stat64 stat
#define statvfs64 statvfs
#define open64 open
#define readdir64 readdir
#define dirent64 dirent
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
				f.isspecial=true;
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
			if(f.usn<0) f.usn*=-1;
			
			if(!f.isdir)
			{
				if(!S_ISREG(f_info.st_mode) )
				{
					f.isspecial=true;
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
			continue;
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

void removeFile(const std::string &path)
{
    unlink((path).c_str());
}

void moveFile(const std::string &src, const std::string &dst)
{
    rename((src).c_str(), (dst).c_str() );
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
        int rc=stat64((path).c_str(), &f_info);
		if(rc!=0)
		{
            rc = lstat64((path).c_str(), &f_info);
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
		int64 blocksize = buf.f_frsize ? buf.f_frsize : buf.f_bsize;
		return blocksize*buf.f_bavail;
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
		fsblkcnt_t used=buf.f_blocks-buf.f_bfree;
		return buf.f_bsize*(used+buf.f_bavail);
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

bool os_remove_nonempty_dir(const std::string &path, os_symlink_callback_t symlink_callback, void* userdata, bool delete_root)
{
    std::string upath=(path);
	std::vector<SFile> tmp;
	DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(upath.c_str())) == NULL)
	{
        Log("No permission to access \""+upath+"\"", LL_ERROR);
        return false;
    }
	
	bool ok=true;
	std::vector<std::string> subdirs;
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
                            symlink_callback((upath+"/"+(std::string)dirp->d_name), userdata);
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
                        subdirs.push_back((dirp->d_name));
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
                subdirs.push_back((dirp->d_name));
			}
			else if(dirp->d_type==DT_LNK )
			{
				if(symlink_callback!=NULL)
				{
                    symlink_callback((upath+"/"+(std::string)dirp->d_name), userdata);
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
    for(size_t i=0;i<subdirs.size();++i)
    {
		bool b=os_remove_nonempty_dir(path+"/"+subdirs[i], symlink_callback, userdata);
		if(!b)
		    ok=false;
    }
	if(delete_root)
	{
		if(rmdir(upath.c_str())!=0)
		{
            Log("Error deleting directory \""+upath+"\"", LL_ERROR);
		}
	}
	return ok;
}

std::string os_file_sep(void)
{
	return "/";
}

bool os_link_symbolic(const std::string &target, const std::string &lname, void* transaction, bool* isdir)
{
    return symlink((target).c_str(), (lname).c_str())==0;
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
    int rc=rename((src).c_str(), (dst).c_str());
	return rc==0;
}

bool os_get_symlink_target(const std::string &lname, std::string &target)
{
    std::string lname_utf8 = (lname);
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
	
    target = (target_buf);
	
	return true;
}

bool os_is_symlink(const std::string &lname)
{
	struct stat sb;
    if(lstat((lname).c_str(), &sb)==-1)
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

int64 os_last_error(std::string& message)
{
	int err = errno;
	char* str = strerror(err);
	if(str!=NULL)
	{
		message = (str);
	}
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

	timespec tss[2];
	tss[0].tv_sec = atime;
	tss[0].tv_nsec = 0;
	tss[1].tv_sec = mtime;
	tss[1].tv_nsec = 0;
	
	int rc = utimensat(0, fn.c_str(), tss, AT_SYMLINK_NOFOLLOW);
	return rc==0;
}

#ifndef OS_FUNC_NO_SERVER
bool copy_file(const std::string &src, const std::string &dst)
{
    IFile *fsrc=Server->openFile(src, MODE_READ);
	if(fsrc==NULL) return false;
	IFile *fdst=Server->openFile(dst, MODE_WRITE);
	if(fdst==NULL)
	{
		Server->destroy(fsrc);
		return false;
	}

	bool copy_ok = copy_file(fsrc, fdst);

	Server->destroy(fsrc);
	Server->destroy(fdst);

	return copy_ok;
}

bool copy_file(IFile *fsrc, IFile *fdst)
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
		if(rc>0)
		{
			fdst->Write(buf, (_u32)rc, &has_error);

			if(has_error)
			{
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

	FILE* in = NULL;

#ifndef _WIN32
#define _popen popen
#define _pclose pclose
#endif

	in = _popen(cmd.c_str(), "r");

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

	return _pclose(in);
}

