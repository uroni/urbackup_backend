/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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
#include "LocalFullFileBackup.h"
#include "../urbackupcommon/filelist_utils.h"
#include "../fileservplugin/IFileServ.h"
#include "../urbackupcommon/chunk_hasher.h"
#include "../urbackupcommon/file_metadata.h"
#include "../common/data.h"
#include "../fileservplugin/IFileMetadataPipe.h"
#include "client.h"
#include "../Interface/BackupFileSystem.h"
#include <ctime>

bool LocalFullFileBackup::prepareBackuppath() {

	const std::time_t t = std::time(nullptr);
	char mbstr[100];
	if (!std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%d %H-%M.new", std::localtime(&t)))
		return false;

	std::string prefix = std::string(mbstr);

	log("Creating new subvolume for backup...", LL_INFO);
	if (!orig_backup_files->createSubvol(prefix))
	{
		log("Error creating new subvolume for backup. " + orig_backup_files->lastError(), LL_ERROR);
		return false;
	}

	prepareBackupFiles(prefix);

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

	unsigned int flags = flag_with_orig_path | flag_with_proper_symlinks;
	int sha_version = 528;
	int running_jobs = 2;

	CWData data;
	data.addChar(IndexThread::IndexThreadAction_StartFullFileBackup);
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

	/*std::string async_id;
	async_id.resize(16);
	Server->randomFill(&async_id[0], async_id.size());

	//for phash
	data.addString2(async_id);*/

	log("Indexing files to backup", LL_INFO);

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

	log("Indexing finished", LL_INFO);

	logIndexResult();

	if (!prepareBackuppath())
		return false;

	std::string filelistpath = IndexThread::getFileSrv()->getShareDir(backupgroup > 0 ?
		("urbackup/filelist_" + convert(backupgroup) + ".ub") :
		"urbackup/filelist.ub", "#I"+server_identity+"#");

	if (filelistpath.empty())
		return false;

	std::unique_ptr<IFile> curr_file_list(Server->openFile(filelistpath, MODE_READ));
	if (curr_file_list.get() == nullptr)
	{
		log("Error opening current file list. " + os_last_error_str(), LL_ERROR);
		return false;
	}

	std::vector<size_t> diffs;
	bool backup_with_components;
	total_bytes = getIncrementalSize(curr_file_list.get(), diffs, backup_with_components, true);

	std::string curr_orig_path;
	std::string orig_sep;
	std::string curr_path;
	std::string curr_os_path;

	log("Copying files...", LL_INFO);
	updateProgressPc(0, total_bytes, 0);

	std::unique_ptr<IFile> filelist_out(backup_files->openFile(getBackupInternalDataDir() + "\\filelist.ub", MODE_WRITE));

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

				int64 ctime = Server->getTimeMS();
				if (ctime - laststatsupdate > status_update_intervall)
				{
					updateProgress(ctime);
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
							log("Error creating directory \"" + curr_os_path + "\"", LL_ERROR);
							return false;
						}

						if (!backup_files->createDir(".hashes\\" + curr_os_path))
						{
							log("Error creating hash directory \".hashes\\" + curr_os_path + "\"", LL_ERROR);
							return false;
						}

						std::string sourcepath = IndexThread::getFileSrv()->getShareDir(curr_path, std::string());
						std::string metadatapath = ".hashes\\" + curr_os_path + "\\"+ metadata_dir_fn;

						std::unique_ptr<IFsFile> metadataf(backup_files->openFile(metadatapath, MODE_WRITE));

						if (metadataf.get() == nullptr)
						{
							log("Error opening metadata file \"" + metadatapath + "\"", LL_ERROR);
							return false;
						}

						if (!backupMetadata(metadataf.get(), sourcepath, &metadata))
						{
							log("Error backing up metadata of \"" + sourcepath + "\"", LL_ERROR);
							return false;
						}

						if (depth == 0)
						{
							referenceShadowcopy(cf.name, server_token, clientsubname);
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

						if (depth == 0)
						{
							unreferenceShadowcopy(curr_path, server_token, clientsubname, 0);
						}
					}
				}
				else
				{
					if (depth == 0)
					{
						referenceShadowcopy(cf.name, server_token, clientsubname);
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

					std::string sourcepath = IndexThread::getFileSrv()->getShareDir(curr_path+"/"+cf.name, std::string());
					std::string metadatapath = ".hashes\\" + curr_os_path + "\\" + cf.name;
					std::string targetpath = curr_os_path + "\\" + cf.name;

					if (metadatapath.find("btrfs.h") != std::string::npos)
						int abc = 5;

					std::unique_ptr<IFsFile> metadataf(backup_files->openFile(metadatapath, MODE_WRITE));

					if (metadataf.get() == nullptr)
					{
						log("Error opening metadata file at \"" + metadatapath + "\"", LL_ERROR);
						return false;
					}

					if (!backupMetadata(metadataf.get(), sourcepath, &metadata))
					{
						log("Error backing up file metadata of \"" + sourcepath + "\"", LL_ERROR);
						return false;
					}

					std::unique_ptr<IFsFile> sourcef(Server->openFile(sourcepath, MODE_READ_SEQUENTIAL_BACKUP));

					if (sourcef.get() == nullptr)
					{
						log("Error opening \"" + sourcepath + "\" for backup. " + os_last_error_str(), LL_ERROR);
						return false;
					}

					if (sourcepath.find("btrfs.c") != std::string::npos)
						int abct = 5;

					std::unique_ptr<IFsFile> destf(backup_files->openFile(targetpath, MODE_WRITE));
					FsExtentIterator extent_iterator(sourcef.get());

					bool b = build_chunk_hashs(sourcef.get(),
						metadataf.get(), nullptr, destf.get(), false,
						nullptr, nullptr, false, nullptr, &extent_iterator,
						std::pair<IFile*, int64>(), this);

					done_bytes += cf.size;

					if (!b)
					{
						log("Error backing up \"" + sourcepath + "\"", LL_ERROR);
						return false;
					}

					if (depth == 0)
					{
						unreferenceShadowcopy(cf.name, server_token, clientsubname, 0);
					}
				}

				writeFileItem(filelist_out.get(), cf);
			}
		}
	}

	filelist_out.reset();

	if (!backup_files->sync(std::string()))
	{
		log("Error syncing backup file system", LL_ERROR);
		return false;
	}

	if (!backup_files->renameToFinal())
	{
		log("Error renaming backup subvol to final location", LL_ERROR);
		return false;
	}

	return sync();
}

void LocalFullFileBackup::updateBchPc(int64 done, int64 total)
{
	file_done_bytes = done;

	int64 ctime = Server->getTimeMS();
	if (ctime - laststatsupdate > status_update_intervall)
	{
		updateProgress(ctime);
	}
}


