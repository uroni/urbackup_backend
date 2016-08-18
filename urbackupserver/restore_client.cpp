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

#include "restore_client.h"
#include "../Interface/Thread.h"
#include "../fileservplugin/IFileServ.h"
#include "../Interface/File.h"
#include "../Interface/Server.h"
#include "server_settings.h"
#include "ClientMain.h"
#include <algorithm>
#include "../urbackupcommon/file_metadata.h"
#include "serverinterface/backups.h"
#include "../urbackupcommon/filelist_utils.h"
#include "../common/data.h"
#include "database.h"
#include "dao/ServerBackupDao.h"
#include "dao/ServerCleanupDao.h"
#include "server.h"

extern IFileServ* fileserv;


namespace
{
	const int64 win32_meta_magic = little_endian(0x320FAB3D119DCB4AULL);
	const int64 unix_meta_magic =  little_endian(0xFE4378A3467647F0ULL);

	const _u32 ID_METADATA_V1_WIN = 1<<0 | 1<<3;
	const _u32 ID_METADATA_V1_UNIX = 1<<2 | 1<<3;

	std::string reconstructOrigPath(const std::string& metadata_fn, bool isdir)
	{
		std::string cp;
		if (isdir)
		{
			cp = ExtractFilePath(metadata_fn, os_file_sep());
		}
		else
		{
			cp = metadata_fn;
		}

		std::string orig_path_add = ExtractFileName(cp, os_file_sep());
		FileMetadata parent_metadata;
		while (!(cp = ExtractFilePath(cp, os_file_sep())).empty()
			&& read_metadata(cp + os_file_sep() + metadata_dir_fn, parent_metadata))
		{
			if (!parent_metadata.orig_path.empty())
			{
				break;
			}

			orig_path_add = ExtractFileName(cp, os_file_sep()) + os_file_sep() + orig_path_add;
		}

		return parent_metadata.orig_path + os_file_sep() + orig_path_add;
	}

	class MetadataCallback : public IFileServ::IMetadataCallback
	{
	public:
		MetadataCallback(const std::string& basedir, const std::vector < std::pair<std::string, std::string> >& map_paths)
			: basedir(basedir), map_paths(map_paths)
		{

		}

		virtual IFile* getMetadata( const std::string& path, std::string* orig_path, int64* offset,
			int64* length, _u32* version, bool get_hashdata)
		{
			if(path.empty()) return NULL;

			std::string metadata_path = basedir;

			std::vector<std::string> path_segments;
			TokenizeMail(path.substr(1), path_segments, "/");

			bool isdir = path[0]=='d';

			for(size_t i=1;i<path_segments.size();++i)
			{
				if(path_segments[i]=="." || path_segments[i]=="..")
				{
					continue;
				}

				metadata_path+=os_file_sep() + escape_metadata_fn((path_segments[i]));

				if(i==path_segments.size()-1 && isdir)
				{
					metadata_path+=os_file_sep() + metadata_dir_fn;
				}
			}

			std::auto_ptr<IFile> metadata_file(Server->openFile(os_file_prefix(metadata_path), MODE_READ));

			if(metadata_file.get()==NULL)
			{
				return NULL;
			}

			if (orig_path != NULL)
			{
				FileMetadata metadata;

				if (!read_metadata(metadata_file.get(), metadata))
				{
					return NULL;
				}

				*orig_path = metadata.orig_path;

				if (orig_path->empty())
				{
					*orig_path = reconstructOrigPath(metadata_path, isdir);
				}

				for (size_t j = 0; j < map_paths.size(); ++j)
				{
					if (next(*orig_path, 0, map_paths[j].first))
					{
						orig_path->replace(orig_path->begin(), orig_path->begin() + map_paths[j].first.size(), map_paths[j].second);
						break;
					}
				}
			}


			if (!get_hashdata)
			{
				int64 m_offset = os_metadata_offset(metadata_file.get());

				if (offset != NULL) *offset = m_offset;

				int64 m_length = metadata_file->Size() - m_offset;

				if (length != NULL) *length = m_length;

				int64 metadata_magic_and_size[2];

				if (!metadata_file->Seek(m_offset))
				{
					return NULL;
				}

				if (metadata_file->Read(reinterpret_cast<char*>(&metadata_magic_and_size), sizeof(metadata_magic_and_size)) != sizeof(metadata_magic_and_size))
				{
					return NULL;
				}
								
				if (version != NULL)
				{
					if (metadata_magic_and_size[1] == win32_meta_magic)
					{
						*version = ID_METADATA_V1_WIN;
					}
					else if (metadata_magic_and_size[1] == unix_meta_magic)
					{
						*version = ID_METADATA_V1_UNIX;
					}
					else
					{
						*version = 0;
					}
				}

				if (!metadata_file->Seek(m_offset))
				{
					return NULL;
				}
			}
			else
			{
				int64 m_length = get_hashdata_size(metadata_file.get());

				if (m_length <= sizeof(int64) )
				{
					return NULL;
				}

				if (offset != NULL) *offset = sizeof(int64);

				if (length != NULL) *length = m_length - sizeof(int64);
			}

			return metadata_file.release();
		}

	private:
		std::string basedir;
		std::vector < std::pair<std::string, std::string> > map_paths;
	};

	class ClientDownloadThread : public IThread
	{
	private:
		struct SRestoreFolder
		{
			std::string foldername;
			std::string hashfoldername;
			std::string share_path;
			std::vector<std::string> filter_fns;
		};

	public:
		ClientDownloadThread(const std::string& curr_clientname, int curr_clientid, int restore_clientid, IFile* filelist_f, const std::string& foldername,
			const std::string& hashfoldername, const std::string& filter, bool skip_special_root,
			const std::string& folder_log_name, int64 restore_id, size_t status_id, logid_t log_id, const std::string& restore_token, const std::string& identity,
			const std::vector<std::pair<std::string, std::string> >& map_paths,
			bool clean_other, bool ignore_other_fs, const std::string& share_path,
			bool follow_symlinks, int64 restore_flags)
			: curr_clientname(curr_clientname), curr_clientid(curr_clientid), restore_clientid(restore_clientid),
			filelist_f(filelist_f),
			skip_special_root(skip_special_root),
			restore_token(restore_token), identity(identity), restore_id(restore_id), status_id(status_id), log_id(log_id),
			single_file(false), map_paths(map_paths), clean_other(clean_other), ignore_other_fs(ignore_other_fs),
			curr_restore_folder_idx(0), follow_symlinks(follow_symlinks), restore_flags(restore_flags)
		{
			SRestoreFolder restore_folder;
			restore_folder.foldername = foldername;
			restore_folder.hashfoldername = hashfoldername;
			restore_folder.share_path = share_path;

			TokenizeMail(filter, restore_folder.filter_fns, "/");

			if (restore_folder.filter_fns.size() == 1)
			{
				single_file = true;
			}
			
			if (!restore_folder.share_path.empty()
				&& restore_folder.share_path[restore_folder.share_path.size() - 1] == '/')
			{
				restore_folder.share_path.erase(restore_folder.share_path.size() - 1, 1);
			}

			restore_folders.push_back(restore_folder);
		}

		void operator()()
		{
			IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
			ServerBackupDao backup_dao(db);

			for (; curr_restore_folder_idx < restore_folders.size(); ++curr_restore_folder_idx)
			{
				SRestoreFolder& restore_folder = restore_folders[curr_restore_folder_idx];

				filter_fns = restore_folder.filter_fns;

				if (!createFilelist(restore_folder.foldername, restore_folder.hashfoldername,
					restore_folder.share_path, 0,
					curr_restore_folder_idx==0 ? skip_special_root : false) )
				{
					ServerStatus::stopProcess(curr_clientname, status_id);

					int errors = 0;
					int warnings = 0;
					int infos = 0;
					std::string logdata = ServerLogger::getLogdata(log_id, errors, warnings, infos);

					backup_dao.saveBackupLog(restore_clientid, errors, warnings, infos, 0,
						0, 0, 1);

					backup_dao.saveBackupLogData(db->getLastInsertID(), logdata);

					backup_dao.setRestoreDone(0, restore_id);

					return;
				}
			}

			fileserv->addIdentity(identity);
			fileserv->shareDir("clientdl_filelist", filelist_f->getFilename(), identity, false);
			ClientMain::addShareToCleanup(curr_clientid, SShareCleanup("clientdl_filelist", identity, true, false));

			delete filelist_f;

			for (size_t i = 0; i < restore_folders.size(); ++i)
			{
				SRestoreFolder& restore_folder = restore_folders[i];

				MetadataCallback* callback = new MetadataCallback(restore_folder.hashfoldername, map_paths);
				fileserv->shareDir("clientdl"+convert(i), restore_folder.foldername, identity, false);
				ClientMain::addShareToCleanup(curr_clientid, SShareCleanup("clientdl" + convert(i), identity, false, true));
				fileserv->registerMetadataCallback("clientdl" + convert(i), identity, callback);
			}
			
			fileserv->shareDir("urbackup", "/tmp/mkmergsdfklrzrehmklregmfdkgfdgwretklödf", identity, false);
			ClientMain::addShareToCleanup(curr_clientid, SShareCleanup("urbackup", identity, false, false));


			CWData data;
			data.addBuffer("RESTORE", 7);
			data.addString(identity);
			data.addInt64(restore_id);
			data.addUInt64(status_id);
			data.addInt64(log_id.first);
			data.addString(restore_token);
			data.addChar(single_file ? 1 : 0);
			data.addChar(clean_other ? 1 : 0);
			data.addChar(ignore_other_fs ? 1 : 0);
			data.addInt64(restore_flags);

			std::string msg(data.getDataPtr(), data.getDataPtr()+data.getDataSize());
			ServerStatus::sendToCommPipe(curr_clientname, msg);
		}

		bool writeFile(const std::string& data)
		{
			std::string towrite = (data);
			return filelist_f->Write(towrite)==towrite.size();
		}


		bool createFilelist(std::string foldername, std::string hashfoldername, std::string curr_share_path,
			size_t depth, bool skip_special)
		{
			bool has_error=false;
			const std::vector<SFile> files = getFiles(os_file_prefix(foldername), &has_error);

			if(has_error)
			{
				ServerLogger::Log(log_id, "Cannot read files from folder "+foldername+". Cannot start restore.", LL_ERROR);
				return false;
			}

			bool ret=true;

			for(size_t i=0;i<files.size();++i)
			{
				SFile file=files[i];

				if(depth==0 && (!filter_fns.empty() && std::find(filter_fns.begin(), filter_fns.end(), file.name)==filter_fns.end()) )
				{
					continue;
				}

				if(skip_special
					&& (file.name==".hashes" || file.name=="user_views") )
				{
					continue;
				}

				std::string metadataname = hashfoldername + os_file_sep() + escape_metadata_fn(file.name);
				std::string filename = foldername + os_file_sep() + file.name;

				if (file.issym)
				{
					std::string pool_path;
					if (os_get_symlink_target(filename, pool_path))
					{
						std::string directory_pool = ExtractFileName(ExtractFilePath(ExtractFilePath(pool_path, os_file_sep()), os_file_sep()), os_file_sep());

						if (directory_pool != ".directory_pool")
						{
							followSymlink(pool_path, filename, metadataname, file.isdir);
							file.size = 0;
						}
						else
						{
							file.issym = false;
							file.isdir = true;
						}
					}
				}
				
				std::string metadatasource;
				bool recurse_dir = false;
				if(file.isdir && !file.issym
					&& os_directory_exists(os_file_prefix(metadataname)) )
				{
					metadatasource = metadataname + os_file_sep()+metadata_dir_fn;
					single_file = false;
					recurse_dir = true;
				}
				else
				{
					metadatasource = metadataname;
				}				

				bool has_metadata = false;

				FileMetadata metadata;
				if(!read_metadata(metadatasource, metadata))
				{
					ServerLogger::Log(log_id, "Cannot read file metadata of file "+filename+" from "+ metadatasource +". Cannot start restore.", LL_ERROR);
					return false;
				}

				std::string extra;

				if (depth == 0
					&& metadata.orig_path.empty())
				{
					metadata.orig_path = reconstructOrigPath(metadatasource, file.isdir);
				}

				if(!metadata.orig_path.empty() &&
					(depth==0 || metadata.orig_path.rfind(file.name)!=metadata.orig_path.size()-file.name.size()))
				{
					std::string alt_orig_path = metadata.orig_path;
					for (size_t j = 0; j < map_paths.size(); ++j)
					{
						if (next(metadata.orig_path, 0, map_paths[j].first))
						{
							metadata.orig_path.replace(metadata.orig_path.begin(), metadata.orig_path.begin()+map_paths[j].first.size(), map_paths[j].second);
							break;
						}
					}

                    extra="&orig_path="+EscapeParamString(metadata.orig_path);

					if (metadata.orig_path != alt_orig_path)
					{
						extra+="&alt_orig_path="+EscapeParamString(alt_orig_path);
					}
				}

				if (depth == 0)
				{
					extra += "&share_path=" + EscapeParamString(curr_share_path.empty() ? file.name : (curr_share_path + "/" + file.name));
					extra += "&server_path=clientdl" + convert(curr_restore_folder_idx);
				}

				if(!metadata.shahash.empty())
				{
					if (BackupServer::useTreeHashing())
					{
						extra += "&thash=" + base64_encode_dash(metadata.shahash);
					}
					else
					{
						extra += "&shahash=" + base64_encode_dash(metadata.shahash);
					}
				}

				if(depth==0 &&
					( !filter_fns.empty() || skip_special ) )
				{
					extra+="&single_item=1";
				}


				writeFileItem(filelist_f, file, extra);

				if(file.isdir)
				{
					if (recurse_dir)
					{
						ret = ret && createFilelist(filename, metadataname, curr_share_path + "/" + file.name, depth + 1, false);
					}

					SFile cf;
					cf.name="..";
					cf.isdir=true;
					writeFileItem(filelist_f, cf);
				}
			}

			return ret;
		}

		void followSymlink(const std::string& symlink_target,
			std::string file_path,
			std::string hash_file_path,
			bool& is_dir)
		{
			std::vector<std::string> symlink_path_toks;
			TokenizeMail(symlink_target, symlink_path_toks, os_file_sep());

			SRestoreFolder restore_folder;
			restore_folder.foldername = file_path;
			restore_folder.hashfoldername = hash_file_path;

			size_t sym_off = 0;
			restore_folder.foldername = ExtractFilePath(restore_folder.foldername, os_file_sep());
			restore_folder.hashfoldername = ExtractFilePath(restore_folder.hashfoldername, os_file_sep());
			for (sym_off = 0; sym_off< symlink_path_toks.size()
				&& symlink_path_toks[sym_off] == ".."; ++sym_off)
			{
				restore_folder.foldername = ExtractFilePath(restore_folder.foldername, os_file_sep());
				restore_folder.hashfoldername = ExtractFilePath(restore_folder.hashfoldername, os_file_sep());
			}

			for (; sym_off < symlink_path_toks.size(); ++sym_off)
			{
				restore_folder.foldername += os_file_sep() + symlink_path_toks[sym_off];
				restore_folder.hashfoldername += os_file_sep() + symlink_path_toks[sym_off];
				if (!restore_folder.share_path.empty())
				{
					restore_folder.share_path += "/";
				}
				restore_folder.share_path += symlink_path_toks[sym_off];
			}

			is_dir = (os_get_file_type(restore_folder.foldername) & EFileType_Directory) > 0;

			if (!follow_symlinks)
			{
				return;
			}

			for (size_t i = 0; i < restore_folders.size(); ++i)
			{
				if (next(restore_folder.foldername, 0, restore_folders[i].foldername) )
				{
					if (restore_folders[i].filter_fns.empty())
					{
						//Is already being restored
						return;
					}
					else
					{
						for (size_t j = 0; j < restore_folders[i].filter_fns.size(); ++j)
						{
							if (next(restore_folder.foldername, 0, restore_folders[i].foldername + os_file_sep()+ restore_folders[i].filter_fns[j]))
							{
								//Is already being restored
								return;
							}
						}
					}
				}
			}

			if (is_dir)
			{
				for (size_t i = curr_restore_folder_idx + 1; i < restore_folders.size();)
				{
					if (next(restore_folders[i].foldername, 0, restore_folder.foldername))
					{
						//Will be restored by new restore folder
						restore_folders.erase(restore_folders.begin() + i);
					}
					else
					{
						++i;
					}
				}
			}

			if (!is_dir)
			{
				std::string fn = ExtractFileName(restore_folder.foldername, os_file_sep());
				restore_folder.foldername = ExtractFilePath(restore_folder.foldername, os_file_sep());
				restore_folder.hashfoldername = ExtractFilePath(restore_folder.hashfoldername, os_file_sep());
				restore_folder.share_path = ExtractFilePath(restore_folder.share_path, "/");
				restore_folder.filter_fns.push_back(fn);
			}

			restore_folders.push_back(restore_folder);
		}

	private:
		std::string curr_clientname;
		int curr_clientid;
		int restore_clientid;
		IFile* filelist_f;
		std::vector<std::string> filter_fns;
		bool skip_special_root;
		int64 restore_id;
		size_t status_id;
		logid_t log_id;
		std::string restore_token;
		std::string identity;
		bool single_file;
		std::vector < std::pair<std::string, std::string> > map_paths;
		bool clean_other;
		bool ignore_other_fs;
		std::vector<SRestoreFolder> restore_folders;
		size_t curr_restore_folder_idx;
		bool follow_symlinks;
		int64 restore_flags;
	};
}

bool create_clientdl_thread(const std::string& curr_clientname, int curr_clientid, int restore_clientid, std::string foldername, std::string hashfoldername,
	const std::string& filter, bool skip_hashes,
	const std::string& folder_log_name, int64& restore_id, size_t& status_id, logid_t& log_id, const std::string& restore_token,
	const std::vector<std::pair<std::string, std::string> >& map_paths, bool clean_other, bool ignore_other_fs, const std::string& share_path,
	bool follow_symlinks, int64 restore_flags, THREADPOOL_TICKET& ticket)
{
	IFile* filelist_f = Server->openTemporaryFile();

	if(filelist_f==NULL)
	{
		return false;
	}

	if(!foldername.empty() && foldername[foldername.size()-1]==os_file_sep()[0])
	{
		foldername = foldername.substr(0, foldername.size()-1);
	}

	if(!hashfoldername.empty() && hashfoldername[hashfoldername.size()-1]==os_file_sep()[0])
	{
		hashfoldername = hashfoldername.substr(0, hashfoldername.size()-1);
	}

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);

	std::string full_log_name;
	if (filter.find("/") != std::string::npos)
	{
		full_log_name = folder_log_name + "[" + filter + "]";
	}
	else
	{
		full_log_name = folder_log_name + filter;
	}


	std::string identity = ServerSettings::generateRandomAuthKey(25);

	backup_dao.addRestore(restore_clientid, full_log_name, identity, 0, std::string());
	restore_id = db->getLastInsertID();

	status_id = ServerStatus::startProcess(curr_clientname, sa_restore_file, full_log_name, log_id, false);

	log_id = ServerLogger::getLogId(restore_clientid);	

	ticket = Server->getThreadPool()->execute(new ClientDownloadThread(curr_clientname, curr_clientid, restore_clientid,
		filelist_f, foldername, hashfoldername, filter, skip_hashes, folder_log_name, restore_id,
		status_id, log_id, restore_token, identity, map_paths, clean_other, ignore_other_fs, share_path, follow_symlinks, restore_flags), "frestore preparation");

	return true;
}

bool create_clientdl_thread( int backupid, const std::string& curr_clientname, int curr_clientid,
	int64& restore_id, size_t& status_id, logid_t& log_id, const std::string& restore_token,
	const std::vector<std::pair<std::string, std::string> >& map_paths,
	bool clean_other, bool ignore_other_fs, bool follow_symlinks, int64 restore_flags)
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);
	ServerCleanupDao cleanup_dao(db);

	ServerBackupDao::SFileBackupInfo file_backup_info = backup_dao.getFileBackupInfo(backupid);

	if(!file_backup_info.exists)
	{
		Server->Log("Could not get file backup info for backupid "+convert(backupid));
		return false;
	}

	ServerCleanupDao::CondString client_name = cleanup_dao.getClientName(file_backup_info.clientid);

	if(!client_name.exists)
	{
		Server->Log("Could not get client name for clientid "+convert(file_backup_info.clientid));
		return false;
	}

	ServerBackupDao::CondString backupfolder = backup_dao.getSetting(0, "backupfolder");

	if(!backupfolder.exists)
	{
		Server->Log("Could not get backup storage folder");
		return false;
	}

	std::string curr_path=backupfolder.value+os_file_sep()+client_name.value+os_file_sep()+file_backup_info.path;
	std::string curr_metadata_path=backupfolder.value+os_file_sep()+client_name.value+os_file_sep()+file_backup_info.path+os_file_sep()+".hashes";
	std::string share_path = greplace(os_file_sep(), "/", file_backup_info.path);
	THREADPOOL_TICKET ticket;

	return create_clientdl_thread(curr_clientname, curr_clientid, file_backup_info.clientid, curr_path, curr_metadata_path, std::string(),
		true, "", restore_id, status_id, log_id, restore_token, map_paths, clean_other, ignore_other_fs, share_path, follow_symlinks, restore_flags, ticket);
}

