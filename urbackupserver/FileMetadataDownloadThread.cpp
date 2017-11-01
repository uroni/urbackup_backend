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

#include "FileMetadataDownloadThread.h"
#include "ClientMain.h"
#include "server_log.h"
#include "../urbackupcommon/file_metadata.h"
#include "../common/data.h"
#include <memory>
#include <limits.h>
#include "../common/adler32.h"
#include "FileBackup.h"

namespace server
{

const _u32 ID_METADATA_OS_WIN = 1<<0;
const _u32 ID_METADATA_OS_UNIX = 1<<2;
const _u32 ID_METADATA_NOP = 0;
const _u32 ID_METADATA_V1 = 1<<3;
const _u32 ID_RAW_FILE = 1 << 4;

FileMetadataDownloadThread::FileMetadataDownloadThread(FileClient* fc, const std::string& server_token, logid_t logid,
	int backupid, int clientid, bool use_tmpfiles, std::string tmpfile_path)
	: fc(fc), server_token(server_token), logid(logid), has_error(false), dry_run(false),
	backupid(backupid), max_metadata_id(0), clientid(clientid), has_fatal_error(false), has_timeout_error(false),
	use_tmpfiles(use_tmpfiles), tmpfile_path(tmpfile_path), is_complete(false), is_finished(false), mutex(Server->createMutex()),
	cond(Server->createCondition()), force_start(false)
{

}

FileMetadataDownloadThread::FileMetadataDownloadThread(const std::string& server_token, std::string metadata_tmp_fn,
	int backupid, int clientid, bool use_tmpfiles, std::string tmpfile_path)
	: fc(NULL), server_token(server_token), has_error(false), metadata_tmp_fn(metadata_tmp_fn),
	dry_run(true), backupid(backupid), max_metadata_id(0), clientid(clientid), has_fatal_error(false), has_timeout_error(false),
	use_tmpfiles(use_tmpfiles), tmpfile_path(tmpfile_path), is_complete(false), is_finished(true), force_start(false)
{

}

void FileMetadataDownloadThread::operator()()
{
	std::auto_ptr<IFsFile> tmp_f(ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid));
	metadata_tmp_fn = tmp_f->getFilename();

	std::string remote_fn = "SCRIPT|urbackup/FILE_METADATA|"+server_token+"|"+convert(backupid);

	orig_progress_log_callback = fc->getProgressLogCallback();

	fc->setProgressLogCallback(NULL);
	fc->setNoFreeSpaceCallback(NULL);

	_u32 rc = fc->GetFile(remote_fn, tmp_f.get(), true, false, 0, true, 0);

	setFinished();

	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error getting file metadata. Errorcode: "+FileClient::getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		has_error=true;

		if(rc==ERR_CONN_LOST || rc==ERR_TIMEOUT)
		{
			has_timeout_error=true;
		}
	}
	else
	{
		has_error=false;

		fc->FinishScript(remote_fn);
		setComplete();
	}
}

bool FileMetadataDownloadThread::applyMetadata( const std::string& backup_metadata_dir,
	const std::string& backup_dir, INotEnoughSpaceCallback *cb, BackupServerHash* local_hash, FilePathCorrections& filepath_corrections,
	size_t& num_embedded_files, MaxFileId* max_file_id)
{
	buffer.resize(32768);
	int mode = MODE_READ_SEQUENTIAL;
#ifdef _WIN32
	mode = MODE_READ_DEVICE;
#endif
	std::auto_ptr<IFile> metadata_f(Server->openFile(metadata_tmp_fn, mode));

	if(metadata_f.get()==NULL)
	{
		ServerLogger::Log(logid, "Error opening metadata file at \""+ metadata_tmp_fn+"\". Cannot save file metadata. "+os_last_error_str(), LL_ERROR);
		return false;
	}

	int64 metadataf_pos = 0;

	int64 lastlogtime = Server->getTimeMS();

	last_metadata_ids.reserve(8000);

	size_t metadata_n_files = 0;

	do 
	{
		char ch;
		if(!readRetry(metadata_f.get(), reinterpret_cast<char*>(&ch), sizeof(ch)))
		{
			bool not_applied=false;
			for(size_t i=0;i<saved_folder_items.size();++i)
			{
				if(saved_folder_items[i].folder_items!=-1)
				{
					not_applied = true;
					break;
				}
			}
			if(not_applied)
			{
				ServerLogger::Log(logid, "Not all folder metadata could be applied. Metadata was inconsistent.", isComplete() ? LL_WARNING : LL_DEBUG);
			}
			std::sort(last_metadata_ids.begin(), last_metadata_ids.end());
			return true;
		}

		metadataf_pos += 1;

		if (isFinished())
		{
			int64 ctime = Server->getTimeMS();
			if (ctime - lastlogtime > 60000)
			{
				int pc_finished = (int)((metadataf_pos*100.f) / metadata_f->Size() + 0.5f);
				ServerLogger::Log(logid, "Saved metadata of " + convert(metadata_n_files) + " files and directories. " + convert(pc_finished) + "% done...", LL_DEBUG);
				lastlogtime = ctime;
			}
		}

		if(ch==ID_METADATA_NOP)
		{
			continue;
		}

		++metadata_n_files;
	
		if(ch & ID_METADATA_V1)
		{
			unsigned int curr_fn_size =0;
			if(!readRetry(metadata_f.get(), reinterpret_cast<char*>(&curr_fn_size), sizeof(curr_fn_size)) )
			{
				ServerLogger::Log(logid, "Error saving metadata. Filename size could not be read.", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}
			
			metadataf_pos += sizeof(curr_fn_size);
			unsigned int path_checksum = urb_adler32(urb_adler32(0, NULL, 0), reinterpret_cast<char*>(&curr_fn_size), sizeof(curr_fn_size));

			curr_fn_size = little_endian(curr_fn_size);

			std::string curr_fn;
			curr_fn.resize(curr_fn_size);

			if(curr_fn_size>1)
			{
				if (!readRetry(metadata_f.get(), &curr_fn[0], static_cast<_u32>(curr_fn.size())))
				{
					ServerLogger::Log(logid, "Error saving metadata. Filename could not be read. Size: "+convert(curr_fn_size), LL_ERROR);
					if (isComplete())
					{
						copyForAnalysis(metadata_f.get());
					}
					return false;
				}

				metadataf_pos += curr_fn.size();
				path_checksum = urb_adler32(path_checksum, &curr_fn[0], static_cast<_u32>(curr_fn.size()));
			}
			else
			{
				ServerLogger::Log(logid, "Error saving metadata. Filename is empty.", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}

			unsigned int read_path_checksum =0;
			if (!readRetry(metadata_f.get(), reinterpret_cast<char*>(&read_path_checksum), sizeof(read_path_checksum)))
			{
				ServerLogger::Log(logid, "Error saving metadata. Path checksum could not be read.", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}

			metadataf_pos += sizeof(read_path_checksum);

			if(little_endian(read_path_checksum)!=path_checksum)
			{
				ServerLogger::Log(logid, "Error saving metadata. Path checksum wrong.", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}

			bool is_dir = (curr_fn[0]=='d' || curr_fn[0]=='l');
			bool is_dir_symlink = curr_fn[0]=='l';

			std::string os_path_metadata;
			std::string os_path;
			std::vector<std::string> fs_toks;
			Tokenize(curr_fn.substr(1), fs_toks, "/");

			std::string curr_path;
			for(size_t i=0;i<fs_toks.size();++i)
			{
				if(fs_toks[i]!="." && fs_toks[i]!="..")
				{
					if(!os_path_metadata.empty())
					{
						os_path_metadata+=os_file_sep();
						os_path+=os_file_sep();
					}

					std::string path_component = (fs_toks[i]);

					curr_path += "/" + path_component;

					filepath_corrections.get(curr_path, path_component);

					os_path+=path_component;

					if(i==fs_toks.size()-1)
					{
						os_path_metadata += escape_metadata_fn(path_component);

						if(is_dir && !is_dir_symlink)
						{
							os_path_metadata+=os_file_sep()+metadata_dir_fn;
						}
					}
					else
					{						
						os_path_metadata += path_component;
					}					
				}
			}

			unsigned int common_metadata_size =0;
			if (!readRetry(metadata_f.get(), reinterpret_cast<char*>(&common_metadata_size), sizeof(common_metadata_size)))
			{
				ServerLogger::Log(logid, "Error saving metadata. Common metadata size could not be read.", isComplete() ? LL_ERROR : LL_DEBUG);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}

			metadataf_pos += sizeof(common_metadata_size);

			unsigned int common_metadata_checksum = urb_adler32(urb_adler32(0, NULL, 0), reinterpret_cast<char*>(&common_metadata_size), sizeof(common_metadata_size));
			
			common_metadata_size = little_endian(common_metadata_size);

			std::vector<char> common_metadata;
			common_metadata.resize(common_metadata_size);

			if (!readRetry(metadata_f.get(), &common_metadata[0], static_cast<_u32>(common_metadata.size())))
			{
				ServerLogger::Log(logid, "Error saving metadata. Common metadata could not be read.", isComplete() ? LL_ERROR : LL_DEBUG);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}

			metadataf_pos += common_metadata.size();

			common_metadata_checksum = urb_adler32(common_metadata_checksum, &common_metadata[0], static_cast<_u32>(common_metadata.size()));

			unsigned int read_common_metadata_checksum =0;
			if (!readRetry(metadata_f.get(), reinterpret_cast<char*>(&read_common_metadata_checksum), sizeof(read_common_metadata_checksum)))
			{
				ServerLogger::Log(logid, "Error saving metadata. Common metadata checksum could not be read.", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}

			metadataf_pos += sizeof(read_common_metadata_checksum);

			if(little_endian(read_common_metadata_checksum)!=common_metadata_checksum)
			{
				ServerLogger::Log(logid, "Error saving metadata. Common metadata checksum wrong.", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				return false;
			}

			CRData common_data(common_metadata.data(), common_metadata.size());
			char common_version;
			common_data.getChar(&common_version);
			int64 created;
			int64 modified;
			int64 accessed;
			int64 folder_items;
			int64 metadata_id;
			std::string permissions;
			if(common_version!=1
				|| !common_data.getVarInt(&created)
				|| !common_data.getVarInt(&modified)
				|| !common_data.getVarInt(&accessed)
				|| !common_data.getVarInt(&folder_items)
				|| !common_data.getVarInt(&metadata_id)
				|| !common_data.getStr(&permissions) )
			{
				ServerLogger::Log(logid, "Error saving metadata. Cannot parse common metadata.", LL_ERROR);
				copyForAnalysis(metadata_f.get());
				return false;
			}

			while (max_file_id != NULL
				&& !max_file_id->isFinished(metadata_id - 1))
			{
				IScopedLock lock(mutex.get());
				if (force_start)
				{
					break;
				}
				cond->wait(&lock, 60000);
			}

			std::auto_ptr<IFile> output_f;
			bool new_metadata_file = false;

			int ftype = 0;

			if (!dry_run)
			{
				ftype = os_get_file_type(os_file_prefix(backup_dir + os_file_sep() + os_path));

				output_f.reset(Server->openFile(os_file_prefix(backup_metadata_dir + os_file_sep() + os_path_metadata), MODE_RW));

				if (output_f.get() == NULL
					&& ftype == 0)
				{
					ServerLogger::Log(logid, "Metadata file and \"" + backup_dir + os_file_sep() + os_path + "\" do not exist. Skipping applying metdata for this file.", isComplete() ? LL_WARNING : LL_DEBUG);
				}
				else if (output_f.get() == NULL)
				{
					output_f.reset(Server->openFile(os_file_prefix(backup_metadata_dir + os_file_sep() + os_path_metadata), MODE_RW_CREATE));
					new_metadata_file = true;
				}

				if (output_f.get() == NULL
					&& ftype != 0)
				{
					ServerLogger::Log(logid, "Error saving metadata. Could not open output file at \"" + backup_metadata_dir + os_file_sep() + os_path_metadata + "\"", LL_ERROR);
					return false;
				}
			}

			FileMetadata curr_metadata;
			if(!dry_run && output_f.get() != NULL && !new_metadata_file && !read_metadata(output_f.get(), curr_metadata))
			{
				ServerLogger::Log(logid, "Error reading current metadata", LL_WARNING);
			}

			curr_metadata.exist=true;
			curr_metadata.created=created;
			curr_metadata.last_modified = modified;
			curr_metadata.file_permissions = permissions;
			curr_metadata.accessed = accessed;

			int64 offset = 0;

			if(!dry_run && output_f.get() != NULL)
			{
				int64 truncate_to_bytes;
				if(!write_file_metadata(output_f.get(), cb, curr_metadata, true, truncate_to_bytes))
				{
					ServerLogger::Log(logid, "Error saving metadata. Cannot write common metadata.", LL_ERROR);
					return false;
				}

				offset = os_metadata_offset(output_f.get());

				if(offset==-1)
				{
					ServerLogger::Log(logid, "Error saving metadata. Metadata offset cannot be calculated at \"" + backup_metadata_dir+os_file_sep()+os_path_metadata + "\"", LL_ERROR);
					return false;
				}

				if(!output_f->Seek(offset))
				{
					ServerLogger::Log(logid, "Error saving metadata. Could not seek to end of file \"" + backup_metadata_dir+os_file_sep()+os_path_metadata + "\"", LL_ERROR);
					return false;
				}
			}			

			int64 metadata_size=0;
			bool ok=false;
			if(ch & ID_METADATA_OS_WIN)
			{
				ok = applyWindowsMetadata(metadata_f.get(), output_f.get(), metadata_size, cb, offset, metadataf_pos);
			}
            else if(ch & ID_METADATA_OS_UNIX)
            {
                ok = applyUnixMetadata(metadata_f.get(), output_f.get(), metadata_size, cb, offset, metadataf_pos);
            }

			if(!ok)
			{
				ServerLogger::Log(logid, "Error saving metadata. Could not save OS specific metadata to \"" + backup_metadata_dir+os_file_sep()+os_path_metadata + "\"", isComplete() ? LL_ERROR : LL_DEBUG);

				if(!dry_run && output_f.get() != NULL)
				{
					output_f.reset();
					if(!os_file_truncate(os_file_prefix(backup_metadata_dir+os_file_sep()+os_path_metadata),
						offset))
					{
						ServerLogger::Log(logid, "Could not truncate file \"" + backup_metadata_dir+os_file_sep()+os_path_metadata + "\" after error.", LL_ERROR);
					}
				}

				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				
				return false;
			}
			else if(!dry_run && output_f.get() != NULL && offset+metadata_size<output_f->Size())
			{
				output_f.reset();
				if(!os_file_truncate(os_file_prefix(backup_metadata_dir+os_file_sep()+os_path_metadata),
					offset+metadata_size))
				{
					ServerLogger::Log(logid, "Error saving metadata. Could not truncate file \"" + backup_metadata_dir+os_file_sep()+os_path_metadata + "\"", LL_ERROR);
					return false;
				}
			}

			max_metadata_id = (std::max)(max_metadata_id, metadata_id);

			last_metadata_ids.push_back(metadata_id);

			if(last_metadata_ids.size()>8000)
			{
				last_metadata_ids.erase(last_metadata_ids.begin(), last_metadata_ids.begin()+4000);
			}

			bool win_is_symlink = false;

#ifdef _WIN32
			if (!is_dir)
			{
				if (ftype &EFileType_Symlink)
				{
					win_is_symlink = true;
				}
			}
#endif

			if(!dry_run && output_f.get() != NULL && !is_dir && !win_is_symlink
				&& !os_set_file_time(os_file_prefix(backup_dir+os_file_sep()+os_path), created, modified, accessed))
			{
				ServerLogger::Log(logid, "Error setting file time of "+backup_dir+os_file_sep()+os_path+" . "+os_last_error_str(), LL_WARNING);
			}

			if(!dry_run && output_f.get() != NULL)
			{
				addFolderItem(curr_fn.substr(1), backup_dir+os_file_sep()+os_path, is_dir, created, modified, accessed, folder_items);
			}
		}
		else if (ch == ID_RAW_FILE)
		{
			unsigned int curr_fn_size = 0;
			if (!readRetry(metadata_f.get(), reinterpret_cast<char*>(&curr_fn_size), sizeof(curr_fn_size)))
			{
				ServerLogger::Log(logid, "Error saving metadata. Filename size could not be read. (2)", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				has_fatal_error = true;
				return false;
			}

			metadataf_pos += sizeof(curr_fn_size);
			unsigned int path_checksum = urb_adler32(urb_adler32(0, NULL, 0), reinterpret_cast<char*>(&curr_fn_size), sizeof(curr_fn_size));

			curr_fn_size = little_endian(curr_fn_size);

			std::string curr_fn;
			curr_fn.resize(curr_fn_size);

			if (curr_fn_size>1)
			{
				if (!readRetry(metadata_f.get(), &curr_fn[0], static_cast<_u32>(curr_fn.size())))
				{
					ServerLogger::Log(logid, "Error saving metadata. Filename could not be read (2). Size: " + convert(curr_fn_size), LL_ERROR);
					if (isComplete())
					{
						copyForAnalysis(metadata_f.get());
					}
					has_fatal_error = true;
					return false;
				}

				metadataf_pos += curr_fn.size();
				path_checksum = urb_adler32(path_checksum, &curr_fn[0], static_cast<_u32>(curr_fn.size()));
			}
			else
			{
				ServerLogger::Log(logid, "Error saving metadata. Filename is empty.", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				has_fatal_error = true;
				return false;
			}

			unsigned int read_path_checksum = 0;
			if (!readRetry(metadata_f.get(), reinterpret_cast<char*>(&read_path_checksum), sizeof(read_path_checksum)))
			{
				ServerLogger::Log(logid, "Error saving metadata. Path checksum could not be read. (2)", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				has_fatal_error = true;
				return false;
			}

			metadataf_pos += sizeof(read_path_checksum);

			if (little_endian(read_path_checksum) != path_checksum)
			{
				ServerLogger::Log(logid, "Error saving metadata. Path checksum wrong. (2)", LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				has_fatal_error = true;
				return false;
			}

			bool is_dir = (curr_fn[0] == 'd' || curr_fn[0] == 'l');
			bool is_dir_symlink = curr_fn[0] == 'l';

			std::string os_path;
			std::string os_path_metadata;
			std::vector<std::string> fs_toks;
			Tokenize(curr_fn.substr(1), fs_toks, "/");

			std::string curr_path;
			for (size_t i = 0; i<fs_toks.size(); ++i)
			{
				if (fs_toks[i] != "." && fs_toks[i] != "..")
				{
					if (!os_path.empty())
					{
						os_path += os_file_sep();
						os_path_metadata += os_file_sep();
					}

					std::string path_component = (fs_toks[i]);

					curr_path += "/" + path_component;

					if(!filepath_corrections.get(curr_path, path_component))
					{
						std::set<std::string> samedir_filenames;
						path_component = FileBackup::fixFilenameForOS(path_component, samedir_filenames, curr_path, true, logid, filepath_corrections);
					}

					if (i == fs_toks.size() - 1)
					{
						os_path_metadata += escape_metadata_fn(path_component);

						if (is_dir && !is_dir_symlink)
						{
							os_path_metadata += os_file_sep() + metadata_dir_fn;
						}
					}
					else
					{
						os_path_metadata += path_component;
					}

					os_path += path_component;

					if (i + 1 < fs_toks.size())
					{
						if (!os_directory_exists(os_file_prefix(backup_dir + os_file_sep() + os_path)))
						{
							os_create_dir(os_file_prefix(backup_dir + os_file_sep() + os_path));
						}
					}
				}
			}

			os_path = backup_dir + os_file_sep() + os_path;
			os_path_metadata = backup_metadata_dir + os_file_sep() + os_path_metadata;

			int64 output_f_size= 0;
			if (!readRetry(metadata_f.get(), reinterpret_cast<char*>(&output_f_size), sizeof(output_f_size)))
			{
				ServerLogger::Log(logid, "Error saving metadata. Output file size could not be read. (2) "+os_last_error_str(), LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				has_fatal_error = true;
				return false;
			}

			metadataf_pos += sizeof(output_f_size);

			output_f_size = little_endian(output_f_size);

			while (metadata_f->Size() < metadataf_pos + output_f_size + SHA512_DIGEST_SIZE)
			{
				IScopedLock lock(mutex.get());
				if (is_finished)
				{
					break;
				}
				cond->wait(&lock, 60000);
			}

			if (!metadata_f->Seek(metadataf_pos + output_f_size))
			{
				ServerLogger::Log(logid, "Error seeking in metadata for output file checksum. "+os_last_error_str(), LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				has_fatal_error = true;
				return false;
			}

			std::string checksum = metadata_f->Read(SHA512_DIGEST_SIZE);
			if (checksum.size() != SHA512_DIGEST_SIZE)
			{
				ServerLogger::Log(logid, "Error reading output file checksum. " + os_last_error_str(), LL_ERROR);
				if (isComplete())
				{
					copyForAnalysis(metadata_f.get());
				}
				has_fatal_error = true;
				return false;
			}

			bool copy_file = true;

			if (output_f_size > 4096
				&& local_hash!=NULL)
			{
				bool tries_once;
				std::string ff_last;
				bool hardlink_limit;
				bool copied_file;
				int64 entryid = 0;
				int entryclientid = 0;
				int64 rsize = 0;
				int64 next_entryid = 0;
				FileMetadata metadata;
				bool ok = local_hash->findFileAndLink(os_path, NULL, os_path_metadata, checksum, output_f_size, std::string(), true,
					tries_once, ff_last, hardlink_limit, copied_file, entryid, entryclientid, rsize, next_entryid,
					metadata, true, NULL);

				if (ok)
				{
					local_hash->addFileSQL(backupid, clientid, 0, os_path, os_path_metadata, checksum, output_f_size,
						(rsize>0 && rsize != output_f_size) ? rsize : (copied_file ? output_f_size : 0), entryid, entryclientid, next_entryid,
						copied_file);

					copy_file = false;

					ServerLogger::Log(logid, "META: Linked file: \"" + os_path + "\"", LL_DEBUG);
				}
			}

			if (copy_file)
			{
				ServerLogger::Log(logid, "META: Copying file: \"" + os_path + "\"", LL_DEBUG);

				std::auto_ptr<IFile> output_f(Server->openFile(os_file_prefix(os_path), MODE_WRITE));

				if (output_f.get() == NULL)
				{
					ServerLogger::Log(logid, "Error opening output file \"" + os_path + "\". " + os_last_error_str(), LL_ERROR);
					if (isComplete())
					{
						copyForAnalysis(metadata_f.get());
					}
					has_fatal_error = true;
					return false;
				}

				if (!metadata_f->Seek(metadataf_pos))
				{
					ServerLogger::Log(logid, "Error seeking in metadata for data start. " + os_last_error_str(), LL_ERROR);
					if (isComplete())
					{
						copyForAnalysis(metadata_f.get());
					}
					has_fatal_error = true;
					return false;
				}

				sha512_ctx shactx;
				sha512_init(&shactx);

				int64 filedata_end = metadataf_pos + output_f_size;
				while (metadataf_pos < filedata_end)
				{
					_u32 toread = static_cast<_u32>((std::min)(filedata_end - metadataf_pos, static_cast<int64>(buffer.size())));

					if (metadata_f->Read(buffer.data(), toread) != toread)
					{
						ServerLogger::Log(logid, "Error saving metadata. Output data could not be read. " + os_last_error_str(), LL_ERROR);
						if (isComplete())
						{
							copyForAnalysis(metadata_f.get());
						}
						has_fatal_error = true;
						return false;
					}

					if (!writeRepeatFreeSpace(output_f.get(), buffer.data(), toread, cb))
					{
						ServerLogger::Log(logid, "Error saving metadata. Error writing to output file. " + os_last_error_str(), LL_ERROR);
						has_fatal_error = true;
						return false;
					}

					sha512_update(&shactx, reinterpret_cast<unsigned char*>(buffer.data()), toread);

					metadataf_pos += toread;
				}

				unsigned char dig[SHA512_DIGEST_SIZE];
				sha512_final(&shactx, dig);

				if (memcmp(checksum.data(), dig, SHA512_DIGEST_SIZE) != 0)
				{
					ServerLogger::Log(logid, "Data checksum of file \""+os_path+"\" wrong.", LL_ERROR);
					if (isComplete())
					{
						copyForAnalysis(metadata_f.get());
					}
					has_fatal_error = true;
					return false;
				}

				metadataf_pos += SHA512_DIGEST_SIZE;

				if (!metadata_f->Seek(metadataf_pos))
				{
					ServerLogger::Log(logid, "Error seeking in metadata for end. " + os_last_error_str(), LL_ERROR);
					if (isComplete())
					{
						copyForAnalysis(metadata_f.get());
					}
					has_fatal_error = true;
					return false;
				}

				if (output_f_size > 4096
					&& local_hash!=NULL)
				{
					local_hash->addFileSQL(backupid, clientid, 0, os_path, os_path_metadata, checksum, output_f_size,
						output_f_size, 0, 0, 0, true);
				}

				FileMetadata metadata;
				metadata.exist = true;
				metadata.set_shahash(checksum);
				write_file_metadata(os_path_metadata, cb, metadata, false);
			}
			else
			{
				metadataf_pos += output_f_size + SHA512_DIGEST_SIZE;
			}

			++num_embedded_files;
		}
		else
		{
			ServerLogger::Log(logid, "Error applying meta data. Unknown meta data.", LL_ERROR);

			std::auto_ptr<IFile> tmp(Server->openTemporaryFile());

			if(copy_file(metadata_f.get(), tmp.get()))
			{
				ServerLogger::Log(logid, "Copied metadata to "+tmp->getFilename()+" for error analysis", LL_INFO);
			}

			return false;
		}

	} while (true);

	std::sort(last_metadata_ids.begin(), last_metadata_ids.end());

	Server->Log("Loop exit in " + std::string(__FUNCTION__), LL_ERROR);
	assert(false);
	return true;
}

namespace
{
	struct WIN32_STREAM_ID_INT
	{
		_u32 dwStreamId;
		_u32 dwStreamAttributes;
		int64 Size;
		_u32 dwStreamNameSize;
	};

	const size_t metadata_id_size = 4+4+8+4;
	const int64 win32_meta_magic = little_endian(0x320FAB3D119DCB4AULL);
    const int64 unix_meta_magic =  little_endian(0xFE4378A3467647F0ULL);
}

bool FileMetadataDownloadThread::applyWindowsMetadata( IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb, int64 output_offset, int64& metadataf_pos)
{
	int64 win32_magic_and_size[2];
	win32_magic_and_size[1]=win32_meta_magic;
	
	if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(win32_magic_and_size), sizeof(win32_magic_and_size), cb))
	{
		ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (beg)", LL_ERROR);
		return false;
	}

	metadata_size=sizeof(win32_magic_and_size);

	unsigned int data_checksum = urb_adler32(0, NULL, 0);

	_u32 stat_data_size;
	if (!readRetry(metadata_f, reinterpret_cast<char*>(&stat_data_size), sizeof(stat_data_size)))
	{
		ServerLogger::Log(logid, "Error reading stat data size from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
		return false;
	}

	metadataf_pos += sizeof(stat_data_size);

	data_checksum = urb_adler32(data_checksum, reinterpret_cast<char*>(&stat_data_size), sizeof(stat_data_size));

	if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&stat_data_size), sizeof(stat_data_size), cb))
	{
		ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (stat_data_size)", LL_ERROR);
		return false;
	}

	metadata_size+=sizeof(_u32);

	stat_data_size = little_endian(stat_data_size);

	if(stat_data_size<1)
	{
		ServerLogger::Log(logid, "stat data size is zero", LL_ERROR);
		return false;
	}

	char version;
	if (!readRetry(metadata_f, &version, 1))
	{
		ServerLogger::Log(logid, "Error reading windows metadata version from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
		return false;
	}

	metadataf_pos += 1;

	data_checksum = urb_adler32(data_checksum, &version, 1);

	if(version!=1)
	{
		ServerLogger::Log(logid, "Unknown windows metadata version +"+convert((int)version)+" in \"" + metadata_f->getFilename() + "\"", LL_ERROR);
		return false;
	}

	if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, &version, sizeof(version), cb))
	{
		ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (ver)", LL_ERROR);
		return false;
	}

	metadata_size+=1;

	std::vector<char> stat_data;
	stat_data.resize(stat_data_size-1);

	if (!readRetry(metadata_f, stat_data.data(), static_cast<_u32>(stat_data.size())))
	{
		ServerLogger::Log(logid, "Error reading windows stat data from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
		return false;
	}

	metadataf_pos += static_cast<_u32>(stat_data.size());
	data_checksum = urb_adler32(data_checksum, stat_data.data(), static_cast<_u32>(stat_data.size()));

	if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, stat_data.data(), stat_data.size(), cb))
	{
		ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (stat_data)", LL_ERROR);
		return false;
	}

	metadata_size+=stat_data.size();
	
	while(true) 
	{
		char cont = 0;
		if (!readRetry(metadata_f, reinterpret_cast<char*>(&cont), sizeof(cont)))
		{
			ServerLogger::Log(logid, "Error reading  cont from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
			return false;
		}

		metadataf_pos += sizeof(cont);
		data_checksum = urb_adler32(data_checksum, &cont, 1);

		if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&cont), sizeof(cont), cb))
		{
			ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (cont)", LL_ERROR);
			return false;
		}
		++metadata_size;

		if(cont==0)
		{
			break;
		}

		if (cont != 1)
		{
			ServerLogger::Log(logid, "Error reading  \"" + metadata_f->getFilename() + "\" -3 cont wrong: "+convert((int)cont), LL_ERROR);
			return false;
		}

		WIN32_STREAM_ID_INT stream_id;

		if (!readRetry(metadata_f, reinterpret_cast<char*>(&stream_id), metadata_id_size))
		{
			ServerLogger::Log(logid, "Error reading  \"" + metadata_f->getFilename() + "\" (metadata_id_size)", isComplete() ? LL_ERROR : LL_DEBUG);
			return false;
		}

		metadataf_pos += metadata_id_size;
		data_checksum = urb_adler32(data_checksum, reinterpret_cast<char*>(&stream_id), metadata_id_size);

		if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&stream_id), metadata_id_size, cb))
		{
			ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\"", LL_ERROR);
			return false;
		}

		metadata_size+=metadata_id_size;

		stream_id.dwStreamNameSize = little_endian(stream_id.dwStreamNameSize);
		stream_id.Size = little_endian(stream_id.Size);

		if(stream_id.dwStreamNameSize>0)
		{
			std::vector<char> stream_name;
			stream_name.resize(stream_id.dwStreamNameSize);

			if (!readRetry(metadata_f, stream_name.data(), static_cast<_u32>(stream_name.size())))
			{
				ServerLogger::Log(logid, "Error reading  \"" + metadata_f->getFilename() + "\" -2 (stream_name)", isComplete() ? LL_ERROR : LL_DEBUG);
				return false;
			}

			metadataf_pos += stream_name.size();
			data_checksum = urb_adler32(data_checksum, stream_name.data(), static_cast<_u32>(stream_name.size()));

			if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, stream_name.data(), stream_name.size(), cb))
			{
				ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" -2", LL_ERROR);
				return false;
			}

			metadata_size+=stream_name.size();
		}	

		int64 curr_pos=0;

		while(curr_pos<stream_id.Size)
		{
			_u32 toread = static_cast<_u32>((std::min)(static_cast<int64>(buffer.size()), stream_id.Size-curr_pos));

			if (!readRetry(metadata_f, buffer.data(), toread))
			{
				ServerLogger::Log(logid, "Error reading  \"" + metadata_f->getFilename() + "\" -3 (stream)", isComplete() ? LL_ERROR : LL_DEBUG);
				return false;
			}

			metadataf_pos += toread;
			data_checksum = urb_adler32(data_checksum, buffer.data(), toread);

			if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, buffer.data(), toread, cb))
			{
				ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" -3", LL_ERROR);
				return false;
			}

			metadata_size+=toread;

			curr_pos+=toread;
		}
	}

	unsigned int read_data_checksum =0;
	if (!readRetry(metadata_f, reinterpret_cast<char*>(&read_data_checksum), sizeof(read_data_checksum)))
	{
		ServerLogger::Log(logid, "Error saving metadata. Data checksum could not be read.", isComplete() ? LL_ERROR : LL_DEBUG);
		return false;
	}

	metadataf_pos += sizeof(read_data_checksum);
	if(little_endian(read_data_checksum)!=data_checksum)
	{
		ServerLogger::Log(logid, "Error saving metadata. Stat data checksum wrong.", LL_ERROR);
		return false;
	}

	if(!dry_run && output_f != NULL && !output_f->Seek(output_offset))
	{
		ServerLogger::Log(logid, "Error seeking to  \"" + convert(output_offset) + "\" -5 in output_f", LL_ERROR);
		return false;
	}

	win32_magic_and_size[0]=little_endian(metadata_size);

	if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&win32_magic_and_size[0]), sizeof(win32_magic_and_size[0]), cb))
	{
		ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (end)", LL_ERROR);
		return false;
	}

	return true;
}

bool FileMetadataDownloadThread::applyUnixMetadata(IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback* cb, int64 output_offset, int64& metadataf_pos)
{
    int64 unix_magic_and_size[2];
    unix_magic_and_size[1]=unix_meta_magic;

    if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(unix_magic_and_size), sizeof(unix_magic_and_size), cb))
    {
        ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (beg, unix)", LL_ERROR);
        return false;
    }

    metadata_size=sizeof(unix_magic_and_size);

	unsigned int data_checksum = urb_adler32(0, NULL, 0);

	_u32 stat_data_size;
	if (!readRetry(metadata_f, reinterpret_cast<char*>(&stat_data_size), sizeof(stat_data_size)))
	{
		ServerLogger::Log(logid, "Error reading stat data size from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
		return false;
	}

	metadataf_pos += sizeof(stat_data_size);
	data_checksum = urb_adler32(data_checksum, reinterpret_cast<char*>(&stat_data_size), sizeof(stat_data_size));

	if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&stat_data_size), sizeof(stat_data_size), cb))
	{
		ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (stat_data_size)", LL_ERROR);
		return false;
	}

	metadata_size+=sizeof(_u32);

	stat_data_size = little_endian(stat_data_size);

	if(stat_data_size<1)
	{
		ServerLogger::Log(logid, "stat data size is zero", LL_ERROR);
		return false;
	}

    char version;
	if (!readRetry(metadata_f, &version, 1))
    {
        ServerLogger::Log(logid, "Error reading unix metadata version from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
        return false;
    }

    if(version!=1)
    {
        ServerLogger::Log(logid, "Unknown unix metadata version +"+convert((int)version)+" in \"" + metadata_f->getFilename() + "\"", LL_ERROR);
        return false;
    }

	metadataf_pos += 1;
	data_checksum = urb_adler32(data_checksum, &version, 1);

    if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, &version, sizeof(version), cb))
    {
        ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (ver)", LL_ERROR);
        return false;
    }

    metadata_size+=1;

    std::vector<char> stat_data;
	stat_data.resize(stat_data_size-1);

	if (!readRetry(metadata_f, stat_data.data(), static_cast<_u32>(stat_data.size())))
    {
        ServerLogger::Log(logid, "Error reading stat data from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
        return false;
    }

	metadataf_pos += static_cast<_u32>(stat_data.size());
	data_checksum = urb_adler32(data_checksum, stat_data.data(), static_cast<_u32>(stat_data.size()));

    if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, stat_data.data(), stat_data.size(), cb))
    {
        ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (stat_data)", LL_ERROR);
        return false;
    }

    metadata_size+=stat_data.size();

    int64 num_eattr_keys;
	if (!readRetry(metadata_f, reinterpret_cast<char*>(&num_eattr_keys), sizeof(num_eattr_keys)))
    {
        ServerLogger::Log(logid, "Error reading eattr num from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
        return false;
    }

	metadataf_pos += sizeof(num_eattr_keys);
	data_checksum = urb_adler32(data_checksum, reinterpret_cast<char*>(&num_eattr_keys), sizeof(num_eattr_keys));

    if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&num_eattr_keys), sizeof(num_eattr_keys), cb))
    {
        ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (num_eattr_keys)", LL_ERROR);
        return false;
    }

    metadata_size+=sizeof(num_eattr_keys);

    num_eattr_keys = little_endian(num_eattr_keys);

    for(int64 i=0;i<num_eattr_keys;++i)
    {
        unsigned int key_size;
		if (!readRetry(metadata_f, reinterpret_cast<char*>(&key_size), sizeof(key_size)))
        {
            ServerLogger::Log(logid, "Error reading eattr key size from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
            return false;
        }

		metadataf_pos += sizeof(key_size);
		data_checksum = urb_adler32(data_checksum, reinterpret_cast<char*>(&key_size), sizeof(key_size));

        if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&key_size), sizeof(key_size), cb))
        {
            ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (key_size)", LL_ERROR);
            return false;
        }

        metadata_size+=sizeof(key_size);
        key_size = little_endian(key_size);

		if (key_size > 1 * 1024 * 1024)
		{
			ServerLogger::Log(logid, "Eattry key "+convert(i)+" for  \"" + (output_f != NULL ? output_f->getFilename() : "NONE") + "\" too large with size " + PrettyPrintBytes(key_size)
				+ " in \"" + metadata_f->getFilename() + "\"", LL_ERROR);
			return false;
		}

        std::string eattr_key;
        eattr_key.resize(key_size);

		if (!readRetry(metadata_f, &eattr_key[0], key_size))
        {
            ServerLogger::Log(logid, "Error reading eattr key from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
            return false;
        }

		metadataf_pos += key_size;
		data_checksum = urb_adler32(data_checksum, &eattr_key[0], key_size);

        if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, eattr_key.data(), eattr_key.size(), cb))
        {
            ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (eattr_key)", LL_ERROR);
            return false;
        }

        metadata_size+=eattr_key.size();

        unsigned int val_size;
		if (!readRetry(metadata_f, reinterpret_cast<char*>(&val_size), sizeof(val_size)))
        {
            ServerLogger::Log(logid, "Error reading eattr value size from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
            return false;
        }

		metadataf_pos += sizeof(val_size);
		data_checksum = urb_adler32(data_checksum, reinterpret_cast<char*>(&val_size), sizeof(val_size));

        if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&val_size), sizeof(val_size), cb))
        {
            ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (val_size)", LL_ERROR);
            return false;
        }

        metadata_size+=sizeof(val_size);
        val_size = little_endian(val_size);

		if (val_size == UINT_MAX)
		{
			continue;
		}

		if (val_size > 10 * 1024 * 1024)
		{
			ServerLogger::Log(logid, "Eattry value for eattr \""+eattr_key+"\" for \""+ (output_f!=NULL ? output_f->getFilename(): "NONE")+"\" too large with size "+PrettyPrintBytes(val_size)
				+" in \"" + metadata_f->getFilename() + "\"", LL_ERROR);
			return false;
		}

        std::string eattr_val;
        eattr_val.resize(val_size);

		if (!readRetry(metadata_f, &eattr_val[0], val_size))
        {
            ServerLogger::Log(logid, "Error reading eattr value from \"" + metadata_f->getFilename() + "\"", isComplete() ? LL_ERROR : LL_DEBUG);
            return false;
        }

		metadataf_pos += val_size;
		data_checksum = urb_adler32(data_checksum, &eattr_val[0], val_size);

        if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, eattr_val.data(), eattr_val.size(), cb))
        {
            ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (eattr_val)", LL_ERROR);
            return false;
        }

        metadata_size+=eattr_val.size();
    }

	unsigned int read_data_checksum =0;
	if (!readRetry(metadata_f, reinterpret_cast<char*>(&read_data_checksum), sizeof(read_data_checksum)))
	{
		ServerLogger::Log(logid, "Error saving metadata. Data checksum could not be read.", isComplete() ? LL_ERROR : LL_DEBUG);
		return false;
	}

	metadataf_pos += sizeof(read_data_checksum);
	if(little_endian(read_data_checksum)!=data_checksum)
	{
		ServerLogger::Log(logid, "Error saving metadata. Stat data checksum wrong.", LL_ERROR);
		return false;
	}

    if(!dry_run && output_f != NULL && !output_f->Seek(output_offset))
    {
        ServerLogger::Log(logid, "Error seeking to  \"" + convert(output_offset) + "\" -6 in output_f", LL_ERROR);
        return false;
    }

    unix_magic_and_size[0]=little_endian(metadata_size);

    if(!dry_run && output_f != NULL && !writeRepeatFreeSpace(output_f, reinterpret_cast<char*>(&unix_magic_and_size[0]), sizeof(unix_magic_and_size[0]), cb))
    {
        ServerLogger::Log(logid, "Error writing to  \"" + output_f->getFilename() + "\" (end,unix)", LL_ERROR);
        return false;
    }

    return true;
}

bool FileMetadataDownloadThread::getHasError()
{
	return has_error;
}

bool FileMetadataDownloadThread::getHasFatalError()
{
	return has_fatal_error;
}

FileMetadataDownloadThread::~FileMetadataDownloadThread()
{
	if(!dry_run && !metadata_tmp_fn.empty())
	{
		Server->deleteFile(metadata_tmp_fn);
	}
}

void FileMetadataDownloadThread::shutdown()
{
	fc->Shutdown();
}

void FileMetadataDownloadThread::addFolderItem(std::string path, const std::string& os_path, bool is_dir, int64 created, int64 modified, int64 accessed, int64 folder_items)
{
	std::vector<std::string> toks;
	Tokenize(path, toks, "/");

	std::string curr_path;
	for(size_t i=0;i<toks.size()-1;++i)
	{
		if(!curr_path.empty())
		{
			curr_path+="/";
		}
		curr_path+=toks[i];
		addSingleFileItem(curr_path);
	}
	
	if(is_dir)
	{
		if(folder_items==0)
		{
			if(!os_set_file_time(os_file_prefix(os_path), created, modified, accessed))
			{
				std::string err = os_last_error_str();
				int ftype = os_get_file_type(os_file_prefix(os_path));
				if (!(ftype & EFileType_Symlink))
				{
					ServerLogger::Log(logid, "Error setting file time of " + os_path+" . "+err, LL_WARNING);
				}
			}
			return;
		}

		for(size_t i=0;i<saved_folder_items.size();++i)
		{
			if(saved_folder_items[i].path==path)
			{
				if(saved_folder_items[i].counted_items==folder_items)
				{
					if(!os_set_file_time(os_file_prefix(os_path), created, modified, accessed))
					{
						ServerLogger::Log(logid, "Error setting file time of "+os_path+" . "+os_last_error_str(), LL_WARNING);
					}
					saved_folder_items.erase(saved_folder_items.begin()+i);
				}
				else
				{
					saved_folder_items[i].folder_items = folder_items;
					saved_folder_items[i].os_path = os_path;
					saved_folder_items[i].accessed = accessed;
					saved_folder_items[i].created = created;
					saved_folder_items[i].modified = modified;
				}
				return;
			}
		}
	}
	
}

bool FileMetadataDownloadThread::readRetry(IFile * metadata_f, char * buf, size_t bsize)
{
	while (true)
	{
		_u32 read = metadata_f->Read(buf, static_cast<_u32>(bsize));
		if (read != static_cast<_u32>(bsize))
		{
			bsize -= read;
			buf += read;
		}
		else
		{
			return true;
		}
		IScopedLock lock(mutex.get());
		if (is_finished)
		{
			break;
		}
		cond->wait(&lock, 60000);
	}

	return metadata_f->Read(buf, static_cast<_u32>(bsize)) == static_cast<_u32>(bsize);
}

void FileMetadataDownloadThread::setComplete()
{
	IScopedLock lock(mutex.get());
	is_complete = true;
}

void FileMetadataDownloadThread::setFinished()
{
	IScopedLock lock(mutex.get());
	is_finished = true;
	cond->notify_all();
}

void FileMetadataDownloadThread::copyForAnalysis(IFile* metadata_f)
{
	std::auto_ptr<IFile> tmp(Server->openTemporaryFile());

	if (copy_file(metadata_f, tmp.get()))
	{
		ServerLogger::Log(logid, "Copied metadata to " + tmp->getFilename() + " for error analysis", LL_INFO);
	}
}

void FileMetadataDownloadThread::addSingleFileItem( std::string dir_path )
{
	for(size_t i=0;i<saved_folder_items.size();++i)
	{
		if(saved_folder_items[i].path==dir_path)
		{
			++saved_folder_items[i].counted_items;

			if(saved_folder_items[i].counted_items==saved_folder_items[i].folder_items)
			{
				if(!os_set_file_time(os_file_prefix(saved_folder_items[i].os_path), saved_folder_items[i].created, saved_folder_items[i].modified, saved_folder_items[i].accessed))
				{
					ServerLogger::Log(logid, "Error setting file time of "+saved_folder_items[i].os_path + " . "+os_last_error_str(), LL_WARNING);
				}
				saved_folder_items.erase(saved_folder_items.begin()+i);
			}

			return;
		}
	}

	SFolderItem new_folder_item;
	new_folder_item.counted_items=1;
	new_folder_item.path = dir_path;
	saved_folder_items.push_back(new_folder_item);
}

bool FileMetadataDownloadThread::isDownloading()
{
	return fc->isDownloading();
}

bool FileMetadataDownloadThread::isComplete()
{
	IScopedLock lock(mutex.get());
	return is_complete;
}

bool FileMetadataDownloadThread::isFinished()
{
	IScopedLock lock(mutex.get());
	return is_finished;
}

void FileMetadataDownloadThread::forceStart()
{
	IScopedLock lock(mutex.get());
	force_start = true;
}

bool FileMetadataDownloadThread::getHasTimeoutError()
{
	return has_timeout_error;
}

bool FileMetadataDownloadThread::hasMetadataId( int64 id )
{
	if(id>max_metadata_id)
	{
		return false;
	}

	if(max_metadata_id-id>1500)
	{
		return true;
	}

	if(!std::binary_search(last_metadata_ids.begin(), last_metadata_ids.end(), id))
	{
		return false;
	}
	else
	{
		return true;
	}
}

int64 FileMetadataDownloadThread::getTransferredBytes()
{
	return fc->getTransferredBytes();
}

void FileMetadataDownloadThread::setProgressLogEnabled(bool b)
{
	if (b)
	{
		fc->setProgressLogCallback(orig_progress_log_callback);
	}
	else
	{
		fc->setProgressLogCallback(NULL);
	}
}

int check_metadata()
{
	std::string metadata_file = Server->getServerParameter("metadata_file");

	std::string dummy_server_token;
	FileMetadataDownloadThread metadata_thread(dummy_server_token, metadata_file,
		0, 0, true, std::string());

	FilePathCorrections corrections;
	size_t num_embedded_files = 0;
	return metadata_thread.applyMetadata(std::string(), std::string(), NULL, NULL, corrections, num_embedded_files, NULL)?0:1;
}

void FileMetadataDownloadThread::FileMetadataApplyThread::operator()()
{
	has_success = fmdlt->applyMetadata(backup_metadata_dir, backup_dir, cb, local_hash, filepath_corrections, num_embedded_files, &max_file_id);
}

} //namespace server

