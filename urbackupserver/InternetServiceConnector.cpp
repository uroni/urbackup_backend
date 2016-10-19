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

#include "InternetServiceConnector.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Database.h"
#include "../common/data.h"
#include "../urbackupcommon/InternetServiceIDs.h"
#include "../urbackupcommon/InternetServicePipe.h"
#include "../urbackupcommon/CompressedPipe2.h"
#include "../urbackupcommon/CompressedPipe.h"
#include "server_settings.h"
#include "database.h"
#include "../stringtools.h"
#include "../cryptoplugin/ICryptoFactory.h"

#include <memory.h>
#include <algorithm>
#include <assert.h>
#include "../urbackupcommon/InternetServicePipe2.h"

const unsigned int ping_interval=50*1000;
const unsigned int ping_timeout=30000;
const unsigned int offline_timeout=ping_interval+10000;
const unsigned int establish_timeout=60000;
const int64 max_ecdh_key_age = 6 * 60 * 60 * 1000; //6h

std::map<std::string, SClientData> InternetServiceConnector::client_data;
IMutex *InternetServiceConnector::mutex=NULL;
IMutex *InternetServiceConnector::onetime_token_mutex=NULL;
std::map<unsigned int, SOnetimeToken> InternetServiceConnector::onetime_tokens;
unsigned int InternetServiceConnector::onetime_token_id=0;
int64 InternetServiceConnector::last_token_remove=0;
std::vector<std::pair<IECDHKeyExchange*, int64> > InternetServiceConnector::ecdh_key_exchange_buffer;


extern ICryptoFactory *crypto_fak;
const size_t pbkdf2_iterations=20000;

ICustomClient* InternetService::createClient()
{
	return new InternetServiceConnector;
}

void InternetService::destroyClient( ICustomClient * pClient)
{
	delete ((InternetServiceConnector*)pClient);
}

InternetServiceConnector::InternetServiceConnector(void)
{
	local_mutex=Server->createMutex();
	ecdh_key_exchange=NULL;
}

InternetServiceConnector::~InternetServiceConnector(void)
{
	IScopedLock lock(mutex);
	if(!connect_start)
	{
		cleanup_pipes(true);
	}
	Server->destroy(local_mutex);

	if(ecdh_key_exchange!=NULL
		&& Server->getTimeMS()-ecdh_key_exchange_age<max_ecdh_key_age)
	{
		ecdh_key_exchange_buffer.push_back(std::make_pair(ecdh_key_exchange, ecdh_key_exchange_age));
	}
	else
	{
		Server->destroy(ecdh_key_exchange);
	}
}

void InternetServiceConnector::Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName)
{
	tid=pTID;
	cs=pPipe;
	comm_pipe=cs;
	is_pipe=NULL;
	conn_version=2;
	comp_pipe=NULL;
	connect_start=false;
	do_connect=false;
	stop_connecting=false;
	is_connected=false;
	pinging=false;
	free_connection=false;
	state=ISS_AUTH;
	has_timeout=false;
	endpoint_name=pEndpointName;
	connection_done_cond=NULL;
	tcpstack.reset();
	tcpstack.setAddChecksum(true);
	tcpstack.setMaxPacketSize(32768);
	challenge=ServerSettings::generateRandomBinaryKey();
	{
		CWData data;
		data.addChar(ID_ISC_CHALLENGE);
		data.addString(challenge);
		unsigned int capa=0;
		ServerSettings server_settings(Server->getDatabase(pTID, URBACKUPDB_SERVER));
		SSettings *settings=server_settings.getSettings();
		if(settings->internet_encrypt)
			capa|=IPC_ENCRYPTED;
		if(settings->internet_compress)
			capa|=IPC_COMPRESSED;

		compression_level=settings->internet_compression_level;
		data.addUInt(capa);
		data.addInt(compression_level);
		data.addUInt((unsigned int)pbkdf2_iterations);

		if(ecdh_key_exchange==NULL)
		{
			IScopedLock lock(mutex);
			if(!ecdh_key_exchange_buffer.empty())
			{
				ecdh_key_exchange = ecdh_key_exchange_buffer[ecdh_key_exchange_buffer.size()-1].first;
				ecdh_key_exchange_age = ecdh_key_exchange_buffer[ecdh_key_exchange_buffer.size() - 1].second;
				ecdh_key_exchange_buffer.pop_back();
			}
		}
		if(ecdh_key_exchange==NULL)
		{
			ecdh_key_exchange = crypto_fak->createECDHKeyExchange();
			ecdh_key_exchange_age = Server->getTimeMS();
		}

		data.addString(ecdh_key_exchange->getPublicKey());

		tcpstack.Send(cs, data);
	}
	lastpingtime=Server->getTimeMS();
}

void InternetServiceConnector::cleanup_pipes(bool remove_connection)
{
	delete is_pipe;
	is_pipe=NULL;
	delete comp_pipe;
	comp_pipe=NULL;

	if(remove_connection)
	{
		std::vector<InternetServiceConnector*>& spare_connections = client_data[clientname].spare_connections;
		std::vector<InternetServiceConnector*>::iterator it=std::find(spare_connections.begin(), spare_connections.end(), this);
		if(it!=spare_connections.end())
		{
			spare_connections.erase(it);
		}
	}
}

bool InternetServiceConnector::Run(IRunOtherCallback* run_other)
{
	if(stop_connecting)
	{
		IScopedLock lock(local_mutex);
		cleanup_pipes(false);
		return false;
	}

	if(state==ISS_CONNECTING)
	{
		return true;
	}

	if(state==ISS_USED)
	{
		if(free_connection)
		{
			return false;
		}
		return true;
	}
	
	if( has_timeout )
	{
		return false;
	}

	if(do_connect && !pinging && state==ISS_AUTHED )
	{
		CWData data;
		{
			IScopedLock lock(local_mutex);
			data.addChar(ID_ISC_CONNECT);
			data.addChar(target_service);
			state=ISS_CONNECTING;
		}
		tcpstack.Send(comm_pipe, data);
		Server->Log("Connecting to target service...", LL_DEBUG);		
		starttime=Server->getTimeMS();
	}

	if(state==ISS_AUTHED && Server->getTimeMS()-lastpingtime>ping_interval && !pinging)
	{
		lastpingtime=Server->getTimeMS();
		pinging=true;
		CWData data;
		data.addChar(ID_ISC_PING);
		tcpstack.Send(comm_pipe, data);
	}
	else if(state==ISS_AUTHED && pinging )
	{
		if(Server->getTimeMS()-lastpingtime>ping_timeout)
		{
			Server->Log("Ping timeout in InternetServiceConnector::Run", LL_DEBUG);
			IScopedLock lock(mutex);
			if(!connect_start)
			{
				has_timeout=true;
				cleanup_pipes(true);
				return false;
			}
		}
	}
	return true;
}

void InternetServiceConnector::ReceivePackets(IRunOtherCallback* run_other)
{
	if(state==ISS_USED || has_timeout)
	{
		return;
	}

	std::string ret;
	size_t rc;
	rc=comm_pipe->Read(&ret);

	if(rc==0)
	{
		if( state!=ISS_CONNECTING && state!=ISS_USED )
		{
			IScopedLock lock(mutex);
			if(!connect_start)
			{
				has_timeout=true;
				cleanup_pipes(true);
			}
		}
		return;
	}

	tcpstack.AddData((char*)ret.c_str(), ret.size());

	char *buf;
	size_t packetsize;
	while((buf=tcpstack.getPacket(&packetsize))!=NULL)
	{
		CRData rd(buf, packetsize);
		char id;
		if(rd.getChar(&id))
		{
			switch(state)
			{
			case ISS_AUTH:
				{
					if(id==ID_ISC_AUTH || id==ID_ISC_AUTH_TOKEN
						|| id==ID_ISC_AUTH2 || id==ID_ISC_AUTH_TOKEN2)
					{
						unsigned int iterations=static_cast<unsigned int>(pbkdf2_iterations);
						if(id==ID_ISC_AUTH_TOKEN || id==ID_ISC_AUTH_TOKEN2)
						{
							iterations=1;
							token_auth=true;
						}
						else
						{
							token_auth=false;
						}

						std::string hmac;
						std::string errmsg;
						std::string client_challenge;
						std::string hmac_key;

						if(rd.getStr(&clientname) && rd.getStr(&hmac) )
						{							
							std::string token;
							bool db_timeout=false;
							if(id==ID_ISC_AUTH_TOKEN || id==ID_ISC_AUTH_TOKEN2)
							{
								unsigned int token_id;
								if(rd.getUInt(&token_id) )
								{
									std::string cname;
									authkey=getOnetimeToken(token_id, &cname);
									if(authkey.empty())
									{
										errmsg="Token not found";
									}
									else if(cname!=clientname)
									{
										errmsg="Wrong token";
									}
								}
								else
								{
									errmsg="Missing field -1";
								}
							}
							else
							{
								authkey=getAuthkeyFromDB(clientname, db_timeout);
							}

							std::string ecdh_pubkey;
							if(id==ID_ISC_AUTH2)
							{
								if(!rd.getStr(&ecdh_pubkey) || ecdh_pubkey.empty())
								{
									errmsg="Missing field -2";
								}
							}
							
							if(!rd.getStr(&client_challenge) || client_challenge.size()<32 )
							{
								errmsg="Client challenge missing or not long enough";
							}

							unsigned int client_iterations = iterations;
							if(id!=ID_ISC_AUTH_TOKEN && id!=ID_ISC_AUTH_TOKEN2)
							{
								rd.getUInt(&client_iterations);
							}

							if(errmsg.empty() && !authkey.empty())
							{
								std::string salt = challenge+client_challenge;

								if(id==ID_ISC_AUTH2)
								{
									salt+=ecdh_key_exchange->getSharedKey(ecdh_pubkey);
								}

								hmac_key=crypto_fak->generateBinaryPasswordHash(authkey, salt, (std::max)(iterations, client_iterations));

								std::string hmac_loc=crypto_fak->generateBinaryPasswordHash(hmac_key, challenge, 1);								
								if(hmac_loc==hmac)
								{
									if(id==ID_ISC_AUTH2 || id==ID_ISC_AUTH_TOKEN2)
									{
										if(id==ID_ISC_AUTH2)
										{
											delete ecdh_key_exchange;
											ecdh_key_exchange=NULL;
										}
										
										is_pipe=new InternetServicePipe2(comm_pipe, hmac_key);
										conn_version=2;
									}
									else
									{
										is_pipe=new InternetServicePipe(comm_pipe, hmac_key);
										conn_version=1;
									}		
									state=ISS_CAPA;	
									comm_pipe=is_pipe;
								}
								else
								{
									errmsg="Auth failed (Authkey/password wrong)";
								}
							}
							else if(errmsg.empty())
							{
								if(db_timeout)
								{
									errmsg="Database timeout while looking for client";
								}
								else
								{
									if(!hasClient(clientname, db_timeout))
									{
										if(db_timeout)
										{
											errmsg="Database timeout while looking for client";
										}
										else
										{
											if(checkhtml(clientname))
											{
												errmsg="Unknown client ("+clientname+")";
											}
											else
											{
												Server->Log("HTML injection detected", LL_WARNING);
												errmsg="Unknown client";
											}
										}
									}
								}
							}
						}
						else
						{
							errmsg="Missing fields";
						}

						CWData data;
						if(!errmsg.empty())
						{
							Server->Log("Authentication failed in InternetServiceConnector::ReceivePackets: "+errmsg, LL_INFO);
							data.addChar(ID_ISC_AUTH_FAILED);
							data.addString(errmsg);
						}
						else if(state==ISS_CAPA)
						{						
							data.addChar(ID_ISC_AUTH_OK);

							std::string hmac_loc;
							hmac_loc=crypto_fak->generateBinaryPasswordHash(hmac_key, client_challenge, 1);
							data.addString(hmac_loc);							
							
							std::string new_token=generateOnetimeToken(clientname);
							data.addString(is_pipe->encrypt(new_token));
						}
						tcpstack.Send(cs, data);
					}
				}break;
			case ISS_CAPA:
				{
					if(id==ID_ISC_CAPA)
					{
						unsigned int capa;
						if( rd.getUInt(&capa) )
						{
							comm_pipe=cs;
							std::string capa_debug_str;
							if(capa & IPC_ENCRYPTED )
							{
								is_pipe->setBackendPipe(comm_pipe);
								comm_pipe=is_pipe;
								capa_debug_str += std::string("encrypted-") + (conn_version==2 ? "v2" : "v1");
							}	
							if(capa & IPC_COMPRESSED )
							{
								if(conn_version==1)
								{
									comp_pipe=new CompressedPipe(comm_pipe, compression_level);
								}
								else if(conn_version==2)
								{
									comp_pipe=new CompressedPipe2(comm_pipe, compression_level);
								}
								else
								{
									Server->Log("Unknown connection version " + convert((int)conn_version) + " in state ISS_CAPA", LL_ERROR);
									assert(false);
								}
								
								comm_pipe=comp_pipe;

								if (!capa_debug_str.empty()) capa_debug_str += ", ";
								capa_debug_str += std::string("compressed-") + (conn_version == 2 ? "v2" : "v1");
							}


							size_t spare_connections_num;

							{
								IScopedLock lock(mutex);
								SClientData& curr_client_data = client_data[clientname];
								curr_client_data.spare_connections.push_back(this);
								curr_client_data.last_seen=Server->getTimeMS();
								curr_client_data.endpoint_name = endpoint_name;

								spare_connections_num = curr_client_data.spare_connections.size();
							}

							if (token_auth)
							{
								if (!capa_debug_str.empty()) capa_debug_str += ", ";
								capa_debug_str += "token auth";
							}

							Server->Log("Authed+capa for client '"+clientname+"' "
								+"("+ capa_debug_str+")"
								+" - "+convert(spare_connections_num)+" spare connections", LL_DEBUG);

							state=ISS_AUTHED;
						}
					}
				}break;
			case ISS_AUTHED:
				{
					if(id==ID_ISC_PONG)
					{
						pinging=false;
						IScopedLock lock(mutex);
						client_data[clientname].last_seen=Server->getTimeMS();
					} 
				}break;
			case ISS_CONNECTING:
				{
					if(id==ID_ISC_CONNECT_OK)
					{
						state=ISS_USED;
						
						{
							IScopedLock lock(mutex);
							client_data[clientname].last_seen=Server->getTimeMS();
						}
						
						IScopedLock lock(local_mutex);
						is_connected=true;
						if(connection_done_cond!=NULL)
						{
							connection_done_cond->notify_all();
						}
					}
				}break;
			}
		}
		delete []buf;
	}
}

void InternetServiceConnector::init_mutex(void)
{
	mutex=Server->createMutex();
	onetime_token_mutex=Server->createMutex();
}

void InternetServiceConnector::destroy_mutex(void)
{
	Server->destroy(mutex);
	mutex=NULL;
	Server->destroy(onetime_token_mutex);
}

IPipe *InternetServiceConnector::getConnection(const std::string &clientname, char service, int timeoutms)
{

	int64 starttime=Server->getTimeMS();
	do
	{
		IScopedLock lock(mutex);
		std::map<std::string, SClientData>::iterator iter=client_data.find(clientname);
		if(iter==client_data.end())
			return NULL;

		if(iter->second.spare_connections.empty())
		{
			lock.relock(NULL);
			Server->wait(100);
		}
		else
		{
			InternetServiceConnector *isc=iter->second.spare_connections.back();

			if(!isc->connectStart())
			{
				Server->Log("Connecting on internet connection failed (1). Service="+convert((int)service), LL_DEBUG);
				continue;
			}

			iter->second.spare_connections.pop_back();

			lock.relock(NULL);

			int64 rtime=Server->getTimeMS()-starttime;
			if((int)rtime<timeoutms)
				rtime=timeoutms-rtime;
			else
				rtime=0;

			if(rtime<100) rtime=100;			
							
			if(!isc->Connect(service, static_cast<int>(rtime)))
			{
				//Automatically freed
				Server->Log("Connecting on internet connection failed. Service="+convert((int)service), LL_DEBUG);
			}
			else
			{
				IPipe *ret=isc->getISPipe();
				isc->freeConnection(); //deletes ics

				CompressedPipe *comp_pipe=dynamic_cast<CompressedPipe*>(ret);
				CompressedPipe2 *comp_pipe2=dynamic_cast<CompressedPipe2*>(ret);
				if(comp_pipe2!=NULL)
				{
					InternetServicePipe2 *isc_pipe2=dynamic_cast<InternetServicePipe2*>(comp_pipe2->getRealPipe());
					if(isc_pipe2!=NULL)
					{
						isc_pipe2->destroyBackendPipeOnDelete(true);
					}
					comp_pipe2->destroyBackendPipeOnDelete(true);
				}
				else if(comp_pipe!=NULL)
				{
					InternetServicePipe *isc_pipe=dynamic_cast<InternetServicePipe*>(comp_pipe->getRealPipe());
					if(isc_pipe!=NULL)
					{
						isc_pipe->destroyBackendPipeOnDelete(true);
					}
					comp_pipe->destroyBackendPipeOnDelete(true);
				}
				else
				{
					InternetServicePipe *isc_pipe=dynamic_cast<InternetServicePipe*>(ret);
					if(isc_pipe!=NULL)
					{
						isc_pipe->destroyBackendPipeOnDelete(true);
					}
					InternetServicePipe2 *isc_pipe2=dynamic_cast<InternetServicePipe2*>(ret);
					if(isc_pipe2!=NULL)
					{
						isc_pipe2->destroyBackendPipeOnDelete(true);
					}
				}
				Server->Log("Established internet connection. Service="+convert((int)service), LL_DEBUG);
					
				return ret;
			}
		}
	}while(timeoutms==-1 || Server->getTimeMS()-starttime<(unsigned int)timeoutms);

	Server->Log("Establishing internet connection failed. Service="+convert((int)service), LL_DEBUG);
	return NULL;
}

bool InternetServiceConnector::wantReceive(void)
{
	if(has_timeout)
		return false;

	if(state!=ISS_USED )
		return true;
	else
		return false;
}

bool InternetServiceConnector::closeSocket(void)
{
	if(free_connection)
		return false;
	else
		return true;
}

bool InternetServiceConnector::connectStart()
{
	if(has_timeout)
	{
		return false;
	}

	connect_start=true;
	return true;
}

bool InternetServiceConnector::Connect(char service, int timems)
{
	IScopedLock lock(local_mutex);

	connection_done_cond=Server->createCondition();
	ObjectScope connection_done_cond_scope(connection_done_cond);

	target_service=service;
	do_connect=true;

	connection_done_cond->wait(&lock, timems);

	if(!is_connected)
	{
		connection_done_cond=NULL;
		stop_connecting=true;
		return false;
	}
	else
	{
		return true;
	}
}

IPipe *InternetServiceConnector::getISPipe(void)
{
	IScopedLock lock(local_mutex);
	return comm_pipe;
}

void InternetServiceConnector::stopConnecting(void)
{
	stop_connecting=true;
}

void InternetServiceConnector::freeConnection(void)
{
	free_connection=true;
}

std::vector<std::pair<std::string, std::string> > InternetServiceConnector::getOnlineClients(void)
{
	std::vector<std::pair<std::string, std::string> > ret;

	IScopedLock lock(mutex);
	int64 ct=Server->getTimeMS();

	if(ct-last_token_remove>30*60*1000)
	{
		removeOldTokens();
		last_token_remove=ct;
	}

	std::vector<std::string> todel;
	for(std::map<std::string, SClientData>::iterator it=client_data.begin();it!=client_data.end();++it)
	{
		if(!it->second.spare_connections.empty())
		{
			if(ct-it->second.last_seen<offline_timeout)
			{
				ret.push_back(std::make_pair(it->first, it->second.endpoint_name));
			}
		}
		else
		{
			if(ct-it->second.last_seen>=establish_timeout)
			{
				todel.push_back(it->first);
			}
		}
	}

	for(size_t i=0;i<todel.size();++i)
	{
		std::map<std::string, SClientData>::iterator it=client_data.find(todel[i]);
		Server->Log("Establish timeout: Deleting internet client \""+it->first+"\"", LL_DEBUG);
		while(!it->second.spare_connections.empty())
		{
			InternetServiceConnector *isc=it->second.spare_connections.back();
			it->second.spare_connections.pop_back();
			isc->connectStart();
			isc->stopConnecting();
		}
		client_data.erase(it);
	}

	return ret;
}


std::string InternetServiceConnector::generateOnetimeToken(const std::string &clientname)
{
	SOnetimeToken token(clientname);
	unsigned int token_id;
	{
		IScopedLock lock(onetime_token_mutex);
		token_id=onetime_token_id++;
		onetime_tokens.insert(std::pair<unsigned int, SOnetimeToken>(token_id, token));
	}
	std::string ret;
	ret.resize(sizeof(unsigned int)+token.token.size());
	token_id=little_endian(token_id);
	memcpy((char*)ret.data(), &token_id, sizeof(unsigned int));
	memcpy((char*)ret.data()+sizeof(unsigned int), token.token.data(), token.token.size());
	return ret;
}

std::string InternetServiceConnector::getOnetimeToken(unsigned int id, std::string *cname)
{
	IScopedLock lock(onetime_token_mutex);
	std::map<unsigned int, SOnetimeToken>::iterator iter=onetime_tokens.find(id);
	if(iter!=onetime_tokens.end())
	{
		*cname=iter->second.clientname;
		std::string token=iter->second.token;
		onetime_tokens.erase(iter);
		return token;
	}
	return std::string();
}

std::string InternetServiceConnector::getAuthkeyFromDB(const std::string &clientname, bool &db_timeout)
{
	IDatabase *db=Server->getDatabase(tid, URBACKUPDB_SERVER);
	IQuery *q=db->Prepare("SELECT value FROM settings_db.settings WHERE key='internet_authkey' AND clientid=(SELECT id FROM clients WHERE name=?)", false);
	q->Bind(clientname);
	int timeoutms=1000;
	db_results res=q->Read(&timeoutms);
	db->destroyQuery(q);
	if(!res.empty())
	{					
		db_timeout=false;
		return (res[0]["value"]);
	}
	else if(timeoutms==1)
	{
		db_timeout=true;
	}
	
	return std::string();
}

bool InternetServiceConnector::hasClient(const std::string &clientname, bool &db_timeout)
{
	IDatabase *db=Server->getDatabase(tid, URBACKUPDB_SERVER);
	IQuery *q=db->Prepare("SELECT id FROM clients WHERE name=?", false);
	q->Bind(clientname);
	int timeoutms=1000;
	db_results res=q->Read(&timeoutms);
	db->destroyQuery(q);
	if(timeoutms==1)
	{
		db_timeout=true;
		return false;
	}

	return !res.empty();
}

void InternetServiceConnector::removeOldTokens(void)
{
	IScopedLock lock(onetime_token_mutex);
	int64 tt=Server->getTimeMS();
	std::vector<unsigned int> todel;
	for(std::map<unsigned int, SOnetimeToken>::iterator it=onetime_tokens.begin();it!=onetime_tokens.end();++it)
	{
		if(tt-it->second.created>1*60*60*1000)
		{
			todel.push_back(it->first);
		}
	}

	for(size_t i=0;i<todel.size();++i)
	{
		std::map<unsigned int, SOnetimeToken>::iterator iter=onetime_tokens.find(todel[i]);
		if(iter!=onetime_tokens.end())
		{
			onetime_tokens.erase(iter);
		}
	}
}