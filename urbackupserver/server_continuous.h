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

	BackupServerContinuous(BackupServerGet* server_get, const std::wstring& continuous_path, const std::wstring& continuous_hash_path, const std::wstring& continuous_backup_path)
		: server_get(server_get), collect_only(true), first_compaction(true), stop(false), continuous_path(continuous_path), continuous_hash_path(continuous_hash_path),
		continuous_backup_path(continuous_backup_path)
	{
		mutex = Server->createMutex();
		cond = Server->createCondition();
	}

	~BackupServerContinuous()
	{
		Server->destroy(mutex);
	}

	void operator()()
	{
		server_settings.reset(new ServerSettings(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER)));

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
					execChange(compacted_changes[i]);
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
		if(!backupFile(change.fn1))
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
			if(!backupFile(change.fn2))
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

		if(f->Size()==0)
		{
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

			fileclient->GetFile()
		}
	}

	bool backupFile(const std::string& fn)
	{

	}

	bool backupDir(const std::string& dir)
	{

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
	std::wstring continuous_backup_path;

	std::auto_ptr<FileClientChunked> fileclient_chunked;
	std::auto_ptr<FileClient> fileclient;

	std::auto_ptr<ServerSettings> server_settings;
};