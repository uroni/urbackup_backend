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

void getMousePos(int &x, int &y)
{
	x=0;
	y=0;
}

std::vector<SFile> getFiles(const std::wstring &path)
{
	std::string upath=Server->ConvertToUTF8(path);
	std::vector<SFile> tmp;
	DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(upath.c_str())) == NULL)
	{
		Server->Log("No permission to access \""+upath+"\"", LL_ERROR);
        return tmp;
    }
	
	upath+=os_file_sepn();

    while ((dirp = readdir(dp)) != NULL)
	{
		SFile f;
		f.name=widen(dirp->d_name);
		if(f.name==L"." || f.name==L".." )
			continue;
		
		f.isdir=(dirp->d_type==DT_DIR);
		if(!f.isdir || dirp->d_type==DT_UNKNOWN)
		{
			struct stat64 f_info;
			int rc=stat64((upath+dirp->d_name).c_str(), &f_info);
			if(rc==0)
			{
				if(dirp->d_type==DT_UNKNOWN)
				{
					f.isdir=S_ISDIR(f_info.st_mode);
					if(!f.isdir)
					{
						f.last_modified=f_info.st_mtime;
						f.size=f_info.st_size;
					}
				}
				else
				{
					f.last_modified=f_info.st_mtime;
					f.size=f_info.st_size;
				}
			}
			else
			{
				Server->Log("No permission to stat \""+upath+dirp->d_name+"\"", LL_ERROR);
				f.last_modified=0;
				f.size=0;
			}
		}
		else
		{
			f.last_modified=0;
			f.size=0;
		}
		tmp.push_back(f);
    }
    closedir(dp);
	
	std::sort(tmp.begin(), tmp.end());
	
    return tmp;
}

void removeFile(const std::wstring &path)
{
	unlink(wnarrow(path).c_str());
}

void moveFile(const std::wstring &src, const std::wstring &dst)
{
	rename(wnarrow(src).c_str(), wnarrow(dst).c_str() );
}

bool isDirectory(const std::wstring &path)
{
        struct stat64 f_info;
		int rc=stat64(Server->ConvertToUTF8(path).c_str(), &f_info);
		if(rc!=0)
		{
			Server->Log(L"No permission to access \""+path+L"\"", LL_ERROR);
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

bool os_create_hardlink(const std::wstring &linkname, const std::wstring &fname)
{
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

bool os_directory_exists(const std::wstring &path)
{
	//std::string upath=Server->ConvertToUTF8(path);
	//DIR *dp=opendir(upath.c_str());
	//closedir(dp);
	//return dp!=NULL;
	return isDirectory(path);
}

bool os_remove_nonempty_dir(const std::wstring &path)
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
			if(dirp->d_type & DT_DIR )
			{
				subdirs.push_back(Server->ConvertToUnicode(dirp->d_name));
			}
			else
			{
				unlink((upath+"/"+(std::string)dirp->d_name).c_str());
			}
		}
    }
    closedir(dp);
    for(size_t i=0;i<subdirs.size();++i)
    {
	bool b=os_remove_nonempty_dir(path+L"/"+subdirs[i]);
	if(!b)
	    ok=false;
    }
    rmdir(upath.c_str());
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

bool os_link_symbolic(const std::wstring &target, const std::wstring &lname)
{
	return symlink(Server->ConvertToUTF8(target).c_str(), Server->ConvertToUTF8(lname).c_str())==0;
}