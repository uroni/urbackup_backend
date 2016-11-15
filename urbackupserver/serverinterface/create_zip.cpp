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
*
*   
**************************************************************************/

#include "action_header.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../Interface/File.h"
#include "backups.h"
#include <memory>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../../common/miniz/miniz.h"
#include "../../common/miniz/miniz_zip.h"
#ifndef _WIN32
#define _fdopen fdopen
#else
#include <io.h>
#include <fcntl.h>
#endif

namespace
{

struct MiniZFileInfo
{
	uint64 file_offset;
	THREAD_ID tid;
};

size_t my_mz_write_func(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n)
{
  MiniZFileInfo* fileInfo = reinterpret_cast<MiniZFileInfo*>(pOpaque);
  if(fileInfo->file_offset!=file_ofs)
  {
	  return 0;
  }

  fileInfo->file_offset=file_ofs+n;

  bool b=Server->WriteRaw(fileInfo->tid, reinterpret_cast<const char*>(pBuf), n, false);
  return b?n:0;
}

bool my_miniz_init(mz_zip_archive *pZip, MiniZFileInfo* fileInfo)
{
	pZip->m_pWrite = my_mz_write_func;
	pZip->m_pIO_opaque = fileInfo;

	if (!mz_zip_writer_init(pZip, 0, MZ_ZIP_FLAG_CASE_SENSITIVE))
		return false;
  
	return true;
}

bool add_dir(mz_zip_archive& zip_archive, const std::string& archivefoldername, const std::string& folderbase, const std::string& foldername, const std::string& hashfolderbase, const std::string& hashfoldername, const std::string& filter,
		bool token_authentication, const std::vector<backupaccess::SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_special)
{
	bool has_error=false;
	const std::vector<SFile> files = getFiles(os_file_prefix(foldername), &has_error);

	if (has_error)
	{
		Server->Log("Error while adding files to ZIP file. Error listing files in folder \""
			+ foldername+"\". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	for(size_t i=0;i<files.size();++i)
	{
		const SFile& file=files[i];

		if(skip_special
			&& (file.name==".hashes" || file.name=="user_views" || next(files[i].name, 0, ".symlink_") ) )
		{
			continue;
		}

		std::string archivename = archivefoldername + (archivefoldername.empty()?"":"/") + file.name;
		std::string metadataname = hashfoldername + os_file_sep() + escape_metadata_fn(file.name);
		std::string filename = foldername + os_file_sep() + file.name;

		std::string next_hashfoldername = metadataname;

		if(!filter.empty() && archivename!=filter)
			continue;

		bool is_dir_link = false;

		if(file.isdir)
		{
			if (!os_directory_exists(os_file_prefix(metadataname)))
			{
				is_dir_link = true;
			}
			else
			{
				metadataname += os_file_sep() + metadata_dir_fn;
			}
		}

		if (is_dir_link
			|| (!file.isdir && os_get_file_type(os_file_prefix(filename)) & EFileType_Symlink) )
		{
			std::string symlink_target;
			if (os_get_symlink_target(os_file_prefix(filename), symlink_target))
			{
				std::string upone = ".." + os_file_sep();
				while (next(symlink_target, 0, upone))
				{
					symlink_target = symlink_target.substr(upone.size());
				}
				
				std::string filename_old = filename;

				filename = folderbase + os_file_sep() + symlink_target;
				if (os_get_file_type(os_file_prefix(filename)) == 0)
				{
					Server->Log("Error opening symlink target \""+filename+"\" of symlink at \"" + filename_old + "\"", LL_INFO);
					continue;
				}
				if (is_dir_link)
				{
					metadataname = hashfolderbase + os_file_sep() + symlink_target + os_file_sep() + metadata_dir_fn;
					next_hashfoldername = hashfolderbase + os_file_sep() + symlink_target;
				}
				else
				{
					metadataname = hashfolderbase + os_file_sep() + symlink_target;
				}
			}
			else
			{
				Server->Log("Error getting symlink target of \"" + filename + "\". "+os_last_error_str(), LL_ERROR);
				continue;
			}
		}

		bool has_metadata = false;

		FileMetadata metadata;
		if(token_authentication &&
			( !read_metadata(metadataname, metadata) ||
			  !backupaccess::checkFileToken(backup_tokens, tokens, metadata) ) )
		{
			continue;
		}
		else if(!token_authentication)
		{
			has_metadata = read_metadata(metadataname, metadata);
		}
		else
		{
			has_metadata = true;
		}

		time_t* last_modified=NULL;
		time_t last_modified_wt;
		if(has_metadata)
		{
#ifdef _WIN32
			last_modified_wt=static_cast<time_t>(metadata.last_modified);
#else
			last_modified_wt=static_cast<time_t>(metadata.last_modified);
#endif
			last_modified=&last_modified_wt;
		}

		mz_bool rc;
		if(file.isdir)
		{
			rc = mz_zip_writer_add_mem_ex(&zip_archive, (archivename + "/").c_str(), NULL, 0, NULL, 0, MZ_DEFAULT_LEVEL,
											0, 0, 1<<11, last_modified);
		}
		else
		{	
			std::auto_ptr<IFsFile> add_file(Server->openFile(os_file_prefix(filename)));
			if (add_file.get() == NULL)
			{
				Server->Log("Error opening file \"" + filename + "\" for ZIP file download." + os_last_error_str(), LL_ERROR);
				return false;
			}
#ifndef _WIN32
			int fd = *reinterpret_cast<int*>(&add_file->getOsHandle());
#else
			int fd =_open_osfhandle(reinterpret_cast<intptr_t>(add_file->getOsHandle()), _O_RDONLY);
			if (fd == -1)
			{
				Server->Log("Error opening file fd for \"" + filename + "\" for ZIP file download." + os_last_error_str(), LL_ERROR);
				return false;
			}
#endif

			FILE* file = _fdopen(fd, "r");
			if (file != NULL)
			{
				rc = mz_zip_writer_add_cfile(&zip_archive, archivename.c_str(), file, add_file->Size(), last_modified, NULL, 0, MZ_DEFAULT_LEVEL);

				fclose(file);
			}
			else
			{
				Server->Log("Error opening FILE handle for \"" + filename + "\" for ZIP file download." + os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		if(rc==MZ_FALSE)
		{
			Server->Log("Error while adding file \""+filename+"\" to ZIP file. RC="+convert((int)rc), LL_ERROR);
			return false;
		}

		if(file.isdir)
		{
			add_dir(zip_archive, archivename, folderbase, filename, hashfolderbase, next_hashfoldername, filter,
				token_authentication, backup_tokens, tokens, false);
		}
	}

	return true;
}

}

bool create_zip_to_output(const std::string& folderbase, const std::string& foldername, const std::string& hashfolderbase,
	const std::string& hashfoldername, const std::string& filter, bool token_authentication,
	const std::vector<backupaccess::SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes)
{
	mz_zip_archive zip_archive;
	memset(&zip_archive, 0, sizeof(zip_archive));

	MiniZFileInfo file_info = {};

	file_info.tid=Server->getThreadID();

	if(!my_miniz_init(&zip_archive, &file_info))
	{
		Server->Log("Error while initializing ZIP archive", LL_ERROR);
		return false;
	}

	if(!add_dir(zip_archive, "", folderbase, foldername, hashfolderbase, 
		hashfoldername, filter, token_authentication, backup_tokens, tokens, skip_hashes))
	{
		Server->Log("Error while adding files and folders to ZIP archive", LL_ERROR);
		return false;
	}

	if(!mz_zip_writer_finalize_archive(&zip_archive))
	{
		Server->Log("Error while finalizing ZIP archive", LL_ERROR);
		return false;
	}

	if(!mz_zip_writer_end(&zip_archive))
	{
		Server->Log("Error while ending ZIP archive writer", LL_ERROR);
		return false;
	}

	return true;
}