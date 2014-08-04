#pragma once

#include "server_get.h"
#include "../Interface/Thread.h"
#include <string>
#include <vector>
#include "../Interface/Mutex.h"
#include "../Interface/Server.h"
#include "../Interface/Condition.h"
#include "../common/data.h"
#include "../urbackupcommon/change_ids.h"
#include <algorithm>
#include "../stringtools.h"
#include "server_settings.h"

extern std::string server_identity;

class BackupServerContinuous : public IThread
{
public:
	struct SSequence
	{
		int64 id;
		int64 next;
		bool updated;

		bool operator==(const SSequence& other)
		{
			return id==other.id && next==other.next;
		}
	};

	struct SChange
	{
		SChange()
		{

		}

		SChange(char action, std::string fn1)
			: action(action), fn1(fn1)
		{

		}

		char action;
		std::string fn1;
		std::string fn2;

		bool operator==(const SChange& other)
		{
			return action==other.action
				&& fn1==other.fn1
				&& fn2==other.fn2;
		}
	};

	BackupServerContinuous(BackupServerGet* server_get, const std::wstring& continuous_path, const std::wstring& continuous_hash_path, const std::wstring& continuous_path_backup,
		const std::wstring& tmpfile_path, bool use_tmpfiles, int clientid, const std::wstring& clientname, int backupid, bool use_snapshots, bool use_reflink)
		: server_get(server_get), collect_only(true), first_compaction(true), stop(false), continuous_path(continuous_path), continuous_hash_path(continuous_hash_path),
		continuous_path_backup(continuous_path_backup),
		tmpfile_path(tmpfile_path), use_tmpfiles(use_tmpfiles), clientid(clientid), clientname(clientname), backupid(backupid),
		use_snapshots(use_snapshots), use_reflink(use_reflink)
	{
		mutex = Server->createMutex();
		cond = Server->createCondition();

		local_hash.reset(new BackupServerHash(NULL, clientid, use_snapshots, use_reflink, use_tmpfiles));
	}

	~BackupServerContinuous()
	{
		Server->destroy(mutex);
	}

	void operator()()
	{
		server_settings.reset(new ServerSettings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER)));

		hashed_transfer_full = true;
		hashed_transfer_incr = true;
		transfer_incr_blockdiff = true;

		if(server_get->isOnInternetConnection())
		{
			hashed_transfer_full = server_settings->getSettings()->internet_full_file_transfer_mode!="raw";
			hashed_transfer_incr = server_settings->getSettings()->internet_incr_file_transfer_mode!="raw";
			transfer_incr_blockdiff = server_settings->getSettings()->internet_incr_file_transfer_mode=="blockhash";
		}
		else
		{
			hashed_transfer_full = server_settings->getSettings()->local_full_file_transfer_mode!="raw";
			hashed_transfer_incr = server_settings->getSettings()->local_incr_file_transfer_mode!="raw";
			transfer_incr_blockdiff = server_settings->getSettings()->local_incr_file_transfer_mode=="blockhash";
		}

		if(server_settings->getSettings()->internet_full_file_transfer_mode=="raw")

		while(true)
		{
			{
				IScopedLock lock(mutex);

				if(stop)
				{
					return;
				}

				while(changes.empty())
				{
					cond->wait(&lock);
					if(stop)
					{
						return;
					}
				}

				if(!collect_only && !compactChanges())
				{
					server_get->sendToPipe("RESYNC");
					return;
				}
			}

			if(!compacted_changes.empty())
			{
				for(size_t i=0;i<compacted_changes.size();++i)
				{
					queueChange(compacted_changes[i]);
				}
			}

			compacted_changes.clear();
		}
	}

	void addChanges(const std::string& change_data)
	{
		IScopedLock lock(mutex);

		changes.push_back(change_data);

		cond->notify_all();
	}

	void setSequences(std::vector<BackupServerContinuous::SSequence> new_sequences)
	{
		IScopedLock lock(mutex);

		sequences=new_sequences;
	}

	void startExecuting()
	{
		IScopedLock lock(mutex);

		collect_only=false;

		cond->notify_all();
	}

	void doStop()
	{
		IScopedLock lock(mutex);
		stop=true;
		cond->notify_all();
	}

private:

	struct SQueueItem
	{
		bool queued_metdata;
		bool queued_dl;
		SChange change;
		std::string out_fn;
		std::string out_hash_fn;
	};

	bool compactChanges()
	{
		for(size_t i=0;i<changes.size();++i)
		{
			compactChanges(changes[i]);
		}

		first_compaction=false;
	}

	bool compactChanges(const std::string& changes)
	{
		CRData data(&changes);

		char id;

		if(!data.getChar(&id) || id!=CHANGE_HEADER)
		{
			return false;
		}

		bool skip=false;
		if(!checkHeaderAndUpdate(data, skip))
		{
			return false;
		}

		if(skip)
		{
			return true;
		}

		while(data.getChar(&id))
		{
			bool b=true;
			SChange change;
			switch(id)
			{
			case CHANGE_REN_FILE:
			case CHANGE_REN_DIR:
				b=parseTwoFn(id, data, change);
				break;
			case CHANGE_DEL_FILE:
			case CHANGE_ADD_FILE:
			case CHANGE_ADD_DIR:
			case CHANGE_MOD:
			case CHANGE_DEL_DIR:
				b=parseOneFn(id, data, change);
				break;
			case CHANGE_MOD_ALL:
				return false;
			default:
				return false;
			}

			if(!b)
			{
				return false;
			}

			switch(id)
			{
			case CHANGE_MOD:
				addChangeCheck(change);
				break;
			case CHANGE_ADD_FILE:
				addChange(change, CHANGE_DEL_FILE, CHANGE_MOD);
				break;
			case CHANGE_ADD_DIR:
				addChange(change, CHANGE_DEL_DIR, CHANGE_NONE);
				break;
			case CHANGE_DEL_FILE:
				delChange(change, CHANGE_ADD_FILE, CHANGE_MOD, CHANGE_REN_FILE);
				break;
			case CHANGE_DEL_DIR:
				delChange(change, CHANGE_DEL_FILE, CHANGE_NONE, CHANGE_REN_DIR);
				break;
			case CHANGE_REN_FILE:
				renChange(change, CHANGE_ADD_FILE, CHANGE_MOD, CHANGE_REN_FILE);
				break;
			case CHANGE_REN_DIR:
				renChange(change, CHANGE_ADD_DIR, CHANGE_NONE, CHANGE_REN_DIR);
				break;
			}
		}
	}

	void addChangeCheck(const SChange& change)
	{
		std::vector<SChange>::iterator it=std::find(
			compacted_changes.begin(), compacted_changes.end(), change);

		if(it==compacted_changes.end())
		{
			compacted_changes.push_back(change);
		}		
	}

	void addChange(SChange& change, char del_action, char mod_action)
	{
		for(size_t i=0;i<compacted_changes.size();)
		{
			if(compacted_changes[i].action==del_action 
				&& compacted_changes[i].fn1==change.fn1)
			{
				compacted_changes.erase(compacted_changes.begin()+i);
				if(mod_action != CHANGE_NONE)
				{
					change.action=mod_action;
				}
				break;
			}
			else
			{
				++i;
			}
		}
		compacted_changes.push_back(change);
	}

	void delChange(SChange change, char add_action, char mod_action, char ren_action)
	{
		size_t sweeps=1;
		bool add=true;
		for(size_t l=0;l<sweeps;++l)
		{
			for(size_t i=0;i<compacted_changes.size();++i)
			{
				bool del=false;
				if(compacted_changes[i].action==add_action
					&& compacted_changes[i].fn1==change.fn1)
				{
					del=true;
					add=false;
				}
				else if( mod_action!=CHANGE_NONE
					&& compacted_changes[i].action==mod_action
					&& compacted_changes[i].fn1==change.fn1)
				{
					del=true;
				}
				else if( compacted_changes[i].action==ren_action
					&& compacted_changes[i].fn2==change.fn1)
				{
					bool do_del=true;
					for(size_t j=0;j<compacted_changes.size();++j)
					{
						if(compacted_changes[j].action==add_action
							&& compacted_changes[j].fn1==change.fn1)
						{
							do_del=false;
							break;
						}
					}

					if(do_del)
					{
						del=true;
						change.fn1=compacted_changes[i].fn1;
						sweeps=2;
					}
				}

				if(del)
				{
					compacted_changes.erase(compacted_changes.begin()+i);
				}
				else
				{
					++i;
				}
			}
		}
		
		if(add)
		{
			compacted_changes.push_back(change);
		}
	}

	void renChange( SChange& change, char add_action, char mod_action, char ren_action )
	{
		bool add_mod=false;
		bool add=true;
		std::string other_fn1;
		size_t sweeps=1;
		for(size_t l=0;l<sweeps;++l)
		{
			for(size_t i=0;i<compacted_changes.size();++i)
			{
				bool del=false;
				if(compacted_changes[i].action==ren_action
					&& compacted_changes[i].fn2==change.fn1)
				{
					del=true;
					other_fn1=change.fn1;
					change.fn1=compacted_changes[i].fn1;
					sweeps=2;
				}
				else if(compacted_changes[i].action==mod_action
					&& (compacted_changes[i].fn1==change.fn1
					|| (!other_fn1.empty() && compacted_changes[i].fn1==other_fn1) ) )
				{
					del=true;
					add_mod=true;
				}
				else if(compacted_changes[i].action==add_action
					&& compacted_changes[i].fn1==change.fn1)
				{
					add=false;
					compacted_changes[i].fn1=change.fn2;
				}

				if(del)
				{
					compacted_changes.erase(compacted_changes.begin()+i);
				}
				else
				{
					++i;
				}
			}
		}
		

		if(add)
		{
			compacted_changes.push_back(change);
		}

		if(add_mod)
		{
			SChange mod_change(CHANGE_MOD, change.fn2);
			compacted_changes.push_back(mod_change);
		}
	}

	bool checkHeaderAndUpdate(CRData& data, bool& skip)
	{
		unsigned int num_sequences;
		if(!data.getUInt(&num_sequences))
		{
			return false;
		}

		for(unsigned int i=0;i<num_sequences;++i)
		{
			int64 seq_id;
			if(!data.getInt64(&seq_id))
			{
				return false;
			}
			int64 seq_start;
			if(!data.getInt64(&seq_id))
			{
				return false;
			}
			int64 seq_stop;
			if(!data.getInt64(&seq_stop))
			{
				return false;
			}

			for(size_t j=0;j<sequences.size();++j)
			{
				if(sequences[j].id == seq_id)
				{
					if(first_compaction
						&& seq_start<sequences[j].next)
					{
						skip=true;
						return true;
					}
					else if(seq_start!=sequences[j].next)
					{
						return first_compaction;
					}
					else
					{
						sequences[j].next=seq_stop;
						sequences[j].updated=true;
					}
				}
			}
		}

		return true;
	}

	bool parseTwoFn(char id, CRData& data, SChange& change)
	{
		change.action=id;

		if(!data.getStr(&change.fn1))
		{
			return false;
		}

		if(!data.getStr(&change.fn2))
		{
			return false;
		}

		return true;
	}

	bool parseOneFn(char id, CRData& data, SChange& change)
	{
		change.action=id;

		if(!data.getStr(&change.fn1))
		{
			return false;
		}

		return true;
	}

	std::wstring getOsFp(const std::string& fn)
	{
		std::vector<std::string> tokens;
		TokenizeMail(fn, tokens, "/");

		std::wstring fp;
		for(size_t i=0;i<tokens.size();++i)
		{
			if(tokens[i]!="." && tokens[i]!="..")
			{
				fp+=os_file_sep()+Server->ConvertToUnicode(tokens[i]);
			}
		}
		return fp;
	}

	std::wstring getFullpath(const std::string& fn)
	{
		return continuous_path+getOsFp(fn);
	}

	std::wstring getFullHashpath(const std::string& fn)
	{
		return continuous_hash_path+getOsFp(fn);
	}

	void queueChange(SChange& change)
	{
		SQueueItem new_item;
		new_item.queued_dl=false;
		new_item.queued_metdata=false;
		new_item.change=change;
		dl_queue.push_back(new_item);
	}

	bool execChange(SChange& change)
	{
		switch(change.action)
		{
		case CHANGE_DEL_FILE:
			return execDelFile(change);
		}
	}

	bool execDelFile(SChange& change)
	{
		if(backupFile(change.fn1)==std::wstring())
		{
			return false;
		}

		if(!Server->deleteFile(getFullpath(change.fn1)) ||
			!Server->deleteFile(getFullHashpath(change.fn1)) )
		{
			Server->Log("Error deleting file \""+change.fn1+"\"", LL_ERROR);
			return false;
		}
		else
		{
			return true;
		}
	}

	bool execDelDir(SChange& change)
	{
		if(!backupDir(change.fn1))
		{
			return false;
		}

		if(!os_remove_nonempty_dir(getFullpath(change.fn1)) ||
			!os_remove_nonempty_dir(getFullHashpath(change.fn1)) )
		{
			Server->Log("Error deleting file \""+change.fn1+"\"", LL_ERROR);
			return false;
		}
		else
		{
			return true;
		}
	}

	bool execAddFile(SChange& change)
	{
		std::auto_ptr<IFile> f(Server->openFile(getFullpath(change.fn1), MODE_WRITE));

		if(!f.get())
		{
			Server->Log("Error creating file \""+change.fn1+"\"", LL_ERROR);
			return false;
		}
		else
		{
			return true;
		}
	}

	bool execAddDir(SChange& change)
	{
		if(!os_create_dir(getFullpath(change.fn1)) 
			|| !os_create_dir(getFullHashpath(change.fn1)) )
		{
			Server->Log("Error creating fir \""+change.fn1+"\"", LL_ERROR);
			return false;
		}
		else
		{
			return true;
		}
	}

	bool execRen(SChange& change)
	{
		std::wstring fn2=getFullpath(change.fn2);
		if(Server->fileExists(fn2))
		{
			if(backupFile(change.fn2)==std::wstring())
			{
				return false;
			}
		}

		if(!os_rename_file(getFullpath(change.fn1), getFullpath(change.fn2)) 
			|| !os_rename_file(getFullHashpath(change.fn1), getFullpath(change.fn2)) )
		{
			Server->Log("Error renaming \""+change.fn1+"\" to \""+change.fn2+"\"", LL_ERROR);
			return false;
		}
		else
		{
			return true;
		}
	}

	bool execMod(SChange& change)
	{
		std::auto_ptr<IFile> f(Server->openFile(getFullpath(change.fn1)));

		if(fileclient.get()==NULL)
		{
			FileClient* new_fc=new FileClient(false, server_identity, server_get->getFilesrvProtocolVersion(), server_get->isOnInternetConnection(), server_get, server_get);
			fileclient.reset(new_fc);
			_u32 rc = server_get->getClientFilesrvConnection(new_fc, server_settings.get());
			if(rc!=ERR_CONNECTED)
			{
				Server->Log("Could not connect to client in continous backup thread", LL_ERROR);
				return false;
			}				
		}

		std::string hash;
		std::string permissions;
		int64 filesize;
		int64 created;
		int64 modified;
		_u32 rc = fileclient->GetFileHashAndMetadata(change.fn1, hash, permissions, filesize, created, modified);

		if(rc!=ERR_SUCCESS)
		{
			ServerLogger::Log(clientid, L"Error getting file hash and metadata for \""+Server->ConvertToUnicode(change.fn1)+L"\" from "+clientname+L". Errorcode: "+widen(fileclient->getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
			return false;
		}

		bool tries_once;
		std::wstring ff_last;
		bool hardlink_limit;
		FileMetadata metadata(permissions, modified, created);
		if(local_hash->findFileAndLink(getFullpath(change.fn1), NULL, getFullHashpath(change.fn1),
			hash, filesize, std::string(), tries_once, ff_last, hardlink_limit, metadata))
		{
			//TODO: delete old file first!
			local_hash->addFileSQL(backupid, 1, getFullpath(change.fn1), getFullHashpath(change.fn1), hash, filesize, 0);
			local_hash->copyFromTmpTable(false);
			return true;
		}

		std::auto_ptr<IFile> tmpf(BackupServerGet::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, clientid));
		if(!tmpf.get())
		{
			return false;
		}

		if(f->Size()==0 || !transfer_incr_blockdiff)
		{
			_u32 rc = fileclient->GetFile(change.fn1, tmpf.get(), hashed_transfer_full);

			int hash_retries=5;
			while(rc==ERR_HASH && hash_retries>0)
			{
				tmpf->Seek(0);
				rc = fileclient->GetFile(change.fn1, tmpf.get(), hashed_transfer_full);
				--hash_retries;
			}

			bool hash_file = false;

			if(rc!=ERR_SUCCESS)
			{
				ServerLogger::Log(clientid, L"Error getting file \""+Server->ConvertToUnicode(change.fn1)+L"\" from "+clientname+L". Errorcode: "+widen(fileclient->getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);

				BackupServerGet::destroyTemporaryFile(tmpf.get());					
				tmpf.release();

				return false;
			}
			else
			{
				hashFile(getFullpath(change.fn1), getFullHashpath(change.fn1), tmpf.get(),
					NULL, Server->ConvertToUTF8(getFullpath(change.fn1)), filesize, metadata );
				tmpf.release();
			}
		}
		else
		{
			if(!fileclient_chunked.get())
			{
				if(!server_get->getClientChunkedFilesrvConnection(fileclient_chunked))
				{
					ServerLogger::Log(clientid, L"Connect error during continuous backup (fileclient_chunked-1)", LL_ERROR);
					return false;
				}
				else
				{
					fileclient_chunked->setDestroyPipe(true);
					if(fileclient_chunked->hasError())
					{
						ServerLogger::Log(clientid, L"Connect error during continuous backup (fileclient_chunked)", LL_ERROR);
						return false;
					}
				}
			}

			_u32 rc = fileclient_chunked->GetFilePatch(change.fn1, )
		}
	}

	std::wstring backupFile(const std::wstring& fn)
	{
		std::wstring filePath = continuous_path + fn;
		time_t tt=time(NULL);
#ifdef _WIN32
		tm lt;
		tm *t=&lt;
		localtime_s(t, &tt);
#else
		tm *t=localtime(&tt);
#endif
		char buffer[500];
		strftime(buffer, 500, "%y-%m-%d %H.%M.%S", t);
		std::wstring backupPath = continuous_path_backup + widen(buffer)+L"-"+convert(Server->getTimeSeconds())+os_file_sep()+fn;
		if(!os_create_dir_recursive(os_file_prefix(ExtractFilePath(backupPath))))
		{
			return std::wstring();
		}
		
		if(!os_rename_file(os_file_prefix(filePath), os_file_prefix(backupPath)))
		{
			return std::wstring();
		}

		return backupPath;
	}

	bool backupDir(const std::string& dir)
	{

	}

	void hashFile(std::wstring dstpath, std::wstring hashpath, IFile *fd, IFile *hashoutput, std::string old_file, int64 t_filesize, const FileMetadata& metadata)
	{
		int l_backup_id=backupid;

		CWData data;
		data.addString(Server->ConvertToUTF8(fd->getFilenameW()));
		data.addInt(l_backup_id);
		data.addChar(1);
		data.addString(Server->ConvertToUTF8(dstpath));
		data.addString(Server->ConvertToUTF8(hashpath));
		if(hashoutput!=NULL)
		{
			data.addString(Server->ConvertToUTF8(hashoutput->getFilenameW()));
		}
		else
		{
			data.addString("");
		}

		data.addString(old_file);
		data.addChar(1);
		data.addInt64(t_filesize);
		metadata.serialize(data);

		ServerLogger::Log(clientid, "GT: Loaded file \""+ExtractFileName(Server->ConvertToUTF8(dstpath))+"\"", LL_DEBUG);

		Server->destroy(fd);
		if(hashoutput!=NULL)
		{
			Server->destroy(hashoutput);
		}
		hashpipe_prepare->Write(data.getDataPtr(), data.getDataSize() );
	}

	std::vector<std::string> changes;
	std::vector<SSequence> sequences;
	std::vector<SChange> compacted_changes;

	IMutex* mutex;
	ICondition* cond;
	BackupServerGet* server_get;
	bool collect_only;
	bool first_compaction;
	bool stop;
	std::wstring continuous_path;
	std::wstring continuous_hash_path;
	std::wstring continuous_path_backup;

	std::wstring tmpfile_path;
	bool use_tmpfiles;
	int clientid;
	bool use_snapshots;
	bool use_reflink;

	std::wstring clientname;

	int backupid;

	std::auto_ptr<BackupServerHash> local_hash;

	std::auto_ptr<FileClientChunked> fileclient_chunked;
	std::auto_ptr<FileClient> fileclient;

	std::auto_ptr<ServerSettings> server_settings;

	std::deque<SQueueItem> dl_queue;

	bool hashed_transfer_full;
	bool hashed_transfer_incr;
	bool transfer_incr_blockdiff;
};