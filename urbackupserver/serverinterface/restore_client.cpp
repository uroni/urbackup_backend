#include "restore_client.h"
#include "../../Interface/Thread.h"
#include "../../fileservplugin/IFileServ.h"
#include "../../Interface/File.h"
#include "../../Interface/Server.h"
#include "../server_settings.h"
#include "../ClientMain.h"
#include <algorithm>
#include "../../urbackupcommon/file_metadata.h"
#include "backups.h"
#include "../../urbackupcommon/filelist_utils.h"
#include "../../common/data.h"
#include "../database.h"

extern IFileServ* fileserv;


namespace
{
	class MetadataCallback : public IFileServ::IMetadataCallback
	{
	public:
		MetadataCallback(const std::wstring& basedir)
			: basedir(basedir)
		{

		}

		virtual IFile* getMetadata( const std::string& path, std::string& orig_path, int64& offset, int64& length )
		{
			if(path.empty()) return NULL;

			std::wstring metadata_path = basedir;

			std::vector<std::string> path_segments;
			TokenizeMail(path.substr(1), path_segments, "/");

			bool isdir = path[0]=='d';

			for(size_t i=0;i<path_segments.size();++i)
			{
				if(path_segments[i]=="." || path_segments[i]=="..")
				{
					continue;
				}

				metadata_path+=os_file_sep() + escape_metadata_fn(Server->ConvertToUnicode(path_segments[i]));

				if(i==path_segments.size()-1 && isdir)
				{
					metadata_path+=os_file_sep() + metadata_dir_fn;
				}
			}

			IFile* metadata_file = Server->openFile(os_file_prefix(metadata_path), MODE_READ);

			if(metadata_file==NULL)
			{
				return metadata_file;
			}

			FileMetadata metadata;

			if(!read_metadata(metadata_file, metadata))
			{
				delete metadata_file;
				return NULL;
			}

			orig_path = metadata.orig_path;
			offset = os_metadata_offset(metadata_file);
			length = metadata_file->Size() - offset;

			return metadata_file;
		}

	private:
		std::wstring basedir;
	};

	class ClientDownloadThread : public IThread
	{
	public:
		ClientDownloadThread(const std::wstring& clientname, int clientid, IFile* filelist_f, const std::wstring& foldername,
			const std::wstring& hashfoldername, const std::wstring& filter,
			bool token_authentication,
			const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_special_root,
			const std::wstring& folder_log_name)
			: clientname(clientname), clientid(clientid), filelist_f(filelist_f), foldername(foldername), hashfoldername(hashfoldername),
			token_authentication(token_authentication), backup_tokens(backup_tokens),
			tokens(tokens), skip_special_root(skip_special_root), folder_log_name(folder_log_name)
		{
			TokenizeMail(filter, filter_fns, L"/");
		}

		void operator()()
		{
			if(!createFilelist(foldername, hashfoldername, 0, skip_special_root))
			{
				return;
			}

			IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
			ServerBackupDao backup_dao(db);

			std::string identity = ServerSettings::generateRandomAuthKey(25);
			backup_dao.addRestore(clientid, folder_log_name, widen(identity));
			int64 restore_id = db->getLastInsertID();
			size_t status_id = ServerStatus::startProcess(clientname, sa_restore);

			logid_t log_id = ServerLogger::getLogId(clientid);

			fileserv->addIdentity(identity);
			fileserv->shareDir(L"clientdl_filelist", filelist_f->getFilenameW(), identity);
			ClientMain::addShareToCleanup(clientid, SShareCleanup("urbackup", identity, true, false));

			delete filelist_f;

			MetadataCallback* callback = new MetadataCallback(hashfoldername);
			fileserv->shareDir(L"clientdl", foldername, identity);
			ClientMain::addShareToCleanup(clientid, SShareCleanup("clientdl", identity, false, true));
			fileserv->registerMetadataCallback(L"clientdl", identity, callback);


			CWData data;
			data.addBuffer("RESTORE", 7);
			data.addString(identity);
			data.addInt64(restore_id);
			data.addUInt64(status_id);
			data.addInt64(log_id.first);

			std::string msg(data.getDataPtr(), data.getDataPtr()+data.getDataSize());
			ServerStatus::sendToCommPipe(clientname, msg);
		}

		bool writeFile(const std::wstring& data)
		{
			std::string towrite = Server->ConvertToUTF8(data);
			return filelist_f->Write(towrite)==towrite.size();
		}


		bool createFilelist(const std::wstring& foldername, const std::wstring& hashfoldername, size_t depth, bool skip_special)
		{
			bool has_error=false;
			const std::vector<SFile> files = getFiles(foldername, &has_error, true);

			if(has_error)
				return false;

			for(size_t i=0;i<files.size();++i)
			{
				const SFile& file=files[i];

				if(depth==0 && (!filter_fns.empty() && std::find(filter_fns.begin(), filter_fns.end(), file.name)==filter_fns.end()) )
				{
					continue;
				}

				if(skip_special
					&& (file.name==L".hashes" || file.name==L"user_views") )
				{
					continue;
				}

				std::wstring metadataname = hashfoldername + os_file_sep() + escape_metadata_fn(file.name);
				std::wstring filename = foldername + os_file_sep() + file.name;

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

				std::string extra;

				std::string fn_utf8 = Server->ConvertToUTF8(file.name);

				if(!metadata.orig_path.empty() &&
					(depth==0 || metadata.orig_path.find(fn_utf8)!=metadata.orig_path.size()-fn_utf8.size()))
				{
					extra="&orig_path="+base64_encode_dash(metadata.orig_path);
				}

				if(!metadata.shahash.empty())
				{
					extra+="&shahash="+base64_encode_dash(metadata.shahash);
				}

				writeFileItem(filelist_f, file, metadata, extra);

				if(file.isdir)
				{
					return createFilelist(filename, hashfoldername + os_file_sep() + escape_metadata_fn(file.name), depth+1, false);
				}
			}

			return true;
		}

	private:
		std::wstring clientname;
		int clientid;
		IFile* filelist_f;
		std::wstring foldername;
		std::wstring hashfoldername;
		std::vector<std::wstring> filter_fns;
		bool token_authentication;
		std::vector<SToken> backup_tokens;
		std::vector<std::string> tokens;
		bool skip_special_root;
		std::wstring folder_log_name;
	};
}

bool create_clientdl_thread(const std::wstring& clientname, int clientid, const std::wstring& foldername, const std::wstring& hashfoldername,
	const std::wstring& filter, bool token_authentication, const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes,
	const std::wstring& folder_log_name)
{
	IFile* filelist_f = Server->openTemporaryFile();

	if(filelist_f==NULL)
	{
		return false;
	}

	Server->getThreadPool()->execute(new ClientDownloadThread(clientname, clientid, filelist_f, foldername, hashfoldername, filter, token_authentication, backup_tokens, tokens, skip_hashes, folder_log_name));

	return true;
}

