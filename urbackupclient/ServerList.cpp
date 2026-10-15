#include "ServerList.h"
#include "ServerIdentityMgr.h"
#include "file_permissions.h"
#include "database.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <memory>
#include <algorithm>

IMutex* ServerList::mutex = NULL;
bool ServerList::loaded = false;
std::vector<SServerEntry> ServerList::entries;

namespace
{
	const char* default_facet_name = "default";
	const char* facet_info_fn = "server.cfg";

	std::string boolStr(bool b)
	{
		return b ? "true" : "false";
	}

	std::string oneLine(std::string str)
	{
		str = greplace("\r", "", str);
		return greplace("\n", " ", str);
	}

	bool getSetting(ISettingsReader* settings, const std::string& key, std::string& value)
	{
		return settings->getValue(key, &value) || settings->getValue(key + "_def", &value);
	}

	bool isInternetKey(const std::string& key)
	{
		return key == "internet_mode_enabled" || key == "internet_server" || key == "internet_server_port"
			|| key == "internet_server_proxy" || key == "internet_authkey" || key == "internet_compress"
			|| key == "internet_encrypt";
	}

	bool sameInternetSettings(const SServerEntry& a, const SServerEntry& b)
	{
		return a.internet == b.internet
			&& a.internet_server == b.internet_server
			&& a.internet_server_port == b.internet_server_port
			&& a.internet_server_proxy == b.internet_server_proxy
			&& a.internet_authkey == b.internet_authkey
			&& a.internet_compress == b.internet_compress
			&& a.internet_encrypt == b.internet_encrypt;
	}

	void copyInternetSettings(const SServerEntry& src, SServerEntry& dst)
	{
		dst.internet = src.internet;
		dst.internet_server = src.internet_server;
		dst.internet_server_port = src.internet_server_port;
		dst.internet_server_proxy = src.internet_server_proxy;
		dst.internet_authkey = src.internet_authkey;
		dst.internet_compress = src.internet_compress;
		dst.internet_encrypt = src.internet_encrypt;
	}

	void clearInternetSettings(SServerEntry& entry)
	{
		copyInternetSettings(SServerEntry(), entry);
	}

	IDatabase* getDb()
	{
		return Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	}
}

void ServerList::init_mutex()
{
	mutex = Server->createMutex();
}

void ServerList::destroy_mutex()
{
	Server->destroy(mutex);
}

std::string ServerList::getFacetDir(int facet_id)
{
	return "urbackup/data_" + convert(facet_id);
}

std::string ServerList::getSettingsFn(int facet_id, const std::string& clientsubname)
{
	if (clientsubname.empty())
	{
		return getFacetDir(facet_id) + "/settings.cfg";
	}
	return getFacetDir(facet_id) + "/settings_" + conv_filename(clientsubname) + ".cfg";
}

std::string ServerList::getDefaultSettingsFn(const std::string& clientsubname)
{
	return getSettingsFn(getDefaultFacetId(), clientsubname);
}

std::string ServerList::toText(const std::vector<SServerEntry>& p_entries, bool with_status, bool with_authkey)
{
	std::string ret = "count=" + convert(p_entries.size()) + "\n";
	for (size_t i = 0; i < p_entries.size(); ++i)
	{
		const SServerEntry& e = p_entries[i];
		std::string p = convert(i) + ".";
		ret += p + "id=" + convert(e.id) + "\n";
		ret += p + "name=" + oneLine(e.name) + "\n";
		ret += p + "ident=" + oneLine(e.ident) + "\n";
		ret += p + "endpoint=" + oneLine(e.endpoint) + "\n";
		ret += p + "local=" + boolStr(e.local) + "\n";
		ret += p + "internet=" + boolStr(e.internet) + "\n";
		ret += p + "internet_server=" + oneLine(e.internet_server) + "\n";
		ret += p + "internet_server_port=" + oneLine(e.internet_server_port) + "\n";
		ret += p + "internet_server_proxy=" + oneLine(e.internet_server_proxy) + "\n";
		if (with_authkey)
		{
			ret += p + "internet_authkey=" + oneLine(e.internet_authkey) + "\n";
		}
		ret += p + "internet_compress=" + boolStr(e.internet_compress) + "\n";
		ret += p + "internet_encrypt=" + boolStr(e.internet_encrypt) + "\n";

		if (with_status && !e.ident.empty())
		{
			ret += p + "fingerprint=" + ServerIdentityMgr::getPublicKeys(e.ident).fingerprint + "\n";
			ret += p + "online=" + boolStr(ServerIdentityMgr::isServerOnline(e.ident)) + "\n";
		}
	}
	return ret;
}

bool ServerList::fromText(const std::string& text, std::vector<SServerEntry>& p_entries)
{
	std::auto_ptr<ISettingsReader> reader(Server->createMemorySettingsReader(text));
	if (reader.get() == NULL)
	{
		return false;
	}

	std::string count_str;
	if (!reader->getValue("count", &count_str))
	{
		return false;
	}

	int count = watoi(count_str);
	p_entries.clear();
	for (int i = 0; i < count; ++i)
	{
		std::string p = convert(i) + ".";
		SServerEntry e;
		std::string id_str;
		if (!reader->getValue(p + "id", &id_str))
		{
			return false;
		}
		e.id = watoi(id_str);
		e.name = reader->getValue(p + "name", "");
		e.ident = reader->getValue(p + "ident", "");
		e.endpoint = reader->getValue(p + "endpoint", "");
		e.local = reader->getValue(p + "local", "true") == "true";
		e.internet = reader->getValue(p + "internet", "false") == "true";
		e.internet_server = reader->getValue(p + "internet_server", "");
		e.internet_server_port = reader->getValue(p + "internet_server_port", "");
		e.internet_server_proxy = reader->getValue(p + "internet_server_proxy", "");
		e.internet_authkey = reader->getValue(p + "internet_authkey", "");
		e.internet_compress = reader->getValue(p + "internet_compress", "true") == "true";
		e.internet_encrypt = reader->getValue(p + "internet_encrypt", "true") == "true";
		p_entries.push_back(e);
	}
	return true;
}

void ServerList::readInternetSettings(SServerEntry& entry)
{
	clearInternetSettings(entry);

	std::auto_ptr<ISettingsReader> settings(Server->createFileSettingsReader(getSettingsFn(entry.id)));
	if (settings.get() == NULL)
	{
		return;
	}

	std::string val;
	entry.internet = getSetting(settings.get(), "internet_mode_enabled", val) && val == "true";
	if (getSetting(settings.get(), "internet_server", val)) entry.internet_server = val;
	if (getSetting(settings.get(), "internet_server_port", val)) entry.internet_server_port = val;
	if (getSetting(settings.get(), "internet_server_proxy", val)) entry.internet_server_proxy = val;
	if (getSetting(settings.get(), "internet_authkey", val)) entry.internet_authkey = val;
	entry.internet_compress = !(getSetting(settings.get(), "internet_compress", val) && val == "false");
	entry.internet_encrypt = !(getSetting(settings.get(), "internet_encrypt", val) && val == "false");

	//Servers send internet_mode_enabled=true without an internet server if none is configured
	//on them. There is nothing to connect to then
	if (entry.internet_server.empty())
	{
		entry.internet = false;
	}
}

bool ServerList::writeInternetSettings(const SServerEntry& entry)
{
	std::string settings_fn = getSettingsFn(entry.id);
	std::string data = getFile(settings_fn);
	std::string new_data;
	int numl = linecount(data);
	for (int i = 0; i <= numl; ++i)
	{
		std::string l = trim(getline(i, data));
		if (l.empty()
			|| isInternetKey(trim(getuntil("=", l))))
		{
			continue;
		}
		new_data += l + "\n";
	}
	new_data += "internet_mode_enabled=" + boolStr(entry.internet) + "\n";
	new_data += "internet_server=" + oneLine(entry.internet_server) + "\n";
	new_data += "internet_server_port=" + oneLine(entry.internet_server_port) + "\n";
	new_data += "internet_server_proxy=" + oneLine(entry.internet_server_proxy) + "\n";
	new_data += "internet_authkey=" + oneLine(entry.internet_authkey) + "\n";
	new_data += "internet_compress=" + boolStr(entry.internet_compress) + "\n";
	new_data += "internet_encrypt=" + boolStr(entry.internet_encrypt) + "\n";

	createFacetDir(entry.id);
	if (!write_file_only_admin(new_data, settings_fn + ".new"))
	{
		return false;
	}
	return os_rename_file(settings_fn + ".new", settings_fn);
}

void ServerList::readFacetInfo(SServerEntry& entry)
{
	std::auto_ptr<ISettingsReader> info(Server->createFileSettingsReader(getFacetDir(entry.id) + "/" + facet_info_fn));
	if (info.get() == NULL)
	{
		return;
	}

	entry.name = info->getValue("name", "");
	entry.endpoint = info->getValue("endpoint", "");
	entry.local = info->getValue("local", "true") != "false";
	entry.internet_confirmed_server = info->getValue("internet_confirmed_server", "");
}

bool ServerList::writeFacetInfo(const SServerEntry& entry)
{
	std::string fn = getFacetDir(entry.id) + "/" + facet_info_fn;
	std::string last_backup;
	std::string backup_interval;
	{
		std::auto_ptr<ISettingsReader> info(Server->createFileSettingsReader(fn));
		if (info.get() != NULL)
		{
			last_backup = info->getValue("last_backup", "");
			backup_interval = info->getValue("backup_interval", "");
		}
	}

	std::string data = "name=" + oneLine(entry.name) + "\n"
		"endpoint=" + oneLine(entry.endpoint) + "\n"
		"local=" + boolStr(entry.local) + "\n"
		"internet_confirmed_server=" + oneLine(entry.internet_confirmed_server) + "\n";
	if (!last_backup.empty())
	{
		data += "last_backup=" + last_backup + "\n";
	}
	if (!backup_interval.empty())
	{
		data += "backup_interval=" + backup_interval + "\n";
	}

	createFacetDir(entry.id);
	return write_file_only_admin(data, fn);
}

void ServerList::createFacetDir(int id)
{
	std::string facet_dir = getFacetDir(id);
	if (!os_directory_exists(facet_dir))
	{
		os_create_dir(facet_dir);
#ifndef _DEBUG
		change_file_permissions_admin_only(facet_dir);
#endif
	}
}

int ServerList::addFacet(const std::string& ident)
{
	IDatabase* db = getDb();
	IQuery* q = db->Prepare("INSERT INTO client_facets (name, server_identity) VALUES (?, ?)", false);
	if (q == NULL)
	{
		return 0;
	}
	q->Bind(entries.empty() ? std::string(default_facet_name) : std::string());
	q->Bind(ident);
	q->Write();
	q->Reset();
	db->destroyQuery(q);

	int id = static_cast<int>(db->getLastInsertID());
	if (!entries.empty())
	{
		db->Write("UPDATE client_facets SET name='server" + convert(id) + "' WHERE id=" + convert(id));
	}

	createFacetDir(id);

	SServerEntry e;
	e.id = id;
	e.ident = ident;
	entries.push_back(e);
	return id;
}

void ServerList::removeFacet(int id)
{
	IDatabase* db = getDb();
	db->Write("DELETE FROM backupdirs WHERE facet=" + convert(id));
	db->Write("DELETE FROM virtual_client_group_offsets WHERE facet=" + convert(id));
	db->Write("UPDATE fileaccess_tokens SET facet=NULL WHERE facet=" + convert(id));
	db->Write("DELETE FROM client_facets WHERE id=" + convert(id));

	os_remove_nonempty_dir(os_file_prefix(Server->getServerWorkingDir() + os_file_sep()
		+ "urbackup" + os_file_sep() + "data_" + convert(id)));

	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (entries[i].id == id)
		{
			entries.erase(entries.begin() + i);
			break;
		}
	}
}

void ServerList::seedFacet(int src_id, int dst_id)
{
	createFacetDir(dst_id);

	std::string src_dir = getFacetDir(src_id);
	std::string dst_dir = getFacetDir(dst_id);
	std::vector<SFile> files = getFiles(src_dir);
	for (size_t i = 0; i < files.size(); ++i)
	{
		//settings.cfg and the settings of the virtual clients
		if (files[i].isdir
			|| !next(files[i].name, 0, "settings")
			|| findextension(files[i].name) != "cfg")
		{
			continue;
		}

		std::string dst_fn = dst_dir + os_file_sep() + files[i].name;
		if (!FileExists(dst_fn))
		{
			//Without the settings version of the other server
			std::string data = getFile(src_dir + os_file_sep() + files[i].name);
			std::string new_data;
			int numl = linecount(data);
			for (int j = 0; j <= numl; ++j)
			{
				std::string l = trim(getline(j, data));
				if (!l.empty()
					&& trim(getuntil("=", l)) != "update_version")
				{
					new_data += l + "\n";
				}
			}
			write_file_only_admin(new_data, dst_fn);
		}
	}

	IDatabase* db = getDb();
	std::string str_src = convert(src_id);
	std::string str_dst = convert(dst_id);
	Server->Log("Client facet " + convert(dst_id) + " starts with the settings and backup paths of facet " + convert(src_id)
		+ " until its server sends its own", LL_INFO);
	db->Write("INSERT INTO virtual_client_group_offsets (virtual_client, group_offset, facet) "
		"SELECT virtual_client, group_offset, " + str_dst + " FROM virtual_client_group_offsets a WHERE facet=" + str_src +
		" AND NOT EXISTS (SELECT 1 FROM virtual_client_group_offsets b WHERE b.facet=" + str_dst + " AND b.virtual_client=a.virtual_client)");
	db->Write("INSERT INTO backupdirs (name, path, server_default, optional, tgroup, symlinked, reset_keep, facet) "
		"SELECT name, path, server_default, optional, tgroup, 0, reset_keep, " + str_dst + " FROM backupdirs a WHERE symlinked=0 AND facet=" + str_src +
		" AND NOT EXISTS (SELECT 1 FROM backupdirs b WHERE b.facet=" + str_dst + " AND b.symlinked=0 AND b.tgroup=a.tgroup AND b.path=a.path)");
}

void ServerList::setFacetIdent(int id, const std::string& ident)
{
	IDatabase* db = getDb();
	IQuery* q = db->Prepare("UPDATE client_facets SET server_identity=? WHERE id=?", false);
	if (q == NULL)
	{
		return;
	}
	q->Bind(ident);
	q->Bind(id);
	q->Write();
	q->Reset();
	db->destroyQuery(q);

	SServerEntry* e = findId(id);
	if (e != NULL)
	{
		e->ident = ident;
	}
}

void ServerList::load()
{
	if (loaded)
	{
		return;
	}
	loaded = true;

	entries.clear();
	db_results res = getDb()->Read("SELECT id, server_identity FROM client_facets ORDER BY id ASC");
	for (size_t i = 0; i < res.size(); ++i)
	{
		SServerEntry e;
		e.id = watoi(res[i]["id"]);
		e.ident = res[i]["server_identity"];
		createFacetDir(e.id);
		readFacetInfo(e);
		readInternetSettings(e);
		entries.push_back(e);
	}

	if (entries.empty())
	{
		//There always is a facet, so the first server has something to take over
		addFacet(std::string());
	}
}

void ServerList::settingsChanged()
{
	IScopedLock lock(mutex);
	load();
	for (size_t i = 0; i < entries.size(); ++i)
	{
		readInternetSettings(entries[i]);
	}
}

SServerEntry* ServerList::findIdent(const std::string& ident)
{
	if (ident.empty())
	{
		return NULL;
	}
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (entries[i].ident == ident)
		{
			return &entries[i];
		}
	}
	return NULL;
}

SServerEntry* ServerList::findId(int id)
{
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (entries[i].id == id)
		{
			return &entries[i];
		}
	}
	return NULL;
}

std::vector<SServerEntry> ServerList::getEntries()
{
	IScopedLock lock(mutex);
	load();
	return entries;
}

bool ServerList::getEntryByIdent(const std::string& ident, SServerEntry& entry)
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findIdent(ident);
	if (e == NULL)
	{
		return false;
	}
	entry = *e;
	return true;
}

bool ServerList::getEntryById(int id, SServerEntry& entry)
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findId(id);
	if (e == NULL)
	{
		return false;
	}
	entry = *e;
	return true;
}

int ServerList::getFacetId(const std::string& ident)
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findIdent(ident);
	return e != NULL ? e->id : 0;
}

int ServerList::getDefaultFacetId()
{
	IScopedLock lock(mutex);
	load();
	//Entries are ordered by id
	return entries.empty() ? 1 : entries[0].id;
}

int ServerList::getFacetIdOrDefault(const std::string& ident)
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findIdent(ident);
	if (e != NULL)
	{
		return e->id;
	}
	return entries.empty() ? 1 : entries[0].id;
}

std::vector<int> ServerList::getFacetIds()
{
	IScopedLock lock(mutex);
	load();
	std::vector<int> ret;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		ret.push_back(entries[i].id);
	}
	return ret;
}

void ServerList::setFacetInfoValue(const std::string& ident, const std::string& key, const std::string& value)
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findIdent(ident);
	if (e == NULL)
	{
		return;
	}

	std::string fn = getFacetDir(e->id) + "/" + facet_info_fn;
	std::string data = getFile(fn);
	std::string new_data;
	int numl = linecount(data);
	for (int i = 0; i <= numl; ++i)
	{
		std::string l = trim(getline(i, data));
		if (!l.empty() && getuntil("=", l) != key)
		{
			new_data += l + "\n";
		}
	}
	new_data += key + "=" + value + "\n";
	write_file_only_admin(new_data, fn);
}

std::string ServerList::getFacetInfoValue(const std::string& ident, const std::string& key)
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findIdent(ident);
	if (e == NULL)
	{
		return std::string();
	}

	std::auto_ptr<ISettingsReader> info(Server->createFileSettingsReader(getFacetDir(e->id) + "/" + facet_info_fn));
	if (info.get() == NULL)
	{
		return std::string();
	}
	return info->getValue(key, "");
}

void ServerList::setLastBackup(const std::string& ident, int64 backup_time)
{
	setFacetInfoValue(ident, "last_backup", convert(backup_time));
}

int64 ServerList::getLastBackup(const std::string& ident)
{
	return watoi64(getFacetInfoValue(ident, "last_backup"));
}

void ServerList::setBackupInterval(const std::string& ident, int backup_interval)
{
	if (getBackupInterval(ident) != backup_interval)
	{
		setFacetInfoValue(ident, "backup_interval", convert(backup_interval));
	}
}

int ServerList::getBackupInterval(const std::string& ident)
{
	return watoi(getFacetInfoValue(ident, "backup_interval"));
}

int ServerList::resolveFacet(const std::string& server)
{
	if (server.empty())
	{
		return 0;
	}

	IScopedLock lock(mutex);
	load();
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if ((!entries[i].ident.empty() && entries[i].ident == server)
			|| convert(entries[i].id) == server
			|| (!entries[i].name.empty() && strlower(entries[i].name) == strlower(server)))
		{
			return entries[i].id;
		}
	}
	return 0;
}

std::string ServerList::resolveServer(const std::string& server)
{
	int facet_id = resolveFacet(server);
	if (facet_id == 0)
	{
		return std::string();
	}

	IScopedLock lock(mutex);
	SServerEntry* e = findId(facet_id);
	return e != NULL ? e->ident : std::string();
}

void ServerList::addTrustedIdent(const std::string& ident)
{
	if (ident.empty())
	{
		return;
	}

	{
		IScopedLock lock(mutex);
		load();
		if (findIdent(ident) != NULL)
		{
			return;
		}

		//The server takes over a facet that has no server yet (e.g. the facet of a new
		//installation, or the one with the settings from before there were facets)
		bool found = false;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (entries[i].ident.empty()
				&& entries[i].local)
			{
				Server->Log("Client facet " + convert(entries[i].id) + " belongs to server " + ident, LL_INFO);
				setFacetIdent(entries[i].id, ident);
				found = true;
				break;
			}
		}

		if (!found)
		{
			int id = addFacet(ident);
			Server->Log("New client facet " + convert(id) + " for server " + ident, LL_INFO);
		}
	}

	ServerIdentityMgr::updateFacetShares();
}

bool ServerList::setInternetIdent(int id, const std::string& ident)
{
	{
		IScopedLock lock(mutex);
		load();
		SServerEntry* e = findId(id);
		if (e == NULL || ident.empty())
		{
			return false;
		}

		if (e->ident == ident)
		{
			if (e->internet_confirmed_server != e->internet_server)
			{
				e->internet_confirmed_server = e->internet_server;
				writeFacetInfo(*e);
			}
			return true;
		}

		SServerEntry* other = findIdent(ident);
		if (other == NULL
			&& e->ident.empty())
		{
			Server->Log("Client facet " + convert(id) + " belongs to internet server " + ident, LL_INFO);
			setFacetIdent(id, ident);
			e->internet_confirmed_server = e->internet_server;
			writeFacetInfo(*e);
		}
		else
		{
			//The internet server has (or gets) its own facet. The internet settings it
			//authenticated with belong there
			int other_id;
			if (other != NULL)
			{
				other_id = other->id;
			}
			else
			{
				other_id = addFacet(ident);
				if (other_id == 0)
				{
					return false;
				}
			}
			e = findId(id);
			other = findId(other_id);

			if (!e->ident.empty()
				&& !FileExists(getSettingsFn(other_id)))
			{
				//Both servers used the settings and paths of this facet so far. A server only
				//sends its settings when it starts with the client or when they are changed,
				//so without them its new facet would have no paths to back up until then
				seedFacet(id, other_id);
			}

			Server->Log("Internet server of client facet " + convert(id) + " has identity " + ident
				+ ". Moving the internet settings to its facet " + convert(other_id), LL_INFO);

			copyInternetSettings(*e, *other);
			writeInternetSettings(*other);
			other->internet_confirmed_server = other->internet_server;

			if (e->ident.empty())
			{
				if (other->name.empty()) other->name = e->name;
				writeFacetInfo(*other);
				removeFacet(id);
			}
			else
			{
				writeFacetInfo(*other);
				clearInternetSettings(*e);
				writeInternetSettings(*e);
			}
		}
	}

	ServerIdentityMgr::updateFacetShares();
	return true;
}

void ServerList::setEndpoint(const std::string& ident, const std::string& endpoint)
{
	if (endpoint.empty())
	{
		return;
	}

	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findIdent(ident);
	if (e != NULL && e->endpoint != endpoint)
	{
		e->endpoint = endpoint;
		writeFacetInfo(*e);
	}
}

bool ServerList::setEntries(const std::vector<SServerEntry>& new_entries)
{
	std::vector<std::string> removed_idents;
	bool ok = true;
	{
		IScopedLock lock(mutex);
		load();

		std::vector<int> removed_ids;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			bool found = false;
			for (size_t j = 0; j < new_entries.size(); ++j)
			{
				if (new_entries[j].id == entries[i].id)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				removed_ids.push_back(entries[i].id);
				if (!entries[i].ident.empty())
				{
					removed_idents.push_back(entries[i].ident);
				}
			}
		}

		for (size_t i = 0; i < removed_ids.size(); ++i)
		{
			removeFacet(removed_ids[i]);
		}

		for (size_t i = 0; i < new_entries.size(); ++i)
		{
			SServerEntry* e = findId(new_entries[i].id);
			if (e == NULL)
			{
				//New server. It gets its identity when it connects
				int id = addFacet(std::string());
				if (id == 0)
				{
					ok = false;
					continue;
				}
				e = findId(id);
			}

			//The identity of an entry is only set by the server itself
			bool info_mod = e->name != new_entries[i].name
				|| e->local != new_entries[i].local;
			e->name = new_entries[i].name;
			e->local = new_entries[i].local;
			if (info_mod
				&& !writeFacetInfo(*e))
			{
				ok = false;
			}

			if (!sameInternetSettings(*e, new_entries[i]))
			{
				copyInternetSettings(new_entries[i], *e);
				if (!writeInternetSettings(*e))
				{
					ok = false;
				}
			}
		}

		if (entries.empty())
		{
			addFacet(std::string());
		}
	}

	for (size_t i = 0; i < removed_idents.size(); ++i)
	{
		Server->Log("Server " + removed_idents[i] + " was removed from the server list. No longer trusting it.", LL_INFO);
		ServerIdentityMgr::removeServerIdentity(removed_idents[i]);
	}

	ServerIdentityMgr::updateFacetShares();

	return ok;
}
