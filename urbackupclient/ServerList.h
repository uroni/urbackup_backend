#pragma once

#include <string>
#include <vector>
#include <map>
#include "../Interface/Types.h"

class IMutex;
class ISettingsReader;

//A server this client is backed up by. Each server has a client facet (table client_facets)
//with its own settings, backup paths and file lists in urbackup/data_<facet id>.
//A server can be reached via LAN (local), via internet or both
struct SServerEntry
{
	SServerEntry()
		: id(0), local(true), internet(false), internet_compress(true),
		internet_encrypt(true)
	{}

	//Id of the client facet of the server
	int id;
	//Display name (empty: use endpoint or internet server host name)
	std::string name;
	//Server identity (empty if the server has not connected yet)
	std::string ident;
	//Last LAN address the server connected from
	std::string endpoint;

	bool local;
	//The internet settings are the ones in the settings.cfg of the facet
	bool internet;

	std::string internet_server;
	std::string internet_server_port;
	std::string internet_server_proxy;
	std::string internet_authkey;
	bool internet_compress;
	bool internet_encrypt;

	//The internet server (internet_server) that authenticated with the identity of this entry.
	//While it differs from internet_server, the internet server may be another server
	std::string internet_confirmed_server;

	bool internetServerConfirmed() const
	{
		return !ident.empty() && !internet_server.empty()
			&& internet_confirmed_server == internet_server;
	}
};

class ServerList
{
public:
	static void init_mutex();
	static void destroy_mutex();

	static std::vector<SServerEntry> getEntries();
	//Replaces all entries (e.g. from the tray UI). Entries removed from the list lose their trust
	//and their facet
	static bool setEntries(const std::vector<SServerEntry>& entries);

	static bool getEntryByIdent(const std::string& ident, SServerEntry& entry);
	static bool getEntryById(int id, SServerEntry& entry);
	//Identity of the server given as identity, facet id or name (empty if unknown)
	static std::string resolveServer(const std::string& server);
	//Facet of the server given as identity, facet id or name (0 if unknown)
	static int resolveFacet(const std::string& server);

	//Facet of the server with this identity (0 if it has none)
	static int getFacetId(const std::string& ident);
	//Facet of the server with this identity. If it has none the default facet
	static int getFacetIdOrDefault(const std::string& ident);
	//The facet with the lowest id. Settings that cannot differ between servers are taken from it
	static int getDefaultFacetId();
	static std::vector<int> getFacetIds();

	//urbackup/data_<facet id>
	static std::string getFacetDir(int facet_id);
	//settings.cfg (or settings_<clientsubname>.cfg) of a facet
	static std::string getSettingsFn(int facet_id, const std::string& clientsubname = std::string());
	static std::string getDefaultSettingsFn(const std::string& clientsubname = std::string());

	//Time (unix seconds) of the last successful backup a server did of this client (0: none yet)
	static void setLastBackup(const std::string& ident, int64 backup_time);
	static int64 getLastBackup(const std::string& ident);

	//Called when a server identity becomes trusted. Gives the server a facet if it has none
	static void addTrustedIdent(const std::string& ident);

	//Called when the server the internet connection of facet id connects to authenticated
	//with its identity. Returns false if that is not possible
	static bool setInternetIdent(int id, const std::string& ident);

	static void setEndpoint(const std::string& ident, const std::string& endpoint);

	//The settings.cfg of a facet changed: read the internet settings again
	static void settingsChanged();

	//Serialization as key=value lines ("count=N", "<n>.<field>=<value>") for the tray UI
	//(with_status adds read-only status fields)
	//with_authkey: false for callers that are not allowed to see the internet auth keys
	static std::string toText(const std::vector<SServerEntry>& entries, bool with_status, bool with_authkey = true);
	static bool fromText(const std::string& text, std::vector<SServerEntry>& entries);

private:
	static void load();
	static void readInternetSettings(SServerEntry& entry);
	static bool writeInternetSettings(const SServerEntry& entry);
	static void readFacetInfo(SServerEntry& entry);
	static bool writeFacetInfo(const SServerEntry& entry);
	static int addFacet(const std::string& ident);
	static void removeFacet(int id);
	static void setFacetIdent(int id, const std::string& ident);
	static void createFacetDir(int id);
	static SServerEntry* findIdent(const std::string& ident);
	static SServerEntry* findId(int id);

	static IMutex* mutex;
	static bool loaded;
	static std::vector<SServerEntry> entries;
};
