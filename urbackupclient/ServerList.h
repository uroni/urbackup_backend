#pragma once

#include <string>
#include <vector>
#include <map>
#include "../Interface/Types.h"

class IMutex;
class ISettingsReader;

//A server this client is backed up by. A server can be reached via LAN (local),
//via internet or both
struct SServerEntry
{
	SServerEntry()
		: id(0), local(true), internet(false), internet_compress(true),
		internet_encrypt(true)
	{}

	//Stable id of the entry (entry 0 mirrors the internet settings in settings.cfg)
	int id;
	//Display name (empty: use endpoint or internet server host name)
	std::string name;
	//Server identity (empty if the server has not connected yet)
	std::string ident;
	//Last LAN address the server connected from
	std::string endpoint;

	bool local;
	bool internet;

	std::string internet_server;
	std::string internet_server_port;
	std::string internet_server_proxy;
	std::string internet_authkey;
	bool internet_compress;
	bool internet_encrypt;
};

class ServerList
{
public:
	static void init_mutex();
	static void destroy_mutex();

	static std::vector<SServerEntry> getEntries();
	//Replaces all entries (e.g. from the tray UI). Entries removed from the list lose their trust
	static bool setEntries(const std::vector<SServerEntry>& entries);

	static bool getEntryByIdent(const std::string& ident, SServerEntry& entry);
	//Identity of the server given as identity, server list id or name (empty if unknown)
	static std::string resolveServer(const std::string& server);

	//Time (unix seconds) of the last successful backup a server did of this client (0: none yet)
	static void setLastBackup(const std::string& ident, int64 backup_time);
	static int64 getLastBackup(const std::string& ident);
	static bool getEntryById(int id, SServerEntry& entry);

	//Entry 0, whose internet settings are mirrored in settings.cfg
	static std::string getDefaultInternetIdent();

	//Called when a server identity becomes trusted. Adds a LAN entry if there is none
	static void addTrustedIdent(const std::string& ident);

	//Called when the server of internet entry id authenticated with its identity.
	//Returns false if the entry belongs to another server
	static bool setInternetIdent(int id, const std::string& ident);

	static void setEndpoint(const std::string& ident, const std::string& endpoint);

	//Update the internet settings of the entry of server ident from settings sent by that server.
	//Returns true if they changed
	static bool updateFromServerSettings(const std::string& ident, ISettingsReader* settings);

	//settings.cfg was changed locally (old tray UI, urbackupclientctl set-settings): update entry 0
	static void updateFromLocalSettings(ISettingsReader* settings);

	//Which servers configured a default backup directory (tgroup, path). Directories without an
	//entry (e.g. added on the client) are backed up by all servers
	typedef std::map<std::string, std::vector<std::string> > BackupDirServers;
	static std::string backupDirKey(int tgroup, std::string path);
	static BackupDirServers getBackupDirServers();
	//Replace the entries with tgroup in [tgroup_min, tgroup_max]
	static void setBackupDirServers(int tgroup_min, int tgroup_max, const BackupDirServers& dir_servers);
	//Servers chosen on the client for a backup directory ("Add/Remove backup paths"). Takes
	//precedence over getBackupDirServers(). Directories without an entry go to all servers
	static BackupDirServers getClientBackupDirServers();
	static void setClientBackupDirServers(int tgroup_min, int tgroup_max, const BackupDirServers& dir_servers);
	//Servers that back up a directory: chosen on the client, else the servers that configured it.
	//Empty: all servers. from_client is set if the servers were chosen on the client
	static std::vector<std::string> getServersOfBackupDir(const BackupDirServers& client_dir_servers,
		const BackupDirServers& dir_servers, const std::string& key, bool* from_client = NULL);

	//Serialization as key=value lines ("count=N", "<n>.<field>=<value>"), used for
	//server_list.cfg and for the tray UI (with_status adds read-only status fields)
	//with_authkey: false for callers that are not allowed to see the internet auth keys
	static std::string toText(const std::vector<SServerEntry>& entries, bool with_status, bool with_authkey = true);
	static bool fromText(const std::string& text, std::vector<SServerEntry>& entries);

private:
	static void load();
	static bool save();
	static void migrate();
	//Sets entry 0 from the internet settings in settings.cfg. Returns true if it changed
	static bool updateEntry0(ISettingsReader* settings);
	static SServerEntry* findIdent(const std::string& ident);
	static SServerEntry* findId(int id);
	static int nextId();

	static BackupDirServers readDirServers(const std::string& fn);
	static void writeDirServers(const std::string& fn, int tgroup_min, int tgroup_max, const BackupDirServers& dir_servers);

	static IMutex* mutex;
	static bool loaded;
	static std::vector<SServerEntry> entries;
};
