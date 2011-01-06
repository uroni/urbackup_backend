/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "../stringtools.h"

std::vector<std::wstring> getSettingsList(void)
{
	std::vector<std::wstring> ret;
	ret.push_back(L"update_freq_incr");
	ret.push_back(L"update_freq_full");
	ret.push_back(L"update_freq_image_full");
	ret.push_back(L"update_freq_image_incr");
	ret.push_back(L"max_file_incr");
	ret.push_back(L"min_file_incr");
	ret.push_back(L"max_file_full");
	ret.push_back(L"min_file_full");
	ret.push_back(L"min_image_incr");
	ret.push_back(L"max_image_incr");
	ret.push_back(L"min_image_full");
	ret.push_back(L"max_image_full");
	ret.push_back(L"startup_backup_delay");
	return ret;
}

#ifndef CLIENT_ONLY

#include "server_settings.h"
#include "../Interface/Server.h"

std::vector<ServerSettings*> ServerSettings::g_settings;
IMutex *ServerSettings::g_mutex=NULL;

void ServerSettings::init_mutex(void)
{
	if(g_mutex==NULL)
		g_mutex=Server->createMutex();
}

ServerSettings::ServerSettings(IDatabase *db, int pClientid) : clientid(pClientid)
{
	{
		IScopedLock lock(g_mutex);
		g_settings.push_back(this);
	}

	settings_default=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings WHERE key=? AND clientid=0");
	if(clientid!=-1)
	{
		settings_client=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings WHERE key=? AND clientid="+nconvert(clientid));
	}
	else
	{
		settings_client=NULL;
	}

	update();
	do_update=false;
}

ServerSettings::~ServerSettings(void)
{
	Server->destroy(settings_default);
	if(settings_client!=NULL)
	{
		Server->destroy(settings_client);
	}

	{
		IScopedLock lock(g_mutex);
		for(size_t i=0;i<g_settings.size();++i)
		{
			if(g_settings[i]==this)
			{
				g_settings.erase(g_settings.begin()+i);
				break;
			}
		}
	}
}

void ServerSettings::updateAll(void)
{
	IScopedLock lock(g_mutex);
	for(size_t i=0;i<g_settings.size();++i)
	{
		g_settings[i]->doUpdate();
	}
}

void ServerSettings::update(void)
{
	readSettingsDefault();
	if(settings_client!=NULL)
	{
		readSettingsClient();
	}
}

void ServerSettings::doUpdate(void)
{
	do_update=true;
}

SSettings *ServerSettings::getSettings(void)
{
	if(do_update)
	{
		do_update=false;
		update();
	}
	return &settings;
}

void ServerSettings::readSettingsDefault(void)
{
	settings.update_freq_incr=settings_default->getValue("update_freq_incr", 60*60);
	settings.update_freq_full=settings_default->getValue("update_freq_full", 30*24*60*60);
	settings.update_freq_image_incr=settings_default->getValue("update_freq_image_incr", 7*24*60*60);
	settings.update_freq_image_full=settings_default->getValue("update_freq_image_full", 60*24*60*60);
	settings.max_file_incr=settings_default->getValue("max_file_incr", 100);
	settings.min_file_incr=settings_default->getValue("min_file_incr", 40);
	settings.max_file_full=settings_default->getValue("max_file_full", 10);
	settings.min_file_full=settings_default->getValue("min_file_full", 2);
	settings.min_image_incr=settings_default->getValue("min_image_incr", 4);
	settings.max_image_incr=settings_default->getValue("max_image_incr", 30);
	settings.min_image_full=settings_default->getValue("min_image_full", 2);
	settings.max_image_full=settings_default->getValue("max_image_full", 5);
	settings.no_images=(settings_default->getValue("no_images", "false")=="true");
	settings.overwrite=false;
	settings.allow_overwrite=(settings_default->getValue("allow_overwrite", "true")=="true");
	settings.backupfolder=settings_default->getValue(L"backupfolder", L"C:\\urbackup");
	settings.client_overwrite=true;
	settings.autoshutdown=false;
	settings.startup_backup_delay=settings_default->getValue("startup_backup_delay", 0);
}

void ServerSettings::readSettingsClient(void)
{	
	std::string stmp=settings_client->getValue("client_overwrite", "");
	if(!stmp.empty())
		settings.client_overwrite=(stmp=="true");
	if(!settings.client_overwrite)
		return;

	int tmp=settings_client->getValue("update_freq_incr", -1);
	if(tmp!=-1)
		settings.update_freq_incr=tmp;
	tmp=settings_client->getValue("update_freq_full", -1);
	if(tmp!=-1)
		settings.update_freq_full=tmp;
	tmp=settings_client->getValue("update_freq_image_incr", -1);
	if(tmp!=-1)
		settings.update_freq_image_incr=tmp;
	tmp=settings_client->getValue("update_freq_image_full", -1);
	if(tmp!=-1)
		settings.update_freq_image_full=tmp;
	tmp=settings_client->getValue("max_file_incr", -1);
	if(tmp!=-1)
		settings.max_file_incr=tmp;
	tmp=settings_client->getValue("min_file_incr", -1);
	if(tmp!=-1)
		settings.min_file_incr=tmp;
	tmp=settings_client->getValue("max_file_full", -1);
	if(tmp!=-1)
		settings.max_file_full=tmp;
	tmp=settings_client->getValue("min_file_full", -1);
	if(tmp!=-1)
		settings.min_file_full=tmp;
	tmp=settings_client->getValue("min_image_incr", -1);
	if(tmp!=-1)
		settings.min_image_incr=tmp;
	tmp=settings_client->getValue("max_image_incr", -1);
	if(tmp!=-1)
		settings.max_image_incr=tmp;
	tmp=settings_client->getValue("min_image_full", -1);
	if(tmp!=-1)
		settings.min_image_full=tmp;
	tmp=settings_client->getValue("max_image_full", -1);
	if(tmp!=-1)
		settings.max_image_full=tmp;
	tmp=settings_client->getValue("startup_backup_delay", -1);
	if(tmp!=-1)
		settings.startup_backup_delay=tmp;

	stmp=settings_client->getValue("overwrite", "");
	if(!stmp.empty())
		settings.overwrite=(stmp=="true");
	if(!settings.overwrite)
		return;

	stmp=settings_client->getValue("allow_overwrite", "");
	if(!stmp.empty())
		settings.allow_overwrite=(stmp=="true");
}

#endif //CLIENT_ONLY