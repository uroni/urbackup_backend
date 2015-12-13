#pragma once

#include "ClientMain.h"
#include "../Interface/Thread.h"
#include <string>
#include <vector>
#include <time.h>
#include "../Interface/Mutex.h"
#include "../Interface/Server.h"
#include "../Interface/Condition.h"
#include "../common/data.h"
#include "../urbackupcommon/change_ids.h"
#include <algorithm>
#include "../stringtools.h"
#include "server_settings.h"
#include "database.h"
#include "ServerDownloadThread.h"
#include "server_log.h"
#include "dao/ServerBackupDao.h"
#include "FileIndex.h"
#include "create_files_index.h"

extern std::string server_identity;
extern std::string server_token;

class BackupServerContinuous : public IThread, public FileClient::QueueCallback
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

	BackupServerContinuous(ClientMain* client_main, const std::string& continuous_path, const std::string& continuous_hash_path, const std::string& continuous_path_backup,
		const std::string& tmpfile_path, bool use_tmpfiles, int clientid, const std::string& clientname, int backupid, bool use_snapshots, bool use_reflink,
		IPipe* hashpipe_prepare)
		: client_main(client_main), collect_only(true), first_compaction(true), stop(false), continuous_path(continuous_path), continuous_hash_path(continuous_hash_path),
		continuous_path_backup(continuous_path_backup),
		tmpfile_path(tmpfile_path), use_tmpfiles(use_tmpfiles), clientid(clientid), clientname(clientname), backupid(backupid),
		use_snapshots(use_snapshots), use_reflink(use_reflink), hashpipe_prepare(hashpipe_prepare), has_fullpath_entryid_mapping_table(false)
	{
		mutex = Server->createMutex();
		cond = Server->createCondition();

		logid = ServerLogger::getLogId(clientid);

		local_hash.reset(new BackupServerHash(NULL, clientid, use_snapshots, use_reflink, use_tmpfiles, logid));
	}

	~BackupServerContinuous()
	{
		Server->destroy(mutex);
	}

	void operator()()
	{
		server_settings.reset(new ServerSettings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER)));
		backupdao.reset(new ServerBackupDao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER)));
		fileindex.reset(create_lmdb_files_index());

		hashed_transfer_full = true;
		hashed_transfer_incr = true;
		transfer_incr_blockdiff = true;

		if(client_main->isOnInternetConnection())
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

		while(true)
		{
			{
				IScopedLock lock(mutex);

				if(stop)
				{
					return;
				}

				size_t n_changes=changes.size();
				bool curr_collect_only=collect_only;
				do
				{
					cond->wait(&lock);
					if(stop)
					{
						return;
					}
				}
				while(n_changes==changes.size() &&
					curr_collect_only==collect_only);

				if(!collect_only && !compactChanges())
				{
					client_main->sendToPipe("RESYNC");
					return;
				}
			}

			if(!collect_only && !has_fullpath_entryid_mapping_table)
			{
				backupdao->createTemporaryPathLookupTable();
				backupdao->populateTemporaryPathLookupTable(backupid);
				backupdao->createTemporaryPathLookupIndex();

				has_fullpath_entryid_mapping_table=true;
			}

			if(!compacted_changes.empty())
			{
				for(size_t i=0;i<compacted_changes.size();++i)
				{
					queueChange(compacted_changes[i]);
				}

				while(!dl_queue.empty())
				{
					execChange(dl_queue.front().change);
					dl_queue.pop_front();
				}
			}

			compacted_changes.clear();
		}

		if(server_download.get())
		{
			server_download->queueStop(false);
			Server->getThreadPool()->waitFor(server_download_ticket);
		}

		if(has_fullpath_entryid_mapping_table)
		{
			backupdao->dropTemporaryPathLookupTable();
		}

		delete this;
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

	void updateSettings(int tbackupid, const std::string& tcontinuous_path, const std::string& tcontinuous_hash_path, const std::string& tcontinuous_path_backup)
	{
		backupid = tbackupid;
		continuous_path = tcontinuous_path;
		continuous_hash_path = tcontinuous_hash_path;
		continuous_path_backup = tcontinuous_path_backup;
	}

private:

	struct SQueueItem
	{
		bool queued_metdata;
		SChange change;
	};

	bool compactChanges()
	{
		bool ret=true;
		for(size_t i=0;i<changes.size();++i)
		{
			ret &= compactChanges(changes[i]);
		}

		first_compaction=false;

		changes.clear();

		return ret;
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

		return true;
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
			if(!data.getInt64(&seq_start))
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
						sequences[j].next=seq_stop;
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

	std::string getOsFp(const std::string& fn)
	{
		std::vector<std::string> tokens;
		TokenizeMail(fn, tokens, "/");

		std::string fp;
		for(size_t i=0;i<tokens.size();++i)
		{
			if(tokens[i]!="." && tokens[i]!="..")
			{
				fp+=os_file_sep()+(tokens[i]);
			}
		}
		return fp;
	}

	std::string getFullpath(const std::string& fn)
	{
		return continuous_path+getOsFp(fn);
	}

	std::string getFullHashpath(const std::string& fn)
	{
		return continuous_hash_path+getOsFp(fn);
	}

	void queueChange(SChange& change)
	{
		SQueueItem new_item;
		new_item.queued_metdata=false;
		new_item.change=change;
		dl_queue.push_back(new_item);
	}
	
	bool execChange(SChange& change)
	{
		switch(change.action)
		{
		case CHANGE_REN_FILE:
		case CHANGE_REN_DIR:
			return execRen(change);
		case CHANGE_DEL_FILE:
			return execDelFile(change);
		case CHANGE_ADD_FILE:
			return execAddFile(change);
		case CHANGE_ADD_DIR:
			return execAddDir(change);
		case CHANGE_MOD:
			return execMod(change);
		case CHANGE_DEL_DIR:
			return execDelDir(change);
		default: return false;
		}
	}

	bool execDelFile(SChange& change)
	{
		if(backupFile((change.fn1))==std::string())
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
		std::string fn2=getFullpath(change.fn2);
		if(Server->fileExists(fn2))
		{
			if(backupFile((change.fn2))==std::string())
			{
				return false;
			}
		}

		if(!os_rename_file(os_file_prefix(getFullpath(change.fn1)), os_file_prefix(getFullpath(change.fn2))) 
			|| !os_rename_file(os_file_prefix(getFullHashpath(change.fn1)), os_file_prefix(getFullHashpath(change.fn2))) )
		{
			Server->Log("Error renaming \""+change.fn1+"\" to \""+change.fn2+"\"", LL_ERROR);
			return false;
		}
		else
		{
			return true;
		}
	}

	bool constructFileClient(std::auto_ptr<FileClient>& new_fc)
	{		
		new_fc.reset(new FileClient(false, server_identity, client_main->getProtocolVersions().file_protocol_version, client_main->isOnInternetConnection(), client_main, client_main));
		_u32 rc = client_main->getClientFilesrvConnection(new_fc.get(), server_settings.get());
		if(rc!=ERR_CONNECTED)
		{
			Server->Log("Could not connect to client in continous backup thread", LL_ERROR);
			return false;
		}
		return true;
	}

	void constructServerDownloadThread()
	{
		server_download.reset(new ServerDownloadThread(*fileclient.get(),
			fileclient_chunked.get(), continuous_path,
			continuous_hash_path, continuous_path, std::string(), hashed_transfer_full,
			false, clientid, clientname, use_tmpfiles, tmpfile_path, server_token,
			use_reflink, backupid, true, hashpipe_prepare, client_main, client_main->getProtocolVersions().file_protocol_version, 0, logid));

		server_download_ticket = Server->getThreadPool()->execute(server_download.get());
	}

	bool execMod(SChange& change)
	{
		std::auto_ptr<IFile> f(Server->openFile(getFullpath(change.fn1)));

		if(!fileclient.get())
		{
			if(!constructFileClient(fileclient))
			{
				return false;
			}
		}

		if(!fileclient_metadata.get())
		{
			if(!constructFileClient(fileclient_metadata))
			{
				return false;
			}
		}

		if(!fileclient_chunked.get())
		{
			if(!client_main->getClientChunkedFilesrvConnection(fileclient_chunked, server_settings.get()))
			{
				ServerLogger::Log(logid, "Connect error during continuous backup (fileclient_chunked-1)", LL_ERROR);
				return false;
			}
			else
			{
				fileclient_chunked->setDestroyPipe(true);
				if(fileclient_chunked->hasError())
				{
					ServerLogger::Log(logid, "Connect error during continuous backup (fileclient_chunked)", LL_ERROR);
					return false;
				}
			}
		}

		std::string hash;
		std::string permissions;
		int64 filesize;
		int64 created;
		int64 modified;
		_u32 rc = fileclient_metadata->GetFileHashAndMetadata(change.fn1, hash, permissions, filesize, created, modified);

		if(rc!=ERR_SUCCESS)
		{
			ServerLogger::Log(logid, "Error getting file hash and metadata for \""+(change.fn1)+"\" from "+clientname+". Errorcode: "+fileclient->getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
			return false;
		}

		{
			ServerBackupDao::CondInt64 entryid = backupdao->lookupEntryIdByPath(getFullpath(change.fn1));
			if(entryid.exists)
			{
				ServerBackupDao::SFindFileEntry fentry = backupdao->getFileEntry(entryid.value);
				if(fentry.exists)
				{
					local_hash->deleteFileSQL(*backupdao, *fileindex, reinterpret_cast<const char*>(fentry.shahash.c_str()),
						fentry.filesize, fentry.rsize, fentry.clientid,
						fentry.backupid, fentry.incremental, fentry.id, fentry.prev_entry, fentry.next_entry, fentry.pointed_to,
						true, true, true, false);
				}
			}
		}

		bool tries_once;
		std::string ff_last;
		bool hardlink_limit = false;
		bool copied_file=false;
		int64 entryid=0;
		int entryclientid=0;
		int64 next_entryid=0;
		int64 rsize = -1;
		FileMetadata metadata(permissions, modified, created, std::string());
		if(local_hash->findFileAndLink(getFullpath(change.fn1), NULL, getFullHashpath(change.fn1),
			hash, filesize, std::string(), true, tries_once, ff_last, hardlink_limit, copied_file, entryid,
			entryclientid, rsize, next_entryid, metadata, true))
		{
			local_hash->addFileSQL(backupid, clientid, 1, getFullpath(change.fn1), getFullHashpath(change.fn1),
				hash, filesize, rsize, entryid, entryclientid, next_entryid, copied_file);
			return true;
		}

		if(!server_download.get())
		{
			constructServerDownloadThread();
		}

		std::string fn = (ExtractFileName(change.fn1));
		std::string fpath = (ExtractFilePath(change.fn1));

		if(f->Size()==0 || !transfer_incr_blockdiff)
		{
			server_download->addToQueueFull(0, fn, fn,
				fpath, fpath, filesize, metadata, false, false, 0);
		}
		else
		{
			server_download->addToQueueChunked(0, fn, fn,
				fpath, fpath, filesize, metadata, false);
		}

		return true;
	}

	std::string backupFile(const std::string& fn)
	{
		std::string filePath = continuous_path + fn;
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
		std::string backupPath = continuous_path_backup + buffer+"-"+convert(Server->getTimeSeconds())+os_file_sep()+fn;
		if(!os_create_dir_recursive(os_file_prefix(ExtractFilePath(backupPath))))
		{
			return std::string();
		}
		
		if(!os_rename_file(os_file_prefix(filePath), os_file_prefix(backupPath)))
		{
			return std::string();
		}

		return backupPath;
	}

	bool backupDir(const std::string& dir)
	{
		return false;
	}

	virtual std::string getQueuedFileFull(FileClient::MetadataQueue& metadata, size_t& folder_items)
	{
		for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
			it!=dl_queue.end();++it)
		{
			if(!it->queued_metdata &&
				(it->change.action==CHANGE_ADD_FILE ||
				 it->change.action==CHANGE_MOD) )
			{
				metadata=FileClient::MetadataQueue_MetadataAndHash;
				it->queued_metdata=true;
				folder_items=0;
				return it->change.fn1;
			}
		}

		return std::string();
	}

	virtual void unqueueFileFull(const std::string& fn)
	{
		for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
			it!=dl_queue.end();++it)
		{
			if(it->change.fn1==fn)
			{
				it->queued_metdata=false;
				return;
			}
		}
	}

	virtual void resetQueueFull()
	{
		for(std::deque<SQueueItem>::iterator it=dl_queue.begin();
			it!=dl_queue.end();++it)
		{
			it->queued_metdata=false;
		}
	}

	std::vector<std::string> changes;
	std::vector<SSequence> sequences;
	std::vector<SChange> compacted_changes;

	IMutex* mutex;
	ICondition* cond;
	ClientMain* client_main;
	bool collect_only;
	bool first_compaction;
	bool stop;
	std::string continuous_path;
	std::string continuous_hash_path;
	std::string continuous_path_backup;

	std::string tmpfile_path;
	bool use_tmpfiles;
	int clientid;
	bool use_snapshots;
	bool use_reflink;

	std::string clientname;

	int backupid;
	IPipe* hashpipe_prepare;

	std::auto_ptr<BackupServerHash> local_hash;

	std::auto_ptr<FileClientChunked> fileclient_chunked;
	std::auto_ptr<FileClient> fileclient;
	std::auto_ptr<FileClient> fileclient_metadata;

	std::auto_ptr<ServerSettings> server_settings;

	std::auto_ptr<ServerDownloadThread> server_download;
	THREADPOOL_TICKET server_download_ticket;

	std::deque<SQueueItem> dl_queue;

	bool hashed_transfer_full;
	bool hashed_transfer_incr;
	bool transfer_incr_blockdiff;

	bool has_fullpath_entryid_mapping_table;
	std::auto_ptr<ServerBackupDao> backupdao;
	std::auto_ptr<FileIndex> fileindex;

	logid_t logid;
};