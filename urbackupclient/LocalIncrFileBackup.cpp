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
#include "LocalIncrFileBackup.h"
#include <ctime>
#include <stack>
#include "../stringtools.h"
#include "../common/data.h"
#include "client.h"
#include "../urbackupserver/treediff/TreeDiff.h"
#include "../urbackupcommon/chunk_hasher.h"

bool LocalIncrFileBackup::prepareBackuppath()
{
	const std::time_t t = std::time(nullptr);
	char mbstr[100];
	if (!std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%d %H-%M.new", std::localtime(&t)))
		return false;

	std::string prefix = std::string(mbstr);

	std::vector<SFile> existing_backups = orig_backup_files->listFiles("");

	if (existing_backups.empty())
		return false;

	for (std::vector<SFile>::reverse_iterator it = existing_backups.rbegin();it!=existing_backups.rend();++it)
	{
		if (it->name.find(".new") == std::string::npos)
		{
			last_backuppath = it->name;
			break;
		}
	}

	if (last_backuppath.empty())
		return false;

	log("Creating snapshot for backup...", LL_INFO);

	if (!orig_backup_files->createSnapshot(last_backuppath, prefix))
	{
		log("Creating snapshot failed", LL_ERROR);
		return false;
	}

	prepareBackupFiles(prefix);

	backup_files->createDir(".hashes");
	backup_files->createDir(getBackupInternalDataDir());

	return (backup_files->getFileType(getBackupInternalDataDir()) & EFileType_Directory) > 0;
}

bool LocalIncrFileBackup::run()
{
	std::string server_token;
	server_token.resize(16);
	Server->secureRandomFill(&server_token[0], server_token.size());
	server_token = bytesToHex(server_token);

	unsigned int flags = flag_with_orig_path | flag_with_proper_symlinks;
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

	file_metadata_pipe.reset(IndexThread::getFileSrv()->getFileMetadataPipe());

	if (!prepareBackuppath())
		return false;

	std::string filelistpath = IndexThread::getFileSrv()->getShareDir(backupgroup > 0 ? ("urbackup/filelist_" + convert(backupgroup) + ".ub") : "urbackup/filelist.ub", "#I" + server_identity + "#");

	if (filelistpath.empty())
	{
		log("Error getting filelistpath", LL_ERROR);
		return false;
	}

	std::unique_ptr<IFile> curr_file_list(Server->openFile(filelistpath, MODE_READ));
	if (!curr_file_list)
	{
		log("Error opening current file list at \"" + filelistpath + "\". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	std::unique_ptr<IFile> last_file_list(backup_files->root()->openFile(last_backuppath + "\\" + getBackupInternalDataDir() + "\\filelist.ub", MODE_READ));

	if (last_file_list.get() == nullptr)
	{
		log("Error opening last file list at \"" + last_backuppath + "\\" + getBackupInternalDataDir() + "\\filelist.ub\"", LL_ERROR);
		return false;
	}

	bool has_symbit = true;
	bool is_windows = false;
#ifdef _WIN32
	is_windows = true;
#endif
	bool diff_error = false;
	std::vector<size_t> deleted_ids;
	std::vector<size_t> modified_inplace_ids;
	std::vector<size_t> dir_diffs;
	std::vector<size_t> diffs = TreeDiff::diffTrees(last_file_list.get(), curr_file_list.get(),
		diff_error, &deleted_ids, nullptr, &modified_inplace_ids,
		dir_diffs, nullptr, has_symbit, is_windows);

	if (diff_error)
	{
		log("Error diffing file trees", LL_ERROR);
		return false;
	}

	bool backup_with_components;
	total_bytes = getIncrementalSize(curr_file_list.get(), diffs, backup_with_components, true);

	if (!deleteFilesInSnapshot(last_file_list.get(), deleted_ids, "", false, false))
	{
		log("Error deleting files in snapshot", LL_ERROR);
		return false;
	}

	if (!deleteFilesInSnapshot(last_file_list.get(), deleted_ids, ".hashes", false, false))
	{
		log("Error deleting file metdata in snapshot", LL_ERROR);
		return false;
	}

	log("Copying files...", LL_INFO);
	updateProgressPc(0, total_bytes, 0);

	std::unique_ptr<IFile> filelist_out(backup_files->openFile(getBackupInternalDataDir() + "\\filelist.ub", MODE_WRITE));

	const std::string last_backuppath_hashes = last_backuppath + "\\.hashes\\";
	bool c_has_error = false;
	int depth = 0;
	FileListParser list_parser;
	std::vector<char> buffer(512 * 1024);
	bool has_read_error = false;
	_i64 filelist_currpos = 0;
	_u32 read;
	SFile cf;
	curr_file_list->Seek(0);
	std::string curr_path;
	std::string curr_os_path;
	std::string curr_hash_path;
	std::string curr_orig_path;
	std::string orig_sep;
	bool indirchange = false;
	std::vector<size_t> folder_items;
	folder_items.push_back(0);
	std::stack<int64> dir_ids;
	std::map<int64, int64> dir_end_ids;
	std::stack<bool> dir_diff_stack;
	bool script_dir = false;
	int changelevel;
	size_t line = 0;
	while ((read = curr_file_list->Read(buffer.data(), buffer.size(), &has_read_error)) > 0)
	{
		if (has_read_error)
		{
			break;
		}

		filelist_currpos += read;

		for (size_t i = 0; i < read; ++i)
		{
			std::map<std::string, std::string> extra_params;
			bool b = list_parser.nextEntry(buffer[i], cf, &extra_params);
			if (b)
			{
				std::string osspecific_name;

				if (!cf.isdir || cf.name != "..")
				{
					osspecific_name = fixFilenameForOS(cf.name);
				}

				int64 ctime = Server->getTimeMS();
				if (ctime - laststatsupdate > status_update_intervall)
				{
					updateProgress(ctime);
				}

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
					if (!indirchange && hasChange(line, diffs))
					{
						indirchange = true;
						changelevel = depth;

						if (cf.name == "..")
						{
							--changelevel;
						}
					}

					if (cf.name != "..")
					{
						bool dir_diff = false;
						if (!indirchange)
						{
							dir_diff = hasChange(line, dir_diffs);
						}

						dir_diff_stack.push(dir_diff);

						if (indirchange || dir_diff)
						{
							for (size_t j = 0; j < folder_items.size(); ++j)
							{
								++folder_items[j];
							}
						}

						std::string orig_curr_path = curr_path;
						std::string orig_curr_os_path = curr_os_path;
						curr_path += "/" + cf.name;
						curr_os_path += "\\" + osspecific_name;
						std::string local_curr_os_path = curr_os_path;

						if (!has_orig_path)
						{
							if (curr_orig_path != orig_sep)
							{
								curr_orig_path += orig_sep;
							}

							curr_orig_path += cf.name;
							metadata.orig_path = curr_orig_path;
							metadata.exist = true;
							metadata.has_orig_path = true;
						}

						std::string metadata_fn = ".hashes\\" + local_curr_os_path + "\\" + metadata_dir_fn;

						bool dir_linked = false;
						if (!dir_linked && (indirchange || dir_diff))
						{
							bool dir_already_exists = dir_diff;
							str_map::iterator sym_target = extra_params.find("sym_target");

							bool symlinked_file = false;

							std::string metadata_srcpath = last_backuppath +"\\.hashes\\" + local_curr_os_path + os_file_sep() + metadata_dir_fn;

							if (sym_target != extra_params.end())
							{
								if (dir_already_exists)
								{
									bool prev_is_symlink = (backup_files->getFileType(local_curr_os_path) & EFileType_Symlink) > 0;

									if (prev_is_symlink)
									{
										if (!backup_files->deleteFile(local_curr_os_path))
										{
											log("Could not remove symbolic link at \"" + backuppath + local_curr_os_path + "\" " +
												backup_files->lastError(), LL_ERROR);
											c_has_error = true;
											break;
										}

										metadata_srcpath = last_backuppath_hashes + orig_curr_os_path + "\\" + escape_metadata_fn(osspecific_name);
									}
									else
									{
										//Directory to directory symlink
										if (!backup_files->deleteFile(local_curr_os_path))
										{
											log("Could not remove directory at \"" + backuppath + local_curr_os_path + "\" " +
												backup_files->lastError(), LL_ERROR);
											c_has_error = true;
											break;
										}

										if (!backup_files->deleteFile(metadata_fn))
										{
											log("Error deleting metadata file \"" + metadata_fn + "\". " + backup_files->lastError(), LL_WARNING);
										}
									}
								}
								else
								{
									metadata_srcpath = last_backuppath_hashes + orig_curr_os_path + "\\" + escape_metadata_fn(osspecific_name);
								}

								if (!createSymlink(backuppath + local_curr_os_path, depth, sym_target->second, orig_sep, true))
								{
									log("Creating symlink at \"" + backuppath + local_curr_os_path + "\" to \"" + sym_target->second + "\" failed. "
										+ backup_files->lastError(), LL_ERROR);
									c_has_error = true;
									break;
								}

								metadata_fn = ".hashes\\" + orig_curr_os_path + "\\" + escape_metadata_fn(osspecific_name);

								symlinked_file = true;
							}
							else if (!dir_already_exists)
							{
								if (!backup_files->createDir( local_curr_os_path))
								{
									std::string errstr = backup_files->lastError();
									if (!backup_files->directoryExists(local_curr_os_path))
									{
										log("Creating directory  \"" + backuppath + local_curr_os_path + "\" failed. - " + errstr, LL_ERROR);
										c_has_error = true;
										break;
									}
									else
									{
										log("Directory \"" + backuppath + local_curr_os_path + "\" does already exist.", LL_WARNING);
									}
								}
							}

							if (!dir_already_exists && !symlinked_file)
							{
								if (!backup_files->directoryExists(".hashes\\" + local_curr_os_path))
								{
									std::string errstr = backup_files->lastError();
									if (!backup_files->directoryExists(".hashes\\" + local_curr_os_path))
									{
										log("Creating directory  \".hashes\\" + local_curr_os_path + "\" failed. - " + errstr, LL_ERROR);
										c_has_error = true;
										break;
									}
									else
									{
										log("Directory  \".hashes\\" + local_curr_os_path + "\" does already exist. - " + errstr, LL_WARNING);
									}
								}
							}

							if (dir_already_exists)
							{
								if (!backup_files->deleteFile(metadata_fn))
								{
									if (sym_target == extra_params.end())
									{
										if (backup_files->getFileType(local_curr_os_path) & EFileType_Symlink)
										{
											//Directory symlink to directory
											if (!backup_files->deleteFile(local_curr_os_path))
											{
												log("Could not remove symbolic link at \"" + backuppath + local_curr_os_path + "\" (2). "
													+ backup_files->lastError(), LL_ERROR);
												c_has_error = true;
												break;
											}

											if (!backup_files->createDir(backuppath + local_curr_os_path))
											{
												if (!backup_files->directoryExists(backuppath + local_curr_os_path))
												{
													log("Creating directory  \"" + backuppath + local_curr_os_path + "\" failed. - " +
														backup_files->lastError(), LL_ERROR);
													c_has_error = true;
													break;
												}
												else
												{
													log("Directory \"" + backuppath + local_curr_os_path + "\" does already exist.", LL_WARNING);
												}
											}

											std::string metadata_fn_curr = ".hashes\\"+ orig_curr_os_path + "\\" + escape_metadata_fn(cf.name);
											if (!backup_files->deleteFile(metadata_fn_curr))
											{
												log("Error deleting metadata file \"" + metadata_fn_curr + "\". " + backup_files->lastError(), LL_WARNING);
											}
											if (!backup_files->createDir(".hashes\\" + local_curr_os_path))
											{
												log("Error creating metadata directory \".hashes\\"  + local_curr_os_path + "\". " + 
													backup_files->lastError(), LL_WARNING);
											}

											metadata_srcpath = last_backuppath_hashes + orig_curr_os_path + "\\" + escape_metadata_fn(cf.name);
										}
									}
									else
									{
										if (!backup_files->deleteFile(metadata_fn))
										{
											log("Error deleting metadata directory \"" + metadata_fn + "\". " + backup_files->lastError(), LL_WARNING);
										}
									}
								}
							}

							if (depth == 0 && curr_path == "/urbackup_backup_scripts")
							{
								metadata.file_permissions = permissionsAllowAll();
								curr_orig_path = local_curr_os_path;
								metadata.orig_path = curr_orig_path;
							}

							if (!dir_diff && !indirchange && curr_path != "/urbackup_backup_scripts")
							{
								if (!backup_files->root()->reflinkFile(metadata_srcpath, backup_files->getPrefix()+metadata_fn))
								{
									if (!backup_files->copyFile(metadata_srcpath, metadata_fn, false, nullptr))
									{
										log("Cannot copy directory metadata from \"" + metadata_srcpath + 
											"\" to \"" + metadata_fn + "\". - " + backup_files->lastError(), LL_ERROR);
										/*if (client_main->handle_not_enough_space(metadata_fn))
										{
											if (!copy_file(metadata_srcpath, metadata_fn))
											{
												ServerLogger::Log(logid, "Cannot copy directory metadata from \"" + metadata_srcpath + "\" to \"" + metadata_fn + "\". - " + systemErrorInfo(), LL_ERROR);
											}
										}
										else
										{
											ServerLogger::Log(logid, "Cannot copy directory metadata from \"" + metadata_srcpath + "\" to \"" + metadata_fn + "\". - " + systemErrorInfo(), LL_ERROR);
										}*/
									}
								}
							}
							else 
							{
								std::unique_ptr<IFsFile> metadata_f(backup_files->openFile(metadata_fn, MODE_RW_CREATE));
								if (metadata_f.get() == nullptr)
								{
									log("Cannot open directory metadata file at \"" + metadata_fn + "\". " +
										backup_files->lastError(), LL_ERROR);
									c_has_error = true;
									break;
								}
								int64 truncate_to_bytes;

								if (!write_file_metadata(metadata_f.get(), nullptr, metadata, false, truncate_to_bytes))
								{
									log("Writing directory metadata to \"" + metadata_fn + "\" failed.", LL_ERROR);
									c_has_error = true;
									break;
								}
								else
								{
									metadata_f->Resize(truncate_to_bytes);
								}
							}
						}

						folder_items.push_back(0);
						dir_ids.push(line);

						++depth;
						if (depth == 1)
						{
							std::string t = curr_path;
							t.erase(0, 1);
							if (t == "urbackup_backup_scripts")
							{
								script_dir = true;
							}
							else
							{
								referenceShadowcopy(t, server_token, clientsubname);
							}
						}
					}
					else //cf.name==".."
					{
						if ((indirchange || dir_diff_stack.top()) && !script_dir)
						{
							std::string metadata_fn = ".hashes\\" + curr_os_path + "\\" + metadata_dir_fn;

							std::unique_ptr<IFsFile> metadata_f(backup_files->openFile(metadata_fn, MODE_RW_CREATE));
							if (metadata_f.get() == nullptr)
							{
								log("Cannot open file metdata file at \"" + metadata_fn + "\". " +
									backup_files->lastError(), LL_ERROR);
								c_has_error = true;
								break;
							}

							std::string sourcepath = IndexThread::getFileSrv()->getShareDir(curr_path, std::string());

							if (!backupMetadata(metadata_f.get(), sourcepath, &metadata))
							{
								log("Error backing up file metadata from \"" + sourcepath + "\".", LL_ERROR);
								c_has_error = true;
								break;
							}

							dir_end_ids[dir_ids.top()] = line;
						}

						folder_items.pop_back();
						dir_diff_stack.pop();
						dir_ids.pop();

						--depth;
						if (indirchange == true && depth == changelevel)
						{
							indirchange = false;
						}
						if (depth == 0)
						{
							std::string t = curr_path;
							t.erase(0, 1);
							if (t == "urbackup_backup_scripts")
							{
								script_dir = false;
							}
							else
							{
								unreferenceShadowcopy(t, server_token, clientsubname, 0);
							}
						}
						curr_path = ExtractFilePath(curr_path, "/");
						curr_os_path = ExtractFilePath(curr_os_path, "\\");

						if (!has_orig_path)
						{
							curr_orig_path = ExtractFilePath(curr_orig_path, orig_sep);
						}
					}
				}
				else //is file
				{
					std::string local_curr_os_path = curr_os_path + "\\" + osspecific_name;
					std::string srcpath = last_backuppath + local_curr_os_path;

					std::string sourcepath = IndexThread::getFileSrv()->getShareDir(curr_path + "/" + cf.name, std::string());
					std::string metadatapath = ".hashes\\" + curr_os_path + "\\" + cf.name;
					std::string targetpath = curr_os_path + "\\" + cf.name;

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

					bool copy_curr_file_entry = false;
					bool readd_curr_file_entry_sparse = false;
					std::string curr_sha2;
					/*
					{
						std::map<std::string, std::string>::iterator hash_it =
							((local_hash.get() == NULL) ? extra_params.end() : extra_params.find(sha_def_identifier));

						if (local_hash.get() != NULL && hash_it == extra_params.end())
						{
							hash_it = extra_params.find("thash");
						}

						if (hash_it != extra_params.end())
						{
							curr_sha2 = base64_decode_dash(hash_it->second);
						}

						if (curr_sha2.empty()
							&& phash_load.get() != NULL
							&& !script_dir
							&& extra_params.find("sym_target") == extra_params.end()
							&& extra_params.find("special") == extra_params.end()
							&& !phash_load_offline
							&& extra_params.find("no_hash") == extra_params.end()
							&& cf.size >= link_file_min_size)
						{
							if (!phash_load->getHash(line, curr_sha2))
							{
								ServerLogger::Log(logid, "Error getting parallel hash for file \"" + cf.name + "\" line " + convert(line) + " (2)", LL_ERROR);
								r_offline = true;
								server_download->queueSkip();
								phash_load_offline = true;
							}
							else
							{
								metadata.shahash = curr_sha2;
							}
						}
					}*/

					bool download_metadata = false;
					bool write_file_metadata = false;

					bool file_changed = hasChange(line, diffs);

					str_map::iterator sym_target = extra_params.find("sym_target");
					if (sym_target != extra_params.end() && (indirchange || file_changed))
					{
						std::string symlink_path = local_curr_os_path;

						if (!createSymlink(symlink_path, depth, sym_target->second, (orig_sep), false))
						{
							log("Creating symlink at \"" + symlink_path + "\" to \"" + sym_target->second 
								+ "\" failed. " + backup_files->lastError(), LL_ERROR);
							c_has_error = true;
							break;
						}
						else
						{
							download_metadata = true;
							write_file_metadata = true;
						}
					}
					else if (extra_params.find("special") != extra_params.end() && (indirchange || file_changed))
					{
						std::string touch_path = local_curr_os_path;
						std::unique_ptr<IFile> touch_file(backup_files->openFile(touch_path, MODE_WRITE));
						if (touch_file.get() == nullptr)
						{
							log("Error touching file at \"" + touch_path + "\". " + backup_files->lastError(), LL_ERROR);
							c_has_error = true;
							break;
						}
						else
						{
							download_metadata = true;
							write_file_metadata = true;
						}
					}
					else if (indirchange || file_changed) //is changed
					{
						bool f_ok = false;
						/*if (!curr_sha2.empty() && cf.size >= link_file_min_size)
						{
							if (link_file(cf.name, osspecific_name, curr_path, curr_os_path, curr_sha2, cf.size, true,
								metadata))
							{
								f_ok = true;
								linked_bytes += cf.size;
								download_metadata = true;
							}
						}*/

						if (!f_ok)
						{
							for (size_t j = 0; j < folder_items.size(); ++j)
							{
								++folder_items[j];
							}

							std::unique_ptr<IFsFile> metadata_f(backup_files->openFile(metadatapath, MODE_RW_CREATE));
							if (metadata_f.get() == nullptr)
							{
								log("Error opening metadata file at \"" + metadatapath + "\". " +
									backup_files->lastError(), LL_ERROR);
								c_has_error = true;
								break;
							}

							std::string last_metadatapath = last_backuppath + "\\.hashes\\" + curr_os_path + "\\" + cf.name;

							std::unique_ptr<IFsFile> last_metadataf(backup_files->openFile(last_metadatapath, MODE_RW_CREATE));
							if (metadata_f.get() == nullptr)
							{
								log("Error opening last medata file at \"" + last_metadatapath + "\". " +
									backup_files->lastError(), LL_ERROR);
								c_has_error = true;
								break;
							}

							std::unique_ptr<IFsFile> sourcef(Server->openFile(os_file_prefix(sourcepath), MODE_READ_SEQUENTIAL_BACKUP));

							if (!sourcef)
							{
								log("Error opening backup source file at \"" + sourcepath +
									"\". " + os_last_error_str(), LL_ERROR);
								c_has_error = true;
								break;
							}

							std::unique_ptr<IFsFile> destf(backup_files->openFile(targetpath, MODE_WRITE));

							if (!destf)
							{
								log("Error opening backup dest file at \"" + targetpath + "\". " +
									backup_files->lastError(), LL_ERROR);
								c_has_error = true;
								break;
							}

							int64 inplace_written = 0;
							FsExtentIterator extent_iterator(sourcef.get());
							bool b = build_chunk_hashs(sourcef.get(),
								metadata_f.get(), nullptr, destf.get(), true,
								&inplace_written, last_metadataf.get(), false,
								nullptr, &extent_iterator, 
								std::pair<IFile*, int64>(), this);

							done_bytes += cf.size;

							if (!b)
							{
								log("Error backing up \"" + sourcepath + "\"", LL_ERROR);
								c_has_error = true;
								break;
							}
							else if (sourcef->Size() != destf->Size())
							{
								if (!destf->Resize(sourcef->Size()))
								{
									log("Error resizing dest file to source size", LL_ERROR);
									c_has_error = true;
									break;
								}
							}
						}
					}
					else
					{
						//copy_curr_file_entry = copy_last_file_entries;
						//readd_curr_file_entry_sparse = readd_file_entries_sparse;
					}

					/*if (copy_curr_file_entry)
					{
						ServerFilesDao::SFileEntry fileEntry = filesdao->getFileEntryFromTemporaryTable(srcpath);

						if (fileEntry.exists)
						{
							addFileEntrySQLWithExisting(backuppath + local_curr_os_path, backuppath_hashes + local_curr_os_path,
								fileEntry.shahash, fileEntry.filesize, fileEntry.filesize, incremental_num);
							++num_copied_file_entries;

							readd_curr_file_entry_sparse = false;
						}
					}

					if (readd_curr_file_entry_sparse)
					{
						addSparseFileEntry(curr_path, cf, copy_file_entries_sparse_modulo, incremental_num,
							local_curr_os_path, num_readded_entries);
					}*/

					
					for (size_t j = 0; j < folder_items.size(); ++j)
					{
						++folder_items[j];
					}

					if (download_metadata)
					{
						std::unique_ptr<IFsFile> metadata_f(backup_files->openFile(metadatapath, MODE_RW_CREATE));
						if (!metadata_f)
						{
							log("Error opening backup metadata file \"" + metadatapath + "\". " + 
								backup_files->lastError(), LL_ERROR);
							c_has_error = true;
							break;
						}

						if (!backupMetadata(metadata_f.get(), sourcepath, &metadata))
						{
							log("Error backing up file metadata of file \"" + metadatapath + "\"", LL_ERROR);
							c_has_error = true;
							break;
						}
					}

					if (depth == 0)
					{
						unreferenceShadowcopy(cf.name, server_token, clientsubname, 0);
					}
				}

				if(!c_has_error)
					writeFileItem(filelist_out.get(), cf);

				++line;
			}
		}

		if (c_has_error)
			break;

		if (read < buffer.size())
			break;
	}

	if (has_read_error)
	{
		log("Error reading from file " + curr_file_list->getFilename() + ". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	filelist_out.reset();

	if (c_has_error)
	{
		log("Error during backup (see previous messages)", LL_ERROR);
		return false;
	}

	if (!backup_files->sync(std::string()))
	{
		log("Error syncing backup subvol", LL_ERROR);
		return false;
	}

	if (!backup_files->renameToFinal())
	{
		log("Error renaming backup subvol to final location", LL_ERROR);
		return false;
	}

	return sync();
}

bool LocalIncrFileBackup::deleteFilesInSnapshot(IFile* curr_file_list, const std::vector<size_t>& deleted_ids,
	std::string snapshot_path, bool no_error, bool hash_dir)
{
	FileListParser list_parser;

	std::vector<char> buffer(512 * 1024);
	size_t read;
	SFile curr_file;
	size_t line = 0;
	std::string curr_path = snapshot_path;
	std::string curr_os_path = snapshot_path;
	bool curr_dir_exists = true;

	curr_file_list->Seek(0);

	while ((read = curr_file_list->Read(buffer.data(), buffer.size())) > 0)
	{
		for (size_t i = 0; i < read; ++i)
		{
			if (list_parser.nextEntry(buffer[i], curr_file, nullptr))
			{
				if (curr_file.isdir && curr_file.name == "..")
				{
					curr_path = ExtractFilePath(curr_path, "\\");
					curr_os_path = ExtractFilePath(curr_os_path, "\\");
					if (!curr_dir_exists)
					{
						curr_dir_exists = backup_files->directoryExists(curr_path);
					}
				}

				std::string osspecific_name;

				if (!curr_file.isdir || curr_file.name != "..")
				{
					std::string cname = hash_dir ? escape_metadata_fn(curr_file.name) : curr_file.name;
					osspecific_name = fixFilenameForOS(cname);
				}

				if (hasChange(line, deleted_ids)
					&& (!curr_file.isdir && curr_dir_exists && curr_path == snapshot_path + "\\urbackup_backup_scripts")
						)
				{
					std::string curr_fn = curr_os_path + "\\" + osspecific_name;
					if (curr_file.isdir)
					{
						if (curr_dir_exists)
						{
							//In the hash snapshot a symlinked directory is represented by a file
							if (hash_dir && (backup_files->getFileType(curr_fn) & EFileType_File))
							{
								if (!backup_files->deleteFile(curr_fn))
								{
									log("Could not remove file \"" + curr_fn + "\" in ::deleteFilesInSnapshot - " 
										+ os_last_error_str(), no_error ? LL_WARNING : LL_ERROR);

									if (!no_error)
									{
										return false;
									}
								}
							}
							else if (!backup_files->removeDirRecursive(curr_fn)
								|| backup_files->directoryExists(curr_fn))
							{
								log("Could not remove directory \"" + curr_fn + "\" in ::deleteFilesInSnapshot - " 
									+ os_last_error_str(), no_error ? LL_WARNING : LL_ERROR);

								if (!no_error)
								{
									return false;
								}
							}
						}
						curr_path += "\\" + curr_file.name;
						curr_os_path += "\\"+ osspecific_name;
						curr_dir_exists = false;
					}
					else
					{
						if (curr_dir_exists)
						{
							int ftype = EFileType_File;
							bool keep_inplace = false;
							if (curr_path == snapshot_path + "\\urbackup_backup_scripts")
							{
								ftype = backup_files->getFileType(curr_fn);

								if (ftype & EFileType_File
									&& !hash_dir)
								{
									keep_inplace = true;
								}
							}

							if (ftype & EFileType_File && !keep_inplace
								&& !backup_files->deleteFile(curr_fn))
							{
								std::unique_ptr<IFile> tf(backup_files->openFile(curr_fn, MODE_READ));
								if (tf.get() != nullptr)
								{
									log("Could not remove file \"" + curr_fn + "\" in ::deleteFilesInSnapshot - " 
										+ os_last_error_str(), no_error ? LL_WARNING : LL_ERROR);
								}
								else
								{
									log("Could not remove file \"" + curr_fn + "\" in ::deleteFilesInSnapshot - "
										+ os_last_error_str() + ". It was already deleted.", no_error ? LL_WARNING : LL_ERROR);
								}

								if (!no_error)
								{
									return false;
								}
							}
							else if (ftype & EFileType_Directory
								&& (!backup_files->removeDirRecursive(curr_fn)
									|| backup_files->directoryExists(curr_fn)))
							{
								log("Could not remove directory \"" + curr_fn + "\" in ::deleteFilesInSnapshot (2) - " 
									+ os_last_error_str(), no_error ? LL_WARNING : LL_ERROR);

								if (!no_error)
								{
									return false;
								}
							}
							else if (ftype == 0)
							{
								log("Cannot get file type in ::deleteFilesInSnapshot. " + 
									os_last_error_str(), no_error ? LL_WARNING : LL_ERROR);
								if (!no_error)
								{
									return false;
								}
							}
						}
					}
				}
				else if (curr_file.isdir && curr_file.name != "..")
				{
					curr_path += "\\" + curr_file.name;
					curr_os_path += "\\" + osspecific_name;
					//folder_files.push(std::set<std::string>());
				}
				++line;
			}
		}
	}

	return true;
}

void LocalIncrFileBackup::updateBchPc(int64 done, int64 total)
{
	file_done_bytes = done;

	int64 ctime = Server->getTimeMS();
	if (ctime - laststatsupdate > status_update_intervall)
	{
		updateProgress(ctime);
	}
}
