#include "ServerList.h"
#include "ServerIdentityMgr.h"
#include "file_permissions.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/SettingsReader.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <memory>
#include <algorithm>

IMutex* ServerList::mutex = NULL;
bool ServerList::loaded = false;
std::vector<SServerEntry> ServerList::entries;

namespace
{
	const char* server_list_fn = "urbackup/data/server_list.cfg";
	const char* settings_fn = "urbackup/data/settings.cfg";
	const char* internet_server_ident_fn = "urbackup/data/internet_server_ident.txt";

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
}

namespace
{
	const char* backupdir_servers_fn = "urbackup/data/backupdir_servers.cfg";
}

std::string ServerList::backupDirKey(int tgroup, std::string path)
{
	//Same normalization as ClientConnector::saveBackupDirs
	if (!path.empty()
		&& (path[path.size() - 1] == '\\' || path[path.size() - 1] == '/'))
	{
		path.erase(path.size() - 1, 1);
#ifndef _WIN32
		if (path.empty())
			path = "/";
#endif
	}
#ifdef _WIN32
	path = strlower(path);
#endif
	return convert(tgroup) + "\t" + path;
}

ServerList::BackupDirServers ServerList::getBackupDirServers()
{
	IScopedLock lock(mutex);
	BackupDirServers ret;
	std::string data = getFile(backupdir_servers_fn);
	int numl = linecount(data);
	for (int i = 0; i <= numl; ++i)
	{
		std::string l = getline(i, data);
		if (!l.empty() && l[l.size() - 1] == '\r')
			l.erase(l.size() - 1);
		size_t last_tab = l.find_last_of('\t');
		if (last_tab == std::string::npos || l.find('\t') == last_tab)
			continue;
		std::vector<std::string> idents;
		Tokenize(l.substr(last_tab + 1), idents, ",");
		ret[l.substr(0, last_tab)] = idents;
	}
	return ret;
}

void ServerList::setBackupDirServers(int tgroup_min, int tgroup_max, const BackupDirServers& dir_servers)
{
	IScopedLock lock(mutex);
	std::string data = getFile(backupdir_servers_fn);
	std::string new_data;
	int numl = linecount(data);
	for (int i = 0; i <= numl; ++i)
	{
		std::string l = trim(getline(i, data));
		if (l.empty())
			continue;
		int tgroup = watoi(getuntil("\t", l));
		if (tgroup >= tgroup_min && tgroup <= tgroup_max)
			continue;
		new_data += l + "\n";
	}
	for (BackupDirServers::const_iterator it = dir_servers.begin(); it != dir_servers.end(); ++it)
	{
		std::string idents;
		for (size_t i = 0; i < it->second.size(); ++i)
		{
			if (!idents.empty()) idents += ",";
			idents += it->second[i];
		}
		new_data += it->first + "\t" + idents + "\n";
	}
	write_file_only_admin(new_data, backupdir_servers_fn);
}

void ServerList::init_mutex()
{
	mutex = Server->createMutex();
}

void ServerList::destroy_mutex()
{
	Server->destroy(mutex);
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

void ServerList::load()
{
	if (loaded)
	{
		return;
	}
	loaded = true;

	std::string data = getFile(server_list_fn);
	if (data.empty()
		|| !fromText(data, entries))
	{
		migrate();
	}
}

bool ServerList::save()
{
	return write_file_only_admin(toText(entries, false), server_list_fn);
}

void ServerList::migrate()
{
	//First start with a server list: one entry for the internet server configured in
	//settings.cfg (id 0) and one for each trusted server
	entries.clear();

	std::auto_ptr<ISettingsReader> settings(Server->createFileSettingsReader(settings_fn));
	std::string internet_server;
	if (settings.get() != NULL
		&& getSetting(settings.get(), "internet_server", internet_server)
		&& !internet_server.empty())
	{
		SServerEntry e;
		e.id = 0;
		e.local = false;
		e.ident = trim(getFile(internet_server_ident_fn));
		entries.push_back(e);
		updateEntry0(settings.get());
	}

	std::vector<std::string> idents = ServerIdentityMgr::getServerIdentities();
	for (size_t i = 0; i < idents.size(); ++i)
	{
		SServerEntry* e = findIdent(idents[i]);
		if (e != NULL)
		{
			e->local = true;
			continue;
		}

		SServerEntry n;
		n.id = nextId();
		n.ident = idents[i];
		n.local = true;
		n.internet = false;
		entries.push_back(n);
	}

	Server->Log("Created server list with " + convert(entries.size()) + " servers", LL_INFO);
	save();
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

int ServerList::nextId()
{
	int id = 1;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		id = (std::max)(id, entries[i].id + 1);
	}
	return id;
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

namespace
{
	const char* server_last_backup_fn = "urbackup/data/server_last_backup.cfg";
}

void ServerList::setLastBackup(const std::string& ident, int64 backup_time)
{
	if (ident.empty())
	{
		return;
	}

	IScopedLock lock(mutex);
	std::string data = getFile(server_last_backup_fn);
	std::string new_data;
	int numl = linecount(data);
	for (int i = 0; i <= numl; ++i)
	{
		std::string l = trim(getline(i, data));
		if (!l.empty() && getuntil("=", l) != ident)
		{
			new_data += l + "\n";
		}
	}
	new_data += ident + "=" + convert(backup_time) + "\n";
	writestring(new_data, server_last_backup_fn);
}

int64 ServerList::getLastBackup(const std::string& ident)
{
	IScopedLock lock(mutex);
	std::string data = getFile(server_last_backup_fn);
	int numl = linecount(data);
	for (int i = 0; i <= numl; ++i)
	{
		std::string l = trim(getline(i, data));
		if (!l.empty() && getuntil("=", l) == ident)
		{
			return watoi64(getafter("=", l));
		}
	}
	return 0;
}

std::string ServerList::resolveServer(const std::string& server)
{
	if (server.empty())
	{
		return std::string();
	}

	IScopedLock lock(mutex);
	load();
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (!entries[i].ident.empty()
			&& (entries[i].ident == server
				|| convert(entries[i].id) == server
				|| (!entries[i].name.empty() && strlower(entries[i].name) == strlower(server))))
		{
			return entries[i].ident;
		}
	}
	return std::string();
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

std::string ServerList::getDefaultInternetIdent()
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findId(0);
	return e != NULL ? e->ident : std::string();
}

void ServerList::addTrustedIdent(const std::string& ident)
{
	if (ident.empty())
	{
		return;
	}

	IScopedLock lock(mutex);
	load();
	if (findIdent(ident) != NULL)
	{
		return;
	}

	SServerEntry e;
	e.id = nextId();
	e.ident = ident;
	e.local = true;
	e.internet = false;
	entries.push_back(e);
	save();
}

bool ServerList::setInternetIdent(int id, const std::string& ident)
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
		return true;
	}

	if (!e->ident.empty())
	{
		Server->Log("Internet server of server list entry " + convert(id) + " has identity " + ident
			+ " but entry belongs to " + e->ident, LL_ERROR);
		return false;
	}

	//The server may already have an entry (e.g. it was trusted via LAN). Merge it into this one
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (entries[i].ident == ident
			&& entries[i].id != id)
		{
			e->local = e->local || entries[i].local;
			if (e->name.empty()) e->name = entries[i].name;
			if (e->endpoint.empty()) e->endpoint = entries[i].endpoint;
			entries.erase(entries.begin() + i);
			e = findId(id);
			break;
		}
	}

	Server->Log("Server list entry " + convert(id) + " belongs to server " + ident, LL_INFO);
	e->ident = ident;
	save();
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
		save();
	}
}

bool ServerList::updateFromServerSettings(const std::string& ident, ISettingsReader* settings)
{
	IScopedLock lock(mutex);
	load();
	SServerEntry* e = findIdent(ident);
	if (e == NULL || e->id == 0)
	{
		//Entry 0 is kept in sync with settings.cfg
		return false;
	}

	bool mod = false;
	std::string val;
	//Like the critical settings in settings.cfg: an empty value from the server does not
	//replace a configured one
	if (getSetting(settings, "internet_server", val) && !val.empty() && val != e->internet_server)
	{
		e->internet_server = val; mod = true;
	}
	if (getSetting(settings, "internet_server_port", val) && !val.empty() && val != e->internet_server_port)
	{
		e->internet_server_port = val; mod = true;
	}
	if (getSetting(settings, "internet_authkey", val) && !val.empty() && val != e->internet_authkey)
	{
		e->internet_authkey = val; mod = true;
	}
	if (getSetting(settings, "internet_server_proxy", val) && val != e->internet_server_proxy)
	{
		e->internet_server_proxy = val; mod = true;
	}
	if (getSetting(settings, "internet_compress", val) && (val != "false") != e->internet_compress)
	{
		e->internet_compress = val != "false"; mod = true;
	}
	if (getSetting(settings, "internet_encrypt", val) && (val != "false") != e->internet_encrypt)
	{
		e->internet_encrypt = val != "false"; mod = true;
	}

	if (mod)
	{
		save();
	}
	return mod;
}

void ServerList::updateFromLocalSettings(ISettingsReader* settings)
{
	IScopedLock lock(mutex);
	load();
	if (updateEntry0(settings))
	{
		save();
	}
}

bool ServerList::updateEntry0(ISettingsReader* settings)
{
	SServerEntry* e = findId(0);
	std::string internet_server;
	getSetting(settings, "internet_server", internet_server);

	if (e == NULL)
	{
		if (internet_server.empty())
		{
			return false;
		}
		SServerEntry n;
		n.id = 0;
		n.local = false;
		entries.insert(entries.begin(), n);
		e = &entries[0];
	}

	SServerEntry old = *e;

	std::string val;
	e->internet = getSetting(settings, "internet_mode_enabled", val) && val == "true";
	e->internet_server = internet_server;
	e->internet_server_port = getSetting(settings, "internet_server_port", val) ? val : std::string();
	e->internet_server_proxy = getSetting(settings, "internet_server_proxy", val) ? val : std::string();
	e->internet_authkey = getSetting(settings, "internet_authkey", val) ? val : std::string();
	e->internet_compress = !(getSetting(settings, "internet_compress", val) && val == "false");
	e->internet_encrypt = !(getSetting(settings, "internet_encrypt", val) && val == "false");

	return old.internet != e->internet
		|| old.internet_server != e->internet_server
		|| old.internet_server_port != e->internet_server_port
		|| old.internet_server_proxy != e->internet_server_proxy
		|| old.internet_authkey != e->internet_authkey
		|| old.internet_compress != e->internet_compress
		|| old.internet_encrypt != e->internet_encrypt;
}

bool ServerList::setEntries(const std::vector<SServerEntry>& new_entries)
{
	std::vector<std::string> removed_idents;
	SServerEntry entry0;
	bool has_entry0 = false;
	{
		IScopedLock lock(mutex);
		load();

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
			if (!found && !entries[i].ident.empty())
			{
				removed_idents.push_back(entries[i].ident);
			}
		}

		std::vector<SServerEntry> result = new_entries;
		for (size_t i = 0; i < result.size(); ++i)
		{
			//The identity of an entry is only set by the server itself
			SServerEntry* old = findId(result[i].id);
			result[i].ident = old != NULL ? old->ident : std::string();
			result[i].endpoint = old != NULL ? old->endpoint : std::string();
			if (old == NULL && result[i].id != 0)
			{
				result[i].id = -1;
			}
		}
		entries = result;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (entries[i].id < 0)
			{
				entries[i].id = 0;
				entries[i].id = nextId();
			}
		}

		SServerEntry* e0 = findId(0);
		if (e0 != NULL)
		{
			entry0 = *e0;
			has_entry0 = true;
		}

		if (!save())
		{
			return false;
		}
	}

	for (size_t i = 0; i < removed_idents.size(); ++i)
	{
		Server->Log("Server " + removed_idents[i] + " was removed from the server list. No longer trusting it.", LL_INFO);
		ServerIdentityMgr::removeServerIdentity(removed_idents[i]);
	}

	if (has_entry0)
	{
		//Mirror entry 0 into settings.cfg for components (and older tray versions) reading it there
		std::string data = getFile(settings_fn);
		std::string new_data;
		int numl = linecount(data);
		for (int i = 0; i <= numl; ++i)
		{
			std::string l = trim(getline(i, data));
			std::string key = trim(getuntil("=", l));
			if (key == "internet_mode_enabled" || key == "internet_server" || key == "internet_server_port"
				|| key == "internet_server_proxy" || key == "internet_authkey" || key == "internet_compress"
				|| key == "internet_encrypt")
			{
				continue;
			}
			if (!l.empty())
			{
				new_data += l + "\n";
			}
		}
		new_data += "internet_mode_enabled=" + boolStr(entry0.internet) + "\n";
		new_data += "internet_server=" + entry0.internet_server + "\n";
		new_data += "internet_server_port=" + entry0.internet_server_port + "\n";
		new_data += "internet_server_proxy=" + entry0.internet_server_proxy + "\n";
		new_data += "internet_authkey=" + entry0.internet_authkey + "\n";
		new_data += "internet_compress=" + boolStr(entry0.internet_compress) + "\n";
		new_data += "internet_encrypt=" + boolStr(entry0.internet_encrypt) + "\n";

		if (write_file_only_admin(new_data, std::string(settings_fn) + ".new"))
		{
			os_rename_file(std::string(settings_fn) + ".new", settings_fn);
		}
	}

	return true;
}
