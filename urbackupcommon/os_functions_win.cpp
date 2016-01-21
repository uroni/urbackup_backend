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
#ifdef _WIN_PRE_VISTA
#define _WIN32_WINNT 0x0500
#endif
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <algorithm>

#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <time.h>
#include <assert.h>
#include <ws2tcpip.h>

#ifdef USE_NTFS_TXF
#include <KtmW32.h>
#endif
#include <shlwapi.h>

#include "server_compat.h"

//For PathIsRelative
#pragma comment(lib, "Shlwapi.lib")

namespace
{
	
#define REPARSE_MOUNTPOINT_HEADER_SIZE   8

	typedef struct {
		DWORD ReparseTag;
		DWORD ReparseDataLength;
		WORD Reserved;
		WORD ReparseTargetLength;
		WORD ReparseTargetMaximumLength;
		WORD Reserved1;
		WCHAR ReparseTarget[1];
	} REPARSE_MOUNTPOINT_DATA_BUFFER, *PREPARSE_MOUNTPOINT_DATA_BUFFER;

	typedef struct _REPARSE_DATA_BUFFER {
		ULONG  ReparseTag;
		USHORT ReparseDataLength;
		USHORT Reserved;
		union {
			struct {
				USHORT SubstituteNameOffset;
				USHORT SubstituteNameLength;
				USHORT PrintNameOffset;
				USHORT PrintNameLength;
				ULONG  Flags;
				WCHAR  PathBuffer[1];
			} SymbolicLinkReparseBuffer;
			struct {
				USHORT SubstituteNameOffset;
				USHORT SubstituteNameLength;
				USHORT PrintNameOffset;
				USHORT PrintNameLength;
				WCHAR  PathBuffer[1];
			} MountPointReparseBuffer;
			struct {
				UCHAR DataBuffer[1];
			} GenericReparseBuffer;
		};
	} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

	namespace usn
	{
		typedef struct {
			DWORD         RecordLength;
			WORD          MajorVersion;
			WORD          MinorVersion;
			BYTE          FileReferenceNumber[16];
			BYTE          ParentFileReferenceNumber[16];
			USN           Usn;
			LARGE_INTEGER TimeStamp;
			DWORD         Reason;
			DWORD         SourceInfo;
			DWORD         SecurityId;
			DWORD         FileAttributes;
			WORD          FileNameLength;
			WORD          FileNameOffset;
			WCHAR         FileName[1];
		} USN_RECORD_V3, *PUSN_RECORD_V3;
	}
}


void getMousePos(int &x, int &y)
{
	POINT mousepos;
	GetCursorPos(&mousepos);
	x=mousepos.x;
	y=mousepos.y;
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

std::vector<SFile> getFiles(const std::string &path, bool *has_error, bool ignore_other_fs)
{
	return getFilesWin(path, has_error, true, false, ignore_other_fs);
}

std::wstring os_file_prefix(std::wstring path)
{
	if(path.size()>=2 && path[0]=='\\' && path[1]=='\\' )
	{
		if(path.size()>=3 && path[2]=='?')
		{
			return path;
		}
		else
		{
			return L"\\\\?\\UNC"+path.substr(1);
		}
	}
	else
		return L"\\\\?\\"+path;
}

std::vector<SFile> getFilesWin(const std::string &path, bool *has_error,
	bool exact_filesize, bool with_usn, bool ignore_other_fs)
{
	if(has_error!=NULL)
	{
		*has_error=false;
	}

	std::vector<char> usn_buffer;
	usn_buffer.resize(1024);

	std::vector<SFile> tmp;
	HANDLE fHandle;
	WIN32_FIND_DATAW wfd;
	std::wstring tpath=ConvertToWchar(path);
	if(!tpath.empty() && tpath[tpath.size()-1]=='\\' ) tpath.erase(path.size()-1, 1);
	fHandle=FindFirstFileW((tpath+L"\\*").c_str(),&wfd); 
	if(fHandle==INVALID_HANDLE_VALUE)
	{
		if(tpath.find(L"\\\\?\\UNC")==0)
		{
			tpath.erase(0, 7);
			tpath=L"\\"+tpath;
			fHandle=FindFirstFileW((tpath+L"\\*").c_str(),&wfd); 
		}
		else if(tpath.find(L"\\\\?\\")==0)
		{
			tpath.erase(0, 4);
			fHandle=FindFirstFileW((tpath+L"\\*").c_str(),&wfd); 
		}
		if(fHandle==INVALID_HANDLE_VALUE)
		{
			if(has_error!=NULL)
			{
				*has_error=true;
			}
			return tmp;
		}
	}

	do
	{
		SFile f;
		f.name=ConvertFromWchar(wfd.cFileName);
		if(f.name=="." || f.name==".." )
			continue;

		f.usn=0;
		f.isdir=(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)>0;			
		LARGE_INTEGER lwt;
		lwt.HighPart=wfd.ftLastWriteTime.dwHighDateTime;
		lwt.LowPart=wfd.ftLastWriteTime.dwLowDateTime;
		f.last_modified=os_windows_to_unix_time(lwt.QuadPart);
		LARGE_INTEGER size;
		size.HighPart=wfd.nFileSizeHigh;
		size.LowPart=wfd.nFileSizeLow;
		f.size=size.QuadPart;

		lwt.HighPart=wfd.ftCreationTime.dwHighDateTime;
		lwt.LowPart=wfd.ftCreationTime.dwLowDateTime;
		f.created=os_windows_to_unix_time(lwt.QuadPart);

		if(wfd.dwFileAttributes &FILE_ATTRIBUTE_REPARSE_POINT)
		{
			f.issym=true;
			f.isspecial=true;
		}
		else
		{
			f.issym=false;
			f.isspecial=false;
		}

		if( (exact_filesize || with_usn ) && !f.issym && !f.isdir)
		{
			if(with_usn)
			{
				HANDLE hFile = CreateFileW(os_file_prefix(tpath+L"\\"+ConvertToWchar(f.name)).c_str(), GENERIC_READ, FILE_SHARE_WRITE|FILE_SHARE_READ, NULL,
					OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

				if(hFile!=INVALID_HANDLE_VALUE)
				{
					BY_HANDLE_FILE_INFORMATION file_info;
					if(GetFileInformationByHandle(hFile, &file_info))
					{
						size.HighPart = file_info.nFileSizeHigh;
						size.LowPart = file_info.nFileSizeLow;
						f.size = size.QuadPart;

						lwt.HighPart = file_info.ftLastWriteTime.dwHighDateTime;
						lwt.LowPart = file_info.ftLastWriteTime.dwLowDateTime;

						f.last_modified = os_windows_to_unix_time(lwt.QuadPart);

						lwt.HighPart=file_info.ftCreationTime.dwHighDateTime;
						lwt.LowPart=file_info.ftCreationTime.dwLowDateTime;

						f.created=os_windows_to_unix_time(lwt.QuadPart);
					}

					
					DWORD last_err=0;
					do 
					{
						DWORD ret_bytes = 0;
						BOOL b = DeviceIoControl(hFile, FSCTL_READ_FILE_USN_DATA, NULL, 0,
							usn_buffer.data(), static_cast<DWORD>(usn_buffer.size()), &ret_bytes, NULL);

						if(b)
						{
							USN_RECORD* usnv2=reinterpret_cast<USN_RECORD*>(usn_buffer.data());
							if(usnv2->MajorVersion==2)
							{
								f.usn = usnv2->Usn;
							}
							else if(usnv2->MajorVersion==3)
							{
								usn::USN_RECORD_V3* usnv3=reinterpret_cast<usn::USN_RECORD_V3*>(usn_buffer.data());
								f.usn = usnv3->Usn;
							}
							else
							{
								Log("USN entry major version "+convert(usnv2->MajorVersion)+" of file \""+ConvertFromWchar(tpath)+"\\"+f.name+"\" not supported", LL_ERROR);
							}
						}
						else
						{
							last_err=GetLastError();
						}

						if(last_err==ERROR_INSUFFICIENT_BUFFER)
						{
							usn_buffer.resize(usn_buffer.size()*2);
						}

					} while (last_err==ERROR_INSUFFICIENT_BUFFER);

					CloseHandle(hFile);
				}
			}
			else
			{
				WIN32_FILE_ATTRIBUTE_DATA fad;
				if( GetFileAttributesExW(os_file_prefix(tpath+L"\\"+ConvertToWchar(f.name)).c_str(),  GetFileExInfoStandard, &fad) )
				{
					size.HighPart = fad.nFileSizeHigh;
					size.LowPart = fad.nFileSizeLow;
					f.size = size.QuadPart;

					lwt.HighPart = fad.ftLastWriteTime.dwHighDateTime;
					lwt.LowPart = fad.ftLastWriteTime.dwLowDateTime;

					f.last_modified = os_windows_to_unix_time(lwt.QuadPart);

					lwt.HighPart=fad.ftCreationTime.dwHighDateTime;
					lwt.LowPart=fad.ftCreationTime.dwLowDateTime;

					f.created=os_windows_to_unix_time(lwt.QuadPart);
				}
			}							
		}
		if(f.last_modified<0) f.last_modified*=-1;
		tmp.push_back(f);		
	}
	while (FindNextFileW(fHandle,&wfd) );
	FindClose(fHandle);

	std::sort(tmp.begin(), tmp.end());

	return tmp;
}

SFile getFileMetadataWin( const std::string &path, bool with_usn )
{
	SFile ret;
	ret.name=path;
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if( GetFileAttributesExW(ConvertToWchar(path).c_str(),  GetFileExInfoStandard, &fad) )
	{
		if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			ret.isdir=true;
		}
		LARGE_INTEGER size;
		size.HighPart = fad.nFileSizeHigh;
		size.LowPart = fad.nFileSizeLow;
		ret.size = size.QuadPart;

		LARGE_INTEGER lwt;
		lwt.HighPart = fad.ftLastWriteTime.dwHighDateTime;
		lwt.LowPart = fad.ftLastWriteTime.dwLowDateTime;

		ret.last_modified = os_windows_to_unix_time(lwt.QuadPart);

		lwt.HighPart=fad.ftCreationTime.dwHighDateTime;
		lwt.LowPart=fad.ftCreationTime.dwLowDateTime;

		ret.created=os_windows_to_unix_time(lwt.QuadPart);

		lwt.HighPart = fad.ftLastAccessTime.dwHighDateTime;
		lwt.LowPart = fad.ftLastAccessTime.dwLowDateTime;

		ret.accessed = os_windows_to_unix_time(lwt.QuadPart);

		if(with_usn && !ret.isdir && !(fad.dwFileAttributes &FILE_ATTRIBUTE_REPARSE_POINT))
		{
			HANDLE hFile = CreateFileW(ConvertToWchar(path).c_str(), GENERIC_READ, FILE_SHARE_WRITE|FILE_SHARE_READ, NULL,
				OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

			if(hFile!=INVALID_HANDLE_VALUE)
			{
				std::vector<char> buffer;
				buffer.resize(1024);
				DWORD last_err=0;
				do 
				{
					DWORD ret_bytes = 0;
					BOOL b = DeviceIoControl(hFile, FSCTL_READ_FILE_USN_DATA, NULL, 0,
						buffer.data(), static_cast<DWORD>(buffer.size()), &ret_bytes, NULL);

					if(b)
					{
						USN_RECORD* usnv2=reinterpret_cast<USN_RECORD*>(buffer.data());
						if(usnv2->MajorVersion==2)
						{
							ret.usn = usnv2->Usn;
						}
						else if(usnv2->MajorVersion==3)
						{
							usn::USN_RECORD_V3* usnv3=reinterpret_cast<usn::USN_RECORD_V3*>(buffer.data());
							ret.usn = usnv3->Usn;
						}
						else
						{
							Log("USN entry major version "+convert(usnv2->MajorVersion)+" of file \""+path+"\" not supported", LL_ERROR);
						}
					}
					else
					{
						last_err=GetLastError();
					}

					if(last_err==ERROR_INSUFFICIENT_BUFFER)
					{
						buffer.resize(buffer.size()*2);
					}

				} while (last_err==ERROR_INSUFFICIENT_BUFFER);

				CloseHandle(hFile);
			}
		}

		return ret;
	}
	else
	{
		return SFile();
	}
}

SFile getFileMetadata( const std::string &path )
{
	return getFileMetadataWin(path, false);
}

void removeFile(const std::string &path)
{
	_unlink(path.c_str());
}

void moveFile(const std::string &src, const std::string &dst)
{
	rename(src.c_str(), dst.c_str() );
}

bool isDirectory(const std::string &path, void* transaction)
{
        DWORD attrib;
#ifdef USE_NTFS_TXF
		if(transaction!=NULL)
		{
			WIN32_FILE_ATTRIBUTE_DATA ad;
			if(!GetFileAttributesTransactedW(ConvertToWchar(path).c_str(), GetFileExInfoStandard, &ad, transaction))
			{
				attrib=0xFFFFFFFF;
			}
			else
			{
				attrib = ad.dwFileAttributes;
			}
		}
		else
		{
#endif
			attrib = GetFileAttributesW(ConvertToWchar(path).c_str());
#ifdef USE_NTFS_TXF
		}
#endif	

        if ( attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY) )
        {
                return false;
        }
        else
        {
                return true;
        }
}

int os_get_file_type(const std::string &path)
{
	DWORD attrib = GetFileAttributesW(ConvertToWchar(path).c_str());

	if(attrib==INVALID_FILE_ATTRIBUTES)
	{
		return 0;
	}

	int ret = 0;

	if(attrib & FILE_ATTRIBUTE_DIRECTORY)
	{
		ret |= EFileType_Directory;
	}
	else
	{
		ret |= EFileType_File;
	}

	if(attrib & FILE_ATTRIBUTE_REPARSE_POINT)
	{
		ret |= EFileType_Symlink;
	}

	return ret;
}

int64 os_atoi64(const std::string &str)
{
	return _atoi64(str.c_str());
}

bool os_create_dir(const std::string &dir)
{
	return CreateDirectoryW(ConvertToWchar(dir).c_str(), NULL)!=0;
}

bool os_create_hardlink(const std::string &linkname, const std::string &fname, bool use_ioref, bool* too_many_links)
{
	BOOL r=CreateHardLinkW(ConvertToWchar(linkname).c_str(), ConvertToWchar(fname).c_str(), NULL);
	if(too_many_links!=NULL)
	{
		*too_many_links=false;
		if(!r)
		{
			int err=GetLastError();
			if(err==ERROR_TOO_MANY_LINKS)
			{
				*too_many_links=true;
			}
		}
	}
	return r!=0;
}

int64 os_free_space(const std::string &path)
{
	std::string cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='\\')
		cp+='\\';

	ULARGE_INTEGER li;
	BOOL r=GetDiskFreeSpaceExW(ConvertToWchar(path).c_str(), &li, NULL, NULL);
	if(r!=0)
		return li.QuadPart;
	else
		return -1;
}

int64 os_total_space(const std::string &path)
{
	std::string cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='\\')
		cp+='\\';

	ULARGE_INTEGER li;
	BOOL r=GetDiskFreeSpaceExW(ConvertToWchar(path).c_str(), NULL, &li, NULL);
	if(r!=0)
		return li.QuadPart;
	else
		return -1;
}

bool os_directory_exists(const std::string &path)
{
	return isDirectory(path, NULL);
}

bool os_remove_symlink_dir_int(const std::wstring &path)
{
	return RemoveDirectoryW(path.c_str())!=FALSE;
}

bool os_remove_nonempty_dir_int(const std::wstring &path, os_symlink_callback_t symlink_callback, void* userdata, bool delete_root)
{
	WIN32_FIND_DATAW wfd; 
	std::wstring tpath=path;
	HANDLE hf=FindFirstFileW((tpath+L"\\*").c_str(), &wfd);
	if(hf==INVALID_HANDLE_VALUE)
	{
		if(tpath.find(L"\\\\?\\UNC")==0)
		{
			tpath.erase(0, 7);
			tpath=L"\\"+tpath;
			hf=FindFirstFileW((tpath+L"\\*").c_str(),&wfd); 
		}
		else if(tpath.find(L"\\\\?\\")==0)
		{
			tpath.erase(0, 4);
			hf=FindFirstFileW((tpath+L"\\*").c_str(),&wfd); 
		}
		if(hf==INVALID_HANDLE_VALUE)
		{
			return false;
		}
	}
	BOOL b=true;
	while( b )
	{
		if( (std::wstring)wfd.cFileName!=L"." && (std::wstring)wfd.cFileName!=L".." )
		{
			if( wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
			{
				if(symlink_callback!=NULL
					&& (wfd.dwReserved0==IO_REPARSE_TAG_MOUNT_POINT
					|| wfd.dwReserved0==IO_REPARSE_TAG_SYMLINK) )
				{
					symlink_callback(ConvertFromWchar(path+L"\\"+wfd.cFileName), userdata);
				}
				else if(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					os_remove_symlink_dir_int(path+L"\\"+wfd.cFileName);
				}
				else
				{
					DeleteFileW((path+L"\\"+wfd.cFileName).c_str());
				}
			}
			else if(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				os_remove_nonempty_dir_int(tpath+L"\\"+wfd.cFileName, symlink_callback, userdata, true);
			}
			else
			{
				DeleteFileW((path+L"\\"+wfd.cFileName).c_str());
			}
		}
		b=FindNextFileW(hf,&wfd);			
	}

	FindClose(hf);

	if(delete_root)
	{
		if(!RemoveDirectoryW(path.c_str()))
		{
			Log("Error deleting directory \""+ConvertFromWchar(path)+"\"", LL_ERROR);
		}
	}
	return true;
}

bool os_remove_nonempty_dir(const std::string &path, os_symlink_callback_t symlink_callback, void* userdata, bool delete_root)
{
	return os_remove_nonempty_dir_int(ConvertToWchar(path), symlink_callback, userdata, delete_root);
}

bool os_remove_symlink_dir(const std::string &path)
{
	return RemoveDirectoryW(ConvertToWchar(path).c_str())!=FALSE;
}

bool os_remove_dir(const std::string &path)
{
	return RemoveDirectoryW(ConvertToWchar(path).c_str())!=FALSE;
}

std::string os_file_sep(void)
{
	return "\\";
}


bool os_link_symbolic_symlink(const std::string &target, const std::string &lname, void* transaction, bool* isdir)
{
#if (_WIN32_WINNT >= 0x0600)
	DWORD flags=0;
	if(isdir!=NULL && *isdir)
	{
		flags|=SYMBOLIC_LINK_FLAG_DIRECTORY;
	}
	else if(isdir==NULL && isDirectory(target, transaction))
	{
		flags|=SYMBOLIC_LINK_FLAG_DIRECTORY;
	}

	BOOL rc;
	if(transaction==NULL)
	{
		rc=CreateSymbolicLink(ConvertToWchar(lname).c_str(), ConvertToWchar(target).c_str(), flags);
	}
	else
	{
		rc=CreateSymbolicLinkTransactedW(ConvertToWchar(lname).c_str(), ConvertToWchar(target).c_str(), flags, transaction);
	}
	if(rc==FALSE)
	{
		Log("Creating symbolic link from \""+lname+"\" to \""+target+"\" failed with error "+convert((int)GetLastError()), LL_ERROR);
	}
	return rc==TRUE;
#else
	return false;
#endif
}

bool os_link_symbolic_junctions(const std::string &target, const std::string &lname)
{
	bool ret=false;
	std::wstring wtarget=ConvertToWchar(target);
	HANDLE hJunc=INVALID_HANDLE_VALUE;
	char *buf=NULL;

	if(wtarget.find(L"\\\\?\\UNC")==0)
	{
		wtarget.erase(0, 7);
		wtarget=L"\\"+wtarget;
	}
	else if(wtarget.find(os_file_prefix(L""))==0)
	{
		wtarget.erase(0, os_file_prefix(L"").size());
	}

	if(!wtarget.empty() && wtarget[0]!='\\')
		wtarget=L"\\??\\"+wtarget;

	if(!wtarget.empty() && wtarget[wtarget.size()-1]=='\\')
		wtarget.erase(wtarget.size()-1, 1);

	std::wstring wlname = ConvertToWchar(lname);
	if(!CreateDirectoryW(wlname.c_str(), NULL) )
	{
		goto cleanup;
	}

	hJunc=CreateFileW(wlname.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if(hJunc==INVALID_HANDLE_VALUE)
		goto cleanup;

	size_t bsize=sizeof(REPARSE_MOUNTPOINT_DATA_BUFFER) + (wtarget.size()+1) * sizeof(wchar_t)+30;
	buf=new char[bsize];
	memset(buf, 0, bsize);

	REPARSE_MOUNTPOINT_DATA_BUFFER *rb=(REPARSE_MOUNTPOINT_DATA_BUFFER*)buf;
	rb->ReparseTag=IO_REPARSE_TAG_MOUNT_POINT;
	rb->ReparseTargetMaximumLength=(WORD)((wtarget.size()+1)*sizeof(wchar_t));
	rb->ReparseTargetLength=rb->ReparseTargetMaximumLength-1*sizeof(wchar_t);
	rb->ReparseDataLength=rb->ReparseTargetLength+12;
	memcpy(rb->ReparseTarget, wtarget.c_str(), rb->ReparseTargetMaximumLength);

	DWORD bytes_ret;
	if(!DeviceIoControl(hJunc, FSCTL_SET_REPARSE_POINT, rb, rb->ReparseDataLength+REPARSE_MOUNTPOINT_HEADER_SIZE, NULL, 0, &bytes_ret, NULL) )
	{
		goto cleanup;
	}
	ret=true;

cleanup:
	if(!ret)
	{
		Log("Creating junction failed. Last error="+convert((int)GetLastError()), LL_ERROR);
	}
	delete []buf;
	if(hJunc!=INVALID_HANDLE_VALUE)
		CloseHandle(hJunc);
	if(!ret)
	{
		RemoveDirectoryW(ConvertToWchar(lname).c_str());
	}
	return ret;
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
			
			freeaddrinfo(h);
		}
		else
		{
			return false;
		}
	}
	return true;
}
#endif

bool os_link_symbolic(const std::string &target, const std::string &lname, void* transaction, bool* isdir)
{
	if(transaction==NULL)
	{
		bool l_isdir;
		if(isdir!=NULL)
		{
			l_isdir=*isdir;
		}
		else
		{
			l_isdir=isDirectory(target, NULL);
		}
		if(!l_isdir || !os_path_absolute(target) ||!os_link_symbolic_junctions(target, lname) )
			return os_link_symbolic_symlink(target, lname, NULL, isdir);
	}
	else
	{
		return os_link_symbolic_symlink(target, lname, transaction, isdir);
	}

	return true;
}

bool os_get_symlink_target(const std::string &lname, std::string &target)
{
	HANDLE hJunc=CreateFileW(ConvertToWchar(lname).c_str(), GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT|FILE_ATTRIBUTE_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if(hJunc==INVALID_HANDLE_VALUE)
		return false;

	DWORD needed_buffer_size = 0;
	DWORD bytes_returned;
	std::string buffer;
	buffer.resize((std::max)((size_t)REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, (size_t)512));
	BOOL b = DeviceIoControl(hJunc, FSCTL_GET_REPARSE_POINT, NULL, 0, &buffer[0], static_cast<DWORD>(buffer.size()), &bytes_returned, NULL);
	if(!b)
	{
		if(GetLastError()!= ERROR_INSUFFICIENT_BUFFER)
		{
			CloseHandle(hJunc);
			return false;
		}
	}
	const REPARSE_DATA_BUFFER* reparse_buffer = reinterpret_cast<const REPARSE_DATA_BUFFER*>(buffer.data());

	if(!IsReparseTagMicrosoft(reparse_buffer->ReparseTag))
	{
		CloseHandle(hJunc);
		return false;
	}

	if(!b)
	{
		buffer.resize(reparse_buffer->ReparseDataLength);
		b = DeviceIoControl(hJunc, FSCTL_GET_REPARSE_POINT, NULL, 0, &buffer[0], static_cast<DWORD>(buffer.size()), &bytes_returned, NULL);
		if(!b)
		{
			CloseHandle(hJunc);
			return false;
		}
	}

	CloseHandle(hJunc);

	bool ret=true;

	std::wstring wtarget;
	if(reparse_buffer->ReparseTag==IO_REPARSE_TAG_SYMLINK)
	{
		wtarget.resize(reparse_buffer->SymbolicLinkReparseBuffer.SubstituteNameLength/sizeof(wchar_t));
		memcpy(&wtarget[0],
			&reparse_buffer->SymbolicLinkReparseBuffer.PathBuffer[reparse_buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset/sizeof(wchar_t)],
			reparse_buffer->SymbolicLinkReparseBuffer.SubstituteNameLength);
	}
	else if(reparse_buffer->ReparseTag==IO_REPARSE_TAG_MOUNT_POINT)
	{
		wtarget.resize(reparse_buffer->MountPointReparseBuffer.SubstituteNameLength/sizeof(wchar_t));
		memcpy(&wtarget[0],
			&reparse_buffer->MountPointReparseBuffer.PathBuffer[reparse_buffer->MountPointReparseBuffer.SubstituteNameOffset/sizeof(wchar_t)],
			reparse_buffer->MountPointReparseBuffer.SubstituteNameLength);
	}
	else
	{
		ret=false;
	}

	target = ConvertFromWchar(wtarget);

	if(next(target, 0, "\\??\\"))
		target.erase(0,4);

	return ret;
}

bool os_is_symlink(const std::string &lname)
{
	DWORD attrs = GetFileAttributesW(ConvertToWchar(lname).c_str());
	if(attrs == INVALID_FILE_ATTRIBUTES)
		return false;

	return (attrs & FILE_ATTRIBUTE_REPARSE_POINT)>0;
}

std::string os_file_prefix(std::string path)
{
	if(path.size()>=2 && path[0]=='\\' && path[1]=='\\' )
	{
		if(path.size()>=3 && path[2]=='?')
		{
			return path;
		}
		else
		{
			return "\\\\?\\UNC"+path.substr(1);
		}
	}
	else
		return "\\\\?\\"+path;
}

bool os_file_truncate(const std::string &fn, int64 fsize)
{
	int fh;
	if( _wsopen_s ( &fh, ConvertToWchar(fn).c_str(), _O_RDWR | _O_CREAT, _SH_DENYNO,
            _S_IREAD | _S_IWRITE ) == 0 )
	{
		if( _chsize_s( fh, fsize ) != 0 )
		{
			_close( fh );
			return false;
		}
		_close( fh );
		return true;
	}
	else
	{
		return false;
	}
}

std::string os_strftime(std::string fs)
{
	time_t rawtime;		
	char buffer [100];
	time ( &rawtime );
	struct tm  timeinfo;
	localtime_s(&timeinfo, &rawtime);
	strftime (buffer,100,fs.c_str(),&timeinfo);
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

std::string os_get_final_path(std::string path)
{
#if (_WIN32_WINNT >= 0x0600)
	std::wstring wret;

	if(path.size()<3 && path.find(":")==std::string::npos)
	{
		path+=":";
	}

	if(path.find("\\")==std::string::npos)
	{
		path+="\\";
	}

	HANDLE hFile = CreateFileW(ConvertToWchar(path).c_str(),               
                       GENERIC_READ,          
                       FILE_SHARE_READ,       
                       NULL,                  
                       OPEN_EXISTING,         
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, 
                       NULL);

	if( hFile==INVALID_HANDLE_VALUE )
	{
		Log("Could not open path in os_get_final_path for \""+path+"\"", LL_ERROR);
		return path;
	}

	DWORD dwBufsize = GetFinalPathNameByHandleW( hFile, NULL, 0, VOLUME_NAME_DOS );

	if(dwBufsize==0)
	{
		Log("Error getting path size in in os_get_final_path error="+convert((int)GetLastError())+" for \""+path+"\"", LL_ERROR);
		CloseHandle(hFile);
		return path;
	}

	wret.resize(dwBufsize+1);

	DWORD dwRet = GetFinalPathNameByHandleW( hFile, (LPWSTR)wret.c_str(), dwBufsize, VOLUME_NAME_DOS );

	CloseHandle(hFile);

	if(dwRet==0)
	{
		Log("Error getting path in in os_get_final_path error="+convert((int)GetLastError()), LL_ERROR);
	}
	else if(dwRet<wret.size())
	{
		wret.resize(dwRet);
		if(wret.find(L"\\\\?\\UNC")==0)
		{
			wret.erase(0, 7);
			wret=L"\\"+wret;
		}
		if(wret.find(L"\\\\?\\")==0)
		{
			wret.erase(0,4);
		}
		/*if(wret.size()>=2 && wret[wret.size()-2]=='.' && ret[wret.size()-1]=='.' )
		{
			wret.resize(ret.size()-2);
		}*/
		return ConvertFromWchar(wret);
	}
	else
	{
		Log("Error getting path (buffer too small) in in os_get_final_path error="+convert((int)GetLastError()), LL_ERROR);
	}

	return path;
#else
	return path;
#endif
}

bool os_rename_file(std::string src, std::string dst, void* transaction)
{
	BOOL rc;
#ifdef USE_NTFS_TXF
	if(transaction==NULL)
	{
#endif
		rc = MoveFileExW(ConvertToWchar(src).c_str(), ConvertToWchar(dst).c_str(), MOVEFILE_REPLACE_EXISTING);
#ifdef USE_NTFS_TXF
	}
	else
	{
		rc=MoveFileTransactedW(ConvertToWchar(src).c_str(), ConvertToWchar(dst).c_str(), NULL, NULL, MOVEFILE_REPLACE_EXISTING, transaction);
	}	
#endif
#ifdef _DEBUG
	if(rc==0)
	{
		Log("MoveFileW error: "+convert((int)GetLastError()), LL_ERROR);
	}
#endif
	return rc!=0;
}

void* os_start_transaction()
{
#ifdef USE_NTFS_TXF
	HANDLE htrans = CreateTransaction(NULL, NULL, 0, 0, 0, 0, NULL);
	if(htrans==INVALID_HANDLE_VALUE)
	{
		Log("Creating transaction failed. ec="+convert((int)GetLastError), LL_WARNING);
		return NULL;
	}
	return htrans;
#else
	return NULL;
#endif
}

bool os_finish_transaction(void* transaction)
{
#ifdef USE_NTFS_TXF
	if(transaction==NULL)
	{
		return false;
	}
	BOOL b = CommitTransaction(transaction);
	if(!b)
	{
		Log("Commiting transaction failed. ec="+convert((int)GetLastError), LL_ERROR);
		CloseHandle(transaction);
		return false;
	}
	CloseHandle(transaction);
	return true;
#else
	return true;
#endif
}

int64 os_last_error()
{
	return GetLastError();
}

bool os_set_file_time(const std::string& fn, int64 created, int64 last_modified, int64 accessed)
{
	HANDLE hFile = CreateFileW(ConvertToWchar(fn).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE|FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if(hFile!=INVALID_HANDLE_VALUE)
	{
		FILETIME* pcreation_time = NULL;
		FILETIME creation_time;
		if (created != 0)
		{
			LARGE_INTEGER li;
			li.QuadPart=os_to_windows_filetime(created);

			
			creation_time.dwHighDateTime=li.HighPart;
			creation_time.dwLowDateTime=li.LowPart;	

			pcreation_time = &creation_time;
		}
		
		LARGE_INTEGER li;
		li.QuadPart = os_to_windows_filetime(last_modified);
		FILETIME modified_time;
		modified_time.dwHighDateTime=li.HighPart;
		modified_time.dwLowDateTime=li.LowPart;

		li.QuadPart=os_to_windows_filetime(accessed);

		FILETIME accessed_time;
		accessed_time.dwHighDateTime = li.HighPart;
		accessed_time.dwLowDateTime = li.LowPart;

		if(SetFileTime(hFile, pcreation_time, &accessed_time, &modified_time))
		{
			CloseHandle(hFile);
			return true;
		}
		else
		{
			CloseHandle(hFile);
			return false;
		}
	}
	else
	{
		return false;
	}
}

#ifndef OS_FUNC_NO_SERVER
bool copy_file(const std::string &src, const std::string &dst, bool flush)
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

	if (copy_ok && flush)
	{
		copy_ok = fdst->Sync();
	}

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

#endif

bool os_path_absolute(const std::string& path)
{
	return PathIsRelativeW(ConvertToWchar(path).c_str())==FALSE;
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

int64 os_last_error(std::string& message)
{
	DWORD last_error = GetLastError();

	wchar_t* output=NULL;

	DWORD r = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, HRESULT_FROM_WIN32(last_error), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&output), 0, NULL);

	if(r>0 && output!=NULL)
	{
		std::wstring wmessage;
		wmessage.resize(r);
		memcpy(&wmessage[0], output, r*sizeof(wchar_t));
		message = ConvertFromWchar(wmessage);
	}

	if(output!=NULL)
	{
		LocalFree(output);
	}

	return last_error;
}

bool os_enable_background_priority(SPrioInfo& prio_info)
{
#ifdef THREAD_MODE_BACKGROUND_BEGIN
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
	return SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) == TRUE;
#else
	return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST)==TRUE;
#endif
}

bool os_disable_background_priority(SPrioInfo& prio_info)
{
#ifdef THREAD_MODE_BACKGROUND_BEGIN
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
	return SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END) == TRUE;
#else
	return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL)==TRUE;
#endif
}

SPrioInfo::SPrioInfo()
{
}

SPrioInfo::~SPrioInfo()
{
}