#include "action_header.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../Interface/File.h"
#include "backups.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../../common/miniz.c"

namespace
{

int my_stat(const wchar_t *pFilename, struct MZ_FILE_STAT_STRUCT* statbuf)
{
#if defined(_MSC_VER) || defined(__MINGW64__)
	return _wstat(pFilename, statbuf);
#else
	return MZ_FILE_STAT(Server->ConvertToUTF8(pFilename).c_str(), statbuf);
#endif
}

mz_bool my_mz_zip_get_file_modified_time(const wchar_t *pFilename, mz_uint16 *pDOS_time, mz_uint16 *pDOS_date)
{
#ifdef MINIZ_NO_TIME
  (void)pFilename; *pDOS_date = *pDOS_time = 0;
#else
  struct MZ_FILE_STAT_STRUCT file_stat;
  // On Linux with x86 glibc, this call will fail on large files (>= 0x80000000 bytes) unless you compiled with _LARGEFILE64_SOURCE. Argh.
  if (my_stat(pFilename, &file_stat) != 0)
    return MZ_FALSE;
  mz_zip_time_to_dos_time(file_stat.st_mtime, pDOS_time, pDOS_date);
#endif // #ifdef MINIZ_NO_TIME
  return MZ_TRUE;
}


mz_bool my_mz_zip_writer_add_file(mz_zip_archive *pZip, const char *pArchive_name, const wchar_t *pSrc_filename, const void *pComment, mz_uint16 comment_size, mz_uint level_and_flags,
	time_t* last_modified)
{
  mz_uint16 gen_flags = 1<<3 | 1<<11; 
  mz_uint uncomp_crc32 = MZ_CRC32_INIT, level, num_alignment_padding_bytes;
  mz_uint16 method = 0, dos_time = 0, dos_date = 0, ext_attributes = 0;
  mz_uint64 local_dir_header_ofs = pZip->m_archive_size, cur_archive_file_ofs = pZip->m_archive_size, uncomp_size = 0, comp_size = 0;
  size_t archive_name_size;
  mz_uint8 local_dir_header[MZ_ZIP_LOCAL_DIR_HEADER_SIZE];
  IFile *pSrc_file = NULL;

  if ((int)level_and_flags < 0)
    level_and_flags = MZ_DEFAULT_LEVEL;
  level = level_and_flags & 0xF;

  if ((!pZip) || (!pZip->m_pState) || (pZip->m_zip_mode != MZ_ZIP_MODE_WRITING) || (!pArchive_name) || ((comment_size) && (!pComment)) || (level > MZ_UBER_COMPRESSION))
    return MZ_FALSE;
  if (level_and_flags & MZ_ZIP_FLAG_COMPRESSED_DATA)
    return MZ_FALSE;
  if (!mz_zip_writer_validate_archive_name(pArchive_name))
    return MZ_FALSE;

  archive_name_size = strlen(pArchive_name);
  if (archive_name_size > 0xFFFF)
    return MZ_FALSE;

  num_alignment_padding_bytes = mz_zip_writer_compute_padding_needed_for_file_alignment(pZip);

  // no zip64 support yet
  if ((pZip->m_total_files == 0xFFFF) || ((pZip->m_archive_size + num_alignment_padding_bytes + MZ_ZIP_LOCAL_DIR_HEADER_SIZE + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + comment_size + archive_name_size) > 0xFFFFFFFF))
    return MZ_FALSE;

  if(last_modified!=NULL)
  {
	  mz_zip_time_to_dos_time(*last_modified, &dos_time, &dos_date);
  }
  else
  {
	  if (!my_mz_zip_get_file_modified_time(pSrc_filename, &dos_time, &dos_date))
		  return MZ_FALSE;
  }
  
    
  pSrc_file = Server->openFile(os_file_prefix(pSrc_filename));
  if (!pSrc_file)
    return MZ_FALSE;

  uncomp_size = pSrc_file->Size();

  if (uncomp_size > 0xFFFFFFFF)
  {
    // No zip64 support yet
	  Server->destroy(pSrc_file);
      return MZ_FALSE;
  }

  if (!mz_zip_writer_write_zeros(pZip, cur_archive_file_ofs, num_alignment_padding_bytes))
  {
    Server->destroy(pSrc_file);
    return MZ_FALSE;
  }
  local_dir_header_ofs += num_alignment_padding_bytes;

  if(uncomp_size && level)
  {
	  method=MZ_DEFLATED;
  }

  if (!mz_zip_writer_create_local_dir_header(pZip, local_dir_header, (mz_uint16)archive_name_size, 0, 0, 0, 0, method, gen_flags, dos_time, dos_date))
    return MZ_FALSE;

  if (pZip->m_pWrite(pZip->m_pIO_opaque, local_dir_header_ofs, local_dir_header, sizeof(local_dir_header)) != sizeof(local_dir_header))
    return MZ_FALSE;

  if (pZip->m_file_offset_alignment) { MZ_ASSERT((local_dir_header_ofs & (pZip->m_file_offset_alignment - 1)) == 0); }
  cur_archive_file_ofs += num_alignment_padding_bytes + sizeof(local_dir_header);

  MZ_CLEAR_OBJ(local_dir_header);
  if (pZip->m_pWrite(pZip->m_pIO_opaque, cur_archive_file_ofs, pArchive_name, archive_name_size) != archive_name_size)
  {
    Server->destroy(pSrc_file);
    return MZ_FALSE;
  }
  cur_archive_file_ofs += archive_name_size;

  if (uncomp_size)
  {
    mz_uint64 uncomp_remaining = uncomp_size;
    void *pRead_buf = pZip->m_pAlloc(pZip->m_pAlloc_opaque, 1, MZ_ZIP_MAX_IO_BUF_SIZE);
    if (!pRead_buf)
    {
      Server->destroy(pSrc_file);
      return MZ_FALSE;
    }

    if (!level)
    {
      while (uncomp_remaining)
      {
        mz_uint n = (mz_uint)MZ_MIN(MZ_ZIP_MAX_IO_BUF_SIZE, uncomp_remaining);
        if ((pSrc_file->Read((char*)pRead_buf, n) != n) || (pZip->m_pWrite(pZip->m_pIO_opaque, cur_archive_file_ofs, pRead_buf, n) != n))
        {
          pZip->m_pFree(pZip->m_pAlloc_opaque, pRead_buf);
          Server->destroy(pSrc_file);
          return MZ_FALSE;
        }
        uncomp_crc32 = (mz_uint32)mz_crc32(uncomp_crc32, (const mz_uint8 *)pRead_buf, n);
        uncomp_remaining -= n;
        cur_archive_file_ofs += n;
      }
      comp_size = uncomp_size;
    }
    else
    {
      mz_bool result = MZ_FALSE;
      mz_zip_writer_add_state state;
      tdefl_compressor *pComp = (tdefl_compressor *)pZip->m_pAlloc(pZip->m_pAlloc_opaque, 1, sizeof(tdefl_compressor));
      if (!pComp)
      {
        pZip->m_pFree(pZip->m_pAlloc_opaque, pRead_buf);
        Server->destroy(pSrc_file);
        return MZ_FALSE;
      }

      state.m_pZip = pZip;
      state.m_cur_archive_file_ofs = cur_archive_file_ofs;
      state.m_comp_size = 0;

      if (tdefl_init(pComp, mz_zip_writer_add_put_buf_callback, &state, tdefl_create_comp_flags_from_zip_params(level, -15, MZ_DEFAULT_STRATEGY)) != TDEFL_STATUS_OKAY)
      {
        pZip->m_pFree(pZip->m_pAlloc_opaque, pComp);
        pZip->m_pFree(pZip->m_pAlloc_opaque, pRead_buf);
        Server->destroy(pSrc_file);
        return MZ_FALSE;
      }

      for ( ; ; )
      {
        size_t in_buf_size = (mz_uint32)MZ_MIN(uncomp_remaining, MZ_ZIP_MAX_IO_BUF_SIZE);
        tdefl_status status;

        if (pSrc_file->Read((char*)pRead_buf, (_u32)in_buf_size) != in_buf_size)
          break;

        uncomp_crc32 = (mz_uint32)mz_crc32(uncomp_crc32, (const mz_uint8 *)pRead_buf, in_buf_size);
        uncomp_remaining -= in_buf_size;

        status = tdefl_compress_buffer(pComp, pRead_buf, in_buf_size, uncomp_remaining ? TDEFL_NO_FLUSH : TDEFL_FINISH);
        if (status == TDEFL_STATUS_DONE)
        {
          result = MZ_TRUE;
          break;
        }
        else if (status != TDEFL_STATUS_OKAY)
          break;
      }

      pZip->m_pFree(pZip->m_pAlloc_opaque, pComp);

      if (!result)
      {
        pZip->m_pFree(pZip->m_pAlloc_opaque, pRead_buf);
        Server->destroy(pSrc_file);
        return MZ_FALSE;
      }

      comp_size = state.m_comp_size;
      cur_archive_file_ofs = state.m_cur_archive_file_ofs;

      method = MZ_DEFLATED;
    }

    pZip->m_pFree(pZip->m_pAlloc_opaque, pRead_buf);
  }

  Server->destroy(pSrc_file); pSrc_file = NULL;

  // no zip64 support yet
  if ((comp_size > 0xFFFFFFFF) || (cur_archive_file_ofs > 0xFFFFFFFF))
    return MZ_FALSE;

  mz_uint8 local_dir_footer[16];

  MZ_WRITE_LE32(local_dir_footer + 0, 0x08074b50);
  MZ_WRITE_LE32(local_dir_footer + 4, uncomp_crc32);
  MZ_WRITE_LE32(local_dir_footer + 8, comp_size);
  MZ_WRITE_LE32(local_dir_footer + 12, uncomp_size);

  if (pZip->m_pWrite(pZip->m_pIO_opaque, cur_archive_file_ofs, local_dir_footer, sizeof(local_dir_footer)) != sizeof(local_dir_footer))
    return MZ_FALSE;

  cur_archive_file_ofs+=sizeof(local_dir_footer);

  if (!mz_zip_writer_add_to_central_dir(pZip, pArchive_name, (mz_uint16)archive_name_size, NULL, 0, pComment, comment_size,
	               uncomp_size, comp_size, uncomp_crc32, method, gen_flags, dos_time, dos_date, local_dir_header_ofs, ext_attributes))
    return MZ_FALSE;

  pZip->m_total_files++;
  pZip->m_archive_size = cur_archive_file_ofs;

  return MZ_TRUE;
}

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

bool miniz_init(mz_zip_archive *pZip, MiniZFileInfo* fileInfo)
{
	pZip->m_pWrite = my_mz_write_func;
	pZip->m_pIO_opaque = fileInfo;

	if (!mz_zip_writer_init(pZip, 0))
		return false;
  
	return true;
}

bool add_dir(mz_zip_archive& zip_archive, const std::wstring& archivefoldername, const std::wstring& foldername, const std::wstring& hashfoldername, const std::wstring& filter,
		bool token_authentication, const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes)
{
	bool has_error=false;
	const std::vector<SFile> files = getFiles(foldername, &has_error, true);

	if(has_error)
		return false;

	for(size_t i=0;i<files.size();++i)
	{
		const SFile& file=files[i];

		if(skip_hashes
			&& file.name==L".hashes")
		{
			continue;
		}

		std::wstring archivename = archivefoldername + (archivefoldername.empty()?L"":L"/") + file.name;
		std::wstring metadataname = hashfoldername + os_file_sep() + escape_metadata_fn(file.name);
		std::wstring filename = foldername + os_file_sep() + file.name;

		if(!filter.empty() && archivename!=filter)
			continue;

		if(file.isdir)
		{
			metadataname+=os_file_sep()+metadata_dir_fn;
		}

		bool has_metadata = false;

		FileMetadata metadata;
		if(token_authentication &&
			( !read_metadata(metadataname, metadata) ||
			  !checkFileToken(backup_tokens, tokens, metadata) ) )
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
			last_modified_wt=static_cast<time_t>(os_to_windows_filetime(metadata.last_modified));
			last_modified=&last_modified_wt;
		}

		mz_bool rc;
		if(file.isdir)
		{
			rc = mz_zip_writer_add_mem_ex(&zip_archive, Server->ConvertToUTF8(archivename + L"/").c_str(), NULL, 0, NULL, 0, MZ_DEFAULT_LEVEL,
			0, 0, 1<<11, last_modified);
		}
		else
		{			
			rc = my_mz_zip_writer_add_file(&zip_archive, Server->ConvertToUTF8(archivename).c_str(), filename.c_str(), NULL, 0, MZ_DEFAULT_LEVEL,
				last_modified);
		}

		if(rc==MZ_FALSE)
		{
			Server->Log(L"Error while adding file \""+filename+L"\" to ZIP file. RC="+convert((int)rc), LL_ERROR);
			return false;
		}

		if(file.isdir)
		{
			add_dir(zip_archive, archivename, filename, hashfoldername + os_file_sep() + file.name, filter,
				token_authentication, backup_tokens, tokens, false);
		}
	}

	return true;
}

}

bool create_zip_to_output(const std::wstring& foldername, const std::wstring& hashfoldername, const std::wstring& filter, bool token_authentication,
	const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes)
{
	mz_zip_archive zip_archive;
	memset(&zip_archive, 0, sizeof(zip_archive));

	MiniZFileInfo file_info = {};

	file_info.tid=Server->getThreadID();

	if(!miniz_init(&zip_archive, &file_info))
	{
		Server->Log("Error while initializing ZIP archive", LL_ERROR);
		return false;
	}

	if(!add_dir(zip_archive, L"", foldername, hashfoldername, filter, token_authentication, backup_tokens, tokens, skip_hashes))
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