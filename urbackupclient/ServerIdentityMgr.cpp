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

#include "ServerIdentityMgr.h"
#include "ServerList.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <algorithm>
#include <memory>
#include "file_permissions.h"
#include "../urbackupcommon/os_functions.h"
#include "../cryptoplugin/ICryptoFactory.h"

const unsigned int ident_online_timeout=1*60*60*1000; //1h
const unsigned int recent_online_timeout=5*60*1000; //Session used in the last 5min

extern ICryptoFactory* crypto_fak;

std::vector<SIdentity> ServerIdentityMgr::identities;
IMutex *ServerIdentityMgr::mutex=NULL;
IFileServ *ServerIdentityMgr::filesrv=NULL;
std::vector<std::string> ServerIdentityMgr::new_identities;
std::vector<SSessionIdentity> ServerIdentityMgr::session_identities;
std::map<std::string, std::string> ServerIdentityMgr::server_token_identities;

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

void ServerIdentityMgr::addServerIdentity(const std::string &pIdentity, const SPublicKeys& pPublicKey)
{
	{
		IScopedLock lock(mutex);
		loadServerIdentities();
		identities.push_back(SIdentity(pIdentity, pPublicKey));
		filesrv->setIdentityServer("#I"+pIdentity+"#", pIdentity);
		if(pPublicKey.empty())
		{
			filesrv->addIdentity("#I"+pIdentity+"#", false);
		}
		writeServerIdentities();
	}
	ServerList::addTrustedIdent(pIdentity);
}

std::vector<std::string> ServerIdentityMgr::getServerIdentities()
{
	IScopedLock lock(mutex);
	std::vector<std::string> ret;
	for(size_t i=0;i<identities.size();++i)
	{
		ret.push_back(identities[i].ident);
	}
	return ret;
}

bool ServerIdentityMgr::isServerOnline(const std::string& server_ident)
{
	IScopedLock lock(mutex);
	int64 ctime=Server->getTimeMS();
	for(size_t i=0;i<session_identities.size();++i)
	{
		if(session_identities[i].server_ident==server_ident
			&& session_identities[i].onlinetime>0
			&& ctime-session_identities[i].onlinetime<recent_online_timeout)
		{
			return true;
		}
	}
	return false;
}

void ServerIdentityMgr::removeServerIdentity(const std::string& server_ident)
{
	IScopedLock lock(mutex);
	loadServerIdentities();
	std::vector<SIdentity>::iterator it=std::find(identities.begin(), identities.end(), SIdentity(server_ident));
	if(it==identities.end())
	{
		return;
	}
	identities.erase(it);
	filesrv->removeIdentity("#I" + server_ident + "#");
	filesrv->setIdentityServer("#I" + server_ident + "#", std::string());

	for(size_t i=0;i<session_identities.size();)
	{
		if(session_identities[i].server_ident==server_ident)
		{
			filesrv->removeIdentity("#I" + session_identities[i].ident + "#");
			filesrv->setIdentityServer("#I" + session_identities[i].ident + "#", std::string());
			session_identities.erase(session_identities.begin()+i);
		}
		else
		{
			++i;
		}
	}

	writeServerIdentities();
	writeSessionIdentities();
}

bool ServerIdentityMgr::checkServerSessionIdentity(const std::string &pIdentity, const std::string& endpoint, std::string& secret_key)
{
	IScopedLock lock(mutex);

	for(size_t i=0;i<session_identities.size();++i)
	{
		int64 time = session_identities[i].onlinetime;
		if(time<0) time*=-1;

		if(session_identities[i].ident==pIdentity
			&& session_identities[i].endpoint==endpoint
			&& Server->getTimeMS()-time<ident_online_timeout)
		{
			session_identities[i].onlinetime=Server->getTimeMS();
			secret_key = session_identities[i].secret_key;
			return true;
		}
	}

	return false;
}

bool ServerIdentityMgr::checkServerIdentity(const std::string &pIdentity)
{
	IScopedLock lock(mutex);

	return std::find(identities.begin(), identities.end(), SIdentity(pIdentity))!=identities.end();
}

std::string ServerIdentityMgr::getServerIdentity(const std::string &pIdentity)
{
	if(pIdentity.empty())
	{
		return std::string();
	}

	IScopedLock lock(mutex);

	for(size_t i=0;i<session_identities.size();++i)
	{
		if(session_identities[i].ident==pIdentity)
		{
			return session_identities[i].server_ident;
		}
	}

	if(std::find(identities.begin(), identities.end(), SIdentity(pIdentity))!=identities.end())
	{
		return pIdentity;
	}

	return std::string();
}

void ServerIdentityMgr::setServerTokenIdentity(const std::string& server_token, const std::string& server_ident)
{
	if(server_token.empty() || server_ident.empty())
	{
		return;
	}

	IScopedLock lock(mutex);
	server_token_identities[server_token] = server_ident;
}

std::string ServerIdentityMgr::getServerSettingsFn(const std::string& settings_fn, const std::string& server_ident)
{
	std::string base = settings_fn;
	if(base.size()>4 && base.substr(base.size()-4)==".cfg")
	{
		base.erase(base.size()-4);
	}
	return base + "_srv_" + conv_filename(server_ident) + ".cfg";
}

std::string ServerIdentityMgr::getServerTokenIdentity(const std::string& server_token)
{
	IScopedLock lock(mutex);
	std::map<std::string, std::string>::iterator it = server_token_identities.find(server_token);
	if(it!=server_token_identities.end())
	{
		return it->second;
	}
	return std::string();
}

void ServerIdentityMgr::loadServerIdentities(void)
{
	IScopedLock lock(mutex);
	std::vector<SIdentity> old_identities=identities;
	identities.clear();
	std::string data=getFile(server_ident_file);
	int numl=linecount(data);
	for(int i=0;i<=numl;++i)
	{
		std::string l=trim(getline(i, data));
		if(!l.empty() && l[0]=='#')
		{
			continue;
		}
		if(l.size()>5)
		{
			size_t hashpos = l.find("#");
			SPublicKeys pubkeys;
			if(hashpos!=std::string::npos)
			{
				str_map params;
				ParseParamStrHttp(l.substr(hashpos+1), &params);
				
				pubkeys = SPublicKeys(base64_decode_dash(params["pubkey"]),
					base64_decode_dash(params["pubkey_ecdsa409k1"]));

				if (!pubkeys.empty() &&
					!checkFingerprint(pubkeys, params["fingerprint"]))
				{
					Server->Log("Fingerprint of public key (" + params["pubkey_ecdsa409k1"] + ") on line " + convert(i) + " is wrong. "
						"Expected " + calcFingerprint(pubkeys) + " got " + params["fingerprint"], LL_ERROR);
					continue;
				}

				pubkeys.fingerprint = params["fingerprint"];

				l = l.substr(0, hashpos);
			}
			else
			{
				filesrv->addIdentity("#I"+l+"#", false);
			}
			identities.push_back(SIdentity(l, pubkeys));
			filesrv->setIdentityServer("#I"+l+"#", l);
			std::vector<SIdentity>::iterator it=std::find(old_identities.begin(), old_identities.end(), SIdentity(l));
			if(it!=old_identities.end())
			{
				identities[identities.size()-1].onlinetime = it->onlinetime;
			}
		}
	}
	new_identities.clear();
	data=getFile(server_new_ident_file);
	numl=linecount(data);
	for(int i=0;i<=numl;++i)
	{
		std::string l=trim(getline(i, data));
		if(l.size()>5)
		{
			new_identities.push_back(l);
		}
	}

	std::vector<SSessionIdentity> old_session_identities=session_identities;
	session_identities.clear();
	data=getFile(server_session_ident_file);
	numl=linecount(data);
	for(int i=0;i<=numl;++i)
	{
		std::string l=trim(getline(i, data));
		if(l.size()>5)
		{
			size_t hashpos = l.find("#");
			if(hashpos==std::string::npos)
				continue;

			str_map params;
			ParseParamStrHttp(l.substr(hashpos+1), &params);

			l = l.substr(0, hashpos);

			if(params["server_ident"].empty())
			{
				//Session from a client version that did not record the server. Let the server
				//authenticate again, so its settings can be kept separately from other servers
				continue;
			}

			std::string secret_key = base64_decode_dash(params["secret_key"]);
			SSessionIdentity session_ident(l, params["endpoint"], -1*Server->getTimeMS(), secret_key, params["server_ident"]);
			session_identities.push_back(session_ident);

			filesrv->addIdentity("#I" + l + "#", !secret_key.empty());
			filesrv->setIdentityServer("#I" + l + "#", params["server_ident"]);

			std::vector<SSessionIdentity>::iterator it=std::find(old_session_identities.begin(), old_session_identities.end(), SSessionIdentity(session_ident));
			if(it!=old_session_identities.end())
			{
				session_identities[session_identities.size()-1].onlinetime = it->onlinetime;
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
		idents+=identities[i].ident;
		if (!identities[i].publickeys.dsa_key.empty()
			|| !identities[i].publickeys.ecdsa409k1_key.empty())
		{
			if (identities[i].publickeys.fingerprint.empty())
			{
				identities[i].publickeys.fingerprint = calcFingerprint(identities[i].publickeys);
			}
			idents += "#fingerprint=" + identities[i].publickeys.fingerprint;
		}
		if(!identities[i].publickeys.dsa_key.empty())
		{
			idents+="&pubkey="+base64_encode_dash(identities[i].publickeys.dsa_key);
		}
		if(!identities[i].publickeys.ecdsa409k1_key.empty())
		{
			idents+="&pubkey_ecdsa409k1="+base64_encode_dash(identities[i].publickeys.ecdsa409k1_key);
		}
	}
	write_file_admin_atomic(idents, server_ident_file);

	std::string new_idents;
	for(size_t i=0;i<new_identities.size();++i)
	{
		if(!new_idents.empty()) new_idents+="\r\n";
		new_idents+=new_identities[i];
	}
	write_file_admin_atomic(new_idents, server_new_ident_file);
}

bool ServerIdentityMgr::hasOnlineServer(void)
{
	IScopedLock lock(mutex);
	int64 ctime=Server->getTimeMS();
	for(size_t i=0;i<identities.size();++i)
	{
		if(identities[i].onlinetime!=0 && ctime-identities[i].onlinetime<ident_online_timeout)
		{
			return true;
		}
	}
	for(size_t i=0;i<session_identities.size();++i)
	{
		if(session_identities[i].onlinetime!=0 && ctime-session_identities[i].onlinetime<ident_online_timeout)
		{
			return true;
		}
	}
	return false;
}

SPublicKeys ServerIdentityMgr::getPublicKeys( const std::string &pIdentity )
{
	IScopedLock lock(mutex);

	std::vector<SIdentity>::iterator it = std::find(identities.begin(), identities.end(), SIdentity(pIdentity));

	if(it!=identities.end())
	{
		return it->publickeys;
	}

	return SPublicKeys(std::string(), std::string());
}

bool ServerIdentityMgr::setPublicKeys( const std::string &pIdentity, const SPublicKeys &pPublicKeys )
{
	IScopedLock lock(mutex);

	std::vector<SIdentity>::iterator it = std::find(identities.begin(), identities.end(), SIdentity(pIdentity));

	if(it!=identities.end())
	{
		std::string new_fingerprint;
		if (!it->publickeys.fingerprint.empty())
		{
			new_fingerprint = calcFingerprint(pPublicKeys);

			if (new_fingerprint != it->publickeys.fingerprint)
			{
				Server->Log("Error while setting public keys. Expected public keys with fingerprint " + it->publickeys.fingerprint + " but got " + new_fingerprint, LL_ERROR);
				return false;
			}
		}

		it->publickeys = pPublicKeys;
		it->publickeys.fingerprint = new_fingerprint;

		if(!pPublicKeys.empty())
		{
			filesrv->removeIdentity(pIdentity);
		}
		writeServerIdentities();
		return true;
	}

	return false;
}

bool ServerIdentityMgr::hasPublicKey( const std::string &pIdentity, bool count_fingerprint )
{
	IScopedLock lock(mutex);
	std::vector<SIdentity>::iterator it = std::find(identities.begin(), identities.end(), SIdentity(pIdentity));

	if(it!=identities.end())
	{
		return !it->publickeys.empty() || (count_fingerprint && !it->publickeys.fingerprint.empty());
	}
	return false;
}

void ServerIdentityMgr::addSessionIdentity( const std::string &pIdentity, const std::string& endpoint, std::string secret_key,
	const std::string& server_ident)
{
	IScopedLock lock(mutex);
	SSessionIdentity session_ident(pIdentity, endpoint, Server->getTimeMS(), secret_key, server_ident);
	session_identities.push_back(session_ident);
	filesrv->addIdentity("#I" + pIdentity + "#", !secret_key.empty());
	filesrv->setIdentityServer("#I" + pIdentity + "#", server_ident);
	writeSessionIdentities();
}

void ServerIdentityMgr::writeSessionIdentities()
{
	IScopedLock lock(mutex);

	const size_t max_session_identities=1000;

	size_t start=0;
	if(session_identities.size()>max_session_identities)
	{
		start=session_identities.size()-max_session_identities;
	}

	size_t written = 0;
	std::string idents;
	for(size_t i=session_identities.size(); i-- >0;)
	{
		int64 time = session_identities[i].onlinetime;
		if(time<0) time*=-1;

		if(Server->getTimeMS()-time<ident_online_timeout)
		{
			if(!idents.empty()) idents+="\r\n";
			idents+=session_identities[i].ident;
			idents+="#endpoint="+session_identities[i].endpoint;
			idents += "&secret_key=" + base64_encode_dash(session_identities[i].secret_key);
			if(!session_identities[i].server_ident.empty())
			{
				idents += "&server_ident=" + session_identities[i].server_ident;
			}

			++written;
			if(written>=max_session_identities)
			{
				break;
			}
		}
		
	}
	write_file_admin_atomic(idents, server_session_ident_file);
}

bool ServerIdentityMgr::write_file_admin_atomic(const std::string & data, const std::string & fn)
{
	if (!write_file_only_admin(data, fn + ".new"))
	{
		return false;
	}

	{
		std::auto_ptr<IFile> f(Server->openFile(fn + ".new", MODE_RW));
		if (f.get() != NULL)
			f->Sync();
	}

	return os_rename_file(fn + ".new", fn);
}

bool ServerIdentityMgr::checkFingerprint(const SPublicKeys& pPublicKeys, const std::string& fingerprint)
{
	if (fingerprint.empty())
		return true;

	return calcFingerprint(pPublicKeys)==fingerprint;
}

std::string ServerIdentityMgr::calcFingerprint(const SPublicKeys& pPublicKeys)
{
	std::string pubkey;

	if (!pPublicKeys.ecdsa409k1_key.empty())
	{
		pubkey = pPublicKeys.ecdsa409k1_key;
	}
	else
	{
		pubkey = pPublicKeys.dsa_key;
	}

	std::string fingerprint = bytesToHex(crypto_fak->sha256Binary(pubkey));
	strupper(&fingerprint);

	std::string fingerprint_disp;
	for (size_t i = 0; i < fingerprint.size(); i += 2)
	{
		fingerprint_disp += fingerprint.substr(i, 2);

		if (i + 2 < fingerprint.size())
			fingerprint_disp += ":";
	}

	return fingerprint_disp;
}
