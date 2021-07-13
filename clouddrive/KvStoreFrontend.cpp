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

#include "KvStoreFrontend.h"
#ifdef HAS_LOCAL_BACKEND
#include "KvStoreBackendLocal.h"
#endif
#include <stdexcept>
#include "KvStoreDao.h"
#include "../urbackupcommon/os_functions.h"
#include "../common/data.h"
#include "../stringtools.h"
#include "ObjectCollector.h"
#include "../Interface/ThreadPool.h"
#include "../Interface/Pipe.h"
#include "../Interface/DatabaseCursor.h"
#include "../urbackupcommon/WalCheckpointThread.h"
#include <set>
#include "CompressEncrypt.h"
#include "CloudFile.h"
#include <memory.h>
#include <assert.h>
#include <memory>
#include <math.h>
#include <limits.h>
#include "../urbackupcommon/events.h"
#include <zlib.h>
#include <memory>
#include "Auto.h"
#ifdef HAS_MIRROR
#include "../urbackupserver/server_settings.h"
#endif
#ifdef HAS_SCRUB
#include "../common/json/json.h"
#endif

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/file.h>
#endif

//#define ASSERT_CHECK_DELETED
//#define ASSERT_CHECK_DELETED_NO_LOCINFO
#define WITH_PUT_CURR_DEL

namespace
{
	const DATABASE_ID CLOUDDRIVE_DB=40;
	const int TASK_REMOVE_OLD_OBJECTS=1;
	const int TASK_REMOVE_TRANSACTION=2;	

	void retryWait(std::string label, size_t n)
	{
		unsigned int waittime = (std::min)(static_cast<unsigned int>(1000.*pow(2., static_cast<double>(n))), (unsigned int)30 * 60 * 1000); //30min
		if (n>20)
		{
			waittime = (unsigned int)30 * 60 * 1000;
		}
		Server->Log(label + ": Waiting " + PrettyPrintTime(waittime)+" for retry");
		Server->wait(waittime);
	}

	const int64 generation_skip_update = 1000;
	const int64 generation_skip_max = generation_skip_update*11;

	namespace
	{
		class HasItemCallback : public IKvStoreBackend::IListCallback
		{
		public:
			bool has_item;
			bool has_callback;
			std::string item_key;
			HasItemCallback()
				:has_item(false), has_callback(false) {}
			virtual bool onlineItem(const std::string & key, const std::string & md5sum, int64 size, int64 last_modified) override
			{
				has_callback = true;
				if (key == "cd_magic_file")
				{
					return true;
				}

				if (key.find('_') == std::string::npos)
				{
					std::string err = "Key " + key + " has unknown format";
					Server->Log(err, LL_ERROR);
					return true;
				}
				std::string key_part = key;
				if (key.find('/') != std::string::npos)
				{
					size_t lslash = key.find_last_of('/');
					if (key.size() <= lslash + 1)
					{
						std::string err = "Key " + key + " has unknown format (2)";
						Server->Log(err, LL_ERROR);
						return true;
					}
					key_part = key.substr(lslash + 1);
				}

				int64 transid = os_atoi64(getuntil("_", key_part).c_str());
				std::string hexkey = getafter("_", key_part);

				if (hexkey == bytesToHex("test_file"))
				{
					return true;
				}

				if (getuntil("_", key_part) != convert(transid))
				{
					std::string err = "Key " + key + " has unknown format (3)";
					Server->Log(err, LL_ERROR);
					return true;
				}

				if (hexkey == "inactive"
					|| hexkey == "complete"
					|| hexkey == "finalized")
				{
					return true;
				}

				if (bytesToHex(hexToBytes(hexkey)) != hexkey)
				{
					std::string err = "Key " + key + " has unknown format (4)";
					Server->Log(err, LL_ERROR);
					return true;
				}

				has_item = true;
				item_key = key;
				return false;
			}
		};
	}
}

bool KvStoreFrontend::scrub_sync_test1 = false;
bool KvStoreFrontend::scrub_sync_test2 = false;

KvStoreFrontend::KvStoreFrontend(const std::string& db_path,
	IKvStoreBackend* backend, bool allow_import, const std::string& scrub_continue, const std::string& scrub_continue_position,
	IKvStoreBackend* backend_mirror, std::string mirror_window, bool background_worker_manual_run,
	bool background_worker_multi_trans_delete, IBackupFileSystem* cachefs)
	: backend(backend), background_worker(backend, this, background_worker_manual_run,
		background_worker_multi_trans_delete), with_prefix(false),
	scrub_worker(nullptr), scrub_mutex(Server->createMutex()), gen_mutex(Server->createMutex()),
	unsynced_keys_mutex(Server->createMutex()),
	curr_unsynced_keys(&unsynced_keys_a), other_unsynced_keys(&unsynced_keys_b),
	put_shared_mutex(Server->createSharedMutex()),
	has_last_modified(false), total_balance_ops(0), total_del_ops(0),
	db_path(db_path), put_db_worker(db_path), task_delay(0),
	backend_mirror(backend_mirror), mirror_window(mirror_window),
	mirror_del_log_mutex(Server->createMutex()),
	mirror_worker(this, background_worker),
	mirror_curr_pos(-1), mirror_state(-1), mirror_curr_total(-1), mirror_items(-1),
	objects_total_size(0), objects_total_num(0), objects_init_complete(false),
	objects_init_ticket(ILLEGAL_THREADPOOL_TICKET), allow_import(allow_import),
	cachefs(cachefs)
{
	backend->setFrontend(this, false);

	std::string str_task_delay = getFile(ExtractFilePath(db_path) + os_file_sep() + "task_delay");

	if (!str_task_delay.empty())
	{
		task_delay = watoi64(trim(str_task_delay));

		Server->Log("Background task delay " + PrettyPrintTime(task_delay*1000), LL_WARNING);
	}

	setMountStatus("{\"state\": \"open_db\"}");

	IDatabase* db = getDatabase();
	str_map params;
	params["wal_autocheckpoint"] = "0";
	if (db == nullptr
		&& !Server->openDatabase(db_path, CLOUDDRIVE_DB, params))
	{
		std::string err = "Error opening database at " + db_path;
		Server->Log(err, LL_ERROR);
		setMountStatusErr(err);
		throw std::runtime_error(err);
	}

	KvStoreDao dao(getDatabase());
	dao.createTables();

	add_created_column();
	add_cd_id_tasks_column();

	if (backend_mirror != nullptr)
	{
		add_mirrored_column();
		backend_mirror_del_log.reset(Server->openFile(ExtractFilePath(db_path) + os_file_sep() + "mirror_del.log", MODE_RW_CREATE));

		if (backend_mirror_del_log.get() == nullptr)
		{
			std::string err = "Error opening mirror del log. " + os_last_error_str();
			Server->Log(err, LL_ERROR);
			setMountStatusErr(err);
			throw std::runtime_error(err);
		}

		backend_mirror_del_log_wpos = backend_mirror_del_log->Size();
		bool more_data;
		if (backend_mirror_del_log_wpos > 0)
		{
			std::vector<IFsFile::SFileExtent> exts = backend_mirror_del_log->getFileExtents(0, 4096, more_data);
			if (exts.empty())
			{
				std::string err = "Error opening mirror del log. Cannot find file exts. " + os_last_error_str();
				Server->Log(err, LL_ERROR);
				setMountStatusErr(err);
				throw std::runtime_error(err);
			}

			backend_mirror_del_log_rpos = exts[0].offset;
		}
		else
		{
			backend_mirror_del_log_rpos = 0;
		}
	}

	if (backend->prefer_sequential_read())
	{
		add_last_modified_column();

		put_db_worker.set_with_last_modified(has_last_modified);
	}

	if(trim(getFile(ExtractFilePath(db_path) + os_file_sep() + "skip_backend_test"))!="1")
	{
		setMountStatus("{\"state\": \"dl_magic_file\"}");

		IFsFile* tmp_f = Server->openTemporaryFile();
		if (tmp_f == nullptr)
		{
			std::string err = "Error opening temporary file (magic file). " + os_last_error_str();
			Server->Log(err, LL_ERROR);
			setMountStatusErr(err);
			throw std::runtime_error(err);
		}
		ScopedDeleteFile tmp_f_del(tmp_f);
		IFsFile* ret_file = tmp_f;
		std::string ret_md5sum;
		unsigned int get_status;
		if (!backend->get("cd_magic_file", std::string(), IKvStoreBackend::GetDecrypted,
			false, ret_file, ret_md5sum, get_status))
		{
			KvStoreDao::CdSingleObject obj = dao.getSingleObject();

			if (obj.exists)
			{
				Server->Log("Retrieving " + prefixKey(encodeKey(obj.tkey, obj.trans_id))
					+ " (first object in cache database)");
				if (!backend->get(prefixKey(encodeKey(obj.tkey, obj.trans_id)),
					obj.md5sum, IKvStoreBackend::GetDecrypted,
					false, ret_file, ret_md5sum, get_status))
				{
					Server->Log("Retrieving " + prefixKey(encodeKey(obj.tkey, obj.trans_id))
						+ " failed. Trying with first listed item on backend storage...");
					obj.exists = false;
				}
			}

			if (!obj.exists)
			{
				Server->Log("Listing first object in bucket...", LL_INFO);
				HasItemCallback has_item;
				if (!backend->list(&has_item)
					&& !has_item.has_callback)
				{
					std::string err = "Error listing objects on cloud drive";
					Server->Log(err, LL_ERROR);
					setMountStatusErr(err);
					throw std::runtime_error(err);
				}

				if (has_item.has_item
					&& !backend->get(has_item.item_key,
						std::string(), IKvStoreBackend::GetDecrypted,
						true, ret_file, ret_md5sum, get_status))
				{
					std::string err = "Error getting item " + has_item.item_key + " from cloud drive. Encryption key may be wrong.";
					setMountStatusErr(err);
					Server->Log(err, LL_ERROR);
					throw std::runtime_error(err);
				}
			}

			IFsFile* new_f = Server->openTemporaryFile();
			if (new_f == nullptr)
			{
				std::string err = "Error opening temporary file (magic file 2). " + os_last_error_str();
				Server->Log(err, LL_ERROR);
				setMountStatusErr(err);
				throw std::runtime_error(err);
			}
			ScopedDeleteFile new_f_del(new_f);

			if (new_f->Write("CD_MAGIC") != 8)
			{
				std::string err = "Error writing to magic file. " + os_last_error_str();
				Server->Log(err, LL_ERROR);
				setMountStatusErr(err);
				throw std::runtime_error(err);
			}

			current_generation = (int64)Server->getSecureRandomNumber() << 32 | Server->getSecureRandomNumber();

			std::string md5sum;
			int64 size;
			if (!backend->put("cd_magic_file", new_f, 0, true, md5sum, size))
			{
				std::string err = "Error uploading magic file.";
				setMountStatusErr(err);
				Server->Log(err, LL_ERROR);
				throw std::runtime_error(err);
			}
		}
		else if (tmp_f->Read(static_cast<int64>(0), static_cast<_u32>(tmp_f->Size())) != "CD_MAGIC")
		{
			std::string content = tmp_f->Read(static_cast<int64>(0), static_cast<_u32>(tmp_f->Size()));
			std::string err = "Magic file content is wrong. ("+ content+")";
			Server->Log(err, LL_ERROR);
			setMountStatusErr(err);
			throw std::runtime_error(err);
		}
	}

	if(allow_import
		&& dao.getMiscValue("import_done").value!="1")
	{
		if(importFromBackend(dao))
		{
			dao.setMiscValue("import_done", "1");
		}
		else
		{
			std::string err = "Importing from backend failed";
			Server->Log(err, LL_ERROR);
			setMountStatusErr(err);
			throw std::runtime_error(err);
		}
	}

	if (dao.getMiscValue("has_prefix").value == "1")
	{
		with_prefix = true;
	}

	dao.getDb()->Write("PRAGMA journal_mode=WAL");

	dao.getDb()->Write("INSERT OR IGNORE INTO misc (key, value) VALUES ('initial_val', '1')");

	int wal_mode = MODE_READ;
#ifdef _WIN32
	wal_mode = MODE_RW_DEVICE;
#endif

	db_wal_file.reset(Server->openFile(db_path + "-wal", wal_mode));

	if (db_wal_file.get() == nullptr)
	{
		std::string err = "Error opening database wal file at " + db_path + "-wal";
		Server->Log(err, LL_ERROR);
		throw std::runtime_error(err);
	}

	{
		DBScopedSynchronous synchronous(dao.getDb());
		KvStoreDao::CondInt64 generation = dao.getGeneration();
		if (generation.exists)
		{
			current_generation = generation.value + generation_skip_max + 100;
			last_persisted_generation = generation.value;
			last_update_generation = generation.value;
		}
		else
		{
			current_generation = 1;
			last_persisted_generation = 0;
			last_update_generation = 0;
			dao.insertGeneration(last_persisted_generation);
		}
	}

	if (backend->has_transactions())
	{
		put_db_worker.set_do_synchonous(false);
		put_db_worker.set_db_wal_file(db_wal_file.get());
	}

	put_db_worker_ticket = Server->getThreadPool()->execute(&put_db_worker, "bput db");

	backend->setFrontend(this, true);

	background_worker_ticket = Server->getThreadPool()->execute(&background_worker, "bkg worker");
	
	static bool has_wal_checkpoint_thread = false;

	if (!has_wal_checkpoint_thread)
	{
		has_wal_checkpoint_thread = true;

		WalCheckpointThread* wal_checkpoint_thread = new WalCheckpointThread(100 * 1024 * 1024, 1000 * 1024 * 1024,
			db_path, CLOUDDRIVE_DB);
		Server->createThread(wal_checkpoint_thread, "db checkpoint");
	}

	setMountStatus("{\"state\": \"init_size\"}");
	objects_init_ticket = Server->getThreadPool()->execute(this, "init sizes");

	{
		std::unique_ptr<IFile> empty_file(cachefs->openFile("empty.file", MODE_WRITE));
		if (!empty_file)
		{
			std::string err = "Error creating empty file. " + os_last_error_str();
			Server->Log(err, LL_ERROR);
			setMountStatusErr(err);
			throw std::runtime_error(err);
		}
		empty_file_path = "empty.file";
	}

	if (scrub_continue == "scrub")
	{
		start_scrub(ScrubAction::Scrub, scrub_continue_position);
	}
	else if (scrub_continue == "balance")
	{
		start_scrub(ScrubAction::Balance, scrub_continue_position);
	}
	else if (scrub_continue == "rebuild")
	{
		start_scrub(ScrubAction::Rebuild, scrub_continue_position);
	}

	if (backend_mirror != nullptr)
	{
		mirror_worker_ticket = Server->getThreadPool()->execute(&mirror_worker, "mirror worker");
	}
	else
	{
		mirror_worker_ticket = ILLEGAL_THREADPOOL_TICKET;
	}

	std::string scrub_obj = getFile(ExtractFilePath(db_path) + os_file_sep() + "scrub_obj");

	if (!scrub_obj.empty())
	{
		IFsFile* tmpf = Server->openTemporaryFile();
		ScopedDeleteFile del_tmpf(tmpf);
		if(tmpf==nullptr)
			abort();
		
		std::string ret_md5sum;
		unsigned int get_status;
		if (!backend->get(trim(scrub_obj), std::string(), IKvStoreBackend::GetScrub, true, tmpf, ret_md5sum, get_status))
		{
			Server->Log("Get scrub of object " + scrub_obj + " failed", LL_ERROR);
		}
		else
		{
			Server->Log("Get scrub of object " + scrub_obj + " succeeded. md5sum=" + bytesToHex(ret_md5sum) + " status=" + convert(get_status), LL_WARNING);
		}
	}
}

KvStoreFrontend::~KvStoreFrontend()
{
	background_worker.quit();
	put_db_worker.quit();
	mirror_worker.quit();

	Server->getThreadPool()->waitFor(background_worker_ticket);
	Server->getThreadPool()->waitFor(put_db_worker_ticket);
	if (mirror_worker_ticket != ILLEGAL_THREADPOOL_TICKET)
	{
		Server->getThreadPool()->waitFor(mirror_worker_ticket);
	}

	if (objects_init_ticket != ILLEGAL_THREADPOOL_TICKET)
	{
		Server->getThreadPool()->waitFor(objects_init_ticket);
	}

	Server->deleteFile(empty_file_path);

	delete backend;
}

IFsFile* KvStoreFrontend::get(int64 cd_id, const std::string& key, int64 transid, 
	bool prioritize_read, IFsFile* tmpl_file, 
	bool allow_error_event, bool& not_found, int64* get_transid)
{
	not_found=false;
	if (tmpl_file == nullptr)
		return nullptr;

	bool is_unsynced = false;
	SUnsyncedKey unsynced_key;
	{
		IScopedLock lock(unsynced_keys_mutex.get());
		auto it_curr = curr_unsynced_keys->find(std::make_pair(cd_id, key));
		auto it_other = other_unsynced_keys->find(std::make_pair(cd_id, key));
		if (it_curr != curr_unsynced_keys->end()
			|| it_other != other_unsynced_keys->end())
		{
			is_unsynced = true;

			if (it_curr == curr_unsynced_keys->end()
				|| it_other == other_unsynced_keys->end())
			{
				unsynced_key = it_curr != curr_unsynced_keys->end() ? it_curr->second : it_other->second;
			}
		}
	}

	KvStoreDao dao(getDatabase());

	bool get_unsynced = false;
	bool use_unsynced_key = false;
	if (is_unsynced)
	{
		if (backend->has_transactions()
			&& backend->can_read_unsynced())
		{
			Server->Log("Item key=" + bytesToHex(key) + " not synced. Reading unsynced item...");
			get_unsynced = true;
			if (unsynced_key.transid!=transid)
			{
				if (unsynced_key.transid != 0
					&& unsynced_key.transid!=-1)
				{
					KvStoreDao::CdObject cd_object = cd_id == 0 ? dao.getObject(transid, key) :
						dao.getObjectCd(cd_id, transid, key);

					if (!cd_object.exists)
					{
						use_unsynced_key = true;
						Server->Log("Item key=" + bytesToHex(key) + " does not exist in db. Using unsynced item from transid "+convert(unsynced_key.transid));
					}
					else if (cd_object.trans_id == unsynced_key.transid)
					{
						use_unsynced_key = true;
						Server->Log("Item key=" + bytesToHex(key) + " has current transid in db. Using unsynced item from transid " + convert(unsynced_key.transid));
					}
					else if (cd_object.trans_id > unsynced_key.transid)
					{
						get_unsynced = false;
						Server->Log("Item key=" + bytesToHex(key) + " more recent in db. Not using unsynced item from transid " + convert(unsynced_key.transid));
					}
					else if(cd_object.trans_id < unsynced_key.transid
						&& ((cd_id==0 && dao.isTransactionActive(unsynced_key.transid).exists)
							|| (cd_id!=0 && dao.isTransactionActiveCd(cd_id, unsynced_key.transid).exists) ) )
					{
						use_unsynced_key = true;
						Server->Log("Item key=" + bytesToHex(key) + " less recent in db. Using unsynced item from transid " + convert(unsynced_key.transid));
					}
					else
					{
						put_db_worker.flush();
					}
				}
				else
				{
					put_db_worker.flush();
				}
			}
			else
			{
				Server->Log("Item key=" + bytesToHex(key) + " has unsynced item in current transid. Using unsynced item from (current) transid " + convert(unsynced_key.transid));
				use_unsynced_key = true;
			}
			
		}
		else
		{
			Server->Log("Item key=" + bytesToHex(key) + " not synced. Syncing now...");
			if (!sync())
			{
				Server->Log("Syncing before get failed", LL_ERROR);
				return nullptr;
			}
		}
	}

	KvStoreDao::CdObject cd_object;
	if (get_unsynced
		&& use_unsynced_key)
	{
		cd_object.exists = true;
		cd_object.md5sum = unsynced_key.md5sum;
		cd_object.size = 0;
		cd_object.trans_id = unsynced_key.transid;
		assert(cd_object.trans_id > 0);
	}
	else
	{
		cd_object = cd_id == 0 ? 
			dao.getObject(transid, key) : 
			dao.getObjectCd(cd_id, transid, key);
	}

	if(!cd_object.exists
		|| cd_object.size==-1)
	{
		not_found=true;
		dao.getDb()->freeMemory();
		return tmpl_file;
	}

	if (cd_object.md5sum.empty()
		&& !backend->has_transactions())
	{
		put_db_worker.flush();
		cd_object = cd_id==0 ? 
			dao.getObject(transid, key) : 
			dao.getObjectCd(cd_id, transid, key);
	}

	int flags = IKvStoreBackend::GetDecrypted;

	if (prioritize_read)
	{
		flags |= IKvStoreBackend::GetPrioritize;
	}

	if (get_unsynced)
	{
		flags |= IKvStoreBackend::GetUnsynced;
	}

	if (get_transid != nullptr)
	{
		*get_transid = cd_object.trans_id;
	}

	IFsFile* ret = tmpl_file;
	std::string ret_md5sum;
	unsigned int get_status;
	bool from_mirror = false;

	std::string bkey = prefixKey(encodeKey(cd_id, key, cd_object.trans_id));
	if (!backend->get(bkey, cd_object.md5sum,
		flags, allow_error_event, ret, ret_md5sum, get_status))
	{
		not_found = (get_status & IKvStoreBackend::GetStatusNotFound)>0;
		bool backend_not_found = not_found;
		if (backend_mirror == nullptr
			|| !backend_mirror->get(bkey, 
					get_md5sum(cd_object.md5sum),
					flags, allow_error_event, ret, ret_md5sum, get_status))
		{
			not_found = (get_status & IKvStoreBackend::GetStatusNotFound)>0;
			dao.getDb()->freeMemory();
			assert(tmpl_file != nullptr || ret == nullptr);
			not_found = backend_not_found && not_found;
			if (not_found)
			{
				return tmpl_file;
			}
			return nullptr;
		}
		else if (backend_mirror != nullptr)
		{
			from_mirror = true;
		}
	}

	if (ret_md5sum != cd_object.md5sum
		&& !from_mirror)
	{
		DBScopedSynchronous synchronous(dao.getDb());
		if(cd_id==0)
			dao.updateObjectMd5sum(ret_md5sum, transid, key);
		else
			dao.updateObjectMd5sumCd(ret_md5sum, cd_id, transid, key);
	}

	dao.getDb()->freeMemory();
	return ret;
}

int64 KvStoreFrontend::get_transid(int64 cd_id, const std::string & key, int64 transid)
{
	bool is_unsynced = false;
	SUnsyncedKey unsynced_key;
	{
		IScopedLock lock(unsynced_keys_mutex.get());
		auto it_curr = curr_unsynced_keys->find(std::make_pair(cd_id, key));
		auto it_other = other_unsynced_keys->find(std::make_pair(cd_id, key));
		if (it_curr != curr_unsynced_keys->end()
			|| it_other != other_unsynced_keys->end())
		{
			is_unsynced = true;

			if (it_curr == curr_unsynced_keys->end()
				|| it_other == other_unsynced_keys->end())
			{
				unsynced_key = it_curr != curr_unsynced_keys->end() ? it_curr->second : it_other->second;
			}
		}
	}

	KvStoreDao dao(getDatabase());
	if (is_unsynced)
	{
		if (unsynced_key.transid == transid)
		{
			Server->Log("Item (get transid) key=" + bytesToHex(key) + " res=" + convert(transid) + " (current)");
			return transid;
		}
		else if (unsynced_key.transid != 0)
		{
			KvStoreDao::CdObject cd_object = cd_id==0 ?
				dao.getObject(transid, key) :
				dao.getObjectCd(cd_id, transid, key);
			dao.getDb()->freeMemory();

			if (!cd_object.exists)
			{
				Server->Log("Item (get transid) key=" + bytesToHex(key) + " does not exist in db. Using unsynced item from transid " + convert(unsynced_key.transid));
				return unsynced_key.transid;
			}
			else if (cd_object.trans_id == unsynced_key.transid)
			{
				Server->Log("Item (get transid) key=" + bytesToHex(key) + " has current transid in db. Using unsynced item from transid " + convert(unsynced_key.transid));
				return unsynced_key.transid;
			}
			else if (cd_object.trans_id > unsynced_key.transid)
			{
				Server->Log("Item (get transid) key=" + bytesToHex(key) + " more recent in db. Not using unsynced item from transid " + convert(unsynced_key.transid));
				return cd_object.trans_id;
			}
			else if (cd_object.trans_id < unsynced_key.transid
				&& ((cd_id==0 && dao.isTransactionActive(unsynced_key.transid).exists)
					|| (cd_id!=0 && dao.isTransactionActiveCd(cd_id, unsynced_key.transid).exists) ) )
			{
				Server->Log("Item (get transid) key=" + bytesToHex(key) + " less recent in db. Using unsynced item from transid " + convert(unsynced_key.transid));
				return unsynced_key.transid;
			}
			else
			{
				put_db_worker.flush();
			}
		}
		else
		{
			put_db_worker.flush();
		}
	}

	KvStoreDao::CdObject cd_object = cd_id==0 ? dao.getObject(transid, key)
		: dao.getObjectCd(cd_id, transid, key);
	dao.getDb()->freeMemory();

	return cd_object.exists ? cd_object.trans_id : 0;
}

bool KvStoreFrontend::reset(const std::string & key, int64 transid)
{
	KvStoreDao dao(getDatabase());
	KvStoreDao::CdObject cd_object = dao.getObject(transid, key);

	if (!cd_object.exists)
	{
		return false;
	}

	if (cd_object.size > 0)
		objects_total_size -= cd_object.size;

	--objects_total_num;

	return dao.deleteObject(cd_object.trans_id, key);
}

bool KvStoreFrontend::put(int64 cd_id, const std::string& key, int64 transid, 
	int64 generation, IFsFile* src, unsigned int cflags,
	bool allow_error_event, int64& compressed_size)
{
	IScopedReadLock read_lock(put_shared_mutex.get());

	std::string tkey = prefixKey(encodeKey(cd_id, key, transid));

#ifdef WITH_PUT_CURR_DEL
	if (backend->need_curr_del())
	{
		//Careful, this makes it so that a backend->sync() at the wrong time causes the item to vanish on crash
		// (put must not be used to overwrite items in transactions that are completed)
		KvStoreDao dao(getDatabase());
		KvStoreDao::CdObject obj = cd_id == 0 ?
			dao.getObjectInTransid(transid, tkey) :
			dao.getObjectInTransidCd(cd_id, transid, tkey);
		if (obj.exists
			&& !get_locinfo(obj.md5sum).empty())
		{
			size_t idx1 = 0;
			size_t idx2 = 0;
			bool b = backend->del([&tkey, &idx1](IKvStoreBackend::key_next_action_t action, std::string* key) {
				if (action==IKvStoreBackend::key_next_action_t::reset) {
					idx1 = 0;
					return true;
				}
				if (action == IKvStoreBackend::key_next_action_t::clear) {
					return true;
				}
				if (idx1>0)
					return false;
				*key = tkey;
				++idx1;
				return true;
			},
				[&obj, &idx2](IKvStoreBackend::key_next_action_t action, std::string* locinfo) {
				if (action == IKvStoreBackend::key_next_action_t::reset) {
					idx2 = 0;
					return true;
				}
				if (action == IKvStoreBackend::key_next_action_t::clear) {
					return true;
				}
				if (idx2>0)
					return false;
				*locinfo = get_locinfo(obj.md5sum);
				++idx2;
				return true;
			},false);
			assert(b);
		}
		if (obj.size > 0)
			objects_total_size -= obj.size;

		if(obj.exists
			&& obj.size>=0)
			--objects_total_num;
	}
	else
#endif
	{
		KvStoreDao dao(getDatabase());
		KvStoreDao::CdObject obj = cd_id == 0 ?
			dao.getObjectInTransid(transid, tkey) :
			dao.getObjectInTransidCd(cd_id, transid, tkey);
		if (obj.exists
			&& obj.size > 0)
		{
			objects_total_size -= obj.size;
		}
		if(obj.exists
			&& obj.size >= 0)
			--objects_total_num;
	}

	int64 object_id;
	if(!backend->has_transactions())
	{
		object_id = put_db_worker.add(cd_id, transid, key, generation);
	}

	int64 last_modified = 0;
	if (has_last_modified)
	{
#ifdef HAS_LOCAL_BACKEND
		last_modified = curr_last_modified();
#endif
	}
	unsigned int put_flags = 0;
	if (cflags & IOnlineKvStore::PutAlreadyCompressedEncrypted)
		put_flags |= IKvStoreBackend::PutAlreadyCompressedEncrypted;
	if (cflags & IOnlineKvStore::PutMetadata)
		put_flags |= IKvStoreBackend::PutMetadata;
	std::string md5sum;
	compressed_size = 0;
	bool ret = backend->put(tkey, src, put_flags, allow_error_event, md5sum, compressed_size);

	if(ret)
	{
		objects_total_size += compressed_size;
		bool init_complete = objects_init_complete;
		int64 total_num = objects_total_num++;

		if (total_num>0
			&& total_num % 1000 == 0
			&& init_complete)
		{
			update_total_num(total_num);
		}

		if (!backend->has_transactions())
		{
			put_db_worker.update(cd_id, object_id, compressed_size, md5sum, last_modified);
		}
		else
		{
			put_db_worker.add(cd_id, transid, key, md5sum, compressed_size, last_modified, generation);
			{
				IScopedLock lock(unsynced_keys_mutex.get());
				SUnsyncedKey& us_key = (*curr_unsynced_keys)[std::make_pair(cd_id, key)];
				if (!us_key.md5sum.empty())
				{
					us_key.md5sum[0] = 'A';
				}
				if (us_key.transid != 0)
				{
					us_key.transid = -1;
				}
				else
				{
					us_key.transid = transid;
					us_key.md5sum = md5sum;
				}
			}
		}

		if (mirror_curr_total >= 0)
		{
			mirror_curr_total += compressed_size;
			++mirror_items;
		}
	}
	else if(!backend->has_transactions())
	{
		IDatabase* db = getDatabase();
		{
			KvStoreDao dao(db);
			if(cd_id==0)
				dao.deletePartialObject(object_id);
			else
				dao.deletePartialObjectCd(object_id);
		}
		db->freeMemory();
	}

	return ret;
}

int64 KvStoreFrontend::new_transaction(int64 cd_id, bool allow_error_event)
{
	Server->Log("Starting new transaction... (cd_id="+convert(cd_id)+")", LL_INFO);
	IDatabase* db = getDatabase();
	int64 id;
	{
		KvStoreDao dao(getDatabase());
		DBScopedSynchronous synchronous(dao.getDb());
		if (cd_id == 0)
			dao.newTransaction();
		else
			dao.newTransactionCd(cd_id);

		id = db->getLastInsertID();
	}
	db->freeMemory();
	return id;
}

bool KvStoreFrontend::transaction_finalize(int64 cd_id, int64 transid, bool complete, bool allow_error_event)
{
	if(complete)
		Server->Log("Marking transaction "+convert(transid)+" as complete... (cd_id=" + convert(cd_id) + ")", LL_INFO);
	else
		Server->Log("Marking transaction " + convert(transid) + " as finalized... (cd_id=" + convert(cd_id) + ")", LL_INFO);

	int64 size;
	std::string md5sum;
	int tries = 4;
	while (tries > 0)
	{
		std::string tkey;
		if(cd_id==0)
			tkey=convert(transid) + (complete ? "_complete" : "_finalized");
		else
			tkey = convert(cd_id)+"_"+convert(transid) + (complete ? "_complete" : "_finalized");

		std::unique_ptr<IFsFile> src(cachefs->openFile(empty_file_path, MODE_READ));

		if(!src)
		{
			Server->Log("Error opening empty file. "+cachefs->lastError(), LL_ERROR);
			return false;
		}

		if (backend->put(prefixKey(tkey),
			src.get(), 0, allow_error_event, md5sum, size))
		{
			tries = -2;
			break;
		}
		--tries;
	}

	if (tries != -2)
		return false;

	if (!sync())
	{
		Server->Log("Syncing failed", LL_ERROR);
		return false;
	}

	IDatabase* db = getDatabase();
	{
		KvStoreDao dao(db);
		DBScopedSynchronous synchronous(dao.getDb());
		DBScopedWriteTransaction scoped_transaction(dao.getDb());

		std::vector<int64> last_finalized;
		if (complete)
		{
			last_finalized = cd_id == 0 ?
				dao.getLastFinalizedTransactions(dao.getMaxCompleteTransaction().value, transid) :
				dao.getLastFinalizedTransactionsCd(cd_id, dao.getMaxCompleteTransactionCd(cd_id).value, transid);
				
		}

		if(cd_id==0)
			dao.setTransactionComplete(complete ? 2 : 1, transid);
		else
			dao.setTransactionCompleteCd(complete ? 2 : 1, cd_id, transid);

		if (complete)
		{
			for (size_t i = 0; i < last_finalized.size(); ++i)
			{
				dao.addTask(TASK_REMOVE_OLD_OBJECTS, last_finalized[i], Server->getTimeSeconds(), cd_id);
			}
			dao.addTask(TASK_REMOVE_OLD_OBJECTS, transid, Server->getTimeSeconds(), cd_id);
		}
	}
	db->freeMemory();
	return true;
}

bool KvStoreFrontend::set_active_transactions(int64 cd_id, const std::vector<int64>& active_transactions )
{
	Server->Log("Setting active transactions... (cd_id=" + convert(cd_id) + ")", LL_INFO);

	std::unique_ptr<IFsFile> empty_f(cachefs->openFile(empty_file_path, MODE_READ));

	if(!empty_f)
	{
		Server->Log("Error opening empty file -2. "+cachefs->lastError(), LL_ERROR);
		return false;
	}

	IDatabase* db = getDatabase();
	KvStoreDao dao(db);
	DBScopedSynchronous synchronous(dao.getDb());

	int64 max_active = cd_id == 0 ?
		dao.getMaxCompleteTransaction().value :
		dao.getMaxCompleteTransactionCd(cd_id).value;

	for (size_t i = 0; i < active_transactions.size(); ++i)
	{
		max_active = (std::max)(max_active, active_transactions[i]);
	}

	std::vector<int64> incomplete_transactions = cd_id == 0 ?
		dao.getIncompleteTransactions(max_active) :
		dao.getIncompleteTransactionsCd(max_active, cd_id);

	for (size_t i = 0; i < incomplete_transactions.size(); ++i)
	{
		if (std::find(active_transactions.begin(), active_transactions.end(), incomplete_transactions[i])
			== active_transactions.end())
		{
			int64 size;
			std::string md5sum;

			const int max_tries = 40;
			int tries = max_tries;
			while (tries > 0)
			{
				std::string tkey;
				if (cd_id == 0)
					tkey = convert(incomplete_transactions[i]) + "_inactive";
				else
					tkey = convert(cd_id)+"_"+convert(incomplete_transactions[i]) + "_inactive";
				if (backend->put(prefixKey(tkey), empty_f.get(), 0, true, md5sum, size))
				{
					tries = -2;
					break;
				}
				--tries;
				retryWait("set_active_transactions", max_tries - tries);				
			}

			if (tries != -2)
				return false;
		}
	}

	sync();

	DBScopedWriteTransaction scoped_transaction(dao.getDb());

	for(size_t i=0;i<incomplete_transactions.size();++i)
	{
		if(std::find(active_transactions.begin(), active_transactions.end(), incomplete_transactions[i])
			==active_transactions.end())
		{
			if (cd_id == 0)
			{
				dao.setTransactionActive(0, incomplete_transactions[i]);
				dao.setTransactionComplete(0, incomplete_transactions[i]);
			}
			else
			{
				dao.setTransactionActiveCd(0, cd_id, incomplete_transactions[i]);
				dao.setTransactionCompleteCd(0, cd_id, incomplete_transactions[i]);
			}
			dao.addTask(TASK_REMOVE_TRANSACTION, incomplete_transactions[i], Server->getTimeSeconds(), cd_id);
		}
	}

	db->freeMemory();

	return true;
}

bool KvStoreFrontend::del(int64 cd_id, const std::vector<std::string>& keys, int64 transid )
{
	std::string hexkey;
	for(size_t i=0;i<keys.size();++i)
	{
		if(!hexkey.empty()) hexkey+=",";
		hexkey += bytesToHex(reinterpret_cast<const unsigned char*>(keys[i].c_str()), keys[i].size());
	}

	Server->Log("Deleting objects "+ hexkey+" (cd_id="+convert(cd_id)+")", LL_INFO);
	hexkey = std::string();

	if(scrub_worker!=nullptr)
	{
		std::vector<std::pair<int64, std::string> > deleted_objects;
		deleted_objects.resize(keys.size());
		for (size_t i = 0; i < keys.size(); ++i)
		{
			deleted_objects[i].first = transid;
			deleted_objects[i].second = keys[i];
		}
		IScopedLock lock(scrub_mutex.get());
		if (scrub_worker != nullptr)
		{
			scrub_worker->add_deleted_objects(cd_id, deleted_objects);
		}
	}

	KvStoreDao dao(getDatabase());
	size_t idx = 0;
	int64 total_del_size = 0;
	int64 total_del_num = 0;
	bool no_reset = true;

	IKvStoreBackend::key_next_fun_t key_next_fun =
		[this, &cd_id, transid, &idx, &keys, &dao, &total_del_size, &total_del_num, &no_reset](IKvStoreBackend::key_next_action_t action, std::string* key) {

		if (action == IKvStoreBackend::key_next_action_t::clear) {
			assert(key==nullptr);
			return true;
		}

		if (action== IKvStoreBackend::key_next_action_t::reset) {
			if(idx!=0)
				no_reset = false;

			idx = 0;
			assert(key==nullptr);
			return true;
		}

		assert(action == IKvStoreBackend::key_next_action_t::next);
		
		KvStoreDao::CdObject cd_object;
		cd_object.exists = false;
		while (!cd_object.exists && idx<keys.size())
		{
			cd_object = cd_id==0 ?
				dao.getObjectInTransid(transid, keys[idx]) :
				dao.getObjectInTransidCd(cd_id, transid, keys[idx]);
			if (!cd_object.exists)
				++idx;
			else if (cd_object.size > 0
				&& no_reset)
				total_del_size += cd_object.size;

			if (no_reset && cd_object.exists)
				++total_del_num;
		}

		if (idx >= keys.size())
			return false;

		*key = this->prefixKey(encodeKey(cd_id, keys[idx], transid));
		++idx;
		return true;
	};

	++total_del_ops;
	
	bool ret = backend->del(key_next_fun, false);


#ifdef ASSERT_CHECK_DELETED
	backend->sync(false, false);
	for (const std::string& key : keys)
	{
		assert(backend->check_deleted(prefixKey(encodeKey(cd_id, key, transid)), std::string()));
	}
#endif

	if(ret)
	{
		sync();

		IDatabase* db = getDatabase();
		{
			KvStoreDao dao(db);
			DBScopedSynchronous synchronous(dao.getDb());
			DBScopedWriteTransaction scoped_transaction(dao.getDb());

			for (size_t i = 0; i < keys.size(); ++i)
			{
				if(cd_id==0)
					dao.addDelMarkerObject(transid, keys[i]);
				else
					dao.addDelMarkerObjectCd(cd_id, transid, keys[i]);
			}
		}
		db->freeMemory();

		objects_total_size -= total_del_size;
		objects_total_num -= total_del_num;
	}

	return ret;
}

size_t KvStoreFrontend::max_del_size()
{
	return backend->max_del_size();
}

int64 KvStoreFrontend::generation_inc(int64 inc )
{
	IScopedLock lock(gen_mutex.get());

	int64 ret = current_generation;
	current_generation += inc;

	if (current_generation - last_persisted_generation >= generation_skip_max ||
		current_generation - last_update_generation >= generation_skip_update)
	{
		last_update_generation = current_generation;

		bool update_ok = false;
		IDatabase* db = getDatabase();
		{
			KvStoreDao dao(getDatabase());
			DBScopedSynchronous synchronous(getDatabase());

			if (current_generation - last_persisted_generation >= generation_skip_max)
			{
				dao.updateGeneration(current_generation);
				update_ok = true;
			}
			else
			{
				IQuery* q = dao.getUpdateGenerationQuery();
				if (q == nullptr)
				{
					q = db->Prepare("UPDATE clouddrive_generation SET generation=?", false);
					dao.setUpdateGenerationQuery(q);
				}
				q->Bind(current_generation);
				if (q->Write(10))
				{
					update_ok = true;
				}
				q->Reset();
			}
		}

		db->freeMemory();

		if (update_ok)
		{
			last_persisted_generation = current_generation;
		}
	}

	return ret;
}

int64 KvStoreFrontend::get_generation(int64 cd_id)
{
	KvStoreDao dao(getDatabase());
	return dao.getGenerationCd(cd_id).value;
}

std::string KvStoreFrontend::get_stats()
{
#if 0
	Server->Log("Getting stats...", LL_INFO);

	int64 size;
	IDatabase* db = getDatabase();
	{
		KvStoreDao dao(getDatabase());
		size = dao.getSize();
	}
	db->freeMemory();

	if (size != objects_total_size)
	{
		Server->Log("Calculated size from db != running size. DB=" + convert(size) + " running objects_total_size=" + convert(objects_total_size)+" diff="+PrettyPrintBytes(abs(objects_total_size-size)), LL_ERROR);
	}

	return "{\"ok\": true, \"size\": "+convert(size)+"}";
#else
	return "{\"ok\": true, \"size\": " + convert(objects_total_size) + ", \"init_complete\": "+convert(objects_init_complete)+" }";
#endif
}

bool KvStoreFrontend::sync()
{
	IScopedWriteLock write_lock(nullptr);
	return sync_lock(write_lock);
}

bool KvStoreFrontend::sync_db()
{
	if (backend->has_transactions())
	{
		put_db_worker.flush();
	}
	return true;
}

bool KvStoreFrontend::sync_lock(IScopedWriteLock& lock)
{
	bool res = db_wal_file->Sync();
	if (!res)
	{
		Server->Log("Syncing wal file " + db_wal_file->getFilename() + " failed. " + os_last_error_str(), LL_ERROR);
	}
	lock.relock(put_shared_mutex.get());
	{
		IScopedLock lock(unsynced_keys_mutex.get());
		std::swap(curr_unsynced_keys, other_unsynced_keys);
	}
	if (backend->has_transactions())
	{
		put_db_worker.flush();
	}
	if (!backend->sync(false, false))
	{
		Server->Log("Syncing backend failed", LL_ERROR);
		res = false;
	}
	{
		IScopedLock lock(unsynced_keys_mutex.get());
		other_unsynced_keys->clear();
	}
	return res;
}

bool KvStoreFrontend::is_put_sync()
{
	return backend->is_put_sync();
}

namespace
{
	class InterfaceLock
	{
	public:
		InterfaceLock(std::string lock_file)
		{
#ifndef _WIN32
			fd = open(lock_file.c_str(), O_RDWR | O_CREAT, S_IRWXU);

			if (fd != -1)
			{
				flock(fd, LOCK_EX);
			}
#endif
		}

		~InterfaceLock()
		{
#ifndef _WIN32
			if (fd != -1)
			{
				flock(fd, LOCK_UN);
				close(fd);
			}
#endif
		}

	private:
		int fd;
	};

	class ImportCallback : public IKvStoreBackend::IListCallback
	{
	public:
		struct STransaction
		{
			int64 cd_id;
			int64 transid;

			STransaction(int64 cd_id, int64 transid)
				: cd_id(cd_id), transid(transid) {}

			bool operator<(const STransaction& other) const
			{
				return std::make_pair(cd_id, transid)
					< std::make_pair(other.cd_id, other.transid);
			}
		};

		std::map<STransaction, bool> transactions;

		ImportCallback(KvStoreDao& dao, bool has_last_modified, int64 total_num_keys)
			: dao(dao), last_modified_key_time(0), n_imported_keys(0), num_noprefix_files(0), num_prefix_files(0),
			has_last_modified(has_last_modified), total_num_keys(total_num_keys)
		{

		}

		~ImportCallback()
		{
			Server->Log("Enumerated a total of " + convert(n_imported_keys) + " objects in bucket.", LL_INFO);
		}

		virtual bool onlineItem(const std::string& key, const std::string& md5sum, int64 size, int64 last_modified)
		{
			if (key == "cd_magic_file"
				|| key == "cd_num_file" )
			{
				return true;
			}

			if(key.find('_')==std::string::npos)
			{
				std::string err = "Key "+key+" has unknown format";
				Server->Log(err, LL_ERROR);
			}
			std::string key_part = key;
			if (key.find('/') != std::string::npos)
			{
				size_t lslash = key.find_last_of('/');
				if (key.size() <= lslash + 1)
				{
					std::string err = "Key " + key + " has unknown format (2)";
					Server->Log(err, LL_ERROR);
					return true;
				}
				key_part = key.substr(lslash + 1);

				if (num_noprefix_files>0
					&& num_prefix_files == 0)
				{
					Server->Log("Key " + key + " has prefix while there are keys with no prefix", LL_WARNING);
				}
				++num_prefix_files;
			}
			else
			{
				if (num_prefix_files>0
					&& num_noprefix_files==0)
				{
					Server->Log("Key " + key+" has no prefix while there are keys with prefix", LL_WARNING);
				}
				++num_noprefix_files;
			}
			int64 transid = os_atoi64(getuntil("_", key_part).c_str());

			std::string hexkey = getafter("_", key_part);

			int64 cd_id = 0;
			if (hexkey.find("_") != std::string::npos)
			{
				cd_id = transid;
				transid = os_atoi64(getuntil("_", hexkey).c_str());
				hexkey = getafter("_", key_part);
			}

			if (hexkey == bytesToHex("test_file"))
			{
				if (key_part != key)
				{
					--num_prefix_files;
				}
				else
				{
					--num_noprefix_files;
				}
				return true;
			}

			auto trans_it = transactions.find(STransaction(cd_id, transid));

			if(trans_it==transactions.end())
			{
				if(cd_id==0)
					dao.insertTransaction(transid);
				else
					dao.insertTransactionCd(transid, cd_id);

				trans_it = transactions.insert(std::make_pair(STransaction(cd_id, transid), false)).first;
			}
			
			if(hexkey=="inactive")
			{
				Server->Log("Imported inactive transaction " + convert(transid)+" cd_id "+convert(cd_id), LL_INFO);
				if(cd_id==0)
					dao.setTransactionActive(0, transid);
				else
					dao.setTransactionActiveCd(0, cd_id, transid);
			}
			else if(hexkey=="complete")
			{
				Server->Log("Imported complete transaction " + convert(transid) + " cd_id " + convert(cd_id), LL_INFO);
				if(cd_id==0)
					dao.setTransactionComplete(2, transid);
				else
					dao.setTransactionCompleteCd(2, cd_id, transid);
			}
			else if (hexkey == "finalized")
			{
				Server->Log("Imported finalized transaction " + convert(transid) + " cd_id " + convert(cd_id), LL_INFO);
				if(cd_id==0)
					dao.setTransactionComplete(1, transid);
				else
					dao.setTransactionCompleteCd(1, cd_id, transid);
			}
			else if(IsHex(hexkey))
			{
				if (last_modified_key_time == 0)
				{
					Server->Log("Import from backend started...");
				}

				std::string bkey = hexToBytes(hexkey);

				if (cd_id == 0)
				{
					if (!trans_it->second &&
						dao.getLowerTransidObject(bkey, transid).exists)
					{
						trans_it->second = true;
					}

					if (has_last_modified)
					{
						dao.addObject2(transid, bkey, md5sum, size, last_modified);
					}
					else
					{
						dao.addObject(transid, bkey, md5sum, size);
					}
				}
				else
				{
					if (!trans_it->second &&
						dao.getLowerTransidObjectCd(cd_id, bkey, transid).exists)
					{
						trans_it->second = true;
					}

					if (has_last_modified)
					{
						dao.addObject2Cd(cd_id, transid, bkey, md5sum, size, last_modified);
					}
					else
					{
						dao.addObjectCd(cd_id, transid, bkey, md5sum, size);
					}
				}
				

				if(last_modified>last_modified_key_time
					&& key.find('_') != std::string::npos)
				{
					last_modified_key = key;
					last_modified_key_time = last_modified;
					last_modified_md5sum = md5sum;
				}

				++n_imported_keys;

				if (n_imported_keys % 100==0)
				{
					Server->Log("Enumerated "+convert(n_imported_keys)+" objects in bucket...", LL_INFO);

					std::string import_state = "{\"state\": \"importing\", \n"
						"\"curr_keys\": " + convert(n_imported_keys) + ", \n"
						"\"total_keys\": " + convert(total_num_keys + 1000) + "}\n";

					setMountStatus(import_state);
				}
			}
			else
			{
				Server->Log("Unknown object (not hex encoded) " + hexkey + " key "+key, LL_WARNING);
			}

			return true;
		}

		std::string lastModifiedKey()
		{
			return last_modified_key;
		}

		std::string lastModifiedMd5sum()
		{
			return last_modified_md5sum;
		}

		size_t hasPrefixFiles()
		{
			return num_prefix_files;
		}

		size_t hasNoPrefixFiles()
		{
			return num_noprefix_files;
		}

	private:
		KvStoreDao& dao;

		std::string last_modified_key;
		std::string last_modified_md5sum;
		int64 last_modified_key_time;
		int64 n_imported_keys;
		size_t num_prefix_files;
		size_t num_noprefix_files;
		bool has_last_modified;
		int64 total_num_keys;
	};
}

bool KvStoreFrontend::importFromBackend( KvStoreDao& dao )
{
	IFsFile* tmp_f = Server->openTemporaryFile();
	if (tmp_f == nullptr)
	{
		std::string err = "Error opening temporary file (num file). " + os_last_error_str();
		Server->Log(err, LL_ERROR);
		return false;
	}
	ScopedDeleteFile tmp_f_del(tmp_f);
	std::string ret_md5sum;
	unsigned int get_status;
	int64 import_total_num_files = -1;
	if (backend->get("cd_num_file", std::string(), IKvStoreBackend::GetDecrypted,
		false, tmp_f, ret_md5sum, get_status))
	{
		if (tmp_f->Size() < 1 * 1024 * 1024)
		{
			std::string num_data = tmp_f->Read(0LL, static_cast<_u32>(tmp_f->Size()));

			CRData rnumdata(num_data.data(), num_data.size());

			char ver = -1;
			if (!rnumdata.getChar(&ver)
				|| ver != 0)
			{
				Server->Log("cd_num_file version wrong. ver="+convert((int)ver), LL_ERROR);
			}
			else if(!rnumdata.getVarInt(&import_total_num_files))
			{
				Server->Log("Error reading total num files", LL_ERROR);
			}
			else
			{
				Server->Log("Import total num files: " + convert(import_total_num_files), LL_INFO);
			}
		}
		else
		{
			Server->Log("cd_num_file too large ("+PrettyPrintBytes(tmp_f->Size())+")", LL_WARNING);
		}
	}
	else
	{
		Server->Log("Error retrieving cd_num_file", LL_WARNING);
	}

	Server->Log("Importing from backend...", LL_INFO);

	DBScopedSynchronous synchronous(dao.getDb());
	DBScopedWriteTransaction scoped_transaction(dao.getDb());

	ImportCallback import_callback(dao, has_last_modified, import_total_num_files);

	if(!backend->list(&import_callback))
	{
		Server->Log("Error enumerating objects in bucket", LL_ERROR);
		setMountStatus("{\"state\": \"error\", \"err\": \"Error enumerating objects in bucket\"}");
		return false;
	}

	setMountStatus("{\"state\": \"get_generation\"}");

	if(!import_callback.lastModifiedKey().empty())
	{
		Server->Log("Retrieving last object to get current generation...", LL_INFO);

		IFsFile* temp_file = Server->openTemporaryFile();
		ScopedDeleteFile delete_last_f(temp_file);

		std::string md5sum_ret;
		unsigned int get_status;
		bool b = backend->get(import_callback.lastModifiedKey(), import_callback.lastModifiedMd5sum(), 0, true, temp_file, md5sum_ret, get_status);		

		if(!b)
		{
			Server->Log("Error retrieving last modified file", LL_ERROR);
			return false;
		}

		int64 generation=0;

		if(!read_generation(temp_file, 0, generation))
		{
			return false;
		}

		generation += 100;

		dao.insertGeneration(generation);
	}
	else
	{
		Server->Log("Found no object. Setting generation to zero...", LL_INFO);

		dao.insertGeneration(0);
	}


	bool has_prefix = true;
	if (import_callback.hasNoPrefixFiles() > import_callback.hasPrefixFiles())
	{
		Server->Log("More files with no prefix (" + convert(import_callback.hasNoPrefixFiles()) + ") then with prefix (" + convert(import_callback.hasPrefixFiles()) + "). Using no prefix", LL_WARNING);
		has_prefix = false;
	}
	if (has_prefix)
	{
		dao.setMiscValue("has_prefix", "1");
	}

	for (auto trans_it : import_callback.transactions)
	{
		if(trans_it.second)
			dao.addTask(TASK_REMOVE_OLD_OBJECTS, trans_it.first.transid, 
				Server->getTimeSeconds(), trans_it.first.cd_id);
	}

	return true;
}

int64 KvStoreFrontend::get_uploaded_bytes()
{
	return backend->get_uploaded_bytes();
}

int64 KvStoreFrontend::get_downloaded_bytes()
{
	return backend->get_downloaded_bytes();
}

IDatabase* KvStoreFrontend::getDatabase()
{
	 return Server->getDatabase(Server->getThreadID(), CLOUDDRIVE_DB);
}

std::string KvStoreFrontend::encodeKey( const std::string& key, int64 transid )
{
	return std::to_string(transid) + "_" + bytesToHex(reinterpret_cast<const unsigned char*>(key.c_str()), key.size());
}

std::string KvStoreFrontend::encodeKey(int64 cd_id, const std::string& key, int64 transid)
{
	return encodeKeyStatic(cd_id, key, transid);
}

std::string KvStoreFrontend::encodeKeyStatic(int64 cd_id, const std::string& key, int64 transid)
{
	if (cd_id == 0)
		return encodeKey(key, transid);

	return std::to_string(cd_id) + "_" + std::to_string(transid) + "_" + bytesToHex(reinterpret_cast<const unsigned char*>(key.c_str()), key.size());
}

std::string KvStoreFrontend::prefixKey(const std::string & key)
{
	if (!with_prefix)
		return key;

	std::string md5sum = Server->GenerateHexMD5(key);
	return md5sum.substr(0, 3) + "/" + md5sum.substr(3, 2) + "/" + key;
}

IKvStoreBackend * KvStoreFrontend::getBackend()
{
	return backend;
}

void KvStoreFrontend::start_scrub(ScrubAction action, const std::string & position)
{
	IScopedLock lock(scrub_mutex.get());

	if (scrub_worker != nullptr)
	{
		scrub_worker->quit();
		THREADPOOL_TICKET t = scrub_worker->ticket;
		lock.relock(nullptr);
		Server->getThreadPool()->waitFor(t);
		lock.relock(scrub_mutex.get());
		if (scrub_worker != nullptr)
		{
			delete scrub_worker;
			scrub_worker = nullptr;
		}
	}
	
	scrub_worker = new ScrubWorker(action, backend, backend_mirror, this,
		background_worker, position, has_last_modified);
	scrub_worker->ticket = Server->getThreadPool()->execute(scrub_worker, strScrubAction(action));
}

std::string KvStoreFrontend::scrub_position()
{
	IScopedLock lock(scrub_mutex.get());
	
	if (scrub_worker != nullptr)
	{
		return scrub_worker->get_position();
	}
	return std::string();
}

std::string KvStoreFrontend::scrub_stats()
{
	IScopedLock lock(scrub_mutex.get());

	if (scrub_worker != nullptr)
	{
		return scrub_worker->stats();
	}

	return std::string();
}

void KvStoreFrontend::stop_scrub()
{
	IScopedLock lock(scrub_mutex.get());
	if (scrub_worker != nullptr)
	{
		scrub_worker->quit();
		if (Server->getThreadPool()->waitFor(scrub_worker->ticket, 1000))
		{
			delete scrub_worker;
			scrub_worker = nullptr;
		}
	}
}

bool KvStoreFrontend::is_background_worker_enabled()
{
	return !background_worker.is_paused();
}

bool KvStoreFrontend::is_background_worker_running()
{
	return background_worker.is_runnnig();
}

void KvStoreFrontend::enable_background_worker(bool b)
{
	background_worker.set_pause(!b);
}

void KvStoreFrontend::set_background_worker_result_fn(const std::string& result_fn)
{
	background_worker.set_result_fn(result_fn);
}

bool KvStoreFrontend::start_background_worker()
{
	if (!has_background_task())
		return false;

	size_t nwork = background_worker.get_nwork();

	enable_background_worker(true);

	while (nwork == background_worker.get_nwork())
	{
		Server->wait(100);

		if (!has_background_task() &&
			nwork == background_worker.get_nwork())
			return false;
	}

	return true;
}

bool KvStoreFrontend::has_background_task()
{
	IDatabase* db = getDatabase();
	KvStoreDao dao(db);
	return dao.getTask(Server->getTimeSeconds() - task_delay).exists;
}

void KvStoreFrontend::start_scrub_sync_test1()
{
	scrub_sync_test1 = true;
}

void KvStoreFrontend::start_scrub_sync_test2()
{
	scrub_sync_test2 = true;
}

bool KvStoreFrontend::reupload(int64 transid_start, int64 transid_stop, IKvStoreBackend * old_backend)
{
	if (transid_stop == 0)
	{
		transid_stop = LLONG_MAX;
	}
	IDatabase* db = getDatabase();
	KvStoreDao dao(db);
	IQuery* q_iter = db->Prepare("SELECT trans_id, tkey, md5sum FROM clouddrive_objects WHERE trans_id>=? AND trans_id<=? AND size!=-1");
	q_iter->Bind(transid_start);
	q_iter->Bind(transid_stop);
	std::vector<KvStoreDao::CdIterObject> new_objects;
	ScopedDatabaseCursor cur(q_iter->Cursor());
	bool ret = true;
	db_single_result res;
	while (cur.next(res))
	{
		IFsFile* tmp_f = Server->openTemporaryFile();
		if (tmp_f == nullptr)
		{
			Server->Log("Error opening temporary file. " + os_last_error_str(), LL_ERROR);
			ret = false;
			break;
		}
		ScopedDeleteFile tmp_f_delete(tmp_f);

		std::string key = res["tkey"];
		int64 transid = watoi64(res["trans_id"]);
		std::string fkey = prefixKey(encodeKey(key, transid));
		std::string md5sum = res["md5sum"];

		std::string ret_md5sum;
		unsigned int get_status;
		int flags = IKvStoreBackend::GetDecrypted;
		if (!old_backend->get(fkey, md5sum, flags, false, tmp_f, ret_md5sum, get_status))
		{
			if (!backend->get(fkey, md5sum, flags, false, tmp_f, ret_md5sum, get_status))
			{
				Server->Log("Error reading " + fkey + ". status=" + convert(get_status), LL_ERROR);
				ret = false;
				break;
			}
		}
		else
		{
			int64 size = 0;
			std::string new_md5sum;
			if (backend->put(fkey, tmp_f, 0, false, ret_md5sum, size))
			{
				Server->Log("Successfully rewritten " + fkey);
				KvStoreDao::CdIterObject new_obj;
				new_obj.md5sum = ret_md5sum;
				new_obj.size = size;
				new_obj.tkey = key;
				new_obj.trans_id = transid;
				new_objects.push_back(new_obj);
			}
		}
	}

	for (const KvStoreDao::CdIterObject& obj : new_objects)
	{
		dao.updateObjectSearch(obj.md5sum, obj.size, obj.tkey, obj.trans_id);
	}
	db->freeMemory();
	return ret;
}

std::string KvStoreFrontend::meminfo()
{
	std::string ret;
	{
		IScopedLock lock(unsynced_keys_mutex.get());
		ret += "##KvStoreFrontend:\n";
		ret += "  unsynced_keys_a: " + convert(unsynced_keys_a.size()) + " * " + PrettyPrintBytes(sizeof(std::string)+sizeof(SUnsyncedKey)) + " = "+PrettyPrintBytes(unsynced_keys_a.size()*(sizeof(std::string) + sizeof(SUnsyncedKey)))+"\n";
		ret += "  unsynced_keys_b: " + convert(unsynced_keys_b.size()) + " * " + PrettyPrintBytes(sizeof(std::string)+ sizeof(SUnsyncedKey)) + " = " + PrettyPrintBytes(unsynced_keys_b.size()*(sizeof(std::string) + sizeof(SUnsyncedKey)))+ "\n";
	}
	ret += put_db_worker.meminfo();
	{
		IScopedLock lock(scrub_mutex.get());
		if (scrub_worker != nullptr)
		{
			ret += scrub_worker->meminfo();
		}
	}
	ret += backend->meminfo();
	ret += background_worker.meminfo();
	return ret;
}

void KvStoreFrontend::retry_all_deletion()
{
	IDatabase* db = getDatabase();
	KvStoreDao dao(db);
	dao.insertAllDeletionTasks();
}

int64 KvStoreFrontend::get_total_balance_ops()
{
	return total_balance_ops;
}

void KvStoreFrontend::incr_total_del_ops()
{
	++total_del_ops;
}

int64 KvStoreFrontend::get_total_del_ops()
{
	return total_del_ops;
}

bool KvStoreFrontend::has_backend_key(const std::string & key, 
	std::string& md5sum, bool update_md5sum)
{
	if (key == "cd_magic_file")
	{
		return true;
	}

	if (key.find('_') == std::string::npos)
	{
		std::string err = "Key " + key + " has unknown format (has_backend_key)";
		Server->Log(err, LL_ERROR);
	}
	std::string key_part = key;
	if (key.find('/') != std::string::npos)
	{
		size_t lslash = key.find_last_of('/');
		if (key.size() <= lslash + 1)
		{
			std::string err = "Key " + key + " has unknown format (2) (has_backend_key)";
			Server->Log(err, LL_ERROR);
			return true;
		}
		key_part = key.substr(lslash + 1);
	}

	int64 transid = os_atoi64(getuntil("_", key_part).c_str());

	std::string hexkey = getafter("_", key_part);

	if (hexkey == bytesToHex("test_file"))
	{
		return true;
	}

	IDatabase* db = getDatabase();
	KvStoreDao dao(db);

	if (hexkey == "inactive")
	{
		KvStoreDao::STransactionProperties props = dao.getTransactionProperties(transid);
		return props.exists
			&& props.active == 0;
	}
	else if (hexkey == "complete")
	{
		KvStoreDao::STransactionProperties props = dao.getTransactionProperties(transid);
		return props.exists
			&& props.completed == 2;
	}
	else if (hexkey == "finalized")
	{
		KvStoreDao::STransactionProperties props = dao.getTransactionProperties(transid);
		return props.exists
			&& props.completed == 1;
	}
	else if (IsHex(hexkey))
	{
		std::string bkey = hexToBytes(hexkey);
		KvStoreDao::CdObject obj = dao.getObjectInTransid(transid, bkey);
		if (obj.exists)
		{
			if (update_md5sum)
				dao.updateObjectMd5sum(md5sum, transid, bkey);
			md5sum = obj.md5sum;
			return true;
		}
		else
			return false;
	}
	else
	{
		Server->Log("Unknown object (not hex encoded) " + hexkey + " key " + key+" (has_backend_key)", LL_WARNING);
	}

	return false;
}

namespace
{
	class BackendDelWorker : public IThread
	{
		IPipe* wpipe;
		bool background_queue;
		const std::vector<IKvStoreBackend::key_next_fun_t>& key_next_funs;
		const std::vector<IKvStoreBackend::locinfo_next_fun_t>& locinfo_next_funs;
		IKvStoreBackend* backend;
		volatile bool& res;
	public:
		BackendDelWorker(IPipe* wpipe, const std::vector<IKvStoreBackend::key_next_fun_t>& key_next_funs,
			const std::vector<IKvStoreBackend::locinfo_next_fun_t>& locinfo_next_funs,
			IKvStoreBackend* backend, bool background_queue, volatile bool& res)
			: wpipe(wpipe),
			backend(backend), res(res),
			background_queue(background_queue),
			key_next_funs(key_next_funs),
			locinfo_next_funs(locinfo_next_funs)
		{

		}

		void operator()()
		{
			while (true)
			{
				std::string msg;
				if (wpipe->Read(&msg) == 0)
				{
					wpipe->Write(std::string());
					break;
				}

				CRData data(msg.data(), msg.size());
				int64 idx;
				bool b = data.getVarInt(&idx);
				assert(b);

				if (locinfo_next_funs[idx] != nullptr)
				{
					if (!backend->del(key_next_funs[idx],
						locinfo_next_funs[idx], background_queue))
					{
						res = false;
					}
					locinfo_next_funs[idx](IKvStoreBackend::key_next_action_t::clear, nullptr);
				}
				else
				{
					size_t retry_n = 0;
					bool del_res;
					while (!(del_res = backend->del(key_next_funs[idx], background_queue))
						&& retry_n<8)
					{
						Server->Log("Deleting objects failed (2). Retrying... Try "+convert(retry_n), LL_WARNING);
						retryWait("BackendDel", ++retry_n);
						key_next_funs[idx](IKvStoreBackend::key_next_action_t::reset, nullptr);
					}

					if (!del_res)
						res = false;
				}

				key_next_funs[idx](IKvStoreBackend::key_next_action_t::clear, nullptr);
			}

			delete this;
		}
	};
}

void KvStoreFrontend::submit_del_post_flush()
{
	KvStoreDao dao(getDatabase());
	dao.setMiscValue("submit_del_done", "1");
}

bool KvStoreFrontend::backend_del_parallel(const std::vector<IKvStoreBackend::key_next_fun_t>& key_next_funs,
	const std::vector<IKvStoreBackend::locinfo_next_fun_t>& locinfo_next_funs,
	bool background_queue)
{
	if (backend->num_del_parallel() <= 1)
	{
		for (size_t i = 0; i < key_next_funs.size(); ++i)
		{
			if (locinfo_next_funs[i] != nullptr)
			{
				if (!backend->del(key_next_funs[i], locinfo_next_funs[i], background_queue))
				{
					return false;
				}
				locinfo_next_funs[i](IKvStoreBackend::key_next_action_t::clear, nullptr);
			}
			else
			{
				size_t retry_n = 0;
				bool res;
				while (!(res=backend->del(key_next_funs[i], background_queue))
					&& retry_n<8)
				{
					Server->Log("Deleting objects failed. Retrying... Try " + convert(retry_n), LL_WARNING);
					retryWait("BackendDel", ++retry_n);
					key_next_funs[i](IKvStoreBackend::key_next_action_t::reset, nullptr);
				}
				if (!res)
					return false;
			}
			key_next_funs[i](IKvStoreBackend::key_next_action_t::clear, nullptr);
		}
		return true;
	}

	
	std::unique_ptr<IPipe> wpipe(Server->createMemoryPipe());
	for (size_t i = 0; i < key_next_funs.size(); ++i)
	{
		CWData wd;
		wd.addVarInt(i);
		wpipe->Write(wd.getDataPtr(), wd.getDataSize());
	}

	wpipe->Write(std::string());

	std::vector<THREADPOOL_TICKET> tt;
	volatile bool res = true;
	for (size_t i = 0; i < backend->num_del_parallel(); ++i)
	{
		tt.push_back(Server->getThreadPool()->execute(
			new BackendDelWorker(wpipe.get(), key_next_funs,
				locinfo_next_funs, backend, background_queue, res),
			"bdel worker"));
	}

	Server->getThreadPool()->waitFor(tt);

	return res;
}

void KvStoreFrontend::add_last_modified_column()
{
	db_results res = getDatabase()->Read("SELECT sql FROM sqlite_master WHERE type='table' AND name='clouddrive_objects'");
	if (!res.empty())
	{
		std::string sql = res[0]["sql"];
		if (sql.find("last_modified") == std::string::npos)
		{
			res = getDatabase()->Read("SELECT * FROM clouddrive_objects LIMIT 1");
			if (res.empty())
			{
				getDatabase()->Write("ALTER TABLE clouddrive_objects ADD last_modified INTEGER");
				has_last_modified = true;
			}
		}
		else
		{
			has_last_modified = true;
		}
	}
}

void KvStoreFrontend::add_cd_id_tasks_column()
{
	db_results res = getDatabase()->Read("SELECT sql FROM sqlite_master WHERE type='table' AND name='tasks'");
	if (!res.empty())
	{
		std::string sql = res[0]["sql"];
		if (sql.find("cd_id") == std::string::npos)
		{
			getDatabase()->Write("ALTER TABLE tasks ADD cd_id INTEGER DEFAULT 0");
		}
	}
}

void KvStoreFrontend::add_created_column()
{
	db_results res = getDatabase()->Read("SELECT sql FROM sqlite_master WHERE type='table' AND name='tasks'");
	if (!res.empty())
	{
		std::string sql = res[0]["sql"];
		if (sql.find("created") == std::string::npos)
		{
			getDatabase()->Write("ALTER TABLE tasks ADD created INTEGER");
		}
	}
}

void KvStoreFrontend::add_mirrored_column()
{
	db_results res = getDatabase()->Read("SELECT sql FROM sqlite_master WHERE type='table' AND name='clouddrive_objects'");
	if (!res.empty())
	{
		std::string sql = res[0]["sql"];
		if (sql.find("mirrored") == std::string::npos)
		{
			getDatabase()->Write("ALTER TABLE clouddrive_objects ADD mirrored INTEGER DEFAULT 0");
			getDatabase()->Write("ALTER TABLE clouddrive_transactions ADD mirrored INTEGER DEFAULT 0");
		}
	}

	getDatabase()->Write("CREATE INDEX IF NOT EXISTS clouddrive_objects_mirrored "
		"ON clouddrive_objects(mirrored) WHERE mirrored=0");
}

int64 KvStoreFrontend::get_backend_mirror_del_log_wpos()
{
	IScopedLock lock(mirror_del_log_mutex.get());
	return backend_mirror_del_log_wpos;
}

bool KvStoreFrontend::update_total_num(int64 num)
{
	IFsFile* new_f = Server->openTemporaryFile();
	if (new_f == nullptr)
	{
		std::string err = "Error opening temporary file (update_total_num). " + os_last_error_str();
		Server->Log(err, LL_ERROR);
		return false;
	}
	ScopedDeleteFile new_f_del(new_f);

	CWData wdata;
	wdata.addChar(0);
	wdata.addVarInt(num);

	if (new_f->Write(wdata.getDataPtr(), wdata.getDataSize()) != wdata.getDataSize())
	{
		std::string err = "Error writing to cd_num_file file. " + os_last_error_str();
		Server->Log(err, LL_ERROR);
		return false;
	}

	std::string md5sum;
	int64 size;
	if (!backend->put("cd_num_file", new_f, 0, true, md5sum, size))
	{
		std::string err = "Error uploading cd_num_file file.";
		Server->Log(err, LL_ERROR);
		return false;
	}
	else
	{
		Server->Log("Updated total num with new num " + convert(num), LL_INFO);
	}

	return true;
}

const unsigned int del_mirror_magic = 0x4A00B231;

bool KvStoreFrontend::log_del_mirror(const std::string & fn)
{
	if (backend_mirror_del_log.get() == nullptr)
	{
		Server->Log("Log del mirror disabled for " + fn, LL_INFO);
		return true;
	}

	IScopedLock lock(mirror_del_log_mutex.get());

	CWData wdata;


	wdata.addUInt(del_mirror_magic);
	wdata.addUShort(0);
	wdata.addString2(fn);
	unsigned short data_len = static_cast<unsigned short>(wdata.getDataSize());
	memcpy(wdata.getDataPtr() + sizeof(del_mirror_magic), &data_len, sizeof(data_len));

	_u32 w = backend_mirror_del_log->Write(backend_mirror_del_log_wpos, wdata.getDataPtr(), wdata.getDataSize());
	if (w != wdata.getDataSize())
	{
		Server->Log("Logging for cloud mirror failed at pos "+convert(backend_mirror_del_log_wpos)
			+" size "+convert((size_t)wdata.getDataSize())+". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	Server->Log("Log del mirror " + fn, LL_INFO);

	backend_mirror_del_log_wpos += w;

	return true;
}

std::string KvStoreFrontend::next_log_del_mirror_item()
{
	char rs[sizeof(unsigned int) + sizeof(unsigned short)];

	while (true)
	{
		if (backend_mirror_del_log_rpos >= backend_mirror_del_log_wpos)
			return std::string();

		if (backend_mirror_del_log->Read(backend_mirror_del_log_rpos, rs, sizeof(rs)) != sizeof(rs))
			return std::string();

		unsigned int magic;
		memcpy(&magic, rs, sizeof(magic));
		if (magic == del_mirror_magic)
		{
			backend_mirror_del_log_rpos += sizeof(rs);
			break;
		}
		++backend_mirror_del_log_rpos;
	}


	unsigned short rsize;
	memcpy(&rsize, &rs[sizeof(unsigned int)], sizeof(rsize));

	unsigned short msg_size = rsize - sizeof(unsigned int) - sizeof(unsigned short);

	std::string msg_buf;
	msg_buf.resize(msg_size);

	if (backend_mirror_del_log->Read(backend_mirror_del_log_rpos, &msg_buf[0], msg_size) != msg_size)
		return std::string();

	backend_mirror_del_log_rpos += msg_size;

	CRData rdata(msg_buf.data(), msg_buf.size());

	std::string ret;
	if (!rdata.getStr2(&ret))
		return std::string();

	return ret;
}

void KvStoreFrontend::set_backend_mirror_del_log_rpos(int64 p)
{
	backend_mirror_del_log_rpos = p;
}

int64 KvStoreFrontend::get_backend_mirror_del_log_rpos()
{
	return backend_mirror_del_log_rpos;
}

void KvStoreFrontend::stop_defrag()
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreBackendLocal* bl = dynamic_cast<KvStoreBackendLocal*>(backend);
	if (bl != nullptr)
	{
		Server->Log("Stopping defrag...");
		bl->stop_defrag();
	}
#endif
}

bool KvStoreFrontend::set_all_mirrored(bool b)
{
	IDatabase* db = getDatabase();
	DBScopedWriteTransaction db_trans(db);

	add_mirrored_column();
	Server->deleteFile(ExtractFilePath(db_path) + os_file_sep() + "mirror_del.log");
	db->Write(std::string("UPDATE clouddrive_objects SET mirrored=") + (b?"1":"0"));
	db->Write(std::string("UPDATE clouddrive_transactions SET mirrored=") + (b ? "1" : "0"));

	return true;
}

std::string KvStoreFrontend::mirror_stats()
{
	return "{ \"state\": " + convert(mirror_state) + ",\n"
		+ "\"curr_pos\": " + convert(mirror_curr_pos) + ",\n"
		+ "\"curr_total\": " + convert(mirror_curr_total) + ",\n"
		+ "\"items\": " + convert(mirror_items) + " }\n";
}

bool KvStoreFrontend::start_defrag(const std::string& settings)
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreBackendLocal* backendl = dynamic_cast<KvStoreBackendLocal*>(backend);
	if (backendl != nullptr)
	{
		if (backendl->start_defrag(settings))
		{
			return sync();
		}
	}
#endif
	return false;
}

void KvStoreFrontend::operator()()
{
	KvStoreDao dao(getDatabase());
	KvStoreDao::SSize sizes = dao.getSize();
	if (sizes.exists)
	{
		objects_total_size += sizes.size;
		objects_total_num += sizes.count;
	}
	objects_init_complete = true;
}

bool KvStoreFrontend::fast_write_retry()
{
	return backend->fast_write_retry();
}

bool KvStoreFrontend::submit_del_cd(int64 cd_id, IHasKeyCallback* p_has_key_callback, int64 ctransid, bool& need_flush)
{
	need_flush = false;

	if (!allow_import)
		return true;

	KvStoreDao dao(getDatabase());

	if (dao.getMiscValue("submit_del_done").value == "1")
		return true;

	DBScopedWriteTransaction db_trans(getDatabase());

	IQuery* q_iter;
	if (cd_id == 0)
		q_iter = getDatabase()->Prepare("SELECT trans_id, tkey, md5sum, size FROM clouddrive_objects WHERE size!=-1 ORDER BY tkey ASC, trans_id ASC", false);
	else
		q_iter = getDatabase()->Prepare("SELECT trans_id, tkey, md5sum, size FROM clouddrive_objects_cd WHERE cd_id=" + convert(cd_id) + " AND size!=-1 ORDER BY tkey ASC, trans_id ASC", false);

	IDatabaseCursor* cur = q_iter->Cursor();

	bool ret = true;
	db_single_result res;

	while (cur->next(res))
	{
		if (!p_has_key_callback->hasKey(res["tkey"]))
		{
			std::string tkey = res["tkey"];
			int64 transid = watoi64(res["trans_id"]);

			Server->Log("Deleting " + bytesToHex(tkey) + " transid " + convert(transid) 
				+ " in transid "+convert(ctransid)+" (bitmap says no data is at this location)", LL_INFO);
			
			if (cd_id == 0)
				dao.addDelMarkerObject(ctransid, tkey);
			else
				dao.addDelMarkerObjectCd(cd_id, ctransid, tkey);
		}
	}

	if (cur->has_error())
		ret = false;

	getDatabase()->destroyQuery(q_iter);

	if (ret)
		need_flush = true;

	return ret;
}

bool KvStoreFrontend::submit_del(IHasKeyCallback* p_has_key_callback, int64 ctransid, bool& need_flush)
{
	return submit_del_cd(0, p_has_key_callback, ctransid, need_flush);
}

KvStoreFrontend::BackgroundWorker::BackgroundWorker(IKvStoreBackend* backend, KvStoreFrontend* frontend,
	bool manual_run, bool multi_trans_delete)
	: do_quit(false), backend(backend), frontend(frontend), pause_mutex(Server->createMutex()), pause(false), paused(false),
	object_collector_size(0), object_collector_size_uncompressed(0), manual_run(manual_run), running(false),
	nwork(0), startup_finished(false), scrub_pause(false), mirror_pause(false), multi_trans_delete(multi_trans_delete)
{

}

void KvStoreFrontend::BackgroundWorker::operator()()
{
	IDatabase* db=Server->getDatabase(Server->getThreadID(), CLOUDDRIVE_DB);
	KvStoreDao dao(db);

	{
		size_t n_wait = 0;
		IScopedLock lock(pause_mutex.get());
		pause = true;
		while (!do_quit
			&& n_wait < 15*60
			&& pause
			&& !scrub_pause
			&& !mirror_pause)
		{
			lock.relock(nullptr);
			Server->wait(1000);
			lock.relock(pause_mutex.get());
			++n_wait;
		}

		if (!pause_set)
			pause = false;
	}


	while(!do_quit)
	{
		KvStoreDao::Task task;
		IScopedLock pause_lock(pause_mutex.get());
		if (!startup_finished)
		{
			task = dao.getActiveTask();

			if (!task.exists)
				startup_finished = true;
		}

		if (startup_finished)
		{
			if (!pause)
			{
				pause_lock.relock(nullptr);
				task = dao.getTask(Server->getTimeSeconds() - frontend->task_delay);
			}
			else
			{
				pause_lock.relock(nullptr);
				task.exists = false;
			}
		}
		else
		{
			pause_lock.relock(nullptr);
		}

		if(task.exists)
		{
			std::vector<KvStoreDao::Task> tasks;

			{
				IScopedLock pause_lock2(pause_mutex.get());
				++nwork;
			}

			dao.setTaskActive(task.id);

			running = true;

			size_t retry_n = 0;
			switch (task.task_id)
			{
			case TASK_REMOVE_OLD_OBJECTS:
			{
				std::vector<int64> trans_ids;
				trans_ids.push_back(task.trans_id);

				if (multi_trans_delete)
				{
					tasks = dao.getTasks(Server->getTimeSeconds() - frontend->task_delay,
							task.task_id, task.cd_id);

					for (auto& ctask : tasks)
					{
						if (ctask.id != task.id)
						{
							trans_ids.push_back(ctask.trans_id);
							dao.setTaskActive(ctask.id);
						}
					}
				}

				while (!removeOldObjects(dao, trans_ids, task.cd_id))
				{
					retryWait("RemoveOldObjects", ++retry_n);
				}
			} break;
			case TASK_REMOVE_TRANSACTION:
				while (!removeTransaction(dao, task.trans_id, task.cd_id))
				{
					retryWait("RemoveTransaction", ++retry_n);
				}
				break;
			default:
				Server->Log("Unknown task "+convert(task.id)+" with id "+convert(task.task_id), LL_ERROR);
				break;
			}

			dao.removeTask(task.id);
			for (auto& ctask : tasks)
			{
				if (ctask.id != task.id)
					dao.removeTask(ctask.id);
			}
		}
		else
		{
			if (running)
			{
				dao.getDb()->freeMemory();
				Server->mallocFlushTcache();
			}

			running = false;

			if (manual_run)
				pause = true;

			{
				IScopedLock lock(pause_mutex.get());
				while (pause || scrub_pause ||
					mirror_pause)
				{
					paused = true;
					lock.relock(nullptr);
					Server->wait(1000);
					lock.relock(pause_mutex.get());
				}
				paused = false;
			}

			Server->wait(1000);
		}
	}
}

void KvStoreFrontend::BackgroundWorker::quit()
{
	do_quit=true;
}

std::string KvStoreFrontend::BackgroundWorker::meminfo()
{
	std::string ret;
	ret += "##KvStoreFrontend::BackgroundWorker:\n";
	ret += "  object_collector_size: " + convert(object_collector_size) + " = " + PrettyPrintBytes(object_collector_size) + "\n";
	ret += "  object_collector_size_uncompressed: " + convert(object_collector_size_uncompressed) + " = " + PrettyPrintBytes(object_collector_size_uncompressed) + "\n";	
	return ret;
}

bool KvStoreFrontend::BackgroundWorker::removeOldObjects(KvStoreDao& dao, 
	const std::vector<int64>& p_trans_ids, int64 cd_id)
{
	std::string trans_ids_str;
	for (int64 trans_id : p_trans_ids)
		trans_ids_str += ", " + std::to_string(trans_id);

	if (!trans_ids_str.empty())
		trans_ids_str.erase(0, 2);

	Server->Log("Worker: Removing old objects of transaction "+trans_ids_str, LL_INFO);
	DBScopedSynchronous synchronous(dao.getDb());

	std::vector<int64> trans_ids;
	for (int64 trans_id : p_trans_ids)
	{
		KvStoreDao::STransactionProperties trans_props = cd_id == 0 ?
			dao.getTransactionProperties(trans_id) :
			dao.getTransactionPropertiesCd(cd_id, trans_id);
		if (!trans_props.exists)
		{
			Server->Log("Worker: Transaction " + convert(trans_id) + " does not exist", LL_WARNING);
			continue;
		}
		if (trans_props.active != 1)
		{
			Server->Log("Worker: Transaction " + convert(trans_id) + " has active = " + convert(trans_props.active), LL_WARNING);
			continue;
		}
		trans_ids.push_back(trans_id);
	}

	if (trans_ids.empty())
		return true;
	
	size_t max_del_size = backend->max_del_size();

	if (max_del_size > 1)
		--max_del_size;

	size_t stride_size = (std::min)(max_del_size, static_cast<size_t>(10000));

	ObjectCollector object_collector(stride_size, frontend, -1, frontend->backend_mirror!=nullptr, cd_id);

	int64 delete_size = 0;
	int64 delete_num = 0;

	bool has_object = false;
	if (backend->del_with_location_info())
	{
		/*std::vector<KvStoreDao::CdDelObjectMd5> deletable_objects;
		if (backend->ordered_del())
		{
			deletable_objects = dao.getDeletableObjectsMd5Ordered(trans_id);
		}
		else
		{
			deletable_objects = dao.getDeletableObjectsMd5(trans_id);
		}*/

		std::string mirrored = "";
		if (frontend->backend_mirror != nullptr)
			mirrored = ", mirrored";

		std::string transid_filter = "(";

		for (int64 transid : trans_ids)
		{
			if (transid_filter.size() > 1)
				transid_filter += " OR ";

			transid_filter += " (trans_id<"+ std::to_string(transid)+
				" AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id="+std::to_string(transid)+")) ";
		}
		transid_filter += ")";

		IQuery *q;
		if (backend->ordered_del())
		{
			std::string sql_str = cd_id == 0 ?
				"SELECT trans_id, tkey, md5sum, size" + mirrored + " FROM clouddrive_objects WHERE "+transid_filter+" AND size != -1 ORDER BY trans_id ASC, tkey ASC" :
				"SELECT trans_id, tkey, md5sum, size" + mirrored + " FROM clouddrive_objects_cd WHERE cd_id=? AND "+transid_filter+"AND size != -1 ORDER BY trans_id ASC, tkey ASC";

			q = dao.getDb()->Prepare(sql_str, false);
		}
		else
		{
			std::string sql_str = cd_id == 0 ?
				"SELECT trans_id, tkey, md5sum, size" + mirrored + " FROM clouddrive_objects WHERE "+transid_filter+" AND size != -1 ORDER BY tkey ASC" :
				"SELECT trans_id, tkey, md5sum, size" + mirrored + " FROM clouddrive_objects_cd WHERE cd_id=? AND "+transid_filter+" AND size != -1 ORDER BY tkey ASC";

			q = dao.getDb()->Prepare(sql_str, false);
		}

		if (cd_id != 0)
			q->Bind(cd_id);
		
		{
			ScopedDatabaseCursor cursor(q->Cursor());

			db_single_result res;
			while(cursor.next(res))
			{
				KvStoreDao::CdDelObjectMd5 deletable_object;
				deletable_object.trans_id = watoi64(res["trans_id"]);
				deletable_object.tkey = res["tkey"];
				deletable_object.md5sum = res["md5sum"];

				bool mirrored = false;
				if (frontend->backend_mirror != nullptr)
				{
					mirrored = watoi(res["mirrored"]) > 0;
				}

				int64 osize = watoi64(res["size"]);
				if (osize > 0)
					delete_size += osize;

				++delete_num;

				Server->Log("Deleting object (1) " + bytesToHex(reinterpret_cast<const unsigned char*>(deletable_object.tkey.c_str()), deletable_object.tkey.size()) + " transid " + convert(deletable_object.trans_id)+" cd_id "+convert(cd_id), LL_INFO);

				object_collector.add(
					deletable_object.trans_id,
					deletable_object.tkey,
					get_locinfo(deletable_object.md5sum),
					mirrored);

				has_object = true;
			}
		}

		dao.getDb()->destroyQuery(q);
	}
	else
	{
		/*std::vector<KvStoreDao::CdDelObject> deletable_objects;
		if (backend->ordered_del())
		{
			deletable_objects = dao.getDeletableObjectsOrdered(trans_id);
		}
		else
		{
			deletable_objects = dao.getDeletableObjects(trans_id);
		}*/

		std::string transid_filter = "(";

		for (int64 transid : trans_ids)
		{
			if (transid_filter.size() > 1)
				transid_filter += " OR ";

			transid_filter += " (trans_id<" + std::to_string(transid) +
				" AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=" + std::to_string(transid) + ")) ";
		}
		transid_filter += ")";

		IQuery *q;
		if (backend->ordered_del())
		{
			std::string sql_str = cd_id == 0 ?
				"SELECT trans_id, tkey, size FROM clouddrive_objects WHERE "+transid_filter+" AND size != -1 ORDER BY trans_id ASC, tkey ASC" :
				"SELECT trans_id, tkey, size FROM clouddrive_objects_cd WHERE cd_id=? AND "+transid_filter+" AND size != -1 ORDER BY trans_id ASC, tkey ASC";
			q = dao.getDb()->Prepare(sql_str, false);
		}
		else
		{
			std::string sql_str = cd_id == 0 ?
				"SELECT trans_id, tkey, size FROM clouddrive_objects WHERE "+transid_filter+" AND size != -1 ORDER BY tkey ASC" :
				"SELECT trans_id, tkey, size FROM clouddrive_objects_cd WHERE cd_id=? AND "+transid_filter+" AND size != -1 ORDER BY tkey ASC";
			q = dao.getDb()->Prepare(sql_str, false);
		}

		if (cd_id != 0)
			q->Bind(cd_id);		

		{
			ScopedDatabaseCursor cursor(q->Cursor());
			db_single_result res;
			while (cursor.next(res))
			{
				KvStoreDao::CdDelObject deletable_object;
				deletable_object.trans_id = watoi64(res["trans_id"]);
				deletable_object.tkey = res["tkey"];

				int64 osize = watoi64(res["size"]);
				if (osize > 0)
					delete_size += osize;

				++delete_num;

				bool mirrored = false;
				if (frontend->backend_mirror != nullptr)
				{
					mirrored = watoi(res["mirrored"]) > 0;
				}

				Server->Log("Deleting object (1) " + bytesToHex(reinterpret_cast<const unsigned char*>(deletable_object.tkey.c_str()), deletable_object.tkey.size()) + " transid " + convert(deletable_object.trans_id)+" cd_id "+convert(cd_id), LL_INFO);

				object_collector.add(
					deletable_object.trans_id,
					deletable_object.tkey,
					mirrored);

				has_object = true;
			}
		}

		dao.getDb()->destroyQuery(q);
	}
	
	bool ret=true;

	if(has_object)
	{
		object_collector.finalize();

		object_collector_size = object_collector.memsize;
		object_collector_size_uncompressed = object_collector.uncompressed_memsize;

		Server->Log("Object collector size " + PrettyPrintBytes(object_collector_size) + " uncompressed " + PrettyPrintBytes(object_collector_size_uncompressed), LL_INFO);

		if (result_fn.empty())
		{
			ret = frontend->backend_del_parallel(object_collector.key_next_funs,
				object_collector.locinfo_next_funs, true);
		}
		else
		{
			ret = object_collector.persist(TASK_REMOVE_OLD_OBJECTS, 0, 0, p_trans_ids, result_fn);
		}

		Server->Log("Backend del done res=" + convert(ret), LL_INFO);
	}

	if(ret)
	{
		if(has_object)
		{
			if (result_fn.empty() 
				&& !backend->sync(false, true))
			{
				object_collector_size = 0;
				object_collector_size_uncompressed = 0;
				return false;
			}

#ifdef ASSERT_CHECK_DELETED
			std::vector<KvStoreDao::CdDelObjectMd5> deletable_objects = cd_id==0 ?
				dao.getDeletableObjectsMd5(trans_id) : 
				dao.getDeletableObjectsMd5Cd(cd_id, trans_id);
			for (KvStoreDao::CdDelObjectMd5& obj : deletable_objects)
			{
#ifdef ASSERT_CHECK_DELETED_NO_LOCINFO
				obj.md5sum.clear();
#endif
				assert(backend->check_deleted(frontend->prefixKey(frontend->encodeKey(cd_id, obj.tkey, obj.trans_id)),
					get_locinfo(obj.md5sum)));
			}
#endif

			if (cd_id == 0)
			{
				for (int64 trans_id : trans_ids)
				{
					if (!dao.deleteDeletableObjects(trans_id))
					{
						object_collector_size = 0;
						object_collector_size_uncompressed = 0;
						return false;
					}
				}
			}
			else
			{
				for (int64 trans_id : trans_ids)
				{
					if (!dao.deleteDeletableObjectsCd(cd_id, trans_id))
					{
						object_collector_size = 0;
						object_collector_size_uncompressed = 0;
						return false;
					}
				}
			}
		}

		for (int64 trans_id : trans_ids)
		{
			std::vector<int64> del_trans_ids = cd_id == 0 ?
				dao.getDeletableTransactions(trans_id) :
				dao.getDeletableTransactionsCd(cd_id, trans_id);

			DBScopedWriteTransaction scoped_transaction(dao.getDb());
			for (size_t i = 0; i < del_trans_ids.size(); ++i)
			{
				dao.addTask(TASK_REMOVE_TRANSACTION, del_trans_ids[i], Server->getTimeSeconds(), cd_id);
			}
		}
		object_collector_size = 0;
		object_collector_size_uncompressed = 0;
		frontend->objects_total_size -= delete_size;
		frontend->objects_total_num -= delete_num;
		return true;
	}
	else
	{
		Server->Log("Delete request failed. Error removing old objects of transaction " + trans_ids_str, LL_ERROR);
		object_collector_size = 0;
		object_collector_size_uncompressed = 0;
		return false;
	}
}

bool KvStoreFrontend::BackgroundWorker::removeTransaction(KvStoreDao& dao, int64 trans_id, int64 cd_id)
{
	KvStoreDao::STransactionProperties trans_props = cd_id == 0 ?
		dao.getTransactionProperties(trans_id) :
		dao.getTransactionPropertiesCd(cd_id, trans_id);

	if (!trans_props.exists)
	{
		Server->Log("Worker: Removing transaction " + convert(trans_id)+" cd_id "+convert(cd_id)+". Was already removed", LL_INFO);
		return true;
	}

	Server->Log("Worker: Removing transaction "+convert(trans_id)+" cd_id "+convert(cd_id), LL_INFO);
	DBScopedSynchronous synchronous(dao.getDb());

	bool case1 = trans_props.active == 0
			&& trans_props.completed == 0;
	bool case2;
	if(cd_id==0)
		case2 = dao.getTransactionObjects(trans_id).empty()
			&& trans_props.completed!=0;
	else
		case2 = dao.getTransactionObjectsCd(cd_id, trans_id).empty()
			&& trans_props.completed != 0;

	if ( !case1 && !case2)
	{
		Server->Log("Worker: Not deleting transaction " + convert(trans_id), LL_WARNING);
		Server->Log("Worker: Transaction " + convert(trans_id) + " has active = " + convert(trans_props.active)+" completed = "+convert(trans_props.completed), LL_WARNING);
		if(cd_id==0)
			Server->Log("Worker: Transaction " + convert(trans_id) + " has "+convert(dao.getTransactionObjects(trans_id).size())+" objects", LL_WARNING);
		else
			Server->Log("Worker: Transaction " + convert(trans_id) + " has " + convert(dao.getTransactionObjectsCd(cd_id, trans_id).size()) + " objects", LL_WARNING);
		return true;
	}

	size_t max_del_size = backend->max_del_size();

	if (max_del_size > 1)
		--max_del_size;

	size_t stride_size = (std::min)(max_del_size, static_cast<size_t>(10000));

	ObjectCollector object_collector(stride_size, frontend, trans_id, frontend->backend_mirror != nullptr, cd_id);

	bool has_object = false;
	int64 delete_size = 0;
	int64 delete_num = 0;

	std::string mirrored = "";
	if (frontend->backend_mirror != nullptr)
		mirrored = ", mirrored";

	if (backend->del_with_location_info())
	{
		//std::vector<KvStoreDao::SDelItemMd5> trans_objects = dao.getTransactionObjectsMd5(trans_id);

		std::string curr_q_str = cd_id == 0 ?
			"SELECT tkey, md5sum, size" + mirrored + " FROM clouddrive_objects WHERE trans_id=? AND size != -1 ORDER BY tkey ASC" :
			"SELECT tkey, md5sum, size" + mirrored + " FROM clouddrive_objects_cd WHERE cd_id=? AND trans_id=? AND size != -1 ORDER BY tkey ASC";

		IQuery* q = dao.getDb()->Prepare(curr_q_str, false);;
		if (cd_id != 0)
			q->Bind(cd_id);
		q->Bind(trans_id);

		{
			ScopedDatabaseCursor cursor(q->Cursor());
			db_single_result res;
			while (cursor.next(res))
			{
				KvStoreDao::SDelItemMd5 trans_object;
				trans_object.tkey = res["tkey"];
				trans_object.md5sum = res["md5sum"];

				int64 osize = watoi64(res["size"]);
				if (osize > 0)
					delete_size += osize;

				++delete_num;

				bool mirrored = false;
				if (frontend->backend_mirror != nullptr)
				{
					mirrored = watoi(res["mirrored"]) > 0;
				}

				Server->Log("Deleting object (2) " +
					bytesToHex(reinterpret_cast<const unsigned char*>(trans_object.tkey.c_str()),
						trans_object.tkey.size()) + " transid " + convert(trans_id)+" cd_id "+convert(cd_id), LL_INFO);

				object_collector.add(-1,
					trans_object.tkey,
					get_locinfo(trans_object.md5sum),
					mirrored);

				has_object = true;
			}
		}

		dao.getDb()->destroyQuery(q);
	}
	else
	{
		//std::vector<std::string> trans_objects = dao.getTransactionObjects(trans_id);
		std::string curr_q_str = cd_id == 0 ?
			"SELECT tkey, size" + mirrored + " FROM clouddrive_objects WHERE  trans_id=? AND size != -1 ORDER BY tkey ASC" :
			"SELECT tkey, size" + mirrored + " FROM clouddrive_objects WHERE cd_id=? AND trans_id=? AND size != -1 ORDER BY tkey ASC";

		IQuery* q = dao.getDb()->Prepare(curr_q_str, false);;
		if (cd_id != 0)
			q->Bind(cd_id);
		q->Bind(trans_id);

		{
			ScopedDatabaseCursor cursor(q->Cursor());
			db_single_result res;
			while (cursor.next(res))
			{
				std::string trans_object = res["tkey"];
				int64 osize = watoi64(res["size"]);
				if (osize > 0)
					delete_size += osize;

				++delete_num;

				bool mirrored = false;
				if (frontend->backend_mirror != nullptr)
				{
					mirrored = watoi(res["mirrored"]) > 0;
				}

				Server->Log("Deleting object (2) " + bytesToHex(reinterpret_cast<const unsigned char*>(trans_object.c_str()), trans_object.size()) + " transid " + convert(trans_id)+" cd_id "+convert(cd_id), LL_INFO);
				object_collector.add(-1,
					trans_object,
					mirrored);

				has_object = true;
			}
		}

		dao.getDb()->destroyQuery(q);
	}

	bool ret=true;

	KvStoreDao::STransactionProperties properties = cd_id == 0 ?
		dao.getTransactionProperties(trans_id) :
		dao.getTransactionPropertiesCd(cd_id, trans_id);

	if (properties.exists)
	{
		has_object = properties.completed != 0 || properties.active == 0;
	}

	std::vector<std::string> add_backend_keys;

	if(has_object)
	{
		object_collector.finalize();

		object_collector_size = object_collector.memsize;
		object_collector_size_uncompressed = object_collector.uncompressed_memsize;

		Server->Log("Object collector size " + PrettyPrintBytes(object_collector_size) + " uncompressed " + PrettyPrintBytes(object_collector_size_uncompressed), LL_INFO);

		if (result_fn.empty())
		{
			ret = frontend->backend_del_parallel(object_collector.key_next_funs, object_collector.locinfo_next_funs, true);
		}
		else
		{
			ret = object_collector.persist(TASK_REMOVE_TRANSACTION,
				properties.completed, properties.active, { trans_id }, result_fn);
		}
	}

	if(ret)
	{
		if (properties.completed != 0)
		{
			if (cd_id == 0)
				add_backend_keys.push_back(frontend->prefixKey(convert(trans_id) + "_complete"));
			else
				add_backend_keys.push_back(frontend->prefixKey(convert(cd_id) + "_" + convert(trans_id) + "_complete"));
		}

		if (properties.active == 0)
		{
			if (cd_id == 0)
				add_backend_keys.push_back(frontend->prefixKey(convert(trans_id) + "_inactive"));
			else
				add_backend_keys.push_back(frontend->prefixKey(convert(cd_id) + "_" + convert(trans_id) + "_inactive"));
		}

		if (!add_backend_keys.empty())
		{
			size_t idx = 0;
			KvStoreFrontend* lfrontend = frontend;
			backend->del([lfrontend, &add_backend_keys, &idx](IKvStoreBackend::key_next_action_t action, std::string* key) {
				if (action == IKvStoreBackend::key_next_action_t::clear) {
					add_backend_keys.clear();
					assert(key == nullptr);
					return true;
				}

				if (action == IKvStoreBackend::key_next_action_t::reset) {
					idx = 0;
					assert(key == nullptr);
					return true;
				}

				assert(action == IKvStoreBackend::key_next_action_t::next);

				if (idx >= add_backend_keys.size())
					return false;
				*key = add_backend_keys[idx];
				lfrontend->log_del_mirror(*key);
				++idx;
				return true;
				}, true);
		}


		if (has_object || add_backend_keys.empty())
		{
			if (!backend->sync(false, true))
			{
				object_collector_size = 0;
				object_collector_size_uncompressed = 0;
				return false;
			}
		}

		if(has_object)
		{
#ifdef ASSERT_CHECK_DELETED
			std::vector<KvStoreDao::SDelItemMd5> trans_objects = cd_id==0 ?
				dao.getTransactionObjectsMd5(trans_id) :
				dao.getTransactionObjectsMd5Cd(cd_id, trans_id);
			for (KvStoreDao::SDelItemMd5& obj : trans_objects)
			{
#ifdef ASSERT_CHECK_DELETED_NO_LOCINFO
				obj.md5sum.clear();
#endif
				assert(backend->check_deleted(frontend->prefixKey(frontend->encodeKey(cd_id, obj.tkey, trans_id)),
					get_locinfo(obj.md5sum)));
			}
			for (std::string& key : add_backend_keys)
			{
				assert(backend->check_deleted(key, std::string()));
			}
#endif

			if (cd_id == 0)
			{
				if (!dao.deleteTransactionObjects(trans_id))
				{
					object_collector_size = 0;
					object_collector_size_uncompressed = 0;
					return false;
				}
			}
			else
			{
				if (!dao.deleteTransactionObjectsCd(cd_id, trans_id))
				{
					object_collector_size = 0;
					object_collector_size_uncompressed = 0;
					return false;
				}
			}
		}
		object_collector_size = 0;
		object_collector_size_uncompressed = 0;

		if (cd_id == 0)
		{
			if (!dao.deleteTransaction(trans_id))
			{
				return false;
			}
		}
		else
		{
			if (!dao.deleteTransactionCd(cd_id, trans_id))
			{
				return false;
			}
		}
		frontend->objects_total_size -= delete_size;
		frontend->objects_total_num -= delete_num;
		return true;
	}
	else
	{
		object_collector_size = 0;
		object_collector_size_uncompressed = 0;
		Server->Log("Delete request failed. Error deleting transaction " + convert(trans_id), LL_ERROR);
		return false;
	}
}

KvStoreFrontend::ScrubWorker::ScrubWorker(ScrubAction scrub_action,
	IKvStoreBackend* backend, IKvStoreBackend* backend_mirror,
	KvStoreFrontend* frontend,
	BackgroundWorker& background_worker, const std::string& position,
	bool has_last_modified)
	: scrub_action(scrub_action), complete_pc(-1), do_quit(false),
	mutex(Server->createMutex()), frontend(frontend), backend(backend), backend_mirror(backend_mirror),
	background_worker(background_worker), position(position),
	has_last_modified(has_last_modified), curr_paused(true),
	done_size(-1), total_size(-1)
{

}

namespace
{
	void writeNewMd5sums(std::vector<KvStoreDao::CdIterObject>& new_md5sums,
		KvStoreDao& dao)
	{
		if (new_md5sums.empty())
			return;

		DBScopedSynchronous synchronous(dao.getDb());
		DBScopedWriteTransaction trans(dao.getDb());
		for (auto& it : new_md5sums)
		{
			dao.updateObjectMd5sum(it.md5sum, it.trans_id, it.tkey);
		}
		new_md5sums.clear();
	}
}

void KvStoreFrontend::ScrubWorker::operator()()
{
	KvStoreDao dao(Server->getDatabase(Server->getThreadID(), CLOUDDRIVE_DB));
	int64 scrub_starttime = Server->getTimeSeconds();
	set_allow_defrag(false);

	Server->Log("Waiting for background worker to finish startup...");

	while (!background_worker.is_startup_finished())
	{
		Server->wait(10000);
	}

	Server->Log("Pausing background worker...");

	background_worker.set_scrub_pause(true);

	while (!background_worker.is_paused())
	{
		Server->wait(100);
		background_worker.set_scrub_pause(true);
	}

	curr_paused = false;

	Server->Log(strScrubActionC(scrub_action) + " started");

	size_t scrub_oks = 0;
	size_t scrub_errors = 0;
	size_t scrub_repaired = 0;

	bool with_last_modified = false;
	if (has_last_modified)
	{
		if (position.empty())
		{
			if (dao.createObjectLastModifiedIdx())
			{
				with_last_modified = true;
			}
		}
	}

	done_size = 0;
	std::vector<KvStoreDao::CdIterObject> results;
	if (position.empty())
	{
		total_size = dao.getSize().size;
		if (total_size == 0)
		{
			if (with_last_modified)
			{
				dao.dropObjectLastModifiedIdx();
			}
			Server->Log(strScrubAction(scrub_action) + " finished. Nothing to "+ strScrubAction(scrub_action)+".");
			background_worker.set_scrub_pause(false);
			set_allow_defrag(true);
			complete_pc = 101;
			return;
		}

		if (with_last_modified)
		{
			results = dao.getInitialObjectsLM();
		}
		else
		{
			results = dao.getInitialObjects();
		}
	}
	else
	{
		done_size = 0;
		int64 transid = 0;
		std::string tkey;
		int64 last_modified_start = -1;

#ifdef HAS_SCRUB
		try
		{
			Json::Value root;
			Json::Reader reader;

			if (reader.parse(position, root, false))
			{
				done_size = root["done_size"].asInt64();
				transid = root.get("transid", 0).asInt64();
				tkey = hexToBytes(root.get("tkey", std::string()).asString());
				last_modified_start = root.get("last_modified", -1).asInt64();

				scrub_oks = root["ok"].asUInt64();
				scrub_errors = root["error"].asUInt64();
				scrub_repaired = root["repaired"].asUInt64();
				scrub_starttime = root["starttime"].asInt64();
			}
			
		}
		catch (const std::exception& e)
		{
			Server->Log(std::string("Error parsing position when continuing scrub") + e.what(), LL_ERROR);
		}
#endif

		if (transid == 0
			&& tkey.empty()
			&& has_last_modified)
		{
			if (dao.createObjectLastModifiedIdx())
			{
				with_last_modified = true;
			}
		}

		if (with_last_modified)
		{
			if(last_modified_start<0)
				total_size = dao.getSize().size;
			else
				total_size = dao.getSizePartialLMInit(last_modified_start);
		}
		else
		{
			if (transid == 0
				&& tkey.empty())
				total_size = dao.getSize().size;
			else
				total_size = dao.getSizePartial(tkey, transid);
		}

		if (total_size == 0)
		{
			if (with_last_modified)
			{
				dao.dropObjectLastModifiedIdx();
			}
			Server->Log(strScrubActionC(scrub_action) + " finished. Nothing to "+ strScrubAction(scrub_action)+".");
			background_worker.set_scrub_pause(false);
			set_allow_defrag(true);
			complete_pc = 101;
			return;
		}

		total_size += done_size;

		if (done_size > total_size)
		{
			complete_pc = 100;
		}
		else
		{
			complete_pc = static_cast<int>((100 * done_size) / total_size);
		}

		Server->Log("Resuming at " + convert(complete_pc) + "%");

		if (with_last_modified)
		{
			if (last_modified_start<0)
				results = dao.getInitialObjectsLM();
			else
				results = dao.getIterObjectsLMInit(last_modified_start);
		}
		else
		{
			if (transid == 0
				&& tkey.empty())
				results = dao.getInitialObjects();
			else
				results = dao.getIterObjects(tkey, transid);
		}
	}

#ifdef HAS_LOCAL_BACKEND
	int64 last_modified_stop = curr_last_modified();
#else
	int64 last_modified_stop = 0;
#endif

	int64 last_background_worker_time = Server->getTimeMS();
	int64 last_size_update_time = Server->getTimeMS();

	int last_complete_pc = -1;
	std::vector<KvStoreDao::CdIterObject> new_md5sums;
	std::vector<ScrubThread*> scrub_threads;
	std::vector<THREADPOOL_TICKET> scrub_thread_tickets;
	size_t n_scrub_threads = backend->num_scrub_parallel();
	if (n_scrub_threads > os_get_num_cpus())
	{
		n_scrub_threads = os_get_num_cpus();
		if (n_scrub_threads < 1)
			n_scrub_threads = 1;
	}

	std::string scrub_thread_mult = getFile(ExtractFilePath(frontend->db_path) + os_file_sep() + "scrub_thread_mult");
	if (!trim(scrub_thread_mult).empty())
	{
		n_scrub_threads = static_cast<size_t>(n_scrub_threads*atof(trim(scrub_thread_mult).c_str())+0.5);
	}
	scrub_threads.resize(n_scrub_threads);
	scrub_thread_tickets.resize(n_scrub_threads);

	ScrubQueue scrub_queue;
	std::unique_ptr<IMutex> new_md5sums_mutex(Server->createMutex());
	bool has_changes = false;
	bool scrubbed_transid_markers = false;

	IScopedLock lock(mutex.get());	

	do
	{
		has_changes = false;

		scrub_queue.reset();

		for (size_t i = 0; i < n_scrub_threads; ++i)
		{
			scrub_threads[i] = new ScrubThread(scrub_queue,
				new_md5sums, new_md5sums_mutex.get(), has_changes,
				done_size, with_last_modified, scrub_action,
				backend, frontend, backend_mirror);
			scrub_thread_tickets[i] = Server->getThreadPool()->execute(scrub_threads[i], strScrubAction(scrub_action) + convert(i));
		}

		for (size_t i = 0; i < results.size() && !do_quit; ++i)
		{
			KvStoreDao::CdIterObject& result = results[i];

			assert(result.size != -1);

			if (deleted_objects.find(std::make_pair(result.trans_id, result.tkey)) != deleted_objects.end())
			{
				continue;
			}

			scrub_queue.add(result);
		}

		KvStoreDao::CdIterObject last_result;
		bool has_last_result = false;

		if (!results.empty()
			&& !do_quit)
		{
			has_last_result = true;
			last_result = results[results.size() - 1];

			lock.relock(mutex.get());
			deleted_objects.clear();
			lock.relock(nullptr);

			if (Server->getTimeMS() - last_size_update_time > 2 * 60 * 60 * 1000)
			{
				if (with_last_modified)
				{
					total_size = dao.getSizePartialLM(last_result.last_modified, last_modified_stop);
				}
				else
				{
					total_size = dao.getSizePartial(last_result.tkey, last_result.trans_id);
				}
				total_size += done_size;
				last_size_update_time = Server->getTimeMS();
			}

			if (with_last_modified)
			{
				results = dao.getIterObjectsLM(last_result.last_modified, last_modified_stop);
			}
			else
			{
				results = dao.getIterObjects(last_result.tkey, last_result.trans_id);
			}
		}

		if (!do_quit && results.empty()
			&& !scrubbed_transid_markers)
		{
			std::vector<KvStoreDao::SCdTrans> transactions = dao.getTransactionIds();

			for (const KvStoreDao::SCdTrans& transaction : transactions)
			{
				KvStoreDao::CdIterObject iter_obj;
				iter_obj.size = 0;
				iter_obj.last_modified = 0;
				iter_obj.trans_id = -1;
				iter_obj.tkey = convert(transaction.id);
				if (transaction.completed == 2)
				{
					iter_obj.tkey += "_complete";
				}
				else if (transaction.completed == 1)
				{
					iter_obj.tkey += "_finalized";
				}
				else if(transaction.active==0)
				{
					iter_obj.tkey += "_inactive";
				}
				else
				{
					continue;
				}
				scrub_queue.add(iter_obj);
			}

			{
				KvStoreDao::CdIterObject iter_obj;
				iter_obj.size = 0;
				iter_obj.last_modified = 0;
				iter_obj.tkey = "cd_magic_file";
				iter_obj.trans_id = -2;
				scrub_queue.add(iter_obj);
			}

			scrubbed_transid_markers = true;
		}

		lock.relock(nullptr);

		scrub_queue.stop();

		Server->getThreadPool()->waitFor(scrub_thread_tickets);

		scrub_errors += scrub_queue.scrub_errors;
		scrub_oks += scrub_queue.scrub_oks;
		scrub_repaired += scrub_queue.scrub_repaired;

		if (scrub_queue.has_error())
		{
			if (with_last_modified)
			{
				dao.dropObjectLastModifiedIdx();
			}

			Server->Log("Error during scrub/balance. Stopping...", LL_ERROR);
			background_worker.set_scrub_pause(false);
			set_allow_defrag(true);
			return;
		}

		if (has_changes)
		{
			backend->sync(scrub_sync_test1, false);
		}

		writeNewMd5sums(new_md5sums, dao);

		if (has_changes)
		{
			backend->sync(scrub_sync_test2, false);
			has_changes = false;
		}

		if (Server->getTimeMS() - last_background_worker_time > 10 * 60 * 1000)
		{
			curr_paused = true;
			Server->Log("Unpausing background worker...");

			int64 nwork = background_worker.get_nwork();

			bool has_task = dao.getTask(Server->getTimeSeconds()-frontend->task_delay).exists;

			background_worker.set_scrub_pause(false);
			while (background_worker.is_paused()
				&& !background_worker.get_pause())
			{
				Server->wait(100);
				background_worker.set_scrub_pause(false);
			}

			if (has_task)
			{
				Server->wait(10000);
			}

			background_worker.set_scrub_pause(true);
			Server->Log("Waiting for background worker...");
			while (!background_worker.is_paused())
			{
				Server->wait(100);
				background_worker.set_scrub_pause(true);
			}

			curr_paused = false;

			last_background_worker_time = Server->getTimeMS();

			if (nwork!=background_worker.get_nwork()
				&& has_last_result)
			{
				lock.relock(mutex.get());
				deleted_objects.clear();
				lock.relock(nullptr);

				if (with_last_modified)
				{
					results = dao.getIterObjectsLM(last_result.last_modified, last_modified_stop);
				}
				else
				{
					results = dao.getIterObjects(last_result.tkey, last_result.trans_id);
				}
			}
		}

		lock.relock(mutex.get());


		if (has_last_result)
		{
			if (with_last_modified)
			{
				position = "{ \"last_modified\": " + convert(last_result.last_modified);
			}
			else
			{
				position = "{ \"transid\": " + convert(last_result.trans_id) + ", \"tkey\": \"" + bytesToHex(last_result.tkey) + "\"";
			}

			position+= ", \"done_size\": " + convert(done_size) + ", \"complete_pc\": " + convert(complete_pc) +
				+ ", \"ok\": " + convert(scrub_oks) + ", \"error\": " + convert(scrub_errors) + ", \"repaired\": " + convert(scrub_repaired) +
				", \"starttime\": " + convert(scrub_starttime) + " }";
		}


		if (done_size > total_size)
		{
			complete_pc = 100;
		}
		else
		{
			complete_pc = static_cast<int>((100 * done_size) / total_size);
		}
		if (complete_pc != last_complete_pc)
		{
			Server->Log(strScrubActionC(scrub_action) + " " + convert(complete_pc) + "% done (ok: "+convert(scrub_oks)+" error: "+convert(scrub_errors)+" repaired: "+convert(scrub_repaired)+")");
		}
		last_complete_pc = complete_pc;

	} while (!results.empty()
		&& !do_quit);

	if (has_changes)
	{
		backend->sync(scrub_sync_test1, false);
	}

	writeNewMd5sums(new_md5sums, dao);

	if (has_changes)
	{
		backend->sync(scrub_sync_test2, false);
		has_changes = false;
	}

	if (with_last_modified)
	{
		dao.dropObjectLastModifiedIdx();
	}

	complete_pc = do_quit ? 102 : 101;
	Server->Log(strScrubActionC(scrub_action) + " finished"+(do_quit?" (quit)":"")+" (ok: "+convert(scrub_oks)+" error : "+convert(scrub_errors)+" repaired: "+convert(scrub_repaired)+")");

	int scrub_finished_prio = LL_INFO;

	if (scrub_errors > 0)
		scrub_finished_prio = LL_ERROR;

	str_map scrub_finished_extra;
	scrub_finished_extra["ok"] = convert(scrub_oks);
	scrub_finished_extra["error"] = convert(scrub_errors);
	scrub_finished_extra["repaired"] = convert(scrub_repaired);
	scrub_finished_extra["runtime_ms"] = convert((Server->getTimeSeconds() - scrub_starttime)*1000);

	std::string scrub_finished_subj = strScrubActionC(scrub_action) + " finished";

	if (scrub_errors > 0)
		scrub_finished_subj += " with error";
	if (scrub_errors > 1)
		scrub_finished_subj += "s";

	if (!do_quit)
	{
		addSystemEvent("scrub_finished", scrub_finished_subj,
			strScrubActionC(scrub_action) + " finished after " + PrettyPrintTime((Server->getTimeSeconds() - scrub_starttime)*1000) +
			" with " + convert(scrub_oks) +
			" ok objects " + convert(scrub_errors) +
			" objects with error and " + convert(scrub_repaired) + " repaired objects.", scrub_finished_prio, scrub_finished_extra);
	}

	background_worker.set_scrub_pause(false);
	set_allow_defrag(true);
}

std::string KvStoreFrontend::ScrubWorker::meminfo() {
	IScopedLock lock(mutex.get());
	std::string ret = "##KvStoreFrontend::ScrubWorker:\n";
	ret += "  deleted_objects: " + convert(deleted_objects.size()) + " * " + PrettyPrintBytes(sizeof(std::string) + sizeof(int64)) + "\n";
	return ret;
}

void KvStoreFrontend::ScrubWorker::set_allow_defrag(bool b)
{
#ifdef HAS_LOCAL_BACKEND
	KvStoreBackendLocal* bl = dynamic_cast<KvStoreBackendLocal*>(backend);
	if (bl != nullptr)
	{
		if(!b)
			Server->Log("Stopping defrag...");

		bl->set_allow_defrag(b);
	}
#endif
}

KvStoreFrontend::PutDbWorker::PutDbWorker(std::string db_path)
	: mutex(Server->createMutex()),
	add_cond(Server->createCondition()),
	commit_cond(Server->createCondition()),
	curr_items(&items_a), do_quit(false), do_synchronous(true),
	db_wal_file(nullptr), with_last_modified(false),
	db_path(db_path)
{

}

int64 KvStoreFrontend::PutDbWorker::add(int64 cd_id, int64 transid, const std::string & key, int64 generation)
{
	IScopedLock lock(mutex.get());
	wait_queue(lock);
	int64 rowid = 0;
	curr_items->push_back(SItem(cd_id, transid, key, generation, &rowid));
	add_cond->notify_all();

	while (rowid==0)
	{
		commit_cond->wait(&lock);
	}

	return rowid;
}

void KvStoreFrontend::PutDbWorker::add(int64 cd_id, int64 transid, const std::string & key, 
	const std::string & md5sum, int64 size, int64 last_modified, int64 generation)
{
	IScopedLock lock(mutex.get());
	if (curr_items->size()>10000)
	{
		add_cond->notify_all();
	}
	wait_queue(lock);
	curr_items->push_back(SItem(cd_id, transid, key, md5sum, size, last_modified, generation));
}

void KvStoreFrontend::PutDbWorker::flush()
{
	IScopedLock lock(mutex.get());
	int64 rowid = 0;
	curr_items->push_back(SItem(&rowid));
	add_cond->notify_all();

	while (rowid == 0)
	{
		commit_cond->wait(&lock);
	}
}

void KvStoreFrontend::PutDbWorker::update(int64 cd_id, int64 objectid, int64 size,
	const std::string & md5sum, int64 last_modified)
{
	IScopedLock lock(mutex.get());
	wait_queue(lock);
	curr_items->push_back(SItem(cd_id, objectid, size, md5sum, last_modified));
	add_cond->notify_all();
}

void KvStoreFrontend::PutDbWorker::quit()
{
	IScopedLock lock(mutex.get());
	do_quit = true;
	add_cond->notify_all();
}

void KvStoreFrontend::PutDbWorker::operator()()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), CLOUDDRIVE_DB);
	KvStoreDao dao(db);
	DBScopedSynchronous synchronous(nullptr);

	if (do_synchronous)
	{
		synchronous.reset(db);
	}

	std::vector<int64> rowids;

	IScopedLock lock(mutex.get());
	while (!do_quit)
	{
		while (curr_items->empty()
			&& !do_quit)
		{
			add_cond->wait(&lock);
		}

		std::vector<SItem>* other_items = curr_items;
		if (curr_items == &items_a)
		{
			curr_items = &items_b;
		}
		else
		{
			curr_items = &items_a;
		}
		rowids.resize(other_items->size());

		lock.relock(nullptr);

		bool needs_flush = false;

		{
			if (!dao.getDb()->BeginWriteTransaction())
			{
				worker_abort();
			}

			for (size_t i = 0; i < other_items->size(); ++i)
			{
				if ((*other_items)[i].type == EType::Add)
				{
					if ((*other_items)[i].cd_id!=0
						&& (*other_items)[i].generation != 0)
					{
						if (!dao.updateGenerationCd((*other_items)[i].cd_id,
							(*other_items)[i].generation))
						{
							worker_abort();
						}
					}

					if ((*other_items)[i].cd_id == 0)
					{
						if (!dao.addPartialObject((*other_items)[i].transid, (*other_items)[i].key))
						{
							worker_abort();
						}
					}
					else
					{
						if (!dao.addPartialObjectCd((*other_items)[i].cd_id, (*other_items)[i].transid, (*other_items)[i].key))
						{
							worker_abort();
						}
					}
					rowids[i] = db->getLastInsertID();
				}
				else if ((*other_items)[i].type == EType::Update)
				{
					if ((*other_items)[i].cd_id == 0)
					{
						if (with_last_modified)
						{
							if (!dao.updateObject2((*other_items)[i].key, (*other_items)[i].transid,
								(*other_items)[i].objectid, (*other_items)[i].last_modified))
							{
								worker_abort();
							}
						}
						else
						{
							if (!dao.updateObject((*other_items)[i].key, (*other_items)[i].transid,
								(*other_items)[i].objectid))
							{
								worker_abort();
							}
						}
					}
					else
					{
						if (with_last_modified)
						{
							if (!dao.updateObject2Cd((*other_items)[i].key, (*other_items)[i].transid,
								(*other_items)[i].objectid, (*other_items)[i].last_modified))
							{
								worker_abort();
							}
						}
						else
						{
							if (!dao.updateObjectCd((*other_items)[i].key, (*other_items)[i].transid,
								(*other_items)[i].objectid))
							{
								worker_abort();
							}
						}
					}
				}
				else if ((*other_items)[i].type == EType::Add2)
				{
					if ((*other_items)[i].cd_id != 0
						&& (*other_items)[i].generation != 0)
					{
						if (!dao.updateGenerationCd((*other_items)[i].cd_id,
							(*other_items)[i].generation))
						{
							worker_abort();
						}
					}

					if ((*other_items)[i].cd_id == 0)
					{
						if (with_last_modified)
						{
							if (!dao.addObject2((*other_items)[i].transid, (*other_items)[i].key,
								(*other_items)[i].md5sum, (*other_items)[i].size,
								(*other_items)[i].last_modified))
							{
								worker_abort();
							}
						}
						else
						{
							if (!dao.addObject((*other_items)[i].transid, (*other_items)[i].key,
								(*other_items)[i].md5sum, (*other_items)[i].size))
							{
								worker_abort();
							}
						}
					}
					else
					{
						if (with_last_modified)
						{
							if (!dao.addObject2Cd((*other_items)[i].cd_id, (*other_items)[i].transid, (*other_items)[i].key,
								(*other_items)[i].md5sum, (*other_items)[i].size,
								(*other_items)[i].last_modified))
							{
								worker_abort();
							}
						}
						else
						{
							if (!dao.addObjectCd((*other_items)[i].cd_id, (*other_items)[i].transid, (*other_items)[i].key,
								(*other_items)[i].md5sum, (*other_items)[i].size))
							{
								worker_abort();
							}
						}
					}
				}
				else if ((*other_items)[i].type == EType::Flush)
				{
					rowids[i] = 1;

					if (!do_synchronous)
					{
						needs_flush = true;
					}
				}
				else
				{
					assert(false);
				}
			}

			if (!dao.getDb()->EndTransaction())
			{
				worker_abort();
			}
		}

		if (needs_flush)
		{
			assert(db_wal_file != nullptr);

			if (db_wal_file != nullptr)
			{
				if (!db_wal_file->Sync())
				{
					Server->Log("Error flushing wal file. " + os_last_error_str(), LL_ERROR);
					worker_abort();
				}
			}
		}

		lock.relock(mutex.get());

		for (size_t i = 0; i < other_items->size(); ++i)
		{
			if ((*other_items)[i].type == EType::Add
				|| (*other_items)[i].type == EType::Flush)
			{
				*(*other_items)[i].rowid = rowids[i];
			}
		}

		other_items->clear();

		commit_cond->notify_all();
	}
}

std::string KvStoreFrontend::PutDbWorker::meminfo() {
	std::string ret;
	IScopedLock lock(mutex.get());
	ret += "##KvStoreFrontend::PutDbWorker:\n";
	ret += "  items_a: " + convert(items_a.capacity()) + " * " + PrettyPrintBytes(sizeof(SItem)) + "\n";
	ret += "  items_b: " + convert(items_b.capacity()) + " * " + PrettyPrintBytes(sizeof(SItem)) + "\n";
	return ret;
}

void KvStoreFrontend::PutDbWorker::wait_queue(IScopedLock& lock)
{
	while (curr_items->size() > 10000)
	{
		commit_cond->wait(&lock);
	}
}

void KvStoreFrontend::PutDbWorker::worker_abort()
{
	std::string last_error = extractLastLogErrors();

	addSystemEvent("cache_db_err", "Error writing to cache db",
		"Error writing to database at " + db_path + ".\n"
		"Please make sure the cache device does not have any problems, then reboot to fix this issue\nLast Error:\n"
		+last_error,
		LL_ERROR);
	abort();
}

void KvStoreFrontend::ScrubThread::operator()()
{
	Auto(delete this);
	IFsFile* tmpf  = Server->openTemporaryFile();
	ScopedDeleteFile del_tmpf(tmpf);

	if (tmpf == nullptr)
	{
		Server->Log("Error opening temporary file in scrub. " + os_last_error_str(), LL_ERROR);
		scrub_queue.set_error(true);
		return;
	}

	while (true)
	{
		KvStoreDao::CdIterObject item = scrub_queue.get();
		if (item.tkey.empty()
			&& item.size==0)
		{
			break;
		}
		
		std::string ret_md5sum;
		unsigned int get_status;

		int flags = IKvStoreBackend::GetBackground;
		if (scrub_action == ScrubAction::Balance)
		{
			flags |= IKvStoreBackend::GetRebalance;
		}
		else if (scrub_action == ScrubAction::Scrub)
		{
			flags |= IKvStoreBackend::GetScrub;
		}
		else if(scrub_action==ScrubAction::Rebuild)
		{
			flags |= IKvStoreBackend::GetRebuild;
			flags |= IKvStoreBackend::GetNoThrottle;
		}

		if (with_last_modified)
		{
			flags |= IKvStoreBackend::GetReadahead;
		}

		std::string object_fn;

		if (item.trans_id==-2)
		{
			object_fn = item.tkey;
		}
		else if (item.trans_id == -1)
		{
			object_fn = frontend->prefixKey(item.tkey);
		}
		else
		{
			object_fn = frontend->prefixKey(frontend->encodeKey(item.tkey, item.trans_id));
		}

		bool b = backend->get(object_fn,
			item.md5sum, flags,
			false, tmpf, ret_md5sum, get_status);

		size_t tries = 3;
		size_t retry_n = 0;
		bool has_newer = false;
		while( (!b || (get_status & IKvStoreBackend::GetStatusRepairError) )
			&& item.trans_id > 0
			&& tries>0)
		{
			if (!(get_status & IKvStoreBackend::GetStatusEnospc))
			{
				int64 max_transid = frontend->get_transid(item.tkey, LLONG_MAX);
				if (max_transid > item.trans_id)
				{
					has_newer = true;
					break;
				}
			}
			Server->wait(1000);
			b = backend->get(object_fn,
				item.md5sum, flags,
				false,
				tmpf, ret_md5sum, get_status);
			--tries;
			++retry_n;

			if (get_status & IKvStoreBackend::GetStatusEnospc)
			{
				retryWait("ScrubEnospc", retry_n);
				tries = 3;
			}
		}

		bool curr_has_error=false;
		if (!b)
		{
			if (backend_mirror == nullptr
				|| !backend_mirror->get(object_fn,
					item.md5sum, flags,
					false, tmpf, ret_md5sum, get_status))
			{
				Server->Log("Error while " + strScrubAction(scrub_action) + " key " + object_fn+" Has_newer = "+convert(has_newer), LL_ERROR);
				curr_has_error = true;
				if (item.trans_id == -2 && item.tkey == "cd_magic_file")
				{
					Server->Log("Ignoring cd_magic_file scrub error", LL_WARNING);
				}
				else if (item.trans_id == -1 && (item.tkey.find("_complete") != std::string::npos
					|| item.tkey.find("_finalized") != std::string::npos
					|| item.tkey.find("_inactive") != std::string::npos))
				{
					Server->Log("Repairing. Adding empty file...", LL_WARNING);
					int64 size;
					std::string md5sum;
					std::unique_ptr<IFsFile> empty_file(frontend->cachefs->openFile("empty.file", MODE_READ));

					if(!empty_file)
					{
						Server->Log("Error while opening empty file. "+frontend->cachefs->lastError(), LL_ERROR);
						++scrub_queue.scrub_errors;
					}
					else
					{
						if (!backend->put(object_fn,
							empty_file.get(), 0, false, md5sum, size))
						{
							++scrub_queue.scrub_errors;
						}
						else
						{
							++scrub_queue.scrub_repaired;
						}
					}
				}
				else
				{
					++scrub_queue.scrub_errors;
				}
			}
			else if (backend_mirror != nullptr &&
				!(get_status & IKvStoreBackend::GetStatusSkipped))
			{
				bool put_success = false;
				size_t tries = 0;
				while (true)
				{
					std::string put_md5sum;
					int64 put_size = 0;
					if (backend->put(object_fn, tmpf, IKvStoreBackend::PutAlreadyCompressedEncrypted, true, put_md5sum, put_size))
					{
						put_success = true;
						item.md5sum = put_md5sum;
						IScopedLock lock(new_md5sums_mutex);
						new_md5sums.push_back(item);
						has_changes = true;
						break;
					}
					retryWait("ScrubBput2", ++tries);
				}

				if (put_success)
				{
					++scrub_queue.scrub_repaired;
				}
				else
				{
					Server->Log("Error while " + strScrubAction(scrub_action) + " key " + object_fn + ". Writing to storage failed (from mirror). Has_newer=" + convert(has_newer), LL_ERROR);
					++scrub_queue.scrub_errors;
				}
					
			}
			else if (backend_mirror != nullptr)
			{
				Server->Log("Backend mirror present, success, but no get_file. Has_newer=" + convert(has_newer), LL_ERROR);
				++scrub_queue.scrub_errors;
			}
		}
		else
		{
			if (get_status & IKvStoreBackend::GetStatusRepaired)
				++scrub_queue.scrub_repaired;
			else
				++scrub_queue.scrub_oks;
		}

		if(!(get_status & IKvStoreBackend::GetStatusSkipped))
		{
			if(!tmpf->Resize(0))
			{
				Server->Log("Error resizing temporary file in scrub. " + os_last_error_str(), LL_ERROR);
				scrub_queue.set_error(true);
				return;
			}
		}

		if (b
			&& !ret_md5sum.empty())
		{
			++frontend->total_balance_ops;
		}
		if (b
			&& !ret_md5sum.empty()
			&& item.md5sum != ret_md5sum)
		{
			item.md5sum = ret_md5sum;
			IScopedLock lock(new_md5sums_mutex);
			new_md5sums.push_back(item);
		}

		done_size += item.size;
		if ( !b && !has_changes)
		{
			IScopedLock lock(new_md5sums_mutex);
			has_changes = true;
		}
	}
}

void KvStoreFrontend::MirrorWorker::operator()()
{
#ifdef HAS_MIRROR
	std::vector<STimeSpan> mirror_window = ServerSettings::getWindow(frontend->mirror_window);

	KvStoreDao dao(Server->getDatabase(Server->getThreadID(), CLOUDDRIVE_DB));

	if (frontend->mirror_curr_total < 0)
	{
		DBScopedWriteTransaction db_write_trans(dao.getDb());

		KvStoreDao::SUnmirrored unmirrored = dao.getUnmirroredObjectsSize();
		frontend->mirror_curr_total = unmirrored.tsize;
		frontend->mirror_items = unmirrored.tcount;
	}

	int64 last_mirror = -1;

	while (!do_quit)
	{
		if (!frontend->mirror_window.empty()
			&& !ServerSettings::isInTimeSpan(mirror_window))
		{
			frontend->mirror_state = 1;
			background_worker.set_mirror_pause(false);
			Server->wait(60);
			continue;
		}

		Server->Log("Pausing background worker (mirror worker)...");

		bool has_task;
		do
		{
			has_task = dao.getTask(Server->getTimeSeconds() - frontend->task_delay).exists;

			background_worker.set_mirror_pause(false);
			while (background_worker.is_paused()
				&& !background_worker.get_pause())
			{
				Server->wait(100);
				background_worker.set_mirror_pause(false);
			}

			if (has_task)
			{
				Server->wait(10000);
			}

			background_worker.set_mirror_pause(true);
			Server->Log("Waiting for background worker (mirror worker)...");
			while (!background_worker.is_paused())
			{
				Server->wait(100);
				background_worker.set_mirror_pause(true);
			}
		} while (has_task);

		if (last_mirror == -1
			|| Server->getTimeMS() - last_mirror>30*60*1000)
		{
			last_mirror = Server->getTimeMS();

			frontend->mirror_state = 0;
			frontend->mirror_curr_pos = 0;
		}

		int64 orig_backend_mirror_del_log_rpos = frontend->backend_mirror_del_log_rpos;
		int64 orig_backend_mirror_del_log_wpos = frontend->get_backend_mirror_del_log_wpos();
		KvStoreFrontend* cfrontend = frontend;

		int64 del_items = 0;
		while (cfrontend->get_backend_mirror_del_log_rpos() < orig_backend_mirror_del_log_wpos)
		{
			if (cfrontend->next_log_del_mirror_item().empty())
				break;
			++del_items;
		}

		cfrontend->set_backend_mirror_del_log_rpos(orig_backend_mirror_del_log_rpos);

		frontend->mirror_items += del_items;

		bool ret = frontend->backend_mirror->del(
			[cfrontend, orig_backend_mirror_del_log_rpos, orig_backend_mirror_del_log_wpos, &del_items]
				(IKvStoreBackend::key_next_action_t action, std::string* key) {
			if (action == IKvStoreBackend::key_next_action_t::clear)
			{
				assert(key == nullptr);
				return true;
			}

			if (action == IKvStoreBackend::key_next_action_t::reset)
			{
				assert(key == nullptr);
				cfrontend->set_backend_mirror_del_log_rpos(orig_backend_mirror_del_log_rpos);
				return true;
			}

			if (cfrontend->get_backend_mirror_del_log_rpos() >= orig_backend_mirror_del_log_wpos)
				return false;

			*key = cfrontend->next_log_del_mirror_item();

			if (key->empty())
				return false;

			Server->Log("Deleting " + *key + " from mirror...", LL_INFO);
			if (del_items > 0)
			{
				--cfrontend->mirror_items;
				--del_items;
			}

			return true;
		}, false);

		if (!ret)
		{
			Server->Log("Deletion from mirror failed.", LL_INFO);
			frontend->mirror_items -= del_items;
			continue;
		}
		else if (del_items > 0)
		{
			last_mirror = Server->getTimeMS();
		}

		cfrontend->backend_mirror_del_log->PunchHole(0, cfrontend->get_backend_mirror_del_log_rpos());

		std::vector<KvStoreDao::CdIterObject2> objs = dao.getUnmirroredObjects();

		if (objs.empty())
		{
			std::vector<KvStoreDao::SCdTrans> transactions = dao.getUnmirroredTransactions();

			frontend->mirror_items += transactions.size();

			for (KvStoreDao::SCdTrans& trans : transactions)
			{
				std::string suffix;
				if (trans.completed == 2)
				{
					suffix = "_complete";
				}
				else if (trans.completed == 1)
				{
					suffix = "_finalized";
				}
				else if (trans.active == 0)
				{
					suffix = "_inactive";
				}
				else
				{
					continue;
				}

				std::unique_ptr<IFsFile> empty_file(frontend->cachefs->openFile(frontend->empty_file_path, MODE_READ));

				if(!empty_file)
				{
					Server->Log("Cannot open empty file: "+frontend->cachefs->lastError(), LL_ERROR);
					abort();
				}

				int64 size;
				std::string md5sum;
				if (frontend->backend_mirror->put(frontend->prefixKey(convert(trans.id) + suffix),
					empty_file.get(), 0, false, md5sum, size))
				{
					dao.setTransactionMirrored(trans.id);
					--frontend->mirror_items;
					last_mirror = Server->getTimeMS();
				}
				else
				{
					Server->Log("Mirroring transaction status failed.", LL_INFO);
					--frontend->mirror_items;
				}
			}

			if (transactions.empty())
			{
				frontend->mirror_state = 2;
			}
		}
		else
		{
			unsigned int stride_size = 100;
			size_t num_threads = os_get_num_cpus();

			std::vector<THREADPOOL_TICKET> tickets;
			std::vector<std::unique_ptr<MirrorThread> > mirror_threads;
			std::unique_ptr<IPipe> rpipe(Server->createMemoryPipe());
			mirror_threads.resize(num_threads);
			tickets.resize(num_threads);
			for (size_t i = 0; i < num_threads; ++i)
			{
				mirror_threads[i].reset(new MirrorThread(objs, rpipe.get(),
					frontend->backend, frontend->backend_mirror, frontend));
				tickets[i] = Server->getThreadPool()->execute(mirror_threads[i].get(),
					"mirror upload");
			}

			for (size_t i = 0; i < objs.size(); i += stride_size)
			{
				CWData wdata;
				wdata.addVarInt(i);
				wdata.addVarInt(stride_size);
				rpipe->Write(wdata.getDataPtr(), wdata.getDataSize());
			}

			rpipe->Write(std::string());

			Server->getThreadPool()->waitFor(tickets);

			bool has_error = false;
			for (auto& mt : mirror_threads)
			{
				if (mt->get_has_error())
					has_error = true;
			}

			if (!has_error)
			{
				DBScopedWriteTransaction trans(dao.getDb());

				for (auto obj : objs)
				{
					dao.setObjectMirrored(obj.id);
				}

				last_mirror = Server->getTimeMS();
			}
			else
			{
				Server->Log("Mirroring objects failed.", LL_INFO);
			}
		}
	}

	delete this;
#endif //HAS_MIRROR
}

void KvStoreFrontend::MirrorThread::operator()()
{
	IFsFile* tmpf = Server->openTemporaryFile();
	ScopedDeleteFile del_tmpf(tmpf);

	if(tmpf==nullptr)
		abort();

	std::string cdata;
	while (!has_error
		&& rpipe->Read(&cdata)>0)
	{
		CRData data(cdata.data(), cdata.size());

		int64 start;
		int64 size;
		bool b = data.getVarInt(&start);
		b &= data.getVarInt(&size);
		assert(b && start>=0 && size>0);

		for (size_t i = start; i < static_cast<size_t>(start + size); ++i)
		{
			KvStoreDao::CdIterObject2& obj = objs[i];

			std::string fn = frontend->prefixKey(frontend->encodeKey(obj.tkey, obj.trans_id));
			std::string ret_md5sum;
			unsigned int get_status;
			size_t retry_n = 0;
			bool success = false;
			while (!success
				&& retry_n < 10)
			{
				if(!tmpf->Resize(0) || !tmpf->Seek(0))
				{
					abort();
				}

				if (backend->get(fn, obj.md5sum, IKvStoreBackend::GetPrependMd5sum, false, tmpf, ret_md5sum, get_status))
				{
					size_t retry_n_put = 0;

					while (!success
						&& retry_n_put < 10)
					{
						tmpf->Seek(0);
						int64 ret_size;

						if (backend_mirror->put(fn, tmpf, IKvStoreBackend::PutAlreadyCompressedEncrypted, false, ret_md5sum, ret_size))
						{
							frontend->mirror_curr_pos += obj.size;
							success = true;
						}

						if (!success)
						{
							retryWait("Mirror", ++retry_n_put);
						}
					}

					if (!success)
					{
						has_error = true;
						break;
					}
				}

				if (!success)
				{
					retryWait("Mirror", ++retry_n);
				}
			}

			--frontend->mirror_items;

			if (!success)
			{
				has_error = true;
				break;
			}
		}
	}

	rpipe->Write(std::string());
}
