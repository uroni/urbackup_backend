#include "LocalFullFileBackup.h"
#include "../urbackupcommon/filelist_utils.h"
#include "../fileservplugin/IFileServ.h"
#include "../urbackupcommon/chunk_hasher.h"
#include "../urbackupcommon/file_metadata.h"
#include "../fileservplugin/IFileMetadataPipe.h"
#include "client.h"
#include "../btrfs/btrfsplugin/IBackupFileSystem.h"


inline bool LocalFullFileBackup::prepareBackuppath() {
	backup_files->createDir(".hashes");
	backup_files->createDir(getBackupInternalDataDir());

	return (backup_files->getFileType(getBackupInternalDataDir()) & EFileType_Directory)>0;
}

bool LocalFullFileBackup::run()
{
	std::string server_token;
	server_token.resize(16);
	Server->secureRandomFill(&server_token[0], server_token.size());
	server_token = bytesToHex(server_token);

	unsigned int flags = flag_with_proper_symlinks | flag_calc_checksums;
	int sha_version = 528;
	int running_jobs = 2;

	CWData data;
	data.addChar(IndexThread::IndexThreadAction_StartIncrFileBackup);
	unsigned int curr_result_id = IndexThread::getResultId();
	data.addUInt(curr_result_id);
	data.addString(server_token);
	data.addInt(backupgroup);
	data.addInt(flags);
	data.addString(clientsubname);
	data.addInt(sha_version);
	data.addInt(running_jobs);
	data.addInt(facet_id);
	data.addChar(1);

	std::string async_id;
	async_id.resize(16);
	Server->randomFill(&async_id[0], async_id.size());

	//for phash
	data.addString2(async_id);

	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	IndexThread::refResult(curr_result_id);

	bool with_phash = false;
	while (true)
	{
		std::string msg;
		bool has_result = IndexThread::getResult(curr_result_id, 0, msg);

		if (has_result
			&& !msg.empty())
		{
			if (msg == "phash")
			{
				with_phash = true;
			}
			else
			{
				IndexThread::removeResult(curr_result_id);

				if (msg != "done")
				{
					Server->Log("Indexing failed with \"" + msg + "\"", LL_ERROR);
					return false;
				}
				break;
			}
		}
		else
		{
			Server->wait(1000);
		}
	}

	file_metadata_pipe.reset(IndexThread::getFileSrv()->getFileMetadataPipe());

	if (!prepareBackuppath())
		return false;

	std::string filelistpath = IndexThread::getFileSrv()->getShareDir(backupgroup > 0 ? ("urbackup/filelist_" + convert(backupgroup) + ".ub") : "urbackup/filelist.ub", "#I"+server_identity+"#");

	if (filelistpath.empty())
		return false;

	std::auto_ptr<IFile> curr_file_list(Server->openFile(filelistpath, MODE_READ));
	if (curr_file_list.get() == NULL)
		return false;

	std::vector<size_t> diffs;
	bool backup_with_components;
	_i64 files_size = getIncrementalSize(curr_file_list.get(), diffs, backup_with_components, true);

	std::string curr_orig_path;
	std::string orig_sep;
	std::string curr_path;
	std::string curr_os_path;

	std::auto_ptr<IFile> filelist_out(backup_files->openFile(getBackupInternalDataDir() + "\\filelist.ub", MODE_WRITE));

	int depth = 0;
	FileListParser list_parser;
	char buffer[4096];
	bool has_read_error = false;
	_u32 read;
	SFile cf;
	curr_file_list->Seek(0);
	while ((read = curr_file_list->Read(buffer, 4096, &has_read_error)) > 0)
	{
		if (has_read_error)
		{
			return false;
		}

		for (size_t i = 0; i < read; ++i)
		{
			std::map<std::string, std::string> extra_params;
			bool b = list_parser.nextEntry(buffer[i], cf, &extra_params);
			if (b)
			{
				FileMetadata metadata;
				metadata.read(extra_params);

				bool has_orig_path = metadata.has_orig_path;
				if (has_orig_path)
				{
					curr_orig_path = metadata.orig_path;
					str_map::iterator it_orig_sep = extra_params.find("orig_sep");
					if (it_orig_sep != extra_params.end())
					{
						orig_sep = it_orig_sep->second;
					}
					if (orig_sep.empty()) orig_sep = "\\";
				}

				if (cf.isdir)
				{
					if (cf.name != "..")
					{
						if (!curr_path.empty())
							curr_path += "/";

						curr_path += cf.name;

						if (!curr_os_path.empty())
							curr_os_path += "\\";

						curr_os_path += cf.name;

						if (!backup_files->createDir(curr_os_path))
						{
							//Log
							return false;
						}

						if (!backup_files->createDir(".hashes\\" + curr_os_path))
						{
							//Log
							return false;
						}

						if (depth == 0)
						{
							//server_download->addToQueueStartShadowcopy(cf.name);
						}
						++depth;
					}
					else
					{
						curr_path = ExtractFilePath(curr_path, "/");
						curr_os_path = ExtractFilePath(curr_os_path, "\\");

						if (!has_orig_path)
						{
							curr_orig_path = ExtractFilePath(curr_orig_path, orig_sep);
						}

						--depth;
					}
				}
				else
				{
					if (depth == 0)
					{
						//server_download->addToQueueStartShadowcopy(cf.name);
					}

					if (!has_orig_path)
					{
						if (curr_orig_path != orig_sep)
						{
							metadata.orig_path = curr_orig_path + orig_sep + cf.name;
						}
						else
						{
							metadata.orig_path = orig_sep + cf.name;
						}
					}

					std::string sourcepath = IndexThread::getFileSrv()->getShareDir(curr_path, std::string());
					std::string metadatapath = ".hashes\\" + curr_os_path + "\\" + cf.name;
					std::string targetpath = curr_os_path + "\\" + cf.name;

					std::auto_ptr<IFsFile> metadataf(backup_files->openFile(metadatapath, MODE_WRITE));

					if (metadataf.get() == NULL)
					{
						//Log
						return false;
					}

					SFile file_meta = getFileMetadata(os_file_prefix(sourcepath));
					if (file_meta.name.empty())
					{
						Server->Log("Error getting metadata (created and last modified time) of " + sourcepath, LL_ERROR);
						return false;
					}
					metadata.accessed = file_meta.accessed;
					metadata.created = file_meta.created;
					metadata.last_modified = file_meta.last_modified;

					int64 truncate_to_bytes;
					if (!write_file_metadata(metadataf.get(), NULL, metadata, true, truncate_to_bytes))
					{
						//Log
						return false;
					}
					else
					{
						if (!metadataf->Resize(truncate_to_bytes))
						{
							//Log
							return false;
						}
					}

					int64 osm_offset = os_metadata_offset(metadataf.get());

					if (osm_offset == -1)
					{
						Server->Log("Error saving metadata. Metadata offset cannot be calculated at \"" + metadatapath + "\"", LL_ERROR);
						return false;
					}

					if (!metadataf->Seek(osm_offset))
					{
						Server->Log("Error saving metadata. Could not seek to end of file \"" + metadatapath + "\"", LL_ERROR);
						return false;
					}

					if (!writeOsMetadata(sourcepath, osm_offset, metadataf.get()))
					{
						//Log
						return false;
					}

					std::auto_ptr<IFile> sourcef(Server->openFile(sourcepath, MODE_READ_SEQUENTIAL_BACKUP));

					if (sourcef.get() == NULL)
					{
						//Log
						return false;
					}

					std::auto_ptr<IFsFile> destf(backup_files->openFile(targetpath, MODE_WRITE));

					bool b = build_chunk_hashs(sourcef.get(),
						metadataf.get(), NULL, destf.get(), false);

					if (!b)
					{
						//Log
						return false;
					}
				}

				writeFileItem(filelist_out.get(), cf);
			}
		}
	}

	return true;
}

namespace
{
	const int64 win32_meta_magic = little_endian(0x320FAB3D119DCB4AULL);
	const int64 unix_meta_magic = little_endian(0xFE4378A3467647F0ULL);
}

bool LocalFullFileBackup::writeOsMetadata(const std::string& sourcefn, int64 dest_start_offset, IFile* dest)
{
	int64 win32_magic_and_size[2];
	win32_magic_and_size[1] = win32_meta_magic;
	win32_magic_and_size[0] = 0;

	if (dest->Write(reinterpret_cast<char*>(win32_magic_and_size), sizeof(win32_magic_and_size)) != sizeof(win32_magic_and_size))
	{
		//Log
		return false;
	}

	if (!file_metadata_pipe->openOsMetadataFile(sourcefn))
	{
		//Log
		return false;
	}

	char buf[4096];

	size_t read;
	while (file_metadata_pipe->readCurrOsMetadata(buf, sizeof(buf), read))
	{
		if (dest->Write(buf, read) != read)
		{
			//Log
			return false;
		}

		win32_magic_and_size[0] += read;
	}

	if (!dest->Seek(dest_start_offset))
	{
		//Log
		return false;
	}

	if (dest->Write(reinterpret_cast<char*>(win32_magic_and_size), sizeof(win32_magic_and_size[0])) != sizeof(win32_magic_and_size[0]))
	{
		//Log
		return false;
	}

	return true;
}
