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

#include "action_header.h"
#include "../../Interface/File.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../urbackupcommon/file_metadata.h"
#include "../../urbackupcommon/mbrdata.h"
#include "../../Interface/SettingsReader.h"
#include "../../cryptoplugin/ICryptoFactory.h"
#include "../../fsimageplugin/IFSImageFactory.h"
#include "../../fsimageplugin/IVHDFile.h"
#include "../server_settings.h"
#include "backups.h"
#include <memory>
#include <algorithm>
#include <assert.h>
#include "../../fileservplugin/IFileServ.h"
#include "../server_status.h"
#include "../restore_client.h"
#include "../dao/ServerBackupDao.h"
#include "../server_dir_links.h"
#include "../ImageMount.h"
#include "../server.h"
#include "../server_cleanup.h"
#include "../dao/ServerCleanupDao.h"

extern ICryptoFactory *crypto_fak;
extern IFileServ* fileserv;
extern IFSImageFactory *image_fak;

namespace backupaccess
{
	std::string constructFilter(const std::vector<int> &clientid, std::string key)
	{
		std::string clientf="(";
		for(size_t i=0;i<clientid.size();++i)
		{
			clientf+=key+"="+convert(clientid[i]);
			if(i+1<clientid.size())
				clientf+=" OR ";
		}
		clientf+=")";
		return clientf;
	}
}


bool create_zip_to_output(const std::string& folderbase, const std::string& foldername, const std::string& hashfolderbase, 
	const std::string& hashfoldername, const std::string& filter, bool token_authentication,
	const std::vector<backupaccess::SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes);

namespace
{
	bool sendFile(Helper& helper, const std::string& filename)
	{
		THREAD_ID tid = Server->getThreadID();
		Server->setContentType(tid, "application/octet-stream");
		Server->addHeader(tid, "Cache-Control: no-cache");
		Server->addHeader(tid, "Content-Disposition: attachment; filename=\""+(ExtractFileName(filename))+"\"");
		IFile *in=Server->openFile(os_file_prefix(filename), MODE_READ);
		if(in!=NULL)
		{
			helper.releaseAll();

			Server->addHeader(tid, "Content-Length: "+convert(in->Size()) );
			char buf[4096];
			_u32 r;
			do
			{
				r=in->Read(buf, 4096);
				Server->WriteRaw(tid, buf, r, false);
			}
			while(r>0);
			Server->destroy(in);
			return true;
		}
		else
		{
			Server->Log("Error opening file \""+filename+"\"", LL_ERROR);
			return false;
		}
	}

	bool sendZip(Helper& helper, std::string folderbase, std::string foldername, std::string hashfolderbase, std::string hashfoldername, const std::string& filter, bool token_authentication,
		const std::vector<backupaccess::SToken>& backup_tokens, const std::vector<std::string>& tokens, bool skip_hashes)
	{
		std::string zipname=ExtractFileName(foldername)+".zip";

		THREAD_ID tid = Server->getThreadID();
		Server->setContentType(tid, "application/octet-stream");
		Server->addHeader(tid, "Cache-Control: no-cache");
		Server->addHeader(tid, "Content-Disposition: attachment; filename=\""+(zipname)+"\"");
		helper.releaseAll();

		if(!foldername.empty())
		{
			if(foldername[foldername.size()-1]==os_file_sep()[0])
			{
				foldername.resize(foldername.size()-1);
			}
		}

		if(!hashfoldername.empty())
		{
			if(hashfoldername[hashfoldername.size()-1]==os_file_sep()[0])
			{
				hashfoldername.resize(hashfoldername.size()-1);
			}
		}

		return create_zip_to_output(folderbase, foldername, hashfolderbase, hashfoldername, filter, token_authentication,
			backup_tokens, tokens, skip_hashes);
	}

	std::vector<FileMetadata> getMetadata(std::string dir, const std::vector<SFile>& files, bool skip_special)
	{
		std::vector<FileMetadata> ret;
		ret.resize(files.size());

		if(dir.empty() || dir[dir.size()-1]!=os_file_sep()[0])
		{
			dir+=os_file_sep();
		}

		for(size_t i=0;i<files.size();++i)
		{
			if(skip_special && (files[i].name==".hashes" || files[i].name=="user_views" || next(files[i].name, 0, ".symlink_") ) )
				continue;

			SFile file = files[i];

			if (file.isdir
				&& file.issym
				&& is_directory_link(dir + file.name) )
			{
				file.issym = false;
				file.isspecialf = false;
			}

			std::string metadata_fn;
			if (file.isdir
				&& !file.issym
				&& !file.isspecialf)
			{
				metadata_fn = dir + escape_metadata_fn(file.name) + os_file_sep() + metadata_dir_fn;
			}
			else
			{
				metadata_fn = dir + escape_metadata_fn(file.name);
			}

			if(!read_metadata(metadata_fn, ret[i]) )
			{
				Server->Log("Error reading metadata of file "+dir+os_file_sep()+ file.name, LL_ERROR);
			}
		}

		return ret;
	}

	FileMetadata getMetaData(std::string path, bool is_file)
	{
		if(path.empty() || (!is_file && path[path.size()-1]!=os_file_sep()[0]) )
		{
			path+=os_file_sep();
		}

		std::string metadata_fn;
		if(is_file)
		{
			metadata_fn = ExtractFilePath(path) + os_file_sep() + escape_metadata_fn(ExtractFileName(path));
		}
		else
		{
			metadata_fn = ExtractFilePath(path) + os_file_sep() + escape_metadata_fn(ExtractFileName(path))+os_file_sep()+metadata_dir_fn;
		}

		FileMetadata ret;
		if(!read_metadata(metadata_fn, ret) )
		{
			Server->Log("Error reading metadata of path "+path, LL_ERROR);
		}
		return ret;
	}

	int getClientid(IDatabase* db, const std::string& clientname)
	{
		IQuery* q=db->Prepare("SELECT id FROM clients WHERE name=?");
		q->Bind(clientname);
		db_results res=q->Read();
		q->Reset();

		if(!res.empty())
		{
			return watoi(res[0]["id"]);
		}
		return -1;
	}

	std::string getClientname(IDatabase* db, int clientid)
	{
		IQuery *q=db->Prepare("SELECT name FROM clients WHERE id=?");
		q->Bind(clientid);
		db_results res=q->Read();
		q->Reset();
		if(!res.empty())
		{
			return res[0]["name"];
		}
		else
		{
			return std::string();
		}
	}

	bool checkBackupTokens(const std::string& fileaccesstokens, const std::string& backupfolder, const std::string& clientname, const std::string& path)
	{
		std::vector<std::string> tokens;
		Tokenize(fileaccesstokens, tokens, ";");

		backupaccess::STokens backup_tokens = backupaccess::readTokens(backupfolder, clientname, path);
		if(backup_tokens.tokens.empty())
		{
			return false;
		}

		for(size_t i=0;i<backup_tokens.tokens.size();++i)
		{
			for(size_t j=0;j<tokens.size();++j)
			{
				if(backup_tokens.tokens[i].token.empty())
					continue;

				if(backup_tokens.tokens[i].token==tokens[j])
				{
					return true;
				}
			}
		}

		return false;
	}

	bool checkFileToken(const std::string& fileaccesstokens, const std::string& backupfolder, const std::string& clientname, const std::string& path, const std::string& filemetadatapath)
	{
		std::vector<std::string> tokens;
		Tokenize(fileaccesstokens, tokens, ";");

		backupaccess::STokens backup_tokens = backupaccess::readTokens(backupfolder, clientname, path);
		if(backup_tokens.tokens.empty())
		{
			return false;
		}

		FileMetadata metadata;
		if(!read_metadata(filemetadatapath, metadata))
		{
			return false;
		}

		return checkFileToken(backup_tokens.tokens, tokens, metadata);
	}
} //unnamed namespace

namespace backupaccess
{
	std::string getBackupFolder(IDatabase* db)
	{
		IQuery* q=db->Prepare("SELECT value FROM settings_db.settings WHERE key='backupfolder' AND clientid=0");
		db_results res_bf=q->Read();
		q->Reset();
		if(!res_bf.empty() )
		{
			return res_bf[0]["value"];
		}
		else
		{
			return std::string();
		}
	}

	std::string decryptTokens(IDatabase* db, const str_map& CURRP)
	{
		if(crypto_fak==NULL)
		{
			return std::string();
		}

		int clientid;
		str_map::const_iterator iter_clientname =CURRP.find("clientname");
		if(iter_clientname!=CURRP.end())
		{
			clientid = getClientid(db, iter_clientname->second);
		}
		else
		{
			str_map::const_iterator iter_clientid = CURRP.find("clientid");
			if(iter_clientid!=CURRP.end())
			{
				clientid = watoi(iter_clientid->second);
			}
			else
			{
				return std::string();
			}
		}

		if(clientid==-1)
		{
			return std::string();
		}

		ServerSettings server_settings(db, clientid);

		std::string client_key = server_settings.getSettings()->client_access_key;

		size_t i=0;
		str_map::const_iterator iter;
		do 
		{
			iter = CURRP.find("tokens"+convert(i));

			if(iter!=CURRP.end())
			{
				std::string bin_input = base64_decode_dash(iter->second);
				std::string session_key = crypto_fak->decryptAuthenticatedAES(bin_input, client_key, 1);
				if(!session_key.empty())
				{
					iter = CURRP.find("token_data");

					if(iter==CURRP.end())
					{
						return std::string();
					}

					bin_input = base64_decode_dash(iter->second);

					std::string decry = crypto_fak->decryptAuthenticatedAES(bin_input, session_key, 1);

					if(!decry.empty())
					{
						std::string tokenhash = Server->GenerateBinaryMD5(bin_input);

						ServerBackupDao backupdao(db);
						if(backupdao.hasUsedAccessToken(tokenhash).exists)
						{
							return std::string();
						}
						else
						{
							backupdao.addUsedAccessToken(clientid, tokenhash);
						}
					}

					return decry;
				}
			}
			++i;
		} while (iter!=CURRP.end());

		return std::string();
	}

	STokens readTokens(const std::string& backupfolder, const std::string& clientname, const std::string& path)
	{
		if(backupfolder.empty() || clientname.empty() || path.empty())
		{
			return STokens();
		}

		std::auto_ptr<ISettingsReader> backup_tokens(Server->createFileSettingsReader(backupfolder+os_file_sep()+clientname+os_file_sep()+path+os_file_sep()+".hashes"+os_file_sep()+".urbackup_tokens.properties"));

		if(!backup_tokens.get())
		{
			return STokens();
		}

		std::string ids_str = backup_tokens->getValue("ids", "");
		std::vector<std::string> ids;
		Tokenize(ids_str, ids, ",");

		std::vector<SToken> ret;
		for(size_t i=0;i<ids.size();++i)
		{
			SToken token = { watoi64(ids[i]),
				base64_decode_dash(backup_tokens->getValue(ids[i]+"."+"accountname", "")),
				backup_tokens->getValue(ids[i]+"."+"token", "") };

			ret.push_back(token);
		}
		STokens tokens = { backup_tokens->getValue("access_key", ""),
			ret };

		return tokens;
	}

	bool checkFileToken( const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, const FileMetadata &metadata )
	{
		bool has_permission=false;
		for(size_t i=0;i<backup_tokens.size();++i)
		{
			for(size_t j=0;j<tokens.size();++j)
			{
				if(backup_tokens[i].token.empty())
					continue;

				bool denied = false;
				if(backup_tokens[i].token==tokens[j] &&
					metadata.hasPermission(backup_tokens[i].id, denied))
				{
					has_permission=true;
				}
				else if(denied)
				{
					return false;
				}
			}
		}

		return has_permission;
	}

	JSON::Array get_backups_with_tokens(IDatabase * db, int t_clientid, std::string clientname, std::string* fileaccesstokens, int backupid_offset, bool& has_access)
	{
		std::string backupfolder = getBackupFolder(db);

		Helper helper(Server->getThreadID(), NULL, NULL);

		int last_filebackup = 0;
		IQuery* q_last = db->Prepare("SELECT id FROM backups WHERE clientid=? AND done=1 ORDER BY backuptime DESC LIMIT 1");
		q_last->Bind(t_clientid);
		db_results res_last = q_last->Read();
		q_last->Reset();		
		if (!res_last.empty())
		{
			last_filebackup = watoi(res_last[0]["id"]);
		}

		IQuery *q=db->Prepare("SELECT id, strftime('"+helper.getTimeFormatString()+"', backuptime) AS t_backuptime, incremental, size_bytes, archived, archive_timeout, path, delete_pending FROM backups WHERE complete=1 AND done=1 AND clientid=? ORDER BY backuptime DESC");
		q->Bind(t_clientid);
		db_results res=q->Read();
		JSON::Array backups;

		has_access = false;
		
		if (res.empty())
		{
			has_access = true;
		}

		for(size_t i=0;i<res.size();++i)
		{
			if(fileaccesstokens!=NULL && !checkBackupTokens(*fileaccesstokens, backupfolder, clientname, res[i]["path"]) )
			{
				continue;
			}

			has_access = true;

			JSON::Object obj;
			int backupid = watoi(res[i]["id"]);
			obj.set("id", backupid+backupid_offset);
			obj.set("backuptime", watoi64(res[i]["t_backuptime"]));
			obj.set("incremental", watoi(res[i]["incremental"]));
            obj.set("size_bytes", watoi64(res[i]["size_bytes"]));
            int archived = watoi(res[i]["archived"]);
            obj.set("archived", watoi(res[i]["archived"]));
			if (res[i]["delete_pending"] == "1")
			{
				obj.set("delete_pending", true);
			}
			_i64 archive_timeout=0;
			if(!res[i]["archive_timeout"].empty())
			{
				archive_timeout=watoi64(res[i]["archive_timeout"]);
				if(archive_timeout!=0)
				{
					archive_timeout-=Server->getTimeSeconds();
				}
			}
            if(archived!=0)
            {
                obj.set("archive_timeout", archive_timeout);
            }
			if (backupid == last_filebackup)
			{
				obj.set("disable_delete", true);
			}
			backups.add(obj);
		}

		return backups;
	}

	bool valid_path_component(const std::string& t_path)
	{
		return !t_path.empty() && t_path != " " && t_path != "." && t_path != ".."
			&& t_path.find("/") == std::string::npos
			&& t_path.find("\\") == std::string::npos;
	}

	SPathInfo get_metadata_path_with_tokens(const std::string& u_path, std::string* fileaccesstokens, std::string clientname, std::string backupfolder, int* backupid, std::string backuppath)
	{
		SPathInfo ret;
		std::vector<std::string> tokens;
		if(fileaccesstokens)
		{
			ret.backup_tokens = readTokens(backupfolder, clientname, backuppath);							
			Tokenize(*fileaccesstokens, tokens, ";");
		}

		std::vector<std::string> t_path;
		Tokenize(u_path, t_path, "/");

        ret.can_access_path=true;

		for(size_t i=0;i<t_path.size();++i)
		{
			if(valid_path_component(t_path[i]))
			{
				ret.rel_path+=UnescapeSQLString(t_path[i]);
				ret.rel_metadata_path+=escape_metadata_fn(UnescapeSQLString(t_path[i]));
				
				std::string curr_full_dir = backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath+(ret.rel_path.empty()?"":(os_file_sep()+ret.rel_path));
				
				if(i==t_path.size()-1 && !os_directory_exists(os_file_prefix(curr_full_dir)) )
				{
					ret.is_file=true;
				}
				else
				{
					ret.rel_path+=os_file_sep();
					ret.rel_metadata_path+=os_file_sep();
				}

				std::string curr_metadata_dir = backupfolder + os_file_sep() + clientname + os_file_sep() + backuppath + os_file_sep() + ".hashes" + (ret.rel_metadata_path.empty() ? "" : (os_file_sep() + ret.rel_metadata_path));

				if(fileaccesstokens)
				{
					std::string curr_metadata_file = curr_metadata_dir;
					if(!ret.is_file)
					{
						curr_metadata_file +=metadata_dir_fn;
					}

					FileMetadata dir_metadata;
					if(!read_metadata(curr_metadata_file, dir_metadata))
					{
						ret.can_access_path=false;
						break;
					}
					if(!checkFileToken(ret.backup_tokens.tokens, tokens, dir_metadata))
					{
						ret.can_access_path=false;
						break;
					}
				}
				
				if (os_directory_exists(os_file_prefix(curr_full_dir))
					&& !os_directory_exists(os_file_prefix(curr_metadata_dir)))
				{
					std::string symlink_target;
					if (os_get_symlink_target(os_file_prefix(curr_full_dir), symlink_target))
					{
						std::string upone = ".." + os_file_sep();
						while (next(symlink_target, 0, upone))
						{
							symlink_target = symlink_target.substr(upone.size());
						}
						ret.rel_metadata_path = symlink_target + os_file_sep();
					}

					if (i == t_path.size() - 1)
					{
						ret.is_symlink = true;
					}
				}
			}
		}

		ret.full_metadata_path = backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath+os_file_sep()+".hashes"+(ret.rel_metadata_path.empty()?"":(os_file_sep()+ret.rel_metadata_path));
		ret.full_path = backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath+(ret.rel_path.empty()?"":(os_file_sep()+ret.rel_path));

		return ret;
	}

	SPathInfo get_image_path_info(const std::string& u_path, std::string clientname, std::string backupfolder, int backupid, std::string backuppath)
	{
		std::vector<std::string> t_path;
		Tokenize(u_path, t_path, "/");

		std::string rel_path;
		for (size_t i = 0; i < t_path.size(); ++i)
		{
			if (valid_path_component(t_path[i]))
			{
				rel_path += os_file_sep() + t_path[i];
			}
		}

		std::string content_path = backuppath + rel_path;

		int ftype = os_get_file_type(os_file_prefix(content_path));

		SPathInfo ret;
		ret.can_access_path = ftype!=0;
		ret.full_path = content_path;
		ret.is_file = (ftype & EFileType_File) > 0;
		ret.is_symlink = (ftype & EFileType_Symlink) > 0;
		ret.rel_path = !rel_path.empty() ? rel_path.substr(1) : rel_path;

		return ret;
	}

	std::string get_backup_path(IDatabase* db, int backupid, int t_clientid)
	{
		IQuery* q=db->Prepare("SELECT path FROM backups WHERE id=? AND clientid=?");
		q->Bind(backupid);
		q->Bind(t_clientid);
		db_results res=q->Read();
		q->Reset();
		if(!res.empty())
		{
			return res[0]["path"];
		}
		else
		{
			return std::string();
		}
	}

    bool get_files_with_tokens(IDatabase* db, int* backupid, int t_clientid, std::string clientname, std::string* fileaccesstokens,
                               const std::string& u_path, int backupid_offset, JSON::Object& ret)
	{
		Helper helper(Server->getThreadID(), NULL, NULL);

		db_results res;
		if(backupid)
		{
			IQuery* q=db->Prepare("SELECT path,strftime('"+helper.getTimeFormatString()+"', backuptime) AS backuptime FROM backups WHERE id=? AND clientid=?");
			q->Bind(*backupid);
			q->Bind(t_clientid);
			res=q->Read();
			q->Reset();

			if(!res.empty())
			{
				ret.set("backuptime", watoi64(res[0]["backuptime"]));
                ret.set("backupid", *backupid + backupid_offset);
			}
            else
            {
                return false;
            }
		}
		else
		{
			IQuery* q=db->Prepare("SELECT id, path,strftime('"+helper.getTimeFormatString()+"', backuptime) AS backuptime FROM backups WHERE clientid=? ORDER BY backuptime DESC");
			q->Bind(t_clientid);
			res=q->Read();
			q->Reset();
		}

		std::string backupfolder=getBackupFolder(db);

		if(backupfolder.empty())
		{
			return false;
		}

		bool can_restore = false;
		bool restore_server_confirms;
		if (ServerStatus::canRestore(clientname, restore_server_confirms) )
		{
			can_restore = true;

			if (fileaccesstokens)
			{
				ServerSettings clientsettings(db, t_clientid);
				if (!clientsettings.getSettings()->allow_file_restore)
				{
					can_restore = false;
				}
			}
		}

		std::string path;
		std::vector<std::string> t_path;
		Tokenize(u_path, t_path, "/");

		bool is_file=false;
		bool has_access=false;

		JSON::Array ret_files;
		for(size_t k=0;k<res.size();++k)
		{
			std::string backuppath=res[k]["path"];

			if( !fileaccesstokens || checkBackupTokens(*fileaccesstokens, backupfolder, clientname, backuppath) )
			{
				SPathInfo path_info = get_metadata_path_with_tokens(u_path, fileaccesstokens, clientname, backupfolder, backupid, backuppath);

				if(fileaccesstokens && !path_info.can_access_path)
				{
					continue;
				}

				has_access=true;

				is_file = path_info.is_file;

				std::vector<std::string> tokens;
				if(fileaccesstokens)
				{	
					Tokenize(*fileaccesstokens, tokens, ";");
				}

				ret.set("clientname", clientname);
				ret.set("clientid", t_clientid);							
				ret.set("path", u_path);
				
				if(can_restore)
				{
					ret.set("can_restore", true);
					if(restore_server_confirms)
					{
						ret.set("server_confirms_restore", true);
					}
				}
				

				if(backupid)
				{
					std::string full_path = path_info.full_path;
					std::string full_metadata_path = path_info.full_metadata_path;
					std::string fn_filter;

					if(is_file)
					{
						fn_filter = ExtractFileName(path_info.full_path, os_file_sep());
						if(!fn_filter.empty())
						{
							full_path = ExtractFilePath(path_info.full_path, os_file_sep());
							full_metadata_path = ExtractFilePath(path_info.full_metadata_path, os_file_sep());
						}
					}

					std::vector<SFile> tfiles=getFiles(os_file_prefix(full_path), NULL);
					std::vector<FileMetadata> tmetadata=getMetadata(full_metadata_path, tfiles, path.empty());

					JSON::Array files;
					for(size_t i=0;i<tfiles.size();++i)
					{
						if(!fn_filter.empty() && tfiles[i].name!=fn_filter)
						{
							continue;
						}

						if(tfiles[i].isdir)
						{
							if(path.empty() && (tfiles[i].name==".hashes" || tfiles[i].name=="user_views" || next(tfiles[i].name, 0, ".symlink_") ) )
								continue;

							if(fileaccesstokens && 
								!checkFileToken(path_info.backup_tokens.tokens, tokens, tmetadata[i]))
							{
								continue;
							}

							JSON::Object obj;
							obj.set("name", tfiles[i].name);
							obj.set("dir", tfiles[i].isdir);
							obj.set("mod", tmetadata[i].last_modified);
							obj.set("creat", tmetadata[i].created);
							obj.set("access", tmetadata[i].accessed);
							files.add(obj);
						}
					}
					for(size_t i=0;i<tfiles.size();++i)
					{
						if(!fn_filter.empty() && tfiles[i].name!=fn_filter)
						{
							continue;
						}

						if(!tfiles[i].isdir)
						{
							if(path.empty() && (tfiles[i].name==".urbackup_tokens.properties" || next(tfiles[i].name, 0, ".symlink_")) )
								continue;

							if(fileaccesstokens && 
								!checkFileToken(path_info.backup_tokens.tokens, tokens, tmetadata[i]))
							{
								continue;
							}

							JSON::Object obj;
							obj.set("name", tfiles[i].name);
							obj.set("dir", tfiles[i].isdir);
							obj.set("size", tfiles[i].size);
							obj.set("mod", tmetadata[i].last_modified);
							obj.set("creat", tmetadata[i].created);
							obj.set("access", tmetadata[i].accessed);
							if(!tmetadata[i].shahash.empty())
							{
								obj.set("shahash", base64_encode(reinterpret_cast<const unsigned char*>(tmetadata[i].shahash.c_str()), static_cast<unsigned int>(tmetadata[i].shahash.size())));
							}
							files.add(obj);
						}
					}

					ret.set("files", files);
				}
				else
				{
					std::auto_ptr<IFile> f;

					if(path_info.is_file)
					{
						f.reset(Server->openFile(os_file_prefix(path_info.full_path), MODE_READ));
					}

					if( (path_info.is_file && f.get()) || os_directory_exists(os_file_prefix(path_info.full_path)) )
					{
						FileMetadata metadata = getMetaData(path_info.full_metadata_path, path_info.is_file);

						JSON::Object obj;
						obj.set("name", ExtractFileName(path_info.full_path));
						if(path_info.is_file)
						{
							obj.set("size", f->Size());
							obj.set("dir", false);
						}
						else
						{
							obj.set("dir", true);
						}
						obj.set("mod", metadata.last_modified);
						obj.set("creat", metadata.created);
						obj.set("access", metadata.accessed);
                        obj.set("backupid", watoi(res[k]["id"])+backupid_offset);
						obj.set("backuptime", watoi64(res[k]["backuptime"]));
						if(!metadata.shahash.empty())
						{
							obj.set("shahash", base64_encode(reinterpret_cast<const unsigned char*>(metadata.shahash.c_str()), static_cast<unsigned int>(metadata.shahash.size())));
						}
						ret_files.add(obj);
					}								
				}
			}
		}

		if(!backupid)
		{
			ret.set("single_item", true);
			ret.set("is_file", is_file);
			ret.set("files", ret_files);
		}

		return has_access;
	}

	JSON::Array get_backup_images(IDatabase * db, int t_clientid, std::string clientname, int backupid_offset)
	{
		std::string backupfolder = getBackupFolder(db);

		Helper helper(Server->getThreadID(), NULL, NULL);

		IQuery *q = db->Prepare("SELECT id, strftime('" + helper.getTimeFormatString() + "', backuptime) AS t_backuptime, incremental, size_bytes, archived, archive_timeout, path, letter, delete_pending FROM backup_images WHERE complete=1 AND clientid=? ORDER BY backuptime DESC");
		q->Bind(t_clientid);
		db_results res = q->Read();
		JSON::Array backups;

		for (size_t i = 0; i<res.size(); ++i)
		{
			JSON::Object obj;
			obj.set("id", watoi(res[i]["id"]) + backupid_offset);
			obj.set("backuptime", watoi64(res[i]["t_backuptime"]));
			obj.set("incremental", watoi(res[i]["incremental"]));
			obj.set("size_bytes", watoi64(res[i]["size_bytes"]));
			obj.set("letter", res[i]["letter"]);
			int archived = watoi(res[i]["archived"]);
			obj.set("archived", watoi(res[i]["archived"]));
			if (res[i]["delete_pending"] == "1")
			{
				obj.set("delete_pending", true);
			}			
			_i64 archive_timeout = 0;
			if (!res[i]["archive_timeout"].empty())
			{
				archive_timeout = watoi64(res[i]["archive_timeout"]);
				if (archive_timeout != 0)
				{
					archive_timeout -= Server->getTimeSeconds();
				}
			}
			if (archived != 0)
			{
				obj.set("archive_timeout", archive_timeout);
			}
			backups.add(obj);
		}

		return backups;
	}

	JSON::Object get_image_info(IDatabase* db, int backupid, int t_clientid, int backupid_offset, std::string& path,
		std::vector<IFSImageFactory::SPartition>& partitions)
	{
		Helper helper(Server->getThreadID(), NULL, NULL);

		IQuery *q = db->Prepare("SELECT id, strftime('" + helper.getTimeFormatString() + "', backuptime) AS t_backuptime, incremental, "
			"size_bytes, archived, archive_timeout, path, letter FROM backup_images WHERE complete=1 AND clientid=? AND id=?");
		q->Bind(t_clientid);
		q->Bind(backupid);
		db_results res = q->Read();
		JSON::Object ret;
		if (!res.empty())
		{
			path = res[0]["path"];
			ret.set("id", watoi(res[0]["id"]) + backupid_offset);
			ret.set("backuptime", watoi64(res[0]["t_backuptime"]));
			ret.set("incremental", watoi(res[0]["incremental"]));
			ret.set("size_bytes", watoi64(res[0]["size_bytes"]));
			ret.set("letter", res[0]["letter"]);
			int archived = watoi(res[0]["archived"]);
			ret.set("archived", watoi(res[0]["archived"]));
			_i64 archive_timeout = 0;
			if (!res[0]["archive_timeout"].empty())
			{
				archive_timeout = watoi64(res[0]["archive_timeout"]);
				if (archive_timeout != 0)
				{
					archive_timeout -= Server->getTimeSeconds();
				}
			}
			if (archived != 0)
			{
				ret.set("archive_timeout", archive_timeout);
			}

			std::string path = res[0]["path"];
			std::string filename = ExtractFileName(path);
			std::string extension = findextension(filename);

			bool disk_image = false;
			std::auto_ptr<IFile> mbrfile(Server->openFile(os_file_prefix(path + ".mbr"), MODE_READ));
			if (mbrfile.get() && mbrfile->Size()<10*1024*1024)
			{
				std::string mbrdata_header = mbrfile->Read(0LL, 2);
				CRData mbrdata_header_view(mbrdata_header.data(), mbrdata_header.size());
				char version = 0;
				if (mbrdata_header_view.getChar(&version)
					&& mbrdata_header_view.getChar(&version)
					&& version != 100)
				{
					std::string mbrdata = mbrfile->Read(0LL, static_cast<_u32>(mbrfile->Size()));
					if (mbrdata.size() == mbrfile->Size())
					{
						CRData mbrdata_view(mbrdata.data(), mbrdata.size());
						SMBRData mbr(mbrdata_view);
						if (!mbr.hasError())
						{
							disk_image = false;
							ret.set("part_table", mbr.gpt_style ? "GPT" : "MBR");
							ret.set("disk_number", mbr.device_number);
							ret.set("partition_number", mbr.partition_number);
							ret.set("volume_name", mbr.volume_name);
							ret.set("fs_type", mbr.fsn);
							ret.set("serial_number", mbr.serial_number);
						}
					}
				}
				else if (version == 100)
				{
					disk_image = true;
				}
			}

			std::auto_ptr<IVHDFile> vhdfile;
			if (extension == "vhd"
				|| extension == "vhdz")
			{
				vhdfile.reset(image_fak->createVHDFile(path, true, 0));
			}
			else if(extension=="raw")
			{
				vhdfile.reset(image_fak->createVHDFile(path, true, 0, 2*1024*1024, false, IFSImageFactory::ImageFormat_RawCowFile));
			}
			else
			{
				assert(false);
			}

			if (vhdfile.get() != NULL)
			{
				ret.set("volume_size", vhdfile->getSize());

				if (disk_image)
				{
					bool gpt_style;
					partitions = image_fak->readPartitions(vhdfile.get(), 0, gpt_style);
					ret.set("part_table", gpt_style ? "GPT" : "MBR");
				}
			}
			
		}
		return ret;
	}

	bool get_image_files(IDatabase* db, int backupid, int t_clientid, std::string clientname,
			std::string u_path, int backupid_offset, bool do_mount, JSON::Object& ret)
	{
		std::string path;
		std::vector<IFSImageFactory::SPartition> partitions;
		JSON::Object image_backup_info = get_image_info(db, backupid, t_clientid, 
			backupid_offset, path, partitions);
		ret.set("image_backup_info", image_backup_info);
		ret.set("single_item", false);

		int partition = -1;
		if (partitions.size()>1)
		{
			std::string m_u_path = u_path;
			while (!m_u_path.empty()
				&& m_u_path[0] == '/')
			{
				m_u_path.erase(0, 1);
			}

			if (next(m_u_path, 0, "partition "))
			{
				std::string part = getbetween("partition ", "/", m_u_path);
				if (part.empty())
				{
					part = getafter("partition ", m_u_path);
				}
				partition = watoi(part)-1;
				u_path = m_u_path.substr(10+part.size());
			}
			else
			{
				JSON::Array files;
				for (size_t i = 0; i < partitions.size(); ++i)
				{
					JSON::Object obj;
					obj.set("name", "partition "+convert(i+1));
					obj.set("dir", true);
					files.add(obj);
				}
				ret.set("files", files);
				ret.set("no_zip", true);

				ret.set("backuptime", image_backup_info.get("backuptime"));
				ret.set("backupid", backupid + backupid_offset);
				return true;
			}
		}
		else
		{
			partition = 0;
		}
		
		if (!path.empty())
		{
			ScopedMountedImage mounted_image;
			bool has_mount_timeout;
			std::string mount_errmsg;
			std::string content_path = ImageMount::get_mount_path(backupid, partition,
				!u_path.empty() && u_path!="/", mounted_image,
				10000, has_mount_timeout, mount_errmsg);

			if (content_path.empty()
				|| !os_directory_exists(content_path) )
			{
				if (!do_mount)
				{
					if (BackupServer::canMountImages())
					{
						if (!has_mount_timeout)
						{
							ret.set("can_mount", true);
#ifndef __linux__
							ret.set("os_mount", true);
#endif
						}
						else
						{
							ret.set("mount_in_progress", true);
						}
					}
					else
					{
						ret.set("no_files", true);
					}
					content_path.clear();
				}
				else
				{
					content_path = ImageMount::get_mount_path(backupid, partition, true,
						mounted_image, 10000, has_mount_timeout, mount_errmsg);
					if (content_path.empty())
					{
						if (has_mount_timeout)
						{
							ret.set("mount_in_progress", true);
						}
						else
						{
							ret.set("mount_failed", true);
							if (!mount_errmsg.empty())
							{
								ret.set("mount_errmsg", mount_errmsg);
							}
						}
					}
				}
			}			
			
			JSON::Array files;

			if (!content_path.empty())
			{
				std::vector<std::string> t_path;
				Tokenize(u_path, t_path, "/");

				for (size_t i = 0; i < t_path.size(); ++i)
				{
					if (valid_path_component(t_path[i]))
					{
						content_path += os_file_sep() + t_path[i];
					}
				}

				std::vector<SFile> tfiles = getFiles(os_file_prefix(content_path), NULL);

				for (size_t i = 0; i<tfiles.size(); ++i)
				{
					if (tfiles[i].isdir)
					{
						JSON::Object obj;
						obj.set("name", tfiles[i].name);
						obj.set("dir", tfiles[i].isdir);
						obj.set("mod", tfiles[i].last_modified);
						obj.set("creat", tfiles[i].created);
						obj.set("access", tfiles[i].accessed);
						files.add(obj);
					}
				}

				for (size_t i = 0; i<tfiles.size(); ++i)
				{
					if (!tfiles[i].isdir)
					{
						JSON::Object obj;
						obj.set("name", tfiles[i].name);
						obj.set("dir", tfiles[i].isdir);
						obj.set("mod", tfiles[i].last_modified);
						obj.set("creat", tfiles[i].created);
						obj.set("access", tfiles[i].accessed);
						obj.set("size", tfiles[i].size);
						files.add(obj);
					}
				}
			}

			ret.set("files", files);
		}

	
		ret.set("backuptime", image_backup_info.get("backuptime"));
		ret.set("backupid", backupid + backupid_offset);
		return true;
	}
} //namespace backupaccess

namespace
{
	bool removeImageBackup(int backupid)
	{
		bool result = false;
		Server->getThreadPool()->executeWait(new ServerCleanupThread(CleanupAction(backupid, false, &result)));
		return result;
	}

	std::string deleteOrMarkImageBackup(IDatabase* db, int backupid, int clientid, bool mark_only)
	{
		ServerCleanupDao cleanup_dao(db);
		if (cleanup_dao.getImageClientId(backupid).value != clientid)
		{
			return "wrong_clientid";
		}

		if (ServerCleanupThread::findArchivedImageRef(&cleanup_dao, backupid))
		{
			return "archived_ref";
		}

		if (!mark_only)
		{
			if (ServerCleanupThread::isImageLockedFromCleanup(backupid))
			{
				return "image_locked";
			}

			if (ServerCleanupThread::findLockedImageRef(&cleanup_dao, backupid))
			{
				return "locked_ref";
			}

			if (ServerCleanupThread::findUncompleteImageRef(&cleanup_dao, backupid))
			{
				return "incomplete_ref";
			}
		}

		IQuery *q = db->Prepare("UPDATE backup_images SET delete_pending=1 WHERE id=? AND clientid=?");

		std::vector<ServerCleanupDao::SImageRef> refs = cleanup_dao.getImageRefs(backupid);
		for (size_t i = 0; i < refs.size(); ++i)
		{
			std::vector<ServerCleanupDao::SImageRef> new_refs = cleanup_dao.getImageRefs(refs[i].id);
			refs.insert(refs.end(), new_refs.begin(), new_refs.end());

			if (mark_only)
			{
				q->Bind(refs[i].id);
				q->Bind(clientid);
				q->Write();
				q->Reset();
			}
			else
			{
				if (!removeImageBackup(refs[i].id))
				{
					return "remove_image_failed";
				}
			}
		}

		std::vector<int> assoc_images = cleanup_dao.getAssocImageBackups(backupid);
		for (size_t i = 0; i < assoc_images.size(); ++i)
		{
			if (mark_only)
			{
				q->Bind(assoc_images[i]);
				q->Bind(clientid);
				q->Write();
				q->Reset();
			}
			else
			{
				if (!removeImageBackup(assoc_images[i]))
				{
					return "remove_image_failed";
				}
			}
		}

		if (mark_only)
		{
			q->Bind(backupid);
			q->Bind(clientid);
			q->Write();
			q->Reset();
		}
		else
		{
			if (!removeImageBackup(backupid))
			{
				return "remove_image_failed";
			}
		}

		return std::string();
	}

	void unmarkImageBackup(IDatabase* db, int backupid, int clientid)
	{
		ServerCleanupDao cleanup_dao(db);
		if (cleanup_dao.getImageClientId(backupid).value != clientid)
		{
			return;
		}

		IQuery *q = db->Prepare("UPDATE backup_images SET delete_pending=0 WHERE id=? AND clientid=?");

		std::vector<int> assoc_images = cleanup_dao.getAssocImageBackups(backupid);
		for (size_t i = 0; i < assoc_images.size(); ++i)
		{
			q->Bind(assoc_images[i]);
			q->Bind(clientid);
			q->Write();
			q->Reset();
		}

		assoc_images = cleanup_dao.getAssocImageBackupsReverse(backupid);
		for (size_t i = 0; i < assoc_images.size(); ++i)
		{
			q->Bind(assoc_images[i]);
			q->Bind(clientid);
			q->Write();
			q->Reset();
		}

		std::vector<ServerCleanupDao::SImageRef> refs = cleanup_dao.getImageRefsReverse(backupid);
		for (size_t i = 0; i < refs.size(); ++i)
		{
			std::vector<ServerCleanupDao::SImageRef> new_refs = cleanup_dao.getImageRefsReverse(refs[i].id);
			refs.insert(refs.end(), new_refs.begin(), new_refs.end());

			q->Bind(refs[i].id);
			q->Bind(clientid);
			q->Write();
			q->Reset();
		}

		q->Bind(backupid);
		q->Bind(clientid);
		q->Write();
		q->Reset();
	}

	void archiveBackup(IDatabase* db, int t_clientid, int backupid, int archive)
	{
		std::string tbl = "backups";
		std::vector<int> toarchive;
		if (backupid < 0)
		{
			tbl = "backup_images";
			backupid *= -1;

			ServerCleanupDao cleanupdao(db);
			std::vector<int> assoc_images = cleanupdao.getAssocImageBackups(backupid);
			toarchive.insert(toarchive.end(), assoc_images.begin(), assoc_images.end());
			assoc_images = cleanupdao.getAssocImageBackupsReverse(backupid);
			toarchive.insert(toarchive.end(), assoc_images.begin(), assoc_images.end());
		}
		toarchive.push_back(backupid);
		IQuery *q = db->Prepare("UPDATE " + tbl + " SET archived="+convert(archive)+", archive_timeout=0 WHERE id=? AND clientid=?");
		for (size_t i = 0; i < toarchive.size(); ++i)
		{
			q->Bind(toarchive[i]);
			q->Bind(t_clientid);
			q->Write();
			q->Reset();
		}
	}
} // unnamed namespace

ACTION_IMPL(backups)
{
	str_map& CURRP = GET.find("ses")==GET.end()? POST : GET;

	Helper helper(tid, &CURRP, &PARAMS);
	JSON::Object ret;
	SUser *session=helper.getSession();

	bool has_tokens = CURRP.find("tokens0")!=CURRP.end();

	bool token_authentication=false;
	std::string fileaccesstokens;
	if( (session==NULL || session->id==SESSION_ID_TOKEN_AUTH) && has_tokens)
	{
		token_authentication=true;
		fileaccesstokens = backupaccess::decryptTokens(helper.getDatabase(), CURRP);

		if(fileaccesstokens.empty())
		{
			JSON::Object err_ret;
			err_ret.set("err", "access_denied");
			err_ret.set("errcode", "token_decryption_failed");
			helper.Write(err_ret.stringify(false));
			return;
		}
		else
		{
			std::string ses=helper.generateSession("anonymous");
			ret.set("session", ses);
			CURRP["ses"]=ses;
			helper.update(tid, &CURRP, &PARAMS);
			if(helper.getSession())
			{
				helper.getSession()->mStr["fileaccesstokens"]=fileaccesstokens;
				helper.getSession()->id = SESSION_ID_TOKEN_AUTH;
			}
		}
	}
	else if(session!=NULL && session->id==SESSION_ID_TOKEN_AUTH)
	{
		fileaccesstokens = session->mStr["fileaccesstokens"];
		if(!fileaccesstokens.empty())
		{
			token_authentication=true;
		}
		else
		{
			JSON::Object err_ret;
			err_ret.set("err", "access_denied");
			err_ret.set("errcode", "token_not_in_session");
			helper.Write(err_ret.stringify(false));
			return;
		}
	}

	if(token_authentication)
	{
		ret.set("token_authentication", true);
	}

	std::string sa=CURRP["sa"];
	std::string rights=helper.getRights("browse_backups");
	std::string archive_rights=helper.getRights("manual_archive");
	std::string delete_rights = helper.getRights("delete_backups");
	std::vector<int> clientid;
	if(rights!="tokens")
	{
		clientid = helper.getRightIDs(rights);
	}
	std::vector<int> clientid_archive=helper.getRightIDs(archive_rights);
	std::vector<int> clientid_delete = helper.getRightIDs(delete_rights);
	if(clientid.size()==1 && sa.empty() )
	{
		sa="backups";
		CURRP["clientid"]=convert(clientid[0]);
	}
	if( (session!=NULL && rights!="none" ) || token_authentication)
	{
		IDatabase *db=helper.getDatabase();
		if(sa.empty())
		{
			std::string qstr = "SELECT id, name, strftime('"+helper.getTimeFormatString()+"', lastbackup) AS lastbackup FROM clients";
			if(token_authentication)
			{
				if(CURRP.find("clientname")==CURRP.end())
				{
					std::vector<std::string> tokens;
					Tokenize(fileaccesstokens, tokens, ";");
					IQuery* q_clients = db->Prepare("SELECT clientid FROM tokens_on_client WHERE token=?");
					for(size_t i=0;i<tokens.size();++i)
					{
						q_clients->Bind(tokens[i]);
						db_results res = q_clients->Read();
						q_clients->Reset();

						if(!res.empty())
						{
							int n_clientid = watoi(res[0]["clientid"]);
							if(std::find(clientid.begin(), clientid.end(), n_clientid)==
								clientid.end())
							{
								clientid.push_back(n_clientid);
							}
						}
					}

					if(!clientid.empty())
					{
						qstr+=" WHERE "+backupaccess::constructFilter(clientid, "id");
					}
					else
					{
						sa="backups";
					}
				}
				else
				{
					sa="backups";
				}				
			}
			else
			{
				if(!clientid.empty())
				{
					qstr+=" WHERE "+backupaccess::constructFilter(clientid, "id");
				}
			}

			qstr+=" ORDER BY name";

			if(sa!="backups")
			{
				IQuery *q=db->Prepare(qstr);
				db_results res=q->Read();
				q->Reset();
				JSON::Array clients;
				for(size_t i=0;i<res.size();++i)
				{
					JSON::Object obj;
					obj.set("name", res[i]["name"]);
					obj.set("id", watoi(res[i]["id"]));
					obj.set("lastbackup", watoi64(res[i]["lastbackup"]));
					clients.add(obj);
				}
				ret.set("clients", clients);
			}			
		}
		if(sa=="backups")
		{
			int t_clientid;
			std::string clientname;

			if(token_authentication && CURRP.find("clientid")==CURRP.end())
			{
				clientname=CURRP["clientname"];
				t_clientid = getClientid(helper.getDatabase(), clientname);
				if(t_clientid==-1)
				{
					ret.set("error", "2");
                    helper.Write(ret.stringify(false));
					return;
				}
			}
			else
			{
				t_clientid=watoi(CURRP["clientid"]);

				clientname = getClientname(helper.getDatabase(), t_clientid);
			}

			bool r_ok=token_authentication?false:helper.hasRights(t_clientid, rights, clientid);
			bool archive_ok=token_authentication?false:helper.hasRights(t_clientid, archive_rights, clientid_archive);
			bool delete_ok= token_authentication ? false : helper.hasRights(t_clientid, delete_rights, clientid_delete);

			if(r_ok || token_authentication)
			{
				if(archive_ok)
				{
					std::string tbl = "backups";				
					if(CURRP.find("archive")!=CURRP.end())
					{
						archiveBackup(db, t_clientid, watoi(CURRP["archive"]), 1);
					}
					else if(CURRP.find("unarchive")!=CURRP.end())
					{
						archiveBackup(db, t_clientid, watoi(CURRP["unarchive"]), 0);
					}
				}

				if (delete_ok)
				{
					if (CURRP.find("delete") != CURRP.end())
					{
						int delete_id = watoi(CURRP["delete"]);
						
						if (delete_id < 0)
						{
							delete_id *= -1;
							std::string err = deleteOrMarkImageBackup(db, delete_id, t_clientid, true);
							if (!err.empty())
							{
								ret.set("delete_err", err);
							}
						}
						else
						{
							IQuery *q = db->Prepare("UPDATE backups SET delete_pending=1 WHERE id=? AND clientid=?");
							q->Bind(delete_id);
							q->Bind(t_clientid);
							q->Write();
							q->Reset();
						}
					}
					if (CURRP.find("stop_delete") != CURRP.end())
					{
						int stop_delete_id = watoi(CURRP["stop_delete"]);
						if (stop_delete_id < 0)
						{
							stop_delete_id *= -1;
							unmarkImageBackup(db, stop_delete_id, t_clientid);
						}
						else
						{
							IQuery *q = db->Prepare("UPDATE backups SET delete_pending=0 WHERE id=? AND clientid=?");
							q->Bind(stop_delete_id);
							q->Bind(t_clientid);
							q->Write();
						}
					}
					if (CURRP.find("delete_now") != CURRP.end())
					{
						int delete_now_id = watoi(CURRP["delete_now"]);
						if (delete_now_id < 0)
						{
							delete_now_id *= -1;
							std::string err = deleteOrMarkImageBackup(db, delete_now_id, t_clientid, false);
							if (!err.empty())
							{
								ret.set("delete_now_err", err);
							}
						}
						else
						{
							ServerSettings settings(db);
							bool result = false;
							Server->getThreadPool()->executeWait(new ServerCleanupThread(CleanupAction(settings.getSettings()->backupfolder, t_clientid, delete_now_id, false, &result)));
							if (!result)
							{
								ret.set("delete_now_err", "delete_file_backup_failed");
							}
						}
					}
				}

				bool has_access;
				JSON::Array backups = backupaccess::get_backups_with_tokens(db, t_clientid, clientname,
					token_authentication ? &fileaccesstokens : NULL, 0, has_access);

				if (!has_access)
				{
					JSON::Object err_ret;
					err_ret.set("err", "access_denied");
					err_ret.set("errcode", "backups_access_denied");
					helper.Write(err_ret.stringify(false));
					return;
				}

				ret.set("backups", backups);

				if (r_ok)
				{
					ret.set("backup_images", backupaccess::get_backup_images(db, t_clientid, clientname, 0));
				}
				else
				{
					JSON::Array empty;
					ret.set("backup_images", empty);
				}

				ret.set("can_archive", archive_ok);
				ret.set("can_delete", delete_ok);

				ret.set("clientname", clientname);
				ret.set("clientid", t_clientid);
			}
			else
			{
				ret.set("error", 2);
			}
		}
		else if(sa=="files" || sa=="filesdl" || sa=="zipdl" || sa=="clientdl" )
		{
			int t_clientid;
			std::string clientname;
			if (token_authentication
				&& CURRP.find("backupid") != CURRP.end()
				&& watoi(CURRP["backupid"]) < 0)
			{
				ret.set("error", "2");
				helper.Write(ret.stringify(false));
				return;
			}

			if(token_authentication && CURRP.find("clientid")==CURRP.end())
			{
				clientname=CURRP["clientname"];
				t_clientid = getClientid(helper.getDatabase(), clientname);
				if(t_clientid==-1)
				{
					ret.set("error", "2");
                    helper.Write(ret.stringify(false));
					return;
				}
			}
			else
			{
				t_clientid = watoi(CURRP["clientid"]);
				clientname = getClientname(helper.getDatabase(), t_clientid);
			}
			bool r_ok = token_authentication ? true : 
				helper.hasRights(t_clientid, rights, clientid);

			if(r_ok)
			{
				bool has_backupid=CURRP.find("backupid")!=CURRP.end();
				int backupid=0;
				if(has_backupid)
				{
					backupid=watoi(CURRP["backupid"]);
				}
				std::string u_path=UnescapeHTML(UnescapeSQLString(CURRP["path"]));

				if (backupid < 0
					&& token_authentication)
				{
					clientname.clear();
				}

				if(!clientname.empty() )
				{
					if( (sa=="filesdl" || sa=="zipdl" || sa=="clientdl") && has_backupid)
					{
						std::string backupfolder = backupaccess::getBackupFolder(db);

						if (backupfolder.empty())
						{
							return;
						}

						std::string backuppath;
						backupaccess::SPathInfo path_info;
						ScopedMountedImage mounted_image;
						if (backupid >= 0)
						{
							backuppath = backupaccess::get_backup_path(db, backupid, t_clientid);
							path_info = backupaccess::get_metadata_path_with_tokens(u_path, token_authentication ? &fileaccesstokens : NULL,
								clientname, backupfolder, has_backupid ? &backupid : NULL, backuppath);
						}
						else
						{
							bool has_mount_timeout;
							std::string mount_errmsg;
							backuppath = ImageMount::get_mount_path(-1*backupid, 0, true, mounted_image, -1, has_mount_timeout, mount_errmsg);
							path_info = backupaccess::get_image_path_info(u_path, clientname, backupfolder, backupid, backuppath);
						}

						if(token_authentication && !path_info.can_access_path)
						{
							JSON::Object err_ret;
							err_ret.set("err", "access_denied");
							err_ret.set("errcode", "path_dl_access_denied");
							helper.Write(err_ret.stringify(false));
							return;
						}

						std::vector<std::string> tokens;
						if(token_authentication)
						{
							Tokenize(fileaccesstokens, tokens, ";");
						}

						if(sa=="filesdl")
						{
							if(!token_authentication || checkFileToken(fileaccesstokens, backupfolder, clientname, backuppath, path_info.full_metadata_path))
							{
								sendFile(helper, path_info.full_path);
							}
							return;
						}
						else if(sa=="zipdl")
						{
							std::string bpath = backupfolder + os_file_sep() + clientname + os_file_sep() + backuppath;
							sendZip(helper, bpath, path_info.full_path, backupid<0 ? "" : bpath + os_file_sep()+".hashes",
								path_info.full_metadata_path, CURRP["filter"], token_authentication,
								path_info.backup_tokens.tokens, tokens, backupid<0 ? false : path_info.rel_path.empty());
							return;
						}
						else if(sa=="clientdl" && fileserv!=NULL)
						{
							if(ServerStatus::getStatus(clientname).comm_pipe==NULL)
							{
								ret.set("err", "client_not_online");
                                helper.Write(ret.stringify(false));
								return;
							}

							int64 restore_id;
							size_t status_id;
							logid_t log_id;
							THREADPOOL_TICKET ticket;
							int64 restore_flags = 0;
							if (!token_authentication)
							{
								restore_flags |= restore_flag_ignore_permissions;
							}

							if(!create_clientdl_thread(clientname, t_clientid, t_clientid, path_info.full_path, path_info.full_metadata_path, CURRP["filter"],
								path_info.rel_path.empty(), path_info.rel_path, restore_id, status_id, log_id, std::string(),
								std::vector< std::pair<std::string, std::string> >(), true, true, greplace(os_file_sep(), "/", path_info.rel_path), true,
								restore_flags, ticket, tokens, path_info.backup_tokens))
							{
								ret.set("err", "internal_error");
                                helper.Write(ret.stringify(false));
								return;
							}

							if (session != NULL)
							{
								std::string wait_key = ServerSettings::generateRandomAuthKey();
								session->mStr[wait_key] = convert(ticket);
								ret.set("wait_key", wait_key);
							}

							ret.set("ok", "true");
                            helper.Write(ret.stringify(false));
							return;
						}
					}
					else
					{
						if (has_backupid && backupid < 0)
						{
							if (!backupaccess::get_image_files(db, -1 * backupid, t_clientid, clientname, u_path, 0, CURRP["mount"]=="1", ret))
							{
								JSON::Object err_ret;
								err_ret.set("err", "internal_error");
								helper.Write(err_ret.stringify(false));
								return;
							}
						}
						else
						{
							if (!backupaccess::get_files_with_tokens(db, has_backupid ? &backupid : NULL, t_clientid, clientname, token_authentication ? &fileaccesstokens : NULL,
								u_path, 0, ret))
							{
								JSON::Object err_ret;
								err_ret.set("err", "access_denied");
								err_ret.set("errcode", "path_browse_access_denied");
								helper.Write(err_ret.stringify(false));
								return;
							}
						}

						ret.set("clientname", clientname);
						ret.set("clientid", t_clientid);							
						ret.set("path", u_path);
					}
				}
			}
		}
	}
	else
	{
		ret.set("error", 1);
	}

    helper.Write(ret.stringify(false));
}
