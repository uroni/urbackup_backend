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

#if defined(__FreeBSD__) || defined(__APPLE__)
#define open64 open
#define stat64 stat
#define lstat64 lstat
#endif


void getMousePos(int &x, int &y)
{
	x=0;
	y=0;
}

#ifdef __linux__
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
			if(has_error!=NULL)
			{
				*has_error=true;
			}
			errno=0;
			continue;
		}
		tmp.push_back(f);
		errno=0;
    }
    
    if(errno!=0)
    {
		if(has_error!=NULL)
			*has_error=true;
    }
    
    closedir(dp);
	
	std::sort(tmp.begin(), tmp.end());
	
    return tmp;
}
#endif

bool os_create_reflink(const std::string &linkname, const std::string &fname)
{
#ifndef sun
	int src_desc=open64(fname.c_str(), O_RDONLY);
	if( src_desc<0)
	    return false;

	int dst_desc=open64(linkname.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG);
	if( dst_desc<0 )
	{
	    close(src_desc);
	    return false;
	}

#define BTRFS_IOCTL_MAGIC 0x94
#define BTRFS_IOC_CLONE _IOW (BTRFS_IOCTL_MAGIC, 9, int)
	
	int rc=ioctl(dst_desc, BTRFS_IOC_CLONE, src_desc);

	close(src_desc);
	close(dst_desc);
	
	return rc==0;
#else
	return false;
#endif
}

bool os_create_hardlink(const std::string &linkname, const std::string &fname, bool ioref, bool* too_many_links)
{
	if(too_many_links!=NULL)
		*too_many_links=false;

	if(ioref)
	    return os_create_reflink(linkname, fname);

	int rc=link(fname.c_str(), linkname.c_str());
	return rc==0;
}

std::string os_file_sep(void)
{
	return "/";
}

bool os_remove_dir(const std::string &path)
{
	return rmdir(path.c_str())==0;
}

bool os_create_dir(const std::string &path)
{
	return mkdir(path.c_str(), S_IRWXU | S_IRWXG)==0;
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

bool os_directory_exists(const std::string &path)
{
	return isDirectory(path);
}

