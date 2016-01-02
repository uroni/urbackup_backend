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

extern IFileServ* fileserv;


namespace
{
	const int64 win32_meta_magic = little_endian(0x320FAB3D119DCB4AULL);
	const int64 unix_meta_magic =  little_endian(0xFE4378A3467647F0ULL);

	const _u32 ID_METADATA_V1_WIN = 1<<0 | 1<<3;
	const _u32 ID_METADATA_V1_UNIX = 1<<2 | 1<<3;

	class MetadataCallback : public IFileServ::IMetadataCallback
	{
	public:
		MetadataCallback(const std::string& basedir)
			: basedir(basedir)
		{

		}

		virtual IFile* getMetadata( const std::string& path, std::string& orig_path, int64& offset, int64& length, _u32& version )
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

			FileMetadata metadata;

			if(!read_metadata(metadata_file.get(), metadata))
			{
				return NULL;
			}

			orig_path = metadata.orig_path;
			offset = os_metadata_offset(metadata_file.get());
			length = metadata_file->Size() - offset;

			int64 metadata_magic_and_size[2];

			if(!metadata_file->Seek(offset))
			{
				return NULL;
			}

			if(metadata_file->Read(reinterpret_cast<char*>(&metadata_magic_and_size), sizeof(metadata_magic_and_size))!=sizeof(metadata_magic_and_size))
			{
				return NULL;
			}

			if(metadata_magic_and_size[1]==win32_meta_magic)
			{
				version=ID_METADATA_V1_WIN;
			}
			else if(metadata_magic_and_size[1]==unix_meta_magic)
			{
				version=ID_METADATA_V1_UNIX;
			}
			else
			{
				version=0;
			}

			if(!metadata_file->Seek(offset))
			{
				return NULL;
			}

			return metadata_file.release();
		}

	private:
		std::string basedir;
	};

	class ClientDownloadThread : public IThread
	{
	public:
		ClientDownloadThread(const std::string& curr_clientname, int curr_clientid, int restore_clientid, IFile* filelist_f, const std::string& foldername,
			const std::string& hashfoldername, const std::string& filter,
			bool token_authentication,
			const std::vector<backupaccess::SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_special_root,
			const std::string& folder_log_name, int64 restore_id, size_t status_id, logid_t log_id, const std::string& restore_token, const std::string& identity)
			: curr_clientname(curr_clientname), curr_clientid(curr_clientid), restore_clientid(restore_clientid),
			filelist_f(filelist_f), foldername(foldername), hashfoldername(hashfoldername),
			token_authentication(token_authentication), backup_tokens(backup_tokens),
			tokens(tokens), skip_special_root(skip_special_root), folder_log_name(folder_log_name),
			restore_token(restore_token), identity(identity), restore_id(restore_id), status_id(status_id), log_id(log_id)
		{
			TokenizeMail(filter, filter_fns, "/");
		}

		void operator()()
		{
			IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
			ServerBackupDao backup_dao(db);

			if(!createFilelist(foldername, hashfoldername, 0, skip_special_root))
			{
				ServerStatus::stopProcess(curr_clientname, status_id);

				int errors=0;
				int warnings=0;
				int infos=0;
				std::string logdata=ServerLogger::getLogdata(log_id, errors, warnings, infos);

				backup_dao.saveBackupLog(restore_clientid, errors, warnings, infos, 0,
					0, 0, 1);

				backup_dao.saveBackupLogData(db->getLastInsertID(), logdata);

				backup_dao.setRestoreDone(0, restore_id);

				return;
			}		

			fileserv->addIdentity(identity);
			fileserv->shareDir("clientdl_filelist", filelist_f->getFilename(), identity);
			ClientMain::addShareToCleanup(curr_clientid, SShareCleanup("clientdl_filelist", identity, true, false));

			delete filelist_f;

			MetadataCallback* callback = new MetadataCallback(hashfoldername);
			fileserv->shareDir("clientdl", foldername, identity);
			fileserv->shareDir("urbackup", "/tmp/mkmergsdfklrzrehmklregmfdkgfdgwretklödf", identity);
			ClientMain::addShareToCleanup(curr_clientid, SShareCleanup("clientdl", identity, false, true));
			ClientMain::addShareToCleanup(curr_clientid, SShareCleanup("urbackup", identity, false, false));
			fileserv->registerMetadataCallback("clientdl", identity, callback);


			CWData data;
			data.addBuffer("RESTORE", 7);
			data.addString(identity);
			data.addInt64(restore_id);
			data.addUInt64(status_id);
			data.addInt64(log_id.first);
			data.addString(restore_token);

			std::string msg(data.getDataPtr(), data.getDataPtr()+data.getDataSize());
			ServerStatus::sendToCommPipe(curr_clientname, msg);
		}

		bool writeFile(const std::string& data)
		{
			std::string towrite = (data);
			return filelist_f->Write(towrite)==towrite.size();
		}


		bool createFilelist(const std::string& foldername, const std::string& hashfoldername, size_t depth, bool skip_special)
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
				const SFile& file=files[i];

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

				if(file.isdir)
				{
					metadataname+=os_file_sep()+metadata_dir_fn;
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

				if(!has_metadata)
				{
					ServerLogger::Log(log_id, "Cannot read file metadata of file "+filename+" from "+metadataname+". Cannot start restore.", LL_ERROR);
					return false;
				}

				std::string extra;

				if(!metadata.orig_path.empty() &&
					(depth==0 || metadata.orig_path.find(file.name)!=metadata.orig_path.size()-file.name.size()))
				{
                    extra="&orig_path="+EscapeParamString(metadata.orig_path);
				}

				if(!metadata.shahash.empty())
				{
					extra+="&shahash="+base64_encode_dash(metadata.shahash);
				}

				if(depth==0 && !filter_fns.empty())
				{
					extra+="&single_item=1";
				}

				writeFileItem(filelist_f, file, extra);

				if(file.isdir)
				{
					ret = ret && createFilelist(filename, hashfoldername + os_file_sep() + escape_metadata_fn(file.name), depth+1, false);

					SFile cf;
					cf.name="..";
					cf.isdir=true;
					writeFileItem(filelist_f, cf);
				}
			}

			return ret;
		}

	private:
		std::string curr_clientname;
		int curr_clientid;
		int restore_clientid;
		IFile* filelist_f;
		std::string foldername;
		std::string hashfoldername;
		std::vector<std::string> filter_fns;
		bool token_authentication;
		std::vector<backupaccess::SToken> backup_tokens;
		std::vector<std::string> tokens;
		bool skip_special_root;
		std::string folder_log_name;
		int64 restore_id;
		size_t status_id;
		logid_t log_id;
		std::string restore_token;
		std::string identity;
	};
}

bool create_clientdl_thread(const std::string& curr_clientname, int curr_clientid, int restore_clientid, std::string foldername, std::string hashfoldername,
	const std::string& filter, bool token_authentication, const std::vector<backupaccess::SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes,
	const std::string& folder_log_name, int64& restore_id, size_t& status_id, logid_t& log_id, const std::string& restore_token)
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

	std::string identity = ServerSettings::generateRandomAuthKey(25);
	backup_dao.addRestore(restore_clientid, folder_log_name, identity, 0, std::string());

	restore_id = db->getLastInsertID();
	status_id = ServerStatus::startProcess(curr_clientname, sa_restore_file, folder_log_name);

	log_id = ServerLogger::getLogId(restore_clientid);

	Server->getThreadPool()->execute(new ClientDownloadThread(curr_clientname, curr_clientid, restore_clientid, 
		filelist_f, foldername, hashfoldername, filter, token_authentication, backup_tokens, tokens, skip_hashes, folder_log_name, restore_id,
		status_id, log_id, restore_token, identity));

	return true;
}

bool create_clientdl_thread( int backupid, const std::string& curr_clientname, int curr_clientid,
	int64& restore_id, size_t& status_id, logid_t& log_id, const std::string& restore_token)
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

	std::vector<backupaccess::SToken> backup_tokens;
	std::vector<std::string> tokens;
	return create_clientdl_thread(curr_clientname, curr_clientid, file_backup_info.clientid, curr_path, curr_metadata_path, std::string(), false, backup_tokens,
		tokens, true, "", restore_id, status_id, log_id, restore_token);
}

