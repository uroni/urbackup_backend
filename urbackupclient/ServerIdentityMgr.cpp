/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include "ServerIdentityMgr.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <algorithm>
#include "file_permissions.h"

const unsigned int ident_online_timeout=1*60*60*1000; //1h

std::vector<std::string> ServerIdentityMgr::identities;
IMutex *ServerIdentityMgr::mutex=NULL;
IFileServ *ServerIdentityMgr::filesrv=NULL;
std::vector<unsigned int> ServerIdentityMgr::online_identities;
std::vector<std::string> ServerIdentityMgr::new_identities;
std::vector<std::string> ServerIdentityMgr::publickeys;
std::vector<std::string> ServerIdentityMgr::session_identities;
std::vector<unsigned int> ServerIdentityMgr::online_session_identities;

#ifdef _WIN32
const std::string server_ident_file="server_idents.txt";
const std::string server_session_ident_file="session_idents.txt";
const std::string server_new_ident_file="new_server_idents.txt";
#else
const std::string server_ident_file="urbackup/server_idents.txt";
const std::string server_session_ident_file="urbackup/session_idents.txt";
const std::string server_new_ident_file="urbackup/new_server_idents.txt";
#endif

void ServerIdentityMgr::init_mutex(void)
{
	mutex=Server->createMutex();
}

void ServerIdentityMgr::destroy_mutex(void)
{
	Server->destroy(mutex);
}

void ServerIdentityMgr::addServerIdentity(const std::string &pIdentity, const std::string& pPublicKey)
{
	IScopedLock lock(mutex);
	loadServerIdentities();
	identities.push_back(pIdentity);
	publickeys.push_back(pPublicKey);
	online_identities.push_back(0);
	if(pPublicKey.empty())
	{
		filesrv->addIdentity("#I"+pIdentity+"#");
	}
	writeServerIdentities();
}

bool ServerIdentityMgr::checkServerSessionIdentity(const std::string &pIdentity)
{
	IScopedLock lock(mutex);

	for(size_t i=0;i<session_identities.size();++i)
	{
		if(session_identities[i]==pIdentity)
		{
			online_session_identities[i]=Server->getTimeMS();
			return true;
		}
	}

	return false;
}

bool ServerIdentityMgr::checkServerIdentity(const std::string &pIdentity)
{
	IScopedLock lock(mutex);

	for(size_t i=0;i<identities.size();++i)
	{
		if(identities[i]==pIdentity)
		{
			online_identities[i]=Server->getTimeMS();
			return true;
		}
	}
	return false;
}

void ServerIdentityMgr::loadServerIdentities(void)
{
	IScopedLock lock(mutex);
	std::vector<std::string> old_identities=identities;
	std::vector<unsigned int> old_online_identities=online_identities;
	identities.clear();
	online_identities.clear();
	std::string data=getFile(server_ident_file);
	int numl=linecount(data);
	for(int i=0;i<numl;++i)
	{
		std::string l=trim(getline(i, data));
		if(!l.empty() && l[0]=='#')
		{
			continue;
		}
		if(l.size()>5)
		{
			size_t hashpos = l.find("#");
			if(hashpos!=std::string::npos)
			{
				str_nmap params;
				ParseParamStr(l.substr(hashpos+1), &params);
				
				publickeys.push_back(base64_decode_dash(params["pubkey"]));

				l = l.substr(0, hashpos);
			}
			else
			{
				filesrv->addIdentity("#I"+l+"#");
				publickeys.push_back("");
			}
			identities.push_back(l);
			std::vector<std::string>::iterator it=std::find(old_identities.begin(), old_identities.end(), l);
			if(it!=old_identities.end())
			{
				online_identities.push_back(old_online_identities[it-old_identities.begin()]);
			}
			else
			{
				online_identities.push_back(0);
			}
		}
	}
	new_identities.clear();
	data=getFile(server_new_ident_file);
	numl=linecount(data);
	for(int i=0;i<numl;++i)
	{
		std::string l=trim(getline(i, data));
		if(l.size()>5)
		{
			new_identities.push_back(l);
		}
	}
	old_identities=session_identities;
	old_online_identities=online_session_identities;
	session_identities.clear();
	data=getFile(server_session_ident_file);
	numl=linecount(data);
	for(int i=0;i<numl;++i)
	{
		std::string l=trim(getline(i, data));
		if(l.size()>5)
		{
			session_identities.push_back(l);

			filesrv->addIdentity("#I"+l+"#");

			std::vector<std::string>::iterator it=std::find(old_identities.begin(), old_identities.end(), l);
			if(it!=old_identities.end())
			{
				online_session_identities.push_back(old_online_identities[it-old_identities.begin()]);
			}
			else
			{
				online_session_identities.push_back(0);
			}
		}
	}
}

void ServerIdentityMgr::setFileServ(IFileServ *pFilesrv)
{
	filesrv=pFilesrv;
}

size_t ServerIdentityMgr::numServerIdentities(void)
{
	IScopedLock lock(mutex);
	return identities.size();
}

bool ServerIdentityMgr::isNewIdentity(const std::string &pIdentity)
{
	IScopedLock lock(mutex);
	if( std::find(new_identities.begin(), new_identities.end(), pIdentity)!=new_identities.end())
	{
		return false;
	}
	else
	{
		new_identities.push_back(pIdentity);
		writeServerIdentities();
		return true;
	}
}

void ServerIdentityMgr::writeServerIdentities(void)
{
	IScopedLock lock(mutex);
	std::string idents;
	for(size_t i=0;i<identities.size();++i)
	{
		if(!idents.empty()) idents+="\r\n";
		idents+=identities[i];
		if(!publickeys[i].empty())
		{
			idents+="#pubkey="+base64_encode_dash(publickeys[i]);
		}
	}
	write_file_only_admin(idents, server_ident_file);

	std::string new_idents;
	for(size_t i=0;i<new_identities.size();++i)
	{
		if(!new_idents.empty()) new_idents+="\r\n";
		new_idents+=new_identities[i];
	}
	write_file_only_admin(new_idents, server_new_ident_file);
}

bool ServerIdentityMgr::hasOnlineServer(void)
{
	IScopedLock lock(mutex);
	unsigned int ctime=Server->getTimeMS();
	for(size_t i=0;i<online_identities.size();++i)
	{
		if(online_identities[i]!=0 && ctime-online_identities[i]<ident_online_timeout)
		{
			return true;
		}
	}
	for(size_t i=0;i<online_session_identities.size();++i)
	{
		if(online_session_identities[i]!=0 && ctime-online_session_identities[i]<ident_online_timeout)
		{
			return true;
		}
	}
	return false;
}

std::string ServerIdentityMgr::getPublicKey( const std::string &pIdentity )
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<identities.size();++i)
	{
		if(identities[i]==pIdentity)
		{
			return publickeys[i];
		}
	}
	return std::string();
}

bool ServerIdentityMgr::setPublicKey( const std::string &pIdentity, const std::string &pPublicKey )
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<identities.size();++i)
	{
		if(identities[i]==pIdentity)
		{
			publickeys[i]=pPublicKey;
			if(!pPublicKey.empty())
			{
				filesrv->removeIdentity(pIdentity);
			}
			writeServerIdentities();
			return true;
		}
	}
	return false;
}

bool ServerIdentityMgr::hasPublicKey( const std::string &pIdentity )
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<identities.size();++i)
	{
		if(identities[i]==pIdentity)
		{
			return !publickeys[i].empty();
		}
	}
	return false;
}

void ServerIdentityMgr::addSessionIdentity( const std::string &pIdentity )
{
	IScopedLock lock(mutex);
	session_identities.push_back(pIdentity);
	online_session_identities.push_back(0);
	filesrv->addIdentity("#I"+pIdentity+"#");
	writeSessionIdentities();
}

void ServerIdentityMgr::writeSessionIdentities()
{
	IScopedLock lock(mutex);

	const size_t max_session_identities=20;

	size_t start=0;
	if(session_identities.size()>max_session_identities)
	{
		start=session_identities.size()-max_session_identities;
	}

	std::string idents;
	for(size_t i=start;i<session_identities.size();++i)
	{
		if(!idents.empty()) idents+="\r\n";
		idents+=session_identities[i];
	}
	write_file_only_admin(idents, server_session_ident_file);
}
