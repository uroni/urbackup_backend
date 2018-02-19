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
#include "server_update.h"
#include "../urlplugin/IUrlFactory.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "DataplanDb.h"
#include <stdlib.h>
#include <memory>

extern IUrlFactory *url_fak;

namespace
{
	std::string urbackup_update_url = "http://update5.urbackup.org/";
	std::string urbackup_update_url_alt;

	struct SUpdatePlatform
	{
		SUpdatePlatform(std::string extension,
			std::string basename, std::string versionname)
			: extension(extension), basename(basename),
			versionname(versionname)
		{}
		std::string extension;
		std::string basename;
		std::string versionname;
	};
}

ServerUpdate::ServerUpdate(void)
{
}

void ServerUpdate::update_client()
{
	if(url_fak==NULL)
	{
		Server->Log("Urlplugin not found. Cannot download client for autoupdate.", LL_ERROR);
		return;
	}

	read_update_location();

	std::string http_proxy = Server->getServerParameter("http_proxy");

	std::vector<SUpdatePlatform> update_files;

	update_files.push_back(SUpdatePlatform("exe", "UrBackupUpdate", "version.txt"));
	update_files.push_back(SUpdatePlatform("sh", "UrBackupUpdateMac", "version_osx.txt"));
	update_files.push_back(SUpdatePlatform("sh", "UrBackupUpdateLinux", "version_linux.txt"));

	std::string curr_update_url = urbackup_update_url;

	for (size_t i = 0; i < update_files.size(); ++i)
	{
		SUpdatePlatform& curr = update_files[i];

		std::string errmsg;
		Server->Log("Downloading version file...", LL_INFO);
		std::string version = url_fak->downloadString(curr_update_url + curr.versionname, http_proxy, &errmsg);
		if (version.empty())
		{
			if (curr_update_url == urbackup_update_url)
			{
				curr_update_url = urbackup_update_url_alt;
				version = url_fak->downloadString(curr_update_url + curr.versionname, http_proxy, &errmsg);
			}

			if (version.empty())
			{
				Server->Log("Error while downloading version info from " + curr_update_url + curr.versionname + ": " + errmsg, LL_ERROR);
				return;
			}
		}
		std::string curr_version = getFile("urbackup/"+curr.versionname);
		if (curr_version.empty()) curr_version = "0";

		if (version!=curr_version)
		{
			Server->Log("Downloading signature...", LL_INFO);

			IFile* sig_file = Server->openFile("urbackup/" + curr.basename + ".sig2", MODE_WRITE);
			if (sig_file == NULL)
			{
				Server->Log("Error opening signature output file urbackup/" + curr.basename + ".sig2", LL_ERROR);
				return;
			}
			ObjectScope sig_file_scope(sig_file);

			bool b = url_fak->downloadFile(curr_update_url + curr.basename + ".sig2", sig_file, http_proxy, &errmsg);

			if (!b)
			{
				Server->Log("Error while downloading update signature from " + curr_update_url + curr.basename + ".sig2: " + errmsg, LL_ERROR);
			}

			if (curr.extension == "exe")
			{
				Server->Log("Downloading old signature...", LL_INFO);

				IFile* old_sig_file = Server->openFile("urbackup/" + curr.basename + ".sig", MODE_WRITE);
				if (old_sig_file == NULL)
				{
					Server->Log("Error opening signature output file urbackup/" + curr.basename + ".sig", LL_ERROR);
					return;
				}
				ObjectScope old_sig_file_scope(old_sig_file);

				bool b = url_fak->downloadFile(curr_update_url + curr.basename + ".sig", old_sig_file, http_proxy, &errmsg);

				if (!b)
				{
					Server->Log("Error while downloading old update signature from " + curr_update_url + curr.basename + ".sig: " + errmsg, LL_ERROR);
				}
			}

			Server->Log("Getting update file URL...", LL_INFO);
			std::string update_url = url_fak->downloadString(curr_update_url + curr.basename + ".url", http_proxy, &errmsg);

			if (update_url.empty())
			{
				Server->Log("Error while downloading update url from " + curr_update_url + curr.basename + ".url: " + errmsg, LL_ERROR);
				return;
			}

			IFile* update_file = Server->openFile("urbackup/" + curr.basename + "." + curr.extension, MODE_WRITE);
			if (update_file == NULL)
			{
				Server->Log("Error opening update output file urbackup/" + curr.basename + "." + curr.extension, LL_ERROR);
				return;
			}
			ObjectScope update_file_scope(update_file);

			Server->Log("Downloading update file...", LL_INFO);
			b = url_fak->downloadFile(update_url, update_file, http_proxy, &errmsg);

			if (!b)
			{
				Server->Log("Error while downloading update file from " + update_url + ": " + errmsg, LL_ERROR);
				return;
			}

			sig_file->Sync();
			update_file->Sync();

			Server->Log("Successfully downloaded update file.", LL_INFO);
			writestring(version, "urbackup/"+curr.versionname);
		}
	}
}

void ServerUpdate::update_server_version_info()
{
	if(url_fak==NULL)
	{
		Server->Log("Urlplugin not found. Cannot download server version info.", LL_ERROR);
		return;
	}

	read_update_location();

	std::string http_proxy = Server->getServerParameter("http_proxy");

	std::string errmsg;
	Server->Log("Downloading server version info...", LL_INFO);

	std::auto_ptr<IFile> server_version_info(Server->openFile("urbackup/server_version_info.properties.new", MODE_WRITE));

	if(!server_version_info.get())
	{
		Server->Log("Error opening urbackup/server_version_info.properties.new for writing", LL_ERROR);
	}
	else
	{
		if(!url_fak->downloadFile(urbackup_update_url+"server_version_info.properties", 
			server_version_info.get(), http_proxy, &errmsg) )
		{
			Server->Log("Error downloading server version information: " + errmsg, LL_ERROR);
		}
		else
		{
			server_version_info.reset();
			if (!os_rename_file("urbackup/server_version_info.properties.new",
								"urbackup/server_version_info.properties"))
			{
				Server->Log("Error renaming server_version_info.properties . " + os_last_error_str(), LL_ERROR);
			}
		}
	}	
}

void ServerUpdate::update_dataplan_db()
{
	if (url_fak == NULL)
	{
		Server->Log("Urlplugin not found. Cannot download dataplan database.", LL_ERROR);
		return;
	}

	read_update_location();

	std::string http_proxy = Server->getServerParameter("http_proxy");

	std::string errmsg;
	Server->Log("Downloading dataplan database...", LL_INFO);

	std::auto_ptr<IFile> dataplan_db(Server->openFile("urbackup/dataplan_db.txt.new", MODE_WRITE));
	if (!dataplan_db.get())
	{
		Server->Log("Error opening urbackup/dataplan_db.txt.new for writing", LL_ERROR);
	}
	else
	{
		if (!url_fak->downloadFile(urbackup_update_url + "dataplan_db.txt",
			dataplan_db.get(), http_proxy, &errmsg))
		{
			Server->Log("Error downloading dataplan database: " + errmsg, LL_ERROR);
		}
		else
		{
			dataplan_db.reset();
			if (!os_rename_file("urbackup/dataplan_db.txt.new",
				"urbackup/dataplan_db.txt"))
			{
				Server->Log("Error renaming urbackup/dataplan_db.txt. " + os_last_error_str(), LL_ERROR);
			}

			DataplanDb::getInstance()->read("urbackup/dataplan_db.txt");
		}

	}
}

void ServerUpdate::read_update_location()
{
	std::string read_update_location = trim(getFile("urbackup/server_update_location.url"));

	if (!read_update_location.empty())
	{
		urbackup_update_url_alt = read_update_location;
		urbackup_update_url = urbackup_update_url_alt;

		if (!urbackup_update_url.empty()
			&& urbackup_update_url[urbackup_update_url.size() - 1] != '/')
			urbackup_update_url += "/";

		urbackup_update_url += "2.2.x/";
	}
}
