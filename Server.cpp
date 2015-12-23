/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "vld.h"
#ifdef _WIN32
#define _CRT_RAND_S
#include <ws2tcpip.h>
#endif

#include <iostream>
#include <fstream>
#include <algorithm>
#include <memory.h>
#include <assert.h>
#ifndef _WIN32
#include <errno.h>
#endif
#include "libfastcgi/fastcgi.hpp"

#include "Interface/PluginMgr.h"
#include "Interface/Thread.h"
#include "Interface/DatabaseFactory.h"

#include "Server.h"
#include "Template.h"
#include "stringtools.h"
#include "defaults.h"
#include "SessionMgr.h"
#include "md5.h"
#include "ServiceAcceptor.h"
#include "LookupService.h"
#include "FileSettingsReader.h"
#include "DBSettingsReader.h"
#include "StreamPipe.h"
#include "ThreadPool.h"
#include "file.h"
#include "utf8/utf8.h"
#include "MemoryPipe.h"
#include "MemorySettingsReader.h"
#include "Database.h"
#include "SQLiteFactory.h"
#include "PipeThrottler.h"
#include "mt19937ar.h"
#include "Query.h"



#ifdef _WIN32
#include <condition_variable>
#include "Mutex_std.h"
#include "Condition_std.h"
#include "SharedMutex_std.h"
#else
#include <pthread.h>
#include "Mutex_lin.h"
#include "Condition_lin.h"
#include "SharedMutex_lin.h"
#endif

#ifdef _WIN32
#	include <windows.h>
#	include "Helper_win32.h"
#	include <time.h>
#else
#	include <ctime>
#	include <sys/time.h>
#	include <unistd.h>
#	include <sys/types.h>
#	include <pwd.h>
#endif
#include "StaticPluginRegistration.h"

const size_t SEND_BLOCKSIZE=8192;
const size_t MAX_THREAD_ID=std::string::npos;

namespace
{
	//GetTickCount64 for Windows Server 2003
#ifdef _WIN32
	typedef ULONGLONG(WINAPI GetTickCount64_t)(VOID);

	GetTickCount64_t* GetTickCount64_fun = NULL;

	void initialize_GetTickCount64()
	{
		HMODULE hKernel32 = GetModuleHandleW(L"Kernel32.dll");
		if(hKernel32)
		{
			GetTickCount64_fun = reinterpret_cast<GetTickCount64_t*> (GetProcAddress(hKernel32, "GetTickCount64"));
		}
	}
#endif
		
}

extern bool run;

CServer::CServer()
{	
	curr_thread_id=0;
	curr_pluginid=0;
	curr_postfilekey=0;
	loglevel=LL_INFO;
	logfile_a=false;
	circular_log_buffer_id=0;
	circular_log_buffer_idx=0;
	has_circular_log_buffer=false;
	failbits=0;

	startup_complete=false;
	
	log_mutex=createMutex();
	action_mutex=createMutex();
	requests_mutex=createMutex();
	outputs_mutex=createMutex();
	db_mutex=createMutex();
	thread_mutex=createMutex();
	plugin_mutex=createMutex();
	rps_mutex=createMutex();
	postfiles_mutex=createMutex();
	param_mutex=createMutex();
	startup_complete_mutex=createMutex();
	startup_complete_cond=createCondition();
	rnd_mutex=createMutex();

	initRandom(static_cast<unsigned int>(time(0)));
	initRandom(getSecureRandomNumber());

	log_rotation_size = std::string::npos;
	log_rotation_files = 10;

	if(FileExists("large_log_rotation"))
	{
		log_rotation_files = 100;
	}

	log_console_time = true;

#ifdef _WIN32
	initialize_GetTickCount64();
	log_rotation_size = 20*1024*1024; //20MB
#endif
}

void CServer::setup(void)
{
	CFileSettingsReader::setup();
	
	sessmgr=new CSessionMgr();
	threadpool=new CThreadPool();

#ifndef NO_SQLITE
	CDatabase::initMutex();
	registerDatabaseFactory("sqlite", new SQLiteFactory );
#endif

	CQuery::init_mutex();

#ifdef MODE_WIN
	File::init_mutex();
#endif
}

void CServer::destroyAllDatabases(void)
{
	IScopedLock lock(db_mutex);

	for(std::map<DATABASE_ID, SDatabase >::iterator i=databases.begin();
		i!=databases.end();++i)
	{
		for( std::map<THREAD_ID, IDatabaseInt*>::iterator j=i->second.tmap.begin();j!=i->second.tmap.end();++j)
		{
			delete j->second;
		}
		i->second.tmap.clear();
	}
}

void CServer::destroyDatabases(THREAD_ID tid)
{
	IScopedLock lock(db_mutex);

	for(std::map<DATABASE_ID, SDatabase >::iterator i=databases.begin();
		i!=databases.end();++i)
	{
		std::map<THREAD_ID, IDatabaseInt*>::iterator iter=i->second.tmap.find(tid);
		if(iter!=i->second.tmap.end())
		{
			delete iter->second;
			i->second.tmap.erase(iter);
		}
	}
}

CServer::~CServer()
{
	if(getServerParameter("leak_check")!="true") //minimal cleanup
	{
		return;
	}

	Log("deleting cached settings...");
	CFileSettingsReader::cleanup();
	
	Log("deleting stream services...");
	//Delete extra Services
	for(size_t i=0;i<stream_services.size();++i)
	{
		delete stream_services[i];
	}

	Log("deleting plugins...");
	//delete Plugins
	for(std::map<PLUGIN_ID, std::pair<IPluginMgr*,str_map> >::iterator iter1=perthread_pluginparams.begin();
		iter1!=perthread_pluginparams.end();++iter1)
	{
		std::map<PLUGIN_ID, std::map<THREAD_ID, IPlugin*> >::iterator iter2=perthread_plugins.find( iter1->first );

		if( iter2!=perthread_plugins.end() )
		{
			std::map<THREAD_ID, IPlugin*> *pmap=&iter2->second;

			for( std::map<THREAD_ID, IPlugin*>::iterator iter3=pmap->begin();iter3!=pmap->end();++iter3 )
			{
				iter1->second.first->destroyPluginInstance( iter3->second );
			}
		}
	}

	Log("Deleting threadsafe plugins...");
	for(std::map<PLUGIN_ID, IPlugin*>::iterator iter1=threadsafe_plugins.begin();iter1!=threadsafe_plugins.end();++iter1)
	{
		iter1->second->Remove();
	}

	Log("Deleting pluginmanagers...");

	for(std::map<std::string, IPluginMgr*>::iterator iter=perthread_pluginmgrs.begin();iter!=perthread_pluginmgrs.end();++iter)
	{
		iter->second->Remove();
	}

	for(std::map<std::string, IPluginMgr*>::iterator iter=threadsafe_pluginmgrs.begin();iter!=threadsafe_pluginmgrs.end();++iter)
	{
		iter->second->Remove();
	}

	Log("Deleting actions...");

	for(std::map< std::string, std::map<std::string, IAction*> >::iterator iter1=actions.begin();iter1!=actions.end();++iter1)
	{
		for(std::map<std::string, IAction*>::iterator iter2=iter1->second.begin();iter2!=iter1->second.end();++iter2)
		{
			iter2->second->Remove();
		}
	}
	actions.clear();

	Log("Deleting sessmgr...");
	delete sessmgr;

	Log("Shutting down ThreadPool...");
	threadpool->Shutdown();
	delete threadpool;

	Log("destroying databases...");
	//Destroy Databases
	destroyAllDatabases();

	Log("deleting database factories...");
	for(std::map<std::string, IDatabaseFactory*>::iterator it=database_factories.begin();it!=database_factories.end();++it)
	{
		it->second->Remove();
	}

	Log("unloading dlls...");
	UnloadDLLs();	
	
	Log("Destroying mutexes");
	
	destroy(log_mutex);
	destroy(action_mutex);
	destroy(requests_mutex);
	destroy(outputs_mutex);
	destroy(db_mutex);
	destroy(thread_mutex);
	destroy(plugin_mutex);
	destroy(rps_mutex);
	destroy(postfiles_mutex);
	destroy(param_mutex);
	destroy(startup_complete_mutex);
	destroy(startup_complete_cond);
	destroy(rnd_mutex);
#ifndef NO_SQLITE
	CDatabase::destroyMutex();
#endif

#ifdef MODE_WIN
	File::destroy_mutex();
#endif

	std::cout << "Server cleanup done..." << std::endl;
}

void CServer::setServerParameters(const str_map &pServerParams)
{
	IScopedLock lock(param_mutex);
	server_params=pServerParams;
}

std::string CServer::getServerParameter(const std::string &key)
{
	return getServerParameter(key, "");
}

std::string CServer::getServerParameter(const std::string &key, const std::string &def)
{
	IScopedLock lock(param_mutex);
	str_map::iterator iter=server_params.find(key);
	if( iter!=server_params.end() )
	{
		return iter->second;
	}
	else
		return def;
}

void CServer::setServerParameter(const std::string &key, const std::string &value)
{
	IScopedLock lock(param_mutex);
	server_params[key]=value;
}

void CServer::Log( const std::string &pStr, int LogLevel)
{
	if( loglevel <=LogLevel )
	{
		IScopedLock lock(log_mutex);

		time_t rawtime;		
		char buffer [100];
		time ( &rawtime );
#ifdef _WIN32
		struct tm  timeinfo;
		localtime_s(&timeinfo, &rawtime);
		strftime (buffer,100,"%Y-%m-%d %X: ",&timeinfo);
#else
		struct tm *timeinfo;
		timeinfo = localtime ( &rawtime );
		strftime (buffer,100,"%Y-%m-%d %X: ",timeinfo);
#endif	

		if(log_console_time)
		{
			std::cout << buffer;
		}

		if( LogLevel==LL_ERROR )
		{
			std::cout << "ERROR: " << pStr << std::endl;
			if(logfile_a)
				logfile << buffer << "ERROR: " << pStr << std::endl;
		}
		else if( LogLevel==LL_WARNING )
		{
			std::cout << "WARNING: " << pStr << std::endl;
			if(logfile_a)
				logfile<< buffer << "WARNING: " << pStr << std::endl;
		}
		else
		{
			std::cout << pStr << std::endl;		
			if(logfile_a)
				logfile << buffer << pStr << std::endl;
		}
		
		if(logfile_a)
		{
			logfile.flush();

			rotateLogfile();

		}

		if(has_circular_log_buffer)
		{
			logToCircularBuffer(pStr, LogLevel);
		}
	}
	else if(has_circular_log_buffer)
	{
		IScopedLock lock(log_mutex);

		logToCircularBuffer(pStr, LogLevel);
	}
}

void CServer::rotateLogfile()
{
	if(static_cast<size_t>(logfile.tellp())>log_rotation_size)
	{
		logfile.close();
		logfile_a=false;

		for(size_t i=log_rotation_files-1;i>0;--i)
		{
			rename((logfile_fn+"."+convert(i)).c_str(), (logfile_fn+"."+convert(i+1)).c_str());
		}

		deleteFile(logfile_fn+"."+convert(log_rotation_files));

		rename(logfile_fn.c_str(), (logfile_fn+".1").c_str());

		setLogFile(logfile_fn, logfile_chown_user);
	}
}

void CServer::setLogFile(const std::string &plf, std::string chown_user)
{
	IScopedLock lock(log_mutex);
	if(logfile_a)
	{
		logfile.close();
		logfile_a=false;
	}
	logfile_fn=plf;
	logfile_chown_user=chown_user;
	logfile.open( logfile_fn.c_str(), std::ios::app | std::ios::out | std::ios::binary );
	if(logfile.is_open() )
	{
#ifndef _WIN32
		if(!chown_user.empty())
		{
			char buf[1000];
			passwd pwbuf;
			passwd *pw;
			int rc=getpwnam_r(chown_user.c_str(), &pwbuf, buf, 1000, &pw);
			if(pw!=NULL)
			{
				chown(plf.c_str(), pw->pw_uid, pw->pw_gid);
			}
			else
			{
	    		Server->Log("Unable to change logfile ownership", LL_ERROR);
			}
		}
#endif
		logfile_a=true;
	}
}

void CServer::setLogLevel(int LogLevel)
{
	IScopedLock lock(log_mutex);
	loglevel=LogLevel;
}

THREAD_ID CServer::Execute(const std::string &action, const std::string &context, str_map &GET, str_map &POST, str_map &PARAMS, IOutputStream *req)
{
	IAction *action_ptr=NULL;
	{
		IScopedLock lock(action_mutex);
		std::map<std::string, std::map<std::string, IAction*> >::iterator iter1=actions.find( context );
		if( iter1!=actions.end() )
		{
			std::map<std::string, IAction*>::iterator iter2=iter1->second.find(action);
			if( iter2!=iter1->second.end() )
				action_ptr=iter2->second;
		}
	}

	if( action_ptr!=NULL )
	{
		THREAD_ID tid=getThreadID();
		IOutputStream *current_req=NULL;
		{
			IScopedLock lock(requests_mutex);
			std::map<THREAD_ID, IOutputStream*>::iterator iter=current_requests.find(tid);
			if( iter!=current_requests.end() )
			{
				current_req=iter->second;
				iter->second=req;
			}
			else
			{
				current_requests.insert(std::pair<THREAD_ID, IOutputStream*>(tid, req) );
			}
		}

		{
			IScopedLock lock(outputs_mutex);
			current_outputs[tid]=std::pair<bool, std::string>(false, "");
		}

		action_ptr->Execute( GET, POST, tid, PARAMS);

		clearDatabases(tid);

		bool b = WriteRaw(tid, NULL, 0, false);

		if( current_req!=NULL )
		{
			IScopedLock lock(requests_mutex);
			current_requests[tid]=current_req;
		}

		if(!b)
		{
			throw std::runtime_error("OutputStream error");
		}

		return tid;
	}
	return 0;
}

std::string CServer::Execute(const std::string &action, const std::string &context, str_map &GET, str_map &POST, str_map &PARAMS)
{
	CStringOutputStream cos;
	Execute(action, context, GET, POST, PARAMS, &cos);
	return cos.getData();
}

void CServer::AddAction(IAction *action)
{
	IScopedLock lock(action_mutex);

	std::map<std::string, IAction*> *ptr=&actions[action_context];
	ptr->insert( std::pair<std::string, IAction*>(action->getName(), action ) );
}

bool CServer::RemoveAction(IAction *action)
{
	IScopedLock lock(action_mutex);

	std::map<std::string, std::map<std::string, IAction*> >::iterator iter1=actions.find(action_context);
	
	if( iter1!=actions.end() )
	{	
		std::map<std::string, IAction*>::iterator iter2=iter1->second.find( action->getName() );
		if( iter2!=iter1->second.end() )
		{
			iter1->second.erase( iter2 );
			return true;
		}
	}
	return false;
}

void CServer::setActionContext(std::string context)
{
	action_context=context;
}

void CServer::resetActionContext(void)
{
	action_context.clear();
}

int64 CServer::getTimeSeconds(void)
{
#ifdef _WIN32
	SYSTEMTIME st;
	GetSystemTime(&st);
	return unix_timestamp(&st);
#else
	timeval t;
	gettimeofday(&t,NULL);
	return t.tv_sec;
#endif
}

int64 CServer::getTimeMS(void)
{
#ifdef _WIN32
	if(GetTickCount64_fun)
	{
		return GetTickCount64_fun();
	}
	else
	{
		return GetTickCount();
	}
#else
	//return (unsigned int)(((double)clock()/(double)CLOCKS_PER_SEC)*1000.0);
	/*
	boost::xtime xt;
    	boost::xtime_get(&xt, boost::TIME_UTC);
	static boost::int_fast64_t start_t=xt.sec;
	xt.sec-=start_t;
	unsigned int t=xt.sec*1000+(unsigned int)((double)xt.nsec/1000000.0);
	return t;*/
	/*timeval tp;
	gettimeofday(&tp, NULL);
	static long start_t=tp.tv_sec;
	tp.tv_sec-=start_t;
	return tp.tv_sec*1000+tp.tv_usec/1000;*/
	timespec tp;
	if(clock_gettime(CLOCK_MONOTONIC, &tp)!=0)
	{
		timeval tv;
		gettimeofday(&tv, NULL);
		static long start_t=tv.tv_sec;
		tv.tv_sec-=start_t;
		return tv.tv_sec*1000+tv.tv_usec/1000;
	}
	return static_cast<int64>(tp.tv_sec)*1000+tp.tv_nsec/1000000;
#endif
}

bool CServer::WriteRaw(THREAD_ID tid, const char *buf, size_t bsize, bool cached)
{
	bool ret=true;
	if( cached==false )
	{
		IOutputStream* req=NULL;
		{
			IScopedLock lock(requests_mutex);
			std::map<THREAD_ID, IOutputStream*>::iterator iter=current_requests.find( tid );
			if( iter!=current_requests.end() )
				req=iter->second;
		}
		if( req!=NULL )
		{
			{
				IScopedLock lock(outputs_mutex);
				std::pair<bool, std::string> *co=&current_outputs[tid];
				std::string *curr_output=&co->second;
				bool sent=co->first;
				
				if( sent==false && next(*curr_output,0,"Content-type: ")==false )
				{
					curr_output->insert(0, "Content-type: "+DEFAULT_CONTENTTYPE+"\r\n\r\n");
					co->first=true;
				}
				
				try
				{
					if(curr_output->size()<SEND_BLOCKSIZE)
					{
						req->write(*curr_output);
					}
					else
					{
						for(size_t i=0,size=curr_output->size();i<size;i+=SEND_BLOCKSIZE)
						{
							req->write(&(*curr_output)[i], (std::min)(SEND_BLOCKSIZE, size-i) );
						}
					}
				}
				catch(std::exception&)
				{
					Server->Log("Sending data failed", LL_INFO);
					ret=false;
				}				
				curr_output->clear();
			}

			try
			{
				if( bsize>0 && bsize<SEND_BLOCKSIZE)
				{
					req->write(buf, bsize);
				}
				else if(bsize>0 )
				{
					for(size_t i=0,size=bsize;i<size;i+=SEND_BLOCKSIZE)
					{
						req->write(&buf[i], (std::min)(SEND_BLOCKSIZE, size-i) );
					}
				}
			}
			catch(std::exception&)
			{
				Server->Log("Sending data failed", LL_INFO);
				ret=false;
			}
		}
		else
			Log("Couldn't find THREAD_ID - cached=true", LL_ERROR);
	}
	else
	{
		if( bsize>0 )
		{
			IScopedLock lock(outputs_mutex);
			std::map<THREAD_ID, std::pair<bool, std::string> >::iterator iter=current_outputs.find( tid );
			if( iter!=current_outputs.end() )
			{
				iter->second.second.append(buf, bsize);
			}
			else
			{
				Log("Couldn't find THREAD_ID - cached=false", LL_ERROR);
				ret=false;
			}
		}
	}
	return ret;
}

bool CServer::Write(THREAD_ID tid, const std::string &str, bool cached)
{
	return WriteRaw(tid, str.c_str(), str.size(), cached);
}

bool CServer::UnloadDLLs(void)
{
	UnloadDLLs2();
	return true;
}

void CServer::ShutdownPlugins(void)
{
	for(std::map<std::string, UNLOADACTIONS>::iterator iter=unload_functs.begin();
	      iter!=unload_functs.end();++iter)
	{
	    if(iter->second!=NULL)
	      iter->second();
	}
	unload_functs.clear();
}

bool CServer::UnloadDLL(const std::string &name)
{
	std::map<std::string, UNLOADACTIONS>::iterator iter=unload_functs.find(name);
	if(iter!=unload_functs.end() )
	{
		if( iter->second!=NULL )
		{
			iter->second();
			unload_functs.erase(iter);
		}
		return true;
	}
	return false;
}

void CServer::destroy(IObject *obj)
{
	if(obj!=NULL)
	{
		obj->Remove();
	}
}

ITemplate* CServer::createTemplate(std::string pFile)
{
	return new CTemplate(pFile);
}


THREAD_ID CServer::getThreadID(void)
{
#ifdef THREAD_BOOST
	IScopedLock lock(thread_mutex);
	
	std::thread::id ct = std::this_thread::get_id();
	std::map<std::thread::id, THREAD_ID>::iterator iter=threads.find(ct);
	
	if(iter!=threads.end() )
	{
	    return iter->second;
	}

	++curr_thread_id;
	if( curr_thread_id>=MAX_THREAD_ID )
		curr_thread_id=0;

	threads.insert( std::pair<std::thread::id, THREAD_ID>( ct, curr_thread_id) );

	return curr_thread_id;
#else
#ifndef _WIN32
	IScopedLock lock(thread_mutex);
	
	pthread_t ct=pthread_self();
	std::map<pthread_t, THREAD_ID>::iterator iter=threads.find(ct);
	
	if(iter!=threads.end() )
	{
	    return iter->second;
	}

	++curr_thread_id;
	if( curr_thread_id>=MAX_THREAD_ID )
		curr_thread_id=0;

	threads.insert( std::pair<pthread_t, THREAD_ID>( ct, curr_thread_id) );

	return curr_thread_id;
#endif
#endif //THREAD_BOOST
}

bool CServer::openDatabase(std::string pFile, DATABASE_ID pIdentifier, std::string pEngine)
{
	IScopedLock lock(db_mutex);

	std::map<DATABASE_ID, SDatabase >::iterator iter=databases.find(pIdentifier);
	if( iter!=databases.end() )
	{
		Log("Database already openend", LL_ERROR);
		return false;
	}

	std::map<std::string, IDatabaseFactory*>::iterator iter2=database_factories.find(pEngine);
	if(iter2==database_factories.end())
	{
		Log("Database engine not found", LL_ERROR);
		return false;
	}

	SDatabase ndb(iter2->second, pFile);
	databases.insert( std::pair<DATABASE_ID, SDatabase >(pIdentifier, ndb)  );

	return true;
}

IDatabase* CServer::getDatabase(THREAD_ID tid, DATABASE_ID pIdentifier)
{
	IScopedLock lock(db_mutex);

	std::map<DATABASE_ID, SDatabase >::iterator database_iter=databases.find(pIdentifier);
	if( database_iter==databases.end() )
	{
		Log("Database with identifier \""+convert((int)pIdentifier)+"\" couldn't be opened", LL_ERROR);
		return NULL;
	}

	std::map<THREAD_ID, IDatabaseInt*>::iterator thread_iter=database_iter->second.tmap.find( tid );
	if( thread_iter==database_iter->second.tmap.end() )
	{
		IDatabaseInt *db=database_iter->second.factory->createDatabase();
		if(db->Open(database_iter->second.file, database_iter->second.attach, database_iter->second.allocation_chunk_size)==false )
		{
			Log("Database \""+database_iter->second.file+"\" couldn't be opened", LL_ERROR);
			return NULL;
		}

		database_iter->second.tmap.insert( std::pair< THREAD_ID, IDatabaseInt* >( tid, db ) );

		return db;
	}
	else
	{
		return thread_iter->second;
	}
}

void CServer::clearDatabases(THREAD_ID tid)
{
	IScopedLock lock(db_mutex);

	for(std::map<DATABASE_ID, SDatabase >::iterator i=databases.begin();
		i!=databases.end();++i)
	{
		std::map<THREAD_ID, IDatabaseInt*>::iterator iter=i->second.tmap.find(tid);
		if( iter!=i->second.tmap.end() )
		{
			iter->second->destroyAllQueries();
		}
	}
}

void CServer::setContentType(THREAD_ID tid, const std::string &str)
{
	{
		IScopedLock lock(outputs_mutex);
		
		std::pair<bool, std::string> *co=&current_outputs[tid];
		std::string *curr_output=&co->second;
		
		if( curr_output->find("Content-type: ")==0 )
		{
			*curr_output=strdelete("Content-type: "+getbetween("Content-type: ","\r\n\r\n", *curr_output)+"\r\n\r\n", *curr_output);
		}

		if(curr_output->find("\r\n\r\n")!=std::string::npos )
		{
			curr_output->insert(0, "Content-type: "+str+"\r\n");
		}
		else
		{
			curr_output->insert(0, "Content-type: "+str+"\r\n\r\n");
		}

		co->first=true;
	}
}

void CServer::addHeader(THREAD_ID tid, const std::string &str)
{
	{
		IScopedLock lock(outputs_mutex);
		
		std::pair<bool, std::string> *co=&current_outputs[tid];
		std::string *curr_output=&co->second;
		
		std::string tadd=str;

		if( curr_output->find("\r\n\r\n")!=std::string::npos )
		{
			tadd+="\r\n";
		}
		else
		{
			tadd+="\r\n\r\n";
		}

		curr_output->insert(0, tadd);
		co->first=true;
	}
}

ISessionMgr *CServer::getSessionMgr(void)
{
	return sessmgr;
}

std::string CServer::GenerateHexMD5(const std::string &input)
{
	MD5 md((unsigned char*)input.c_str() );
	char *p=md.hex_digest();
	std::string ret=p;
	delete []p;
	return ret;
}

std::string CServer::GenerateBinaryMD5(const std::string &input)
{
	MD5 md((unsigned char*)input.c_str(), static_cast<unsigned int>(input.size()));
	unsigned char *p=md.raw_digest_int();
	std::string ret(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p)+16);
	return ret;
}

void CServer::StartCustomStreamService(IService *pService, std::string pServiceName, unsigned short pPort, int pMaxClientsPerThread, IServer::BindTarget bindTarget)
{
	CServiceAcceptor *acc=new CServiceAcceptor(pService, pServiceName, pPort, pMaxClientsPerThread, bindTarget);
	Server->createThread(acc);

	stream_services.push_back( acc );
}

IPipe* CServer::ConnectStream(std::string pServer, unsigned short pPort, unsigned int pTimeoutms)
{
	sockaddr_in server;
	memset(&server, 0, sizeof(server));
	LookupBlocking(pServer, &server.sin_addr);
	server.sin_port=htons(pPort);
	server.sin_family=AF_INET;

	SOCKET s=socket(AF_INET, SOCK_STREAM, 0);
	if(s==SOCKET_ERROR)
	{
		return NULL;
	}

#ifdef _WIN32
	u_long nonBlocking=1;
	ioctlsocket(s,FIONBIO,&nonBlocking);
#else
	fcntl(s,F_SETFL,fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif

	int rc=connect(s, (sockaddr*)&server, sizeof(sockaddr_in) );
#ifndef _WIN32
	if(rc==SOCKET_ERROR)
	{
		if(errno!=EINPROGRESS)
		{
			closesocket(s);
			Server->Log("errno !=EINPROGRESS. Connect failed...", LL_DEBUG);
			return NULL;
		}
	}
	else
	{
		return new CStreamPipe(s);
	}
#else
	if(rc!=SOCKET_ERROR)
	{
		return new CStreamPipe(s);
	}
#endif

#ifdef _WIN32
	fd_set conn;
	FD_ZERO(&conn);
	FD_SET(s, &conn);

	timeval lon;
	lon.tv_sec=(long)(pTimeoutms/1000);
	lon.tv_usec=(long)(pTimeoutms%1000)*1000;

	rc=select((int)s+1,NULL,&conn,NULL,&lon);

	if( rc>0 && FD_ISSET(s, &conn) )
	{
#else
	pollfd conn[1];
	conn[0].fd=s;
	conn[0].events=POLLOUT;
	conn[0].revents=0;
	rc = poll(conn, 1, pTimeoutms);
	if( rc>0 )
	{
#endif	
		int err;
		socklen_t len=sizeof(int);
		rc=getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
		if(rc<0)
		{
			closesocket(s);
			Server->Log("Error getting socket status.", LL_ERROR);
			return NULL;
		}
		if(err)
		{
			closesocket(s);
			Server->Log("Socket has error: "+convert(err), LL_INFO);
			return NULL;
		}
		else
		{
#ifdef _WIN32
			int window_size=512*1024;
			setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, sizeof(window_size));
			setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, sizeof(window_size));
#endif
		    return new CStreamPipe(s);
		}
	}
	else
	{
		closesocket(s);
		return NULL;
	}
}

IPipe *CServer::PipeFromSocket(SOCKET pSocket)
{
	return new CStreamPipe(pSocket);
}

void CServer::DisconnectStream(IPipe *pipe)
{
	CStreamPipe *sp=(CStreamPipe*)pipe;
	SOCKET s=sp->getSocket();
	closesocket(s);
}

bool CServer::RegisterPluginPerThreadModel(IPluginMgr *pPluginMgr, std::string pName)
{
	IScopedLock lock(plugin_mutex);

	std::map<std::string, IPluginMgr*>::iterator iter=perthread_pluginmgrs.find( pName );

	if( iter!= perthread_pluginmgrs.end() )
		return false;

	perthread_pluginmgrs.insert( std::pair<std::string, IPluginMgr*>( pName, pPluginMgr ) );
	return true;
}

bool CServer::RegisterPluginThreadsafeModel(IPluginMgr *pPluginMgr, std::string pName)
{
	IScopedLock lock(plugin_mutex);

	std::map<std::string, IPluginMgr*>::iterator iter=threadsafe_pluginmgrs.find( pName );

	if( iter!= threadsafe_pluginmgrs.end() )
		return false;

	threadsafe_pluginmgrs.insert( std::pair<std::string, IPluginMgr*>( pName, pPluginMgr ) );
	return true;
}

PLUGIN_ID CServer::StartPlugin(std::string pName, str_map &params)
{
	IScopedLock lock(plugin_mutex);
	{
		std::map<std::string, IPluginMgr*>::iterator iter=perthread_pluginmgrs.find( pName );

		if( iter!=perthread_pluginmgrs.end() )
		{
			++curr_pluginid;
			std::pair<IPluginMgr*, str_map> tmp(iter->second, params);
			perthread_pluginparams.insert( std::pair<PLUGIN_ID, std::pair<IPluginMgr*, str_map> >(curr_pluginid, tmp) );
			return curr_pluginid;
		}
	}
	{
		std::map<std::string, IPluginMgr*>::iterator iter1=threadsafe_pluginmgrs.find( pName );

		if( iter1!=threadsafe_pluginmgrs.end() )
		{
			++curr_pluginid;
			IPlugin *plugin=iter1->second->createPluginInstance( params );
			threadsafe_plugins.insert( std::pair<PLUGIN_ID, IPlugin*>( curr_pluginid, plugin) );
			return curr_pluginid;
		}
	}

	return ILLEGAL_PLUGIN_ID;
}

bool CServer::RestartPlugin(PLUGIN_ID pIdentifier)
{
	IScopedLock lock(plugin_mutex);
	{
		std::map<PLUGIN_ID, std::map<THREAD_ID, IPlugin*> >::iterator iter1=perthread_plugins.find( pIdentifier );

		if( iter1!=perthread_plugins.end() )
		{
			bool ret=true;
			for( std::map<THREAD_ID, IPlugin*>::iterator iter2=iter1->second.begin();iter2!=iter1->second.end();++iter2)
			{
				bool b=iter2->second->Reload();
				if( b==false )
					ret=false;
			}

			return ret;
		}
	}

	{
		std::map<PLUGIN_ID, IPlugin*>::iterator iter1=threadsafe_plugins.find( pIdentifier );

		if( iter1!=threadsafe_plugins.end() )
		{
			return iter1->second->Reload();
		}
	}

	return false;
}

IPlugin* CServer::getPlugin(THREAD_ID tid, PLUGIN_ID pIdentifier)
{
	IScopedLock lock(plugin_mutex);
	{
		std::map<PLUGIN_ID, IPlugin*>::iterator iter1=threadsafe_plugins.find( pIdentifier );
		
		if( iter1!=threadsafe_plugins.end() )
		{
			return iter1->second;
		}
	}

	{
		std::map<PLUGIN_ID, std::pair<IPluginMgr*,str_map> >::iterator iter1=perthread_pluginparams.find( pIdentifier );

		if( iter1!= perthread_pluginparams.end() )
		{
			std::map<THREAD_ID, IPlugin*> *pmap=&perthread_plugins[pIdentifier];

			std::map<THREAD_ID, IPlugin*>::iterator iter2=pmap->find(tid);

			if( iter2==pmap->end() )
			{
				IPlugin* newplugin=iter1->second.first->createPluginInstance( iter1->second.second);
				pmap->insert( std::pair<THREAD_ID, IPlugin*>( tid, newplugin) );
				newplugin->Reset();
				return newplugin;
			}
			else
			{
				iter2->second->Reset();
				return iter2->second;
			}
		}
	}

	return NULL;
}

IMutex* CServer::createMutex(void)
{
	return new CMutex;
}

IPipe *CServer::createMemoryPipe(void)
{
	return new CMemoryPipe;
}

#ifdef THREAD_BOOST
void thread_helper_f(IThread *t)
{
#ifndef _DEBUG
	__try
	{
#endif
		(*t)();
#ifndef _DEBUG
	}
	__except(CServer::WriteDump(GetExceptionInformation()))
	{
		throw;
	}
#endif
}
#else
#ifndef _WIN32
void* thread_helper_f(void * t)
{
	IThread *tmp=(IThread*)t;
	(*tmp)();
	return NULL;
}
#endif
#endif //THREAD_BOOST

void CServer::createThread(IThread *thread)
{
#ifdef _WIN32
	std::thread tr(thread_helper_f, thread);
	tr.detach();
#else
	pthread_attr_t attr;
	pthread_attr_init(&attr);
#ifndef _LP64
	//Only on 32bit architectures
	pthread_attr_setstacksize(&attr, 1*1024*1024);
#endif

	pthread_t t;
	pthread_create(&t, &attr, &thread_helper_f,  (void*)thread);
	pthread_detach(t);

	pthread_attr_destroy(&attr);
#endif
}

IThreadPool *CServer::getThreadPool(void)
{
	return threadpool;
}

ISettingsReader* CServer::createFileSettingsReader(const std::string& pFile)
{
	return new CFileSettingsReader(pFile);
}

ISettingsReader* CServer::createDBSettingsReader(THREAD_ID tid, DATABASE_ID pIdentifier, const std::string &pTable, const std::string &pSQL)
{
	return new CDBSettingsReader(tid, pIdentifier, pTable, pSQL);
}

ISettingsReader* CServer::createDBSettingsReader(IDatabase *db, const std::string &pTable, const std::string &pSQL)
{
	return new CDBSettingsReader(db, pTable, pSQL);
}

ISettingsReader* CServer::createMemorySettingsReader(const std::string &pData)
{
	return new CMemorySettingsReader(pData);
}

void CServer::wait(unsigned int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep(ms*1000);
#endif
}

unsigned int CServer::getNumRequests(void)
{
	IScopedLock lock(rps_mutex);
	unsigned int ret=num_requests;
	num_requests=0;
	return ret;	
}

void CServer::addRequest(void)
{
	IScopedLock lock(rps_mutex);
	++num_requests;
}

IFile* CServer::openFile(std::string pFilename, int pMode)
{
	File *file=new File;
	if(!file->Open(pFilename, pMode) )
	{
		delete file;
		return NULL;
	}
	return file;
}

IFile* CServer::openFileFromHandle(void *handle)
{
	File *file=new File;
	if(!file->Open(handle) )
	{
		delete file;
		return NULL;
	}
	return file;
}

IFile* CServer::openTemporaryFile(void)
{
	File *file=new File;
	if(!file->OpenTemporaryFile(tmpdir) )
	{
		Server->Log("Error creating temporary file at \""+file->getFilename()+"\"", LL_ERROR);
		delete file;
		return NULL;
	}
	return file;
}

IFile* CServer::openMemoryFile(void)
{
	//return new CMemoryFile();
	return openTemporaryFile();
}

bool CServer::deleteFile(std::string pFilename)
{
	return DeleteFileInt(pFilename);
}

bool CServer::fileExists(std::string pFilename)
{
#ifndef WIN32
	return ::FileExists(pFilename);
#else
	fstream in(ConvertToWchar(pFilename).c_str(), ios::in);
	if( in.is_open()==false )
		return false;

	in.close();
	return true;
#endif
}

std::string CServer::ConvertToUTF16(const std::string &input)
{
	std::string ret;
	try
	{
		std::vector<utf8::uint16_t> tmp;
		utf8::utf8to16(input.begin(), input.end(), back_inserter(tmp) );
		ret.resize(tmp.size()*2);
		memcpy(&ret[0], &tmp[0], tmp.size()*2); 
	}
	catch(...){}

	return ret;
}

std::string CServer::ConvertToUTF32(const std::string &input)
{
	std::string ret;
	try
	{
		std::vector<utf8::uint32_t> tmp;
		utf8::utf8to32(input.begin(), input.end(), back_inserter(tmp) );
		ret.resize(tmp.size()*4);
		memcpy(&ret[0], &tmp[0], tmp.size()*4); 
	}
	catch(...){}

	return ret;
}

std::string CServer::ConvertFromUTF16(const std::string &input)
{
	if(input.empty())
	{
		return std::string();
	}

	std::string ret;
    try
    {
		utf8::utf16to8((utf8::uint16_t*)&input[0], (utf8::uint16_t*)(&input[input.size()-1]+1), back_inserter(ret));
    }
    catch(...){}	
    return ret;
}

std::string CServer::ConvertFromUTF32(const std::string &input)
{
	if(input.empty())
	{
		return std::string();
	}

	std::string ret;
    try
    {
		utf8::utf32to8((utf8::uint32_t*)&input[0], (utf8::uint32_t*)(&input[input.size()-1]+1), back_inserter(ret));
    }
    catch(...){}	
    return ret;
}

std::wstring CServer::ConvertToWchar(const std::string &input)
{
	if(input.empty())
	{
		return std::wstring();
	}

	std::wstring ret;
	try
	{
		if(sizeof(wchar_t)==2)
		{
			utf8::utf8to16(&input[0], &input[input.size()-1]+1, back_inserter(ret));
		}
		else if(sizeof(wchar_t)==4)
		{
			utf8::utf8to32(&input[0], &input[input.size()-1]+1, back_inserter(ret));
		}
		
	}
	catch(...){}	
	return ret;
}

std::string CServer::ConvertFromWchar(const std::wstring &input)
{
	if(input.empty())
	{
		return std::string();
	}

	std::string ret;
	try
	{
		if(sizeof(wchar_t)==2)
		{
			utf8::utf16to8(&input[0], &input[input.size()-1]+1, back_inserter(ret));
		}
		else if(sizeof(wchar_t)==4)
		{
			utf8::utf32to8(&input[0], &input[input.size()-1]+1, back_inserter(ret));
		}

	}
	catch(...){}	
	return ret;
}

ICondition* CServer::createCondition(void)
{
	return new CCondition();
}

void CServer::addPostFile(POSTFILE_KEY pfkey, const std::string &name, const SPostfile &pf)
{
	IScopedLock lock(postfiles_mutex);
	postfiles[pfkey][name]=pf;
}

SPostfile CServer::getPostFile(POSTFILE_KEY pfkey, const std::string &name)
{
	IScopedLock lock(postfiles_mutex);
	std::map<THREAD_ID, std::map<std::string, SPostfile > >::iterator iter1=postfiles.find(pfkey);
	if(iter1!=postfiles.end())
	{
		std::map<std::string, SPostfile >::iterator iter2=iter1->second.find(name);
		if(iter2!=iter1->second.end() )
		{
			return iter2->second;
		}
	}
	return SPostfile();
}

void CServer::clearPostFiles(POSTFILE_KEY pfkey)
{
	IScopedLock lock(postfiles_mutex);

	std::map<THREAD_ID, std::map<std::string, SPostfile > >::iterator iter1=postfiles.find(pfkey);
	if(iter1!=postfiles.end())
	{
		for(std::map<std::string, SPostfile >::iterator iter2=iter1->second.begin();iter2!=iter1->second.end();++iter2)
		{
			destroy(iter2->second.file);
		}
		postfiles.erase(iter1);
	}
}

POSTFILE_KEY CServer::getPostFileKey()
{
	IScopedLock lock(postfiles_mutex);
	return curr_postfilekey++;
}

std::string CServer::getServerWorkingDir(void)
{
	return workingdir;
}

void CServer::setServerWorkingDir(const std::string &wdir)
{
	workingdir=wdir;
}

void CServer::setTemporaryDirectory(const std::string &dir)
{
	tmpdir=dir;
}

void CServer::registerDatabaseFactory(const std::string &pEngineName, IDatabaseFactory *factory)
{
	IScopedLock lock(db_mutex);

	database_factories[pEngineName]=factory;
}

bool CServer::hasDatabaseFactory(const std::string &pEngineName)
{
	IScopedLock lock(db_mutex);

	std::map<std::string, IDatabaseFactory*>::iterator it=database_factories.find(pEngineName);
	return it!=database_factories.end();
}

bool CServer::attachToDatabase(const std::string &pFile, const std::string &pName, DATABASE_ID pIdentifier)
{
	IScopedLock lock(db_mutex);

	std::map<DATABASE_ID, SDatabase >::iterator iter=databases.find(pIdentifier);
	if( iter==databases.end() )
	{
		return false;
	}

	if(std::find(iter->second.attach.begin(), iter->second.attach.end(), std::pair<std::string,std::string>(pFile, pName))==iter->second.attach.end())
	{
		iter->second.attach.push_back(std::pair<std::string,std::string>(pFile, pName));
	}

	return true;
}

bool CServer::setDatabaseAllocationChunkSize(DATABASE_ID pIdentifier, size_t allocation_chunk_size)
{
	IScopedLock lock(db_mutex);

	std::map<DATABASE_ID, SDatabase >::iterator iter=databases.find(pIdentifier);
	if( iter==databases.end() )
	{
		return false;
	}

	iter->second.allocation_chunk_size = allocation_chunk_size;

	return true;
}

void CServer::waitForStartupComplete(void)
{
	IScopedLock lock(startup_complete_mutex);
	if(!startup_complete)
	{
		startup_complete_cond->wait(&lock);
	}
}

void CServer::startupComplete(void)
{
	IScopedLock lock(startup_complete_mutex);
	startup_complete=true;
	startup_complete_cond->notify_all();
}

IPipeThrottler* CServer::createPipeThrottler(size_t bps,
	IPipeThrottlerUpdater* updater)
{
	return new PipeThrottler(bps, updater);
}


void CServer::shutdown(void)
{
	run=false;
}

void CServer::initRandom(unsigned int seed)
{
	init_genrand(seed);
}

unsigned int CServer::getRandomNumber(void)
{
	IScopedLock lock(rnd_mutex);
	return genrand_int32();
}

std::vector<unsigned int> CServer::getRandomNumbers(size_t n)
{
	IScopedLock lock(rnd_mutex);
	std::vector<unsigned int> ret;
	ret.resize(n);
	for(size_t i=0;i<n;++i)
	{
		ret[i]=genrand_int32();
	}
	return ret;
}

void CServer::randomFill(char *buf, size_t blen)
{
	IScopedLock lock(rnd_mutex);
	char *dptr=buf+blen;
	while(buf<dptr)
	{
		if(dptr-buf>=sizeof(unsigned int))
		{
			*((unsigned int*)buf)=genrand_int32();
			buf+=sizeof(unsigned int);
		}
		else
		{
			unsigned int rnd=genrand_int32();
			memcpy(buf, &rnd, dptr-buf);
			buf+=dptr-buf;
		}
	}
}

unsigned int CServer::getSecureRandomNumber(void)
{
#ifdef _WIN32
	unsigned int rnd;
	errno_t err=rand_s(&rnd);
	if(err!=0)
	{
		Log("Error generating secure random number", LL_ERROR);
		return getRandomNumber();
	}
	return rnd;
#else
	unsigned int rnd;
	std::fstream rnd_in("/dev/urandom", std::ios::in | std::ios::binary );
	if(!rnd_in.is_open())
	{
		Log("Error opening /dev/urandom for secure random number", LL_ERROR);
		return getRandomNumber();
	}
	rnd_in.read((char*)&rnd, sizeof(unsigned int));
	assert(rnd_in.gcount()==sizeof(unsigned int));
	if(rnd_in.fail() || rnd_in.eof() )
	{
		Log("Error reading secure random number", LL_ERROR);
		return getRandomNumber();
	}
	return rnd;
#endif
}

std::vector<unsigned int> CServer::getSecureRandomNumbers(size_t n)
{
#ifndef _WIN32
	std::fstream rnd_in("/dev/urandom", std::ios::in | std::ios::binary );
	if(!rnd_in.is_open())
	{
		Log("Error opening /dev/urandom for secure random number", LL_ERROR);
		return getRandomNumbers(n);
	}
#endif
	std::vector<unsigned int> ret;
	ret.resize(n);
	for(size_t i=0;i<n;++i)
	{
#ifdef _WIN32
		ret[i]=getSecureRandomNumber();
#else
		unsigned int rnd;
		rnd_in.read((char*)&rnd, sizeof(unsigned int));
		ret[i]=rnd;
#endif		
	}

#ifndef _WIN32
	if(rnd_in.fail() || rnd_in.eof() )
	{
		Log("Error reading secure random numbers", LL_ERROR);
		return getRandomNumbers(n);
	}
#endif
	return ret;
}

void CServer::secureRandomFill(char *buf, size_t blen)
{
#ifdef _WIN32
	char *dptr=buf+blen;
	while(buf<dptr)
	{
		if(dptr-buf>=sizeof(unsigned long))
		{
			*((unsigned long*)buf)=getSecureRandomNumber();
			buf+=sizeof(unsigned long);
		}
		else
		{
			unsigned long rnd=getSecureRandomNumber();
			memcpy(buf, &rnd, dptr-buf);
			buf+=dptr-buf;
		}
	}
#else
	std::fstream rnd_in("/dev/urandom", std::ios::in | std::ios::binary );
	if(!rnd_in.is_open())
	{
		Log("Error opening /dev/urandom for secure random number fill", LL_ERROR);
		randomFill(buf, blen);
		return;
	}

	rnd_in.read(buf, blen);

	assert(rnd_in.gcount()==blen);

	if(rnd_in.fail() || rnd_in.eof() )
	{
		Log("Error reading secure random numbers fill", LL_ERROR);
		randomFill(buf, blen);
		return;
	}
#endif
}

std::string CServer::secureRandomString(size_t len)
{
	std::string rchars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	std::string key;
	std::vector<unsigned int> rnd_n=Server->getSecureRandomNumbers(len);
	for(size_t j=0;j<len;++j)
		key+=rchars[rnd_n[j]%rchars.size()];
	return key;
}

void CServer::setLogCircularBufferSize(size_t size)
{
	IScopedLock lock(log_mutex);

	circular_log_buffer.resize(size);

	has_circular_log_buffer=size>0?true:false;
}

std::vector<SCircularLogEntry> CServer::getCicularLogBuffer( size_t minid )
{
	IScopedLock lock(log_mutex);

	if(minid==std::string::npos)
	{
		return circular_log_buffer;
	}

	for(size_t i=0;i<circular_log_buffer.size();++i)
	{
		if(circular_log_buffer[i].id>minid &&
			circular_log_buffer[i].id!=std::string::npos)
		{
			return circular_log_buffer;
		}
	}

	return std::vector<SCircularLogEntry>();
}

void CServer::logToCircularBuffer(const std::string& msg, int loglevel)
{
	if(circular_log_buffer.empty())
		return;

	SCircularLogEntry &entry=circular_log_buffer[circular_log_buffer_idx];

	entry.utf8_msg=msg;
	entry.loglevel=loglevel;
	entry.id=circular_log_buffer_id++;
	entry.time=Server->getTimeSeconds();

	circular_log_buffer_idx=(circular_log_buffer_idx+1)%circular_log_buffer.size();
}

void CServer::setFailBit(size_t failbit)
{
	failbits=failbits|failbit;
}

void CServer::clearFailBit(size_t failbit)
{
	failbits=failbit & (~failbit);
}

size_t CServer::getFailBits(void)
{
	return failbits;
}

ISharedMutex* CServer::createSharedMutex()
{
	return new SharedMutex;
}

void CServer::setLogRotationFilesize( size_t filesize )
{
	log_rotation_size=filesize;
}

void CServer::setLogRotationNumFiles( size_t numfiles )
{
	log_rotation_files=numfiles;
}

void CServer::LoadStaticPlugins()
{
	std::vector<SStaticPlugin>& staticplugins = get_static_plugin_registrations();
	std::sort(staticplugins.begin(), staticplugins.end());
	for(size_t i=0;i<staticplugins.size();++i)
	{
		LOADACTIONS loadfunc = staticplugins[i].loadactions;
		loadfunc(this);
	}
}

void CServer::setLogConsoleTime(bool b)
{
	log_console_time = b;
}
