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

#include "client.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../Interface/SettingsReader.h"
#ifdef _WIN32
#include "DirectoryWatcherThread.h"
#else
#include <errno.h>
#endif
#include "../stringtools.h"
#include "../common/data.h"
#include "database.h"
#include "ServerIdentityMgr.h"
#include "ClientService.h"
#include "../urbackupcommon/sha2/sha2.h"
#include <algorithm>
#include <fstream>
#include <stdlib.h>

//For truncating files
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif
volatile bool IdleCheckerThread::idle=false;
volatile bool IdleCheckerThread::pause=false;
volatile bool IndexThread::stop_index=false;
std::map<std::wstring, std::wstring> IndexThread::filesrv_share_dirs;

const char IndexThread::IndexThreadAction_GetLog=9;

extern PLUGIN_ID filesrv_pluginid;

const int64 idletime=60000;
const unsigned int nonidlesleeptime=500;
const unsigned short tcpport=35621;
const unsigned short udpport=35622;
const unsigned int shadowcopy_timeout=7*24*60*60*1000;
const unsigned int shadowcopy_startnew_timeout=55*60*1000;
const size_t max_modify_file_buffer_size=500*1024;
const size_t max_modify_hash_buffer_size=500*1024;
const int64 save_filehash_limit=20*4096;

#ifndef SERVER_ONLY
#define ENABLE_VSS
#endif

#define CHECK_COM_RESULT_RELEASE(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog(#x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); if(backupcom!=NULL){backupcom->AbortBackup();backupcom->Release();} return false; }}
#define CHECK_COM_RESULT_RETURN(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); return false; }}
#define CHECK_COM_RESULT_RELEASE_S(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); if(backupcom!=NULL){backupcom->AbortBackup();backupcom->Release();} return ""; }}
#define CHECK_COM_RESULT(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); }}
#define CHECK_COM_RESULT_OK(x, ok) { HRESULT r; if( (r=(x))!=S_OK ){ ok=false; VSSLog( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); }}
#define CHECK_COM_RESULT_OK_HR(x, ok, r) { if( (r=(x))!=S_OK ){ ok=false; VSSLog( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); }}

void IdleCheckerThread::operator()(void)
{
	int lx,ly;
	int x,y;
	getMousePos(x,y);
	lx=x;
	ly=y;

	int64 last_move=Server->getTimeMS();

	while(true)
	{
		Server->wait(1000);
		getMousePos(x,y);
		if(x!=lx || y!=ly )
		{
			lx=x;
			ly=y;
			last_move=Server->getTimeMS();
			idle=false;
		}
		else if(Server->getTimeMS()-last_move>idletime)
		{
			idle=true;
		}
	}
}

bool IdleCheckerThread::getIdle(void)
{
	return true;//idle;
}

bool IdleCheckerThread::getPause(void)
{
	return pause;
}

void IdleCheckerThread::setPause(bool b)
{
	pause=b;
}

IMutex *IndexThread::filelist_mutex=NULL;
IPipe* IndexThread::msgpipe=NULL;
IFileServ *IndexThread::filesrv=NULL;
IMutex *IndexThread::filesrv_mutex=NULL;

IndexThread::IndexThread(void)
	: index_error(false), last_filebackup_filetime(0)
{
	if(filelist_mutex==NULL)
		filelist_mutex=Server->createMutex();
	if(msgpipe==NULL)
		msgpipe=Server->createMemoryPipe();
	if(filesrv_mutex==NULL)
		filesrv_mutex=Server->createMutex();

	contractor=NULL;

	dwt=NULL;

	if(Server->getPlugin(Server->getThreadID(), filesrv_pluginid))
	{
		start_filesrv();
	}
	else
	{
		filesrv=NULL;
		Server->Log("Error starting fileserver", LL_ERROR);
	}

	modify_file_buffer_size=0;
	end_to_end_file_backup_verification_enabled=0;
	calculate_filehashes_on_client=0;
	last_tmp_update_time=0;
}

IndexThread::~IndexThread()
{
	filesrv->stopServer();

#ifdef _WIN32
	if(dwt!=NULL)
	{
		dwt->stop();
		Server->getThreadPool()->waitFor(dwt_ticket);
		delete dwt;
	}
#endif

	((IFileServFactory*)(Server->getPlugin(Server->getThreadID(), filesrv_pluginid)))->destroyFileServ(filesrv);
	Server->destroy(filelist_mutex);
	Server->destroy(msgpipe);
	Server->destroy(filesrv_mutex);
	cd->destroyQueries();
	delete cd;
}

IMutex* IndexThread::getFilelistMutex(void)
{
	return filelist_mutex;
}

void IndexThread::updateDirs(void)
{
	readBackupDirs();

#ifdef _WIN32
	std::vector<std::wstring> watching;
	for(size_t i=0;i<backup_dirs.size();++i)
	{
		watching.push_back(backup_dirs[i].path);
	}
	if(dwt==NULL)
	{
		dwt=new DirectoryWatcherThread(watching);
		dwt_ticket=Server->getThreadPool()->execute(dwt);
	}
	else
	{
		for(size_t i=0;i<backup_dirs.size();++i)
		{
			std::wstring msg=L"A"+backup_dirs[i].path;
			dwt->getPipe()->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
		}
	}
#endif
}

void IndexThread::operator()(void)
{
	Server->waitForStartupComplete();

#ifdef _WIN32
#ifndef SERVER_ONLY
	CHECK_COM_RESULT(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	CHECK_COM_RESULT(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY, NULL, EOAC_NONE, NULL));
#endif
#endif
#ifdef _WIN32

#ifdef THREAD_MODE_BACKGROUND_BEGIN
#if defined(VSS_XP) || defined(VSS_S03)
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#else
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
#else
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif //THREAD_MODE_BACKGROUND_BEGIN

#endif

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	db->Write("CREATE TEMPORARY TABLE files_tmp (num NUMERIC, data BLOB, name TEXT);");

	cd=new ClientDAO(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT));

#ifdef _WIN32
#ifdef ENABLE_VSS
	cleanup_saved_shadowcopies();
#endif
#endif
	
	updateDirs();

	while(true)
	{
		std::string msg;
		if(contractor!=NULL)
		{
			while(msg!="exit")
			{
				contractor->Read(&msg);
				if(msg!="exit")
				{
					contractor->Write(msg);
					Server->wait(100);
				}
			}
			Server->destroy(contractor);
			contractor=NULL;
		}
		msgpipe->Read(&msg);

		CRData data(&msg);
		char action;
		data.getChar(&action);
		data.getVoidPtr((void**)&contractor);
		if(action==0)
		{
			Server->Log("Removing VSS log data...", LL_DEBUG);
			vsslog.clear();

			data.getStr(&starttoken);
			data.getInt(&end_to_end_file_backup_verification_enabled);
			data.getInt(&calculate_filehashes_on_client);

			//incr backup
			readBackupDirs();
			if(backup_dirs.empty())
			{
				contractor->Write("no backup dirs");
				continue;
			}
#ifdef _WIN32
			if(cd->hasChangedGap())
			{
				Server->Log("Deleting file-index... GAP found...", LL_INFO);

				std::vector<std::wstring> gaps=cd->getGapDirs();

				std::string q_str="DELETE FROM files";
				if(!gaps.empty())
				{
					q_str+=" WHERE ";
				}
				for(size_t i=0;i<gaps.size();++i)
				{
					q_str+="name GLOB ?";
					if(i+1<gaps.size())
						q_str+=" OR ";
				}

				IQuery *q=db->Prepare(q_str, false);
				for(size_t i=0;i<gaps.size();++i)
				{
					Server->Log(L"Deleting file-index from drive \""+gaps[i]+L"\"", LL_INFO);
					q->Bind(gaps[i]+L"*");
				}

				q->Write();
				q->Reset();
				db->destroyQuery(q);

				if(dwt!=NULL)
				{
					dwt->stop();
					Server->getThreadPool()->waitFor(dwt_ticket);
					delete dwt;
					dwt=NULL;
					updateDirs();
				}
			}
#endif
			execute_prebackup_hook();
			if(!stop_index)
			{
				indexDirs();
				if(stop_index)
				{
					contractor->Write("error - stopped indexing 2");
				}
				else if(index_error)
				{
					contractor->Write("error - index error");
				}
				else
				{
					contractor->Write("done");
				}
			}
			else
			{
				contractor->Write("error - stop_index 1");
			}
			execute_postindex_hook();
		}
		else if(action==1)
		{
			Server->Log("Removing VSS log data...", LL_DEBUG);
			vsslog.clear();

			data.getStr(&starttoken);
			data.getInt(&end_to_end_file_backup_verification_enabled);
			data.getInt(&calculate_filehashes_on_client);

			readBackupDirs();
			if(backup_dirs.empty())
			{
				contractor->Write("no backup dirs");
				continue;
			}
			//full backup
			{
				cd->deleteChangedDirs();
				cd->deleteSavedChangedDirs();

				Server->Log("Deleting files... doing full index...", LL_INFO);
				resetFileEntries();
			}
			execute_prebackup_hook();
			indexDirs();
			execute_postindex_hook();
			if(stop_index)
			{
				contractor->Write("error - stopped indexing");
			}
			else if(index_error)
			{
				contractor->Write("error - index error");
			}
			else
			{
				contractor->Write("done");
			}
		}
		else if(action==2) // create shadowcopy
		{
			std::string scdir;
			data.getStr(&scdir);
			data.getStr(&starttoken);
			uchar image_backup=0;
			data.getUChar(&image_backup);
			std::wstring wscdir=Server->ConvertToUnicode(scdir);
			SCDirs *scd=getSCDir(wscdir);

			unsigned char fileserv;
			bool hfs=data.getUChar(&fileserv);
			
			if(scd->running==true && Server->getTimeSeconds()-scd->starttime<shadowcopy_timeout/1000)
			{
				if(scd->ref!=NULL && image_backup==0)
				{
					scd->ref->dontincrement=true;
				}
				if(start_shadowcopy(scd, NULL, image_backup==1?true:false, std::vector<SCRef*>(), image_backup==1?true:false))
				{
					contractor->Write("done-"+nconvert(scd->ref->save_id)+"-"+Server->ConvertToUTF8(scd->target));
				}
				else
				{
					VSSLog(L"Getting shadowcopy of \""+scd->dir+L"\" failed.", LL_ERROR);
					contractor->Write("failed");
				}
			}
			else
			{
				if(scd->running==true)
				{
					Server->Log(L"Removing shadowcopy \""+scd->dir+L"\" because of timeout...", LL_WARNING);
					bool b=release_shadowcopy(scd, false, -1, scd);
					if(!b)
					{
#ifdef _WIN32
						Server->Log(L"Deleting shadowcopy of \""+scd->dir+L"\" failed.", LL_ERROR);
#endif
					}
				}

				scd->dir=wscdir;
				scd->starttime=Server->getTimeSeconds();
				if(hfs && fileserv==0)
				{
					scd->target=scd->dir;
					scd->fileserv=false;
				}
				else
				{
					scd->target=getShareDir(scd->dir);
					scd->fileserv=true;
				}

				scd->orig_target=scd->target;

				Server->Log(L"Creating shadowcopy of \""+scd->dir+L"\"...", LL_DEBUG);
				bool b=start_shadowcopy(scd, NULL, image_backup==1?true:false, std::vector<SCRef*>(), image_backup==0?false:true);
				Server->Log("done.", LL_DEBUG);
				if(!b || scd->ref==NULL)
				{
					if(scd->fileserv)
					{
						shareDir(widen(starttoken), scd->dir, scd->target);
					}

					contractor->Write("failed");
#ifdef _WIN32
					Server->Log(L"Creating shadowcopy of \""+scd->dir+L"\" failed.", LL_ERROR);
#endif
				}
				else
				{
					contractor->Write("done-"+nconvert(scd->ref->save_id)+"-"+Server->ConvertToUTF8(scd->target));
					scd->running=true;
				}
			}
		}
		else if(action==3) // remove shadowcopy
		{
			std::string scdir;
			data.getStr(&scdir);
			data.getStr(&starttoken);
			uchar image_backup=0;
			data.getUChar(&image_backup);
			SCDirs *scd=getSCDir(Server->ConvertToUnicode(scdir));

			int save_id=-1;
			data.getInt(&save_id);

			if(scd->running==false )
			{				
				if(!release_shadowcopy(scd, image_backup==1?true:false, save_id))
				{
					Server->Log("Invalid action -- Creating shadow copy failed?", LL_ERROR);
					contractor->Write("failed");
				}
				else
				{
					contractor->Write("done");
				}
			}
			else
			{
				std::wstring release_dir=scd->dir;
				bool b=release_shadowcopy(scd, image_backup==1?true:false, save_id);
				if(!b)
				{
					contractor->Write("failed");
#ifdef _WIN32
					Server->Log(L"Deleting shadowcopy of \""+release_dir+L"\" failed.", LL_ERROR);
#endif
				}
				else
				{
					contractor->Write("done");
				}
			}
		}
		else if(action==4) //lookup shadowdrive path
		{
			int save_id;
			if(data.getInt(&save_id))
			{
				std::string path=lookup_shadowcopy(save_id);
				if(path.empty())
				{
					contractor->Write("failed");
				}
				else
				{
					contractor->Write("done-"+nconvert(save_id)+"-"+path);
				}
			}
		}
#ifdef _WIN32
		else if(action==5) //add watch directory
		{
			std::string dir;
			if(data.getStr(&dir))
			{
				std::wstring msg=L"A"+os_get_final_path(Server->ConvertToUnicode( dir ));
				dwt->getPipe()->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
			}
			contractor->Write("done");
			stop_index=false;
		}
		else if(action==6) //remove watch directory
		{
			std::string dir;
			if(data.getStr(&dir))
			{
				std::wstring msg=L"D"+os_get_final_path(Server->ConvertToUnicode( dir ));
				dwt->getPipe()->Write((char*)msg.c_str(), sizeof(wchar_t)*msg.size());
			}
			contractor->Write("done");
			stop_index=false;
		}
#endif
		else if(action==7) // restart filesrv
		{
			IScopedLock lock(filesrv_mutex);
			filesrv->stopServer();
			start_filesrv();
			readBackupDirs();
		}
		else if(action==8) //stop
		{
			break;
		}
		else if(action==IndexThreadAction_GetLog)
		{
			std::string ret;
			for(size_t i=0;i<vsslog.size();++i)
			{
				ret+=nconvert(vsslog[i].second)+"-"+vsslog[i].first+"\n";
			}
			Server->Log("VSS logdata - "+nconvert(ret.size())+" bytes", LL_DEBUG);
			contractor->Write(ret);
		}
	}

	delete this;
}

const char * filelist_fn="urbackup/data/filelist_new.ub";

void IndexThread::indexDirs(void)
{
	bool patterns_changed=false;
	readPatterns(patterns_changed, false);

	if(patterns_changed)
	{
		VSSLog("Deleting file-cache because include/exclude pattern changed...", LL_INFO);
		resetFileEntries();
	}

	updateDirs();
#ifdef _WIN32
	//Invalidate cache
	DirectoryWatcherThread::freeze();
	Server->wait(10000);
	DirectoryWatcherThread::update_and_wait();
	changed_dirs=cd->getChangedDirs();
	cd->moveChangedFiles();

	bool has_stale_shadowcopy=false;

	std::vector<std::wstring> deldirs=cd->getDelDirs();
	for(size_t i=0;i<deldirs.size();++i)
	{
		cd->removeDeletedDir(deldirs[i]);
	}

	std::wstring tmp = cd->getMiscValue("last_filebackup_filetime_lower");
	if(!tmp.empty())
	{
		last_filebackup_filetime = watoi64(tmp);
	}
	else
	{
		last_filebackup_filetime = 0;
	}

	_i64 last_filebackup_filetime_new = DirectoryWatcherThread::get_current_filetime();
#endif

	std::sort(changed_dirs.begin(), changed_dirs.end());


	std::vector<SCRef*> past_refs;

	last_tmp_update_time=Server->getTimeMS();
	index_error=false;

	{
		std::fstream outfile(filelist_fn, std::ios::out|std::ios::binary);
		for(size_t i=0;i<backup_dirs.size();++i)
		{
			SCDirs *scd=getSCDir(backup_dirs[i].tname);
			if(!scd->running)
			{
				scd->dir=backup_dirs[i].tname;
				scd->starttime=Server->getTimeSeconds();
				scd->target=getShareDir(backup_dirs[i].tname);
				scd->orig_target=scd->target;
			}
			scd->fileserv=true;

			std::wstring mod_path=backup_dirs[i].path;

			VSSLog(L"Creating shadowcopy of \""+scd->dir+L"\" in indexDirs()", LL_DEBUG);
			bool onlyref=true;
			bool stale_shadowcopy=false;
			bool b=start_shadowcopy(scd, &onlyref, true, past_refs, false, &stale_shadowcopy);
			VSSLog("done.", LL_DEBUG);

#ifdef _WIN32
			if(stale_shadowcopy)
			{
				has_stale_shadowcopy=true;
			}
#endif

			if(!b)
			{
#ifdef _WIN32
				VSSLog(L"Creating shadowcopy of \""+scd->dir+L"\" failed in indexDirs().", LL_ERROR);
#endif
				shareDir(widen(starttoken), scd->dir, scd->target);
			}
			else
			{
				mod_path=scd->target;
				scd->running=true;
			}

			mod_path=removeDirectorySeparatorAtEnd(mod_path);
			backup_dirs[i].path=removeDirectorySeparatorAtEnd(backup_dirs[i].path);

#ifdef _WIN32
			if(mod_path.size()==2) //e.g. C:
			{
				mod_path+=os_file_sep();
			}
#endif

#ifdef _WIN32
			if(!b || !onlyref)
			{
				past_refs.push_back(scd->ref);
				DirectoryWatcherThread::update_and_wait();
				Server->wait(1000);
				std::vector<SMDir> acd=cd->getChangedDirs(false);
				changed_dirs.insert(changed_dirs.end(), acd.begin(), acd.end() );
				std::sort(changed_dirs.begin(), changed_dirs.end());

				#ifndef VSS_XP
				#ifndef VSS_S03
				VSSLog(L"Scanning for changed hard links in \""+backup_dirs[i].tname+L"\"...", LL_DEBUG);
				handleHardLinks(backup_dirs[i].path, mod_path);
				#endif
				#endif

				std::vector<std::wstring> deldirs=cd->getDelDirs(false);
				for(size_t i=0;i<deldirs.size();++i)
				{
					cd->removeDeletedDir(deldirs[i]);
				}
			}
#endif

			for(size_t k=0;k<changed_dirs.size();++k)
			{
				VSSLog(L"Changed dir: " + changed_dirs[k].name, LL_DEBUG);
			}

			VSSLog(L"Indexing \""+backup_dirs[i].tname+L"\"...", LL_DEBUG);
			index_c_db=0;
			index_c_fs=0;
			index_c_db_update=0;
			outfile << "d\"" << escapeListName(Server->ConvertToUTF8(backup_dirs[i].tname)) << "\"\n";
			//db->Write("BEGIN IMMEDIATE;");
			last_transaction_start=Server->getTimeMS();
			index_root_path=mod_path;
			initialCheck( backup_dirs[i].path, mod_path, backup_dirs[i].tname, outfile, true, backup_dirs[i].optional);

			cd->copyFromTmpFiles();
			commitModifyFilesBuffer();

			if(stop_index || index_error)
			{
				for(size_t k=0;k<backup_dirs.size();++k)
				{
					SCDirs *scd=getSCDir(backup_dirs[k].tname);
					release_shadowcopy(scd);
				}
				
				outfile.close();
				removeFile(Server->ConvertToUnicode(filelist_fn));

				if(stop_index)
				{
					VSSLog(L"Indexing files failed, because of error", LL_ERROR);
				}

				return;
			}
			//db->EndTransaction();
			outfile << "d\"..\"\n";
			VSSLog(L"Indexing of \""+backup_dirs[i].tname+L"\" done. "+convert(index_c_fs)+L" filesystem lookups "+convert(index_c_db)+L" db lookups and "+convert(index_c_db_update)+L" db updates" , LL_INFO);		
		}
		std::streampos pos=outfile.tellp();
		outfile.seekg(0, std::ios::end);
		if(pos!=outfile.tellg())
		{
			outfile.close();
			bool b=os_file_truncate(Server->ConvertToUnicode(filelist_fn), pos);
			if(!b)
			{
				VSSLog("Error changing filelist size", LL_ERROR);
			}
		}
	}

	cd->copyFromTmpFiles();
	commitModifyFilesBuffer();

#ifdef _WIN32
	if(!has_stale_shadowcopy)
	{
		if(!index_error)
		{
			VSSLog("Deleting backup of changed dirs...", LL_DEBUG);
			cd->deleteSavedChangedDirs();
			cd->deleteSavedDelDirs();
			cd->deleteSavedChangedFiles();

			DirectoryWatcherThread::update_last_backup_time();
			DirectoryWatcherThread::commit_last_backup_time();

			cd->updateMiscValue("last_filebackup_filetime_lower", convert(last_filebackup_filetime_new));
		}
		else
		{
			VSSLog("Did not delete backup of changed dirs because there was an error while indexing which might not occur the next time.", LL_INFO);
		}
	}
	else
	{
		VSSLog("Did not delete backup of changed dirs because a stale shadowcopy was used.", LL_INFO);
	}

	DirectoryWatcherThread::unfreeze();
	
#endif

	{
		IScopedLock lock(filelist_mutex);
		removeFile(L"urbackup/data/filelist.ub");
		moveFile(L"urbackup/data/filelist_new.ub", L"urbackup/data/filelist.ub");
		Server->wait(1000);
	}

	if(patterns_changed)
	{
		readPatterns(patterns_changed, true);
	}
	share_dirs();
}

void IndexThread::resetFileEntries(void)
{
	db->Write("DELETE FROM files");
	db->Write("DELETE FROM mdirs");
	db->Write("DELETE FROM mdirs_backup");
	db->Write("DELETE FROM mfiles");
	db->Write("DELETE FROM mfiles_backup");
}

bool IndexThread::skipFile(const std::wstring& filepath, const std::wstring& namedpath)
{
	if( isExcluded(filepath) || isExcluded(namedpath) )
	{
		return true;
	}
	if( !isIncluded(filepath, NULL) && !isIncluded(namedpath, NULL) )
	{
		return true;
	}

	return false;
}

bool IndexThread::initialCheck(const std::wstring &orig_dir, const std::wstring &dir, const std::wstring &named_path, std::fstream &outfile, bool first, bool optional)
{
	bool has_include=false;

	//Server->Log(L"Indexing "+dir, LL_DEBUG);
	if(Server->getTimeMS()-last_transaction_start>1000)
	{
		/*db->EndTransaction();
		db->Write("BEGIN IMMEDIATE;");*/
		last_transaction_start=Server->getTimeMS();
	}
	if( IdleCheckerThread::getIdle()==false )
	{
		Server->wait(nonidlesleeptime);
	}
	if(IdleCheckerThread::getPause())
	{
		Server->wait(5000);
	}

	if(stop_index)
	{
		return false;
	}

	if(first && !os_directory_exists(os_file_prefix(add_trailing_slash(dir))) )
	{
		VSSLog(L"Cannot access directory to backup: \""+dir+L"\"", LL_ERROR);
		if(!optional)
		{
			index_error=true;
		}
		return false;
	}

	std::vector<SFileAndHash> files=getFilesProxy(orig_dir, dir, named_path, !first);

	if(index_error)
	{
		return false;
	}
	
	for(size_t i=0;i<files.size();++i)
	{
		if( !files[i].isdir )
		{
			if( skipFile(orig_dir+os_file_sep()+files[i].name, named_path+os_file_sep()+files[i].name) )
			{
				continue;
			}
			has_include=true;
			outfile << "f\"" << escapeListName(Server->ConvertToUTF8(files[i].name)) << "\" " << files[i].size << " " << files[i].last_modified;

			if(end_to_end_file_backup_verification_enabled || calculate_filehashes_on_client)
			{
				outfile << "#";

				if(calculate_filehashes_on_client)
				{
					outfile << "sha512=" << base64_encode_dash(files[i].hash);
				}
				
				if(end_to_end_file_backup_verification_enabled)
				{
					if(calculate_filehashes_on_client) outfile << "&";

					outfile << "sha256=" << getSHA256(dir+os_file_sep()+files[i].name);
				}
			}
			
			outfile << "\n";
		}
	}

	for(size_t i=0;i<files.size();++i)
	{
		if( files[i].isdir )
		{
			if( isExcluded(orig_dir+os_file_sep()+files[i].name) || isExcluded(named_path+os_file_sep()+files[i].name) )
			{
				continue;
			}
			bool curr_included=false;
			bool adding_worthless1, adding_worthless2;
			if( isIncluded(orig_dir+os_file_sep()+files[i].name, &adding_worthless1) || isIncluded(named_path+os_file_sep()+files[i].name, &adding_worthless2) )
			{
				has_include=true;
				curr_included=true;
			}

			if( curr_included ||  !adding_worthless1 || !adding_worthless2 )
			{
				std::streampos pos=outfile.tellp();
				outfile << "d\"" << escapeListName(Server->ConvertToUTF8(files[i].name)) << "\"\n";
				bool b=initialCheck(orig_dir+os_file_sep()+files[i].name, dir+os_file_sep()+files[i].name, named_path+os_file_sep()+files[i].name, outfile, false, optional);			
				outfile << "d\"..\"\n";

				if(!b)
				{
					if(!curr_included)
					{
						outfile.seekp(pos);
					}
				}
				else
				{
					has_include=true;
				}

				if(index_error)
				{
					return false;
				}
			}
		}
	}

	return has_include;
}

void IndexThread::readBackupDirs(void)
{
	backup_dirs=cd->getBackupDirs();

	for(size_t i=0;i<backup_dirs.size();++i)
	{
#ifdef _WIN32
		backup_dirs[i].path=os_get_final_path(backup_dirs[i].path);
		Server->Log(L"Final path: "+backup_dirs[i].path, LL_INFO);
#endif

		if(filesrv!=NULL)
			shareDir(L"", backup_dirs[i].tname, backup_dirs[i].path);
	}
}

namespace
{
	std::vector<SFileAndHash> convertToFileAndHash(const std::vector<SFile> files)
	{
		std::vector<SFileAndHash> ret;
		ret.resize(files.size());
		for(size_t i=0;i<files.size();++i)
		{
			ret[i].isdir=files[i].isdir;
			ret[i].last_modified=files[i].last_modified;
			ret[i].name=files[i].name;
			ret[i].size=files[i].size;
		}
		return ret;
	}
}

bool IndexThread::addMissingHashes(std::vector<SFileAndHash>* dbfiles, std::vector<SFileAndHash>* fsfiles, const std::wstring &orig_path, const std::wstring& filepath, const std::wstring& namedpath)
{
	bool calculated_hash=false;

	if(fsfiles!=NULL)
	{
		for(size_t i=0;i<fsfiles->size();++i)
		{
			SFileAndHash& fsfile = fsfiles->at(i);
			if( fsfile.isdir )
				continue;

			if(!fsfile.hash.empty())
				continue;

			if(skipFile(orig_path+os_file_sep()+fsfile.name, namedpath+os_file_sep()+fsfile.name))
				continue;			

			bool needs_hashing=true;

			if(dbfiles!=NULL)
			{
				std::vector<SFileAndHash>::iterator it = std::lower_bound(dbfiles->begin(), dbfiles->end(), fsfile);

				if( it!=dbfiles->end()
					&& it->name==fsfile.name
					&& it->isdir==false
					&& it->last_modified==fsfile.last_modified
					&& it->size==fsfile.size )
				{
					fsfile.hash=it->hash;
					needs_hashing=false;
				}
			}

			if(needs_hashing)
			{
				fsfile.hash=getSHA512Binary(filepath+os_file_sep()+fsfile.name);
				calculated_hash=true;
			}
		}
	}
	else if(dbfiles!=NULL)
	{
		for(size_t i=0;i<dbfiles->size();++i)
		{
			SFileAndHash& dbfile = dbfiles->at(i);
			if( dbfile.isdir )
				continue;

			if(!dbfile.hash.empty())
				continue;

			if(skipFile(orig_path+os_file_sep()+dbfile.name, namedpath+os_file_sep()+dbfile.name))
				continue;

			dbfile.hash=getSHA512Binary(filepath+os_file_sep()+dbfile.name);
			calculated_hash=true;
		}
	}

	return calculated_hash;
}

std::vector<SFileAndHash> IndexThread::getFilesProxy(const std::wstring &orig_path, std::wstring path, const std::wstring& named_path, bool use_db/*=true*/)
{
#ifndef _WIN32
	if(path.empty())
	{
		path = os_file_sep();
	}
	std::wstring path_lower=orig_path + os_file_sep();
#else
	std::wstring path_lower=strlower(orig_path+os_file_sep());
#endif

	std::vector<SMDir>::iterator it_dir=changed_dirs.end();
#ifdef _WIN32

	it_dir=std::lower_bound(changed_dirs.begin(), changed_dirs.end(), SMDir(0, path_lower) );
	if(it_dir!=changed_dirs.end() && (*it_dir).name!=path_lower)
		it_dir=changed_dirs.end();
	
	if(path_lower==strlower(Server->getServerWorkingDir())+os_file_sep()+L"urbackup"+os_file_sep())
	{
		use_db=false;
	}
#else
	use_db=false;
#endif
	std::vector<SFileAndHash> tmp;
	if(use_db==false || it_dir!=changed_dirs.end())
	{
		++index_c_fs;

		std::wstring tpath=os_file_prefix(path);

		bool has_error;
		tmp=convertToFileAndHash(getFiles(tpath, &has_error));

		if(has_error)
		{
			if(os_directory_exists(os_file_prefix(index_root_path)))
			{
#ifdef _WIN32
				VSSLog(L"Error while getting files in folder \""+path+L"\". SYSTEM may not have permissions to access this folder. Windows errorcode: "+convert((int)GetLastError()), LL_ERROR);
#else
				VSSLog(L"Error while getting files in folder \""+path+L"\". User may not have permissions to access this folder. Errorno is "+convert(errno), LL_ERROR);
#endif
			}
			else
			{
#ifdef _WIN32
				VSSLog(L"Error while getting files in folder \""+path+L"\". Windows errorcode: "+convert((int)GetLastError())+L". Access to root directory is gone too. Shadow copy was probably deleted while indexing.", LL_ERROR);
#else
				VSSLog(L"Error while getting files in folder \""+path+L"\". Errorno is "+convert(errno)+L". Access to root directory is gone too. Snapshot was probably deleted while indexing.", LL_ERROR);
#endif
				index_error=true;
			}
		}

		std::vector<SFileAndHash> db_files;
		bool has_files=false;
		
#ifndef _WIN32
		if(calculate_filehashes_on_client)
		{
#endif
			has_files = cd->getFiles(path_lower, db_files);
#ifndef _WIN32
		}
#endif

#ifdef _WIN32
		if(it_dir!=changed_dirs.end())
		{
			VSSLog(L"Indexing changed dir: " + path, LL_DEBUG);

			std::vector<std::wstring> changed_files=cd->getChangedFiles((*it_dir).id);
			std::sort(changed_files.begin(), changed_files.end());

			if(!changed_files.empty())
			{
				for(size_t i=0;i<tmp.size();++i)
				{
					if(!tmp[i].isdir)
					{
						if( std::binary_search(changed_files.begin(), changed_files.end(), tmp[i].name) )
						{
							VSSLog(L"Found changed file: " + tmp[i].name, LL_DEBUG);

							tmp[i].last_modified*=Server->getRandomNumber();
							if(tmp[i].last_modified>0)
								tmp[i].last_modified*=-1;
							else if(tmp[i].last_modified==0)
								tmp[i].last_modified=-1;
						}
						else
						{
							std::vector<SFileAndHash>::const_iterator it_db_file=std::lower_bound(db_files.begin(), db_files.end(), tmp[i]);
							if( it_db_file!=db_files.end()
									&& (*it_db_file).name==tmp[i].name
									&& (*it_db_file).isdir==tmp[i].isdir
									&& (*it_db_file).last_modified<0 )
							{
								VSSLog(L"File changed at last backup: "+ tmp[i].name, LL_DEBUG);

								if( tmp[i].last_modified<last_filebackup_filetime)
								{
									tmp[i].last_modified=it_db_file->last_modified;
								}
								else
								{
									VSSLog("Modification time indicates the file may have another change", LL_DEBUG);
									tmp[i].last_modified*=Server->getRandomNumber();
									if(tmp[i].last_modified>0)
										tmp[i].last_modified*=-1;
								}
							}
						}
					}
				}
			}
		}
#endif


		if(calculate_filehashes_on_client)
		{
			addMissingHashes(has_files ? &db_files : NULL, &tmp, orig_path, path, named_path);
		}

		if( has_files)
		{
			if(tmp!=db_files)
			{
				++index_c_db_update;
				modifyFilesInt(path_lower, tmp);
			}
		}
		else
		{
#ifndef _WIN32
			if(calculate_filehashes_on_client)
			{
#endif
				cd->addFiles(path_lower, tmp);
#ifndef _WIN32
			}
#endif
		}

		return tmp;
	}
#ifdef _WIN32
	else
	{	
		if( cd->getFiles(path_lower, tmp) )
		{
			++index_c_db;

			if(calculate_filehashes_on_client)
			{
				if(addMissingHashes(&tmp, NULL, orig_path, path, named_path))
				{
					++index_c_db_update;
					modifyFilesInt(path_lower, tmp);
				}
			}

			return tmp;
		}
		else
		{
			++index_c_fs;

			std::wstring tpath=os_file_prefix(path);

			bool has_error;
			tmp=convertToFileAndHash(getFiles(tpath, &has_error));
			if(has_error)
			{
				if(os_directory_exists(index_root_path))
				{
					VSSLog(L"Error while getting files in folder \""+path+L"\". SYSTEM may not have permissions to access this folder. Windows errorcode: "+convert((int)GetLastError()), LL_ERROR);
				}
				else
				{
					VSSLog(L"Error while getting files in folder \""+path+L"\". Windows errorcode: "+convert((int)GetLastError())+L". Access to root directory is gone too. Shadow copy was probably deleted while indexing.", LL_ERROR);
					index_error=true;
				}
			}

			if(calculate_filehashes_on_client)
			{
				addMissingHashes(NULL, &tmp, orig_path, path, named_path);
			}

			cd->addFiles(path_lower, tmp);
			return tmp;
		}
	}
#endif
}

IPipe * IndexThread::getMsgPipe(void)
{
	return msgpipe;
}

void IndexThread::stopIndex(void)
{
	stop_index=true;
}

#ifdef _WIN32
bool IndexThread::wait_for(IVssAsync *vsasync)
{
	if(vsasync==NULL)
	{
		VSSLog("vsasync is NULL", LL_ERROR);
		return false;
	}

	CHECK_COM_RESULT(vsasync->Wait());

	HRESULT res;
	CHECK_COM_RESULT(vsasync->QueryStatus(&res, NULL));

	while(res==VSS_S_ASYNC_PENDING )
	{
		CHECK_COM_RESULT(vsasync->Wait());

		CHECK_COM_RESULT(vsasync->QueryStatus(&res, NULL));
	}

	if( res!=VSS_S_ASYNC_FINISHED )
	{
		VSSLog("res!=VSS_S_ASYNC_FINISHED CCOM fail", LL_ERROR);
		vsasync->Release();
		return false;
	}
	vsasync->Release();
	return true;
}
#endif

bool IndexThread::start_shadowcopy(SCDirs *dir, bool *onlyref, bool allow_restart, std::vector<SCRef*> no_restart_refs, bool for_imagebackup, bool *stale_shadowcopy)
{
#ifdef _WIN32
#ifdef ENABLE_VSS
	cleanup_saved_shadowcopies(true);

	WCHAR volume_path[MAX_PATH]; 
	BOOL ok = GetVolumePathNameW(dir->orig_target.c_str(), volume_path, MAX_PATH);
	if(!ok)
	{
		VSSLog("GetVolumePathName(dir.target, volume_path, MAX_PATH) failed", LL_ERROR);
		return false;
	}

	std::wstring wpath=volume_path;

	{
		for(size_t i=0;i<sc_refs.size();++i)
		{
			if(sc_refs[i]->target==wpath && sc_refs[i]->ok)
			{
				bool do_restart=true;
				bool found_in_no_restart_refs=false;
				for(size_t k=0;k<no_restart_refs.size();++k)
				{
					if(no_restart_refs[k]==sc_refs[i])
					{
						found_in_no_restart_refs=true;
						break;
					}
				}

				if(found_in_no_restart_refs)
				{
					do_restart=false;
				}

				bool only_own_tokens=true;
				for(size_t k=0;k<sc_refs[i]->starttokens.size();++k)
				{
					int64 last_token_time = ClientConnector::getLastTokenTime(sc_refs[i]->starttokens[k]);
					int64 curr_time = Server->getTimeSeconds();
					bool token_timeout=true;
					if(curr_time>=last_token_time && curr_time-last_token_time<10*60*1000)
					{
						token_timeout=false;
					}
					if( sc_refs[i]->starttokens[k]!=starttoken && !token_timeout)
					{
						only_own_tokens=false;
						break;
					}
				}

				bool cannot_open_shadowcopy = false;

				IFile *volf=Server->openFile(sc_refs[i]->volpath, MODE_READ);
				if(volf==NULL)
				{
					if(!do_restart)
					{
						VSSLog("Cannot open shadowcopy. Creating new or choosing other.", LL_WARNING);
						continue;
					}
					else
					{
						VSSLog("Removing reference because shadowcopy could not be openend", LL_WARNING);
						cannot_open_shadowcopy=true;
					}
				}
				else
				{
					Server->destroy(volf);
				}

				if( do_restart && allow_restart && (Server->getTimeSeconds()-sc_refs[i]->starttime>shadowcopy_startnew_timeout/1000
													|| only_own_tokens 
													|| cannot_open_shadowcopy ) )
				{
					if( only_own_tokens)
					{
						VSSLog(L"Restarting shadow copy of " + sc_refs[i]->target + L" because it was started by this server", LL_WARNING);
					}
					else
					{
						VSSLog(L"Restarting/not using already existing shadow copy of " + sc_refs[i]->target + L" because it is too old", LL_INFO);
					}

					SCRef *curr=sc_refs[i];
					std::map<std::wstring, SCDirs*>& scdirs_server = scdirs[starttoken];
					for(std::map<std::wstring, SCDirs*>::iterator it=scdirs_server.begin();
						it!=scdirs_server.end();)
					{
						std::map<std::wstring, SCDirs*>::iterator nextit = it;
						++nextit;
						if(it->second->ref==curr)
						{
							VSSLog(L"Releasing "+it->first+L" orig_target="+it->second->orig_target+L" target="+it->second->target, LL_DEBUG);
							release_shadowcopy(it->second, false, -1, dir);
						}
						it=nextit;
					}
					dir->target=dir->orig_target;
					continue;
				}
				else if(!cannot_open_shadowcopy)
				{
					dir->ref=sc_refs[i];
					if(!dir->ref->dontincrement)
					{
						dir->ref->rcount++;
						sc_refs[i]->starttokens.push_back(starttoken);
					}
					else
					{
						dir->ref->dontincrement=false;
					}

					VSSLog(L"orig_target="+dir->orig_target+L" volpath="+dir->ref->volpath, LL_DEBUG);

					dir->target=dir->orig_target;
					dir->target.erase(0,wpath.size());

					dir->target=dir->ref->volpath+os_file_sep()+dir->target;
					if(dir->fileserv)
					{
						shareDir(widen(starttoken), dir->dir, dir->target);
					}

					if(for_imagebackup && dir->ref->save_id!=-1)
					{
						cd->modShadowcopyRefCount(dir->ref->save_id, 1);
					}

					if(onlyref!=NULL)
					{
						*onlyref=true;
					}

					if( stale_shadowcopy!=NULL )
					{
						if(found_in_no_restart_refs)
						{
							*stale_shadowcopy=false;
						}
						else if(!only_own_tokens || !allow_restart)
						{
							*stale_shadowcopy=true;
						}
					}

					VSSLog("Shadowcopy already present.", LL_DEBUG);
					return true;
				}
			}
		}

		dir->ref=new SCRef;
		dir->ref->rcount=1;
		dir->ref->starttime=Server->getTimeSeconds();
		dir->ref->target=wpath;
		dir->ref->starttokens.push_back(starttoken);
		sc_refs.push_back(dir->ref);
	}

	if(dir->ref->rcount!=1)
	{
		VSSLog("Error rcount!=1", LL_ERROR);
	}

	int tries=3;
	bool retryable_error=true;
	IVssBackupComponents *backupcom=NULL; 
	while(tries>0 && retryable_error)
	{		
		CHECK_COM_RESULT_RELEASE(CreateVssBackupComponents(&backupcom));

		if(!backupcom)
		{
			VSSLog("backupcom is NULL", LL_ERROR);
			return false;
		}

		CHECK_COM_RESULT_RELEASE(backupcom->InitializeForBackup());

		CHECK_COM_RESULT_RELEASE(backupcom->SetBackupState(FALSE, TRUE, VSS_BT_FULL, FALSE));

		IVssAsync *pb_result;

		CHECK_COM_RESULT_RELEASE(backupcom->GatherWriterMetadata(&pb_result));
		wait_for(pb_result);

	#ifndef VSS_XP
	#ifndef VSS_S03
		CHECK_COM_RESULT_RELEASE(backupcom->SetContext(VSS_CTX_APP_ROLLBACK) );
	#endif
	#endif

		std::wstring errmsg;

		retryable_error=false;
		check_writer_status(backupcom, errmsg, LL_ERROR, &retryable_error);
	
		bool b_ok=true;
		int tries_snapshot_set=5;
		while(b_ok==true)
		{
			HRESULT r;
			CHECK_COM_RESULT_OK_HR(backupcom->StartSnapshotSet(&dir->ref->ssetid), b_ok, r);
			if(b_ok)
			{
				break;
			}

			if(b_ok==false && tries_snapshot_set>=0 && r==VSS_E_SNAPSHOT_SET_IN_PROGRESS )
			{
				VSSLog("Retrying starting shadow copy in 30s", LL_WARNING);
				b_ok=true;
				--tries_snapshot_set;
			}
			Server->wait(30000);
		}

		if(!b_ok)
		{
			CHECK_COM_RESULT_RELEASE(backupcom->StartSnapshotSet(&dir->ref->ssetid));
		}

		CHECK_COM_RESULT_RELEASE(backupcom->AddToSnapshotSet(volume_path, GUID_NULL, &dir->ref->ssetid) );
	
		CHECK_COM_RESULT_RELEASE(backupcom->PrepareForBackup(&pb_result));
		wait_for(pb_result);

		retryable_error=false;
		check_writer_status(backupcom, errmsg, LL_ERROR, &retryable_error);

		CHECK_COM_RESULT_RELEASE(backupcom->DoSnapshotSet(&pb_result));
		wait_for(pb_result);

		retryable_error=false;

		bool snapshot_ok=false;
		if(tries>1)
		{
			snapshot_ok = check_writer_status(backupcom, errmsg, LL_ERROR, &retryable_error);
		}
		else
		{
			snapshot_ok = check_writer_status(backupcom, errmsg, LL_ERROR, NULL);
		}
		--tries;
		if(!snapshot_ok && !retryable_error)
		{
			VSSLog("Writer is in error state during snapshot creation. Writer data may not be consistent.", LL_ERROR);
			break;
		}
		else if(!snapshot_ok)
		{
			if(tries==0)
			{
				VSSLog("Creating snapshot failed after three tries. Giving up. Writer data may not be consistent.", LL_ERROR);
				break;
			}
			else
			{
				VSSLog("Snapshotting failed because of Writer. Retrying in 30s...", LL_WARNING);
			}
			bool bcom_ok=true;
			CHECK_COM_RESULT_OK(backupcom->BackupComplete(&pb_result), bcom_ok);
			if(bcom_ok)
			{
				wait_for(pb_result);
			}

#ifndef VSS_XP
#ifndef VSS_S03
			if(bcom_ok)
			{
				LONG dels; 
				GUID ndels;
				CHECK_COM_RESULT_OK(backupcom->DeleteSnapshots(dir->ref->ssetid, VSS_OBJECT_SNAPSHOT, TRUE, 
					&dels, &ndels), bcom_ok);

				if(dels==0)
				{
					VSSLog("Deleting shadowcopy failed.", LL_ERROR);
				}
			}
#endif
#endif
			backupcom->Release();
			backupcom=NULL;

			Server->wait(30000);			
		}
		else
		{
			break;
		}
	}

	VSS_SNAPSHOT_PROP snap_props; 
    CHECK_COM_RESULT_RELEASE(backupcom->GetSnapshotProperties(dir->ref->ssetid, &snap_props));

	dir->target.erase(0,wpath.size());
	dir->ref->volpath=(std::wstring)snap_props.m_pwszSnapshotDeviceObject;
	dir->starttime=Server->getTimeSeconds();
	dir->target=dir->ref->volpath+os_file_sep()+dir->target;
	if(dir->fileserv)
	{
		shareDir(widen(starttoken), dir->dir, dir->target);
	}

	SShadowCopy tsc;
	tsc.vssid=snap_props.m_SnapshotId;
	tsc.ssetid=snap_props.m_SnapshotSetId;
	tsc.target=dir->orig_target;
	tsc.path=(std::wstring)snap_props.m_pwszSnapshotDeviceObject;
	tsc.orig_target=dir->orig_target;
	tsc.filesrv=dir->fileserv;
	tsc.vol=wpath;
	tsc.tname=dir->dir;
	tsc.starttoken=widen(starttoken);
	if(for_imagebackup)
	{
		tsc.refs=1;
	}
	dir->ref->save_id=cd->addShadowcopy(tsc);
	dir->ref->ok=true;

	VSSLog(L"Shadowcopy path: "+tsc.path, LL_DEBUG);

	dir->ref->ssetid=snap_props.m_SnapshotId;

	VssFreeSnapshotProperties(&snap_props);

	dir->ref->backupcom=backupcom;
	if(onlyref!=NULL)
	{
		*onlyref=false;
	}
	return true;
#else
	return false;
#endif
#else
	return false;
#endif
}

bool IndexThread::release_shadowcopy(SCDirs *dir, bool for_imagebackup, int save_id, SCDirs *dontdel)
{
#ifdef _WIN32
#ifdef ENABLE_VSS

	if(for_imagebackup)
	{
		if(dir->ref!=NULL && dir->ref->save_id!=-1)
		{
			cd->modShadowcopyRefCount(dir->ref->save_id, -1);
		}
		else if(save_id!=-1)
		{
			cd->modShadowcopyRefCount(save_id, -1);
		}
	}

	bool has_dels=false;
	bool ok=false;

	if(dir->ref!=NULL && dir->ref->backupcom!=NULL)
	{
		if(dir->ref->rcount<=1 || Server->getTimeSeconds()-dir->ref->starttime>shadowcopy_timeout/1000)
		{
			IVssBackupComponents *backupcom=dir->ref->backupcom;
			IVssAsync *pb_result;
			bool bcom_ok=true;
			CHECK_COM_RESULT_OK(backupcom->BackupComplete(&pb_result), bcom_ok);
			if(bcom_ok)
			{
				wait_for(pb_result);
			}

			std::wstring errmsg;
			check_writer_status(backupcom, errmsg, LL_ERROR, NULL);

			VSSLog(L"Deleting shadowcopy for path \""+dir->target+L"\" -2", LL_DEBUG);
			
			if(dir->ref->save_id!=-1)
			{
				cd->deleteShadowcopy(dir->ref->save_id);
			}

#ifndef VSS_XP
#ifndef VSS_S03
#ifndef SERVER_ONLY

			if(bcom_ok)
			{
				LONG dels; 
				GUID ndels; 
				CHECK_COM_RESULT_OK(backupcom->DeleteSnapshots(dir->ref->ssetid, VSS_OBJECT_SNAPSHOT, TRUE, 
					&dels, &ndels), bcom_ok);

				if(dels==0)
				{
					VSSLog("Deleting shadowcopy failed.", LL_ERROR);
				}
				else
				{
					ok=true;
				}
				has_dels=true;
			}
#endif
#endif
#endif
#if defined(VSS_XP) || defined(VSS_S03)
			ok=true;
#endif

			backupcom->Release();
			dir->ref->backupcom=NULL;
			dir->ref->rcount=1;			
		}
		--dir->ref->rcount;
	}
	if(dir->ref!=NULL)
	{
		for(size_t k=0;k<dir->ref->starttokens.size();++k)
		{
			if(dir->ref->starttokens[k]==starttoken)
			{
				dir->ref->starttokens.erase(dir->ref->starttokens.begin()+k);
				break;
			}
		}
	}

	cleanup_saved_shadowcopies();

	{

#if defined(VSS_XP) || defined(VSS_S03)
		std::vector<SShadowCopy> scs=cd->getShadowcopies();

		bool found=false;

		for(size_t i=0;i<scs.size();++i)
		{
			if( scs[i].target==dir->target || ( dir->ref!=NULL && dir->ref->backupcom==NULL )  )
			{
				found=true;
			}
		}

		if(found)
		{
			for(size_t i=0;i<scs.size();++i)
			{
				if(scs[i].target==dir->target || ( dir->ref!=NULL && dir->ref->backupcom==NULL ) )
				{
					VSSLog(L"Removing shadowcopy entry for path \""+scs[i].path+L"\"", LL_DEBUG);
					cd->deleteShadowcopy(scs[i].id);
				}
			}
		}
#endif
	}

	

	bool r=true;
	while(r)
	{
		r=false;
		for(size_t i=0;i<sc_refs.size();++i)
		{
			if(sc_refs[i]->rcount<=0)
			{
				VSSLog(L"Deleting Shadowcopy for dir \""+sc_refs[i]->target+L"\"", LL_DEBUG);
				bool c=true;
				while(c)
				{
					c=false;
					for(std::map<std::string, std::map<std::wstring, SCDirs*> >::iterator server_it = scdirs.begin();
						server_it!=scdirs.end();++server_it)
					{
						for(std::map<std::wstring, SCDirs*>::iterator it=server_it->second.begin();
							it!=server_it->second.end();++it)
						{
							if(it->second->ref==sc_refs[i])
							{
								if(it->second->fileserv)
								{
									shareDir(widen(server_it->first), it->second->dir, it->second->orig_target);
								}
								it->second->target=it->second->orig_target;

								it->second->ref=NULL;
								if(dontdel==NULL || it->second!=dontdel )
								{
									delete it->second;
									server_it->second.erase(it);
									c=true;
									break;
								}
							}
						}
					}					
				}
				delete sc_refs[i];
				sc_refs.erase(sc_refs.begin()+i);
				r=true;
				break;
			}
		}
	}

	
	if(has_dels)
	{
		return ok;
	}
	else
	{
		return true;
	}
#else
	return true;
#endif
#else
	return true;
#endif
}

bool IndexThread::cleanup_saved_shadowcopies(bool start)
{
#ifdef _WIN32
#ifdef ENABLE_VSS
#ifndef VSS_XP
#ifndef VSS_S03
#ifndef SERVER_ONLY
		std::vector<SShadowCopy> scs=cd->getShadowcopies();

		bool found=false;

		for(size_t i=0;i<scs.size();++i)
		{
			bool f2=true;
			for(size_t j=0;j<sc_refs.size();++j)
			{
				if(sc_refs[j]->save_id==scs[i].id)
				{
					f2=false;
					break;
				}
			}
			if(f2==true && (scs[i].refs<=0 || scs[i].passedtime>shadowcopy_timeout/1000 || (start && scs[i].filesrv==false && scs[i].refs==1 && !starttoken.empty() && scs[i].starttoken==widen(starttoken) ) ) )
			{
				found=true;
				break;
			}
		}


		if(found)
		{
			bool ok=false;
			IVssBackupComponents *backupcom=NULL; 
			CHECK_COM_RESULT_RELEASE(CreateVssBackupComponents(&backupcom));
			CHECK_COM_RESULT_RELEASE(backupcom->InitializeForBackup());
			CHECK_COM_RESULT_RELEASE(backupcom->SetContext(VSS_CTX_APP_ROLLBACK) );
			
			for(size_t i=0;i<scs.size();++i)
			{
				bool f2=true;
				for(size_t j=0;j<sc_refs.size();++j)
				{
					if(sc_refs[j]->save_id==scs[i].id)
					{
						f2=false;
						break;
					}
				}
				if( f2==true && (scs[i].refs<=0 || scs[i].passedtime>shadowcopy_timeout/1000 || (start && scs[i].filesrv==false && scs[i].refs==1 && !starttoken.empty() &&scs[i].starttoken==widen(starttoken) ) ) )
				{
					VSSLog(L"Deleting shadowcopy for path \""+scs[i].path+L"\"", LL_DEBUG);
					LONG dels; 
					GUID ndels; 
					CHECK_COM_RESULT(backupcom->DeleteSnapshots(scs[i].vssid, VSS_OBJECT_SNAPSHOT, TRUE, 
						&dels, &ndels));
					cd->deleteShadowcopy(scs[i].id);
					if(dels>0)
						ok=true;
				}
			}

			backupcom->Release();
			return ok;
		}
#endif
#endif
#endif
#endif //ENABLE_VSS
#endif //_WIN32
		return true;
}

#ifdef _WIN32
#ifdef ENABLE_VSS

bool IndexThread::checkErrorAndLog(BSTR pbstrWriter, VSS_WRITER_STATE pState, HRESULT pHrResultFailure, std::wstring& errmsg, int loglevel, bool* retryable_error)
{
#define FAIL_STATE(x) case x: { state=L#x; failure=true; } break
#define OK_STATE(x) case x: { state=L#x; } break

	std::wstring state;
	bool failure=false;
	switch(pState)
	{
		FAIL_STATE(VSS_WS_UNKNOWN);
		FAIL_STATE(VSS_WS_FAILED_AT_IDENTIFY);
		FAIL_STATE(VSS_WS_FAILED_AT_PREPARE_BACKUP);
		FAIL_STATE(VSS_WS_FAILED_AT_PREPARE_SNAPSHOT);
		FAIL_STATE(VSS_WS_FAILED_AT_FREEZE);
		FAIL_STATE(VSS_WS_FAILED_AT_THAW);
		FAIL_STATE(VSS_WS_FAILED_AT_POST_SNAPSHOT);
		FAIL_STATE(VSS_WS_FAILED_AT_BACKUP_COMPLETE);
		FAIL_STATE(VSS_WS_FAILED_AT_PRE_RESTORE);
		FAIL_STATE(VSS_WS_FAILED_AT_POST_RESTORE);
#ifndef VSS_XP
#ifndef VSS_S03
		FAIL_STATE(VSS_WS_FAILED_AT_BACKUPSHUTDOWN);
#endif
#endif
		OK_STATE(VSS_WS_STABLE);
		OK_STATE(VSS_WS_WAITING_FOR_FREEZE);
		OK_STATE(VSS_WS_WAITING_FOR_THAW);
		OK_STATE(VSS_WS_WAITING_FOR_POST_SNAPSHOT);
		OK_STATE(VSS_WS_WAITING_FOR_BACKUP_COMPLETE);
	}

#undef FAIL_STATE
#undef OK_STATE

	std::wstring err;
	bool has_error=false;
#define HR_ERR(x) case x: { err=L#x; has_error=true; } break
	switch(pHrResultFailure)
	{
	case S_OK: { err=L"S_OK"; } break;
		HR_ERR(VSS_E_WRITERERROR_INCONSISTENTSNAPSHOT);
		HR_ERR(VSS_E_WRITERERROR_OUTOFRESOURCES);
		HR_ERR(VSS_E_WRITERERROR_TIMEOUT);
		HR_ERR(VSS_E_WRITERERROR_RETRYABLE);
		HR_ERR(VSS_E_WRITERERROR_NONRETRYABLE);
		HR_ERR(VSS_E_WRITER_NOT_RESPONDING);
#ifndef VSS_XP
#ifndef VSS_S03
		HR_ERR(VSS_E_WRITER_STATUS_NOT_AVAILABLE);
#endif
#endif
	}
#undef HR_ERR

	std::wstring writerName;
	if(pbstrWriter)
		writerName=pbstrWriter;
	else
		writerName=L"(NULL)";

	if(failure || has_error)
	{
		const std::wstring erradd=L". UrBackup will continue with the backup but the associated data may not be consistent.";
		std::wstring nerrmsg=L"Writer "+writerName+L" has failure state "+state+L" with error "+err;
		if(retryable_error && pHrResultFailure==VSS_E_WRITERERROR_RETRYABLE)
		{
			loglevel=LL_INFO;
			*retryable_error=true;
		}
		else
		{
			nerrmsg+=erradd;
		}
		VSSLog(nerrmsg, loglevel);
		errmsg+=nerrmsg;
		return false;
	}
	else
	{
		VSSLog(L"Writer "+writerName+L" has failure state "+state+L" with error "+err+L".", LL_DEBUG);
	}

	return true;
}


bool IndexThread::check_writer_status(IVssBackupComponents *backupcom, std::wstring& errmsg, int loglevel, bool* retryable_error)
{
	IVssAsync *pb_result;
	CHECK_COM_RESULT_RETURN(backupcom->GatherWriterStatus(&pb_result));

	if(!wait_for(pb_result))
	{
		VSSLog("Error while waiting for result from GatherWriterStatus", LL_ERROR);
		return false;
	}

	UINT nWriters;
	CHECK_COM_RESULT_RETURN(backupcom->GetWriterStatusCount(&nWriters));

	VSSLog("Number of Writers: "+nconvert(nWriters), LL_DEBUG);

	bool has_error=false;
	for(UINT i=0;i<nWriters;++i)
	{
		VSS_ID pidInstance;
		VSS_ID pidWriter;
		BSTR pbstrWriter;
		VSS_WRITER_STATE pState;
		HRESULT pHrResultFailure;

		bool ok=true;
		CHECK_COM_RESULT_OK(backupcom->GetWriterStatus(i,
													&pidInstance,
													&pidWriter,
													&pbstrWriter,
													&pState,
													&pHrResultFailure), ok);

		if(ok)
		{
			if(!checkErrorAndLog(pbstrWriter, pState, pHrResultFailure, errmsg, loglevel, retryable_error))
			{
				has_error=true;
			}

			SysFreeString(pbstrWriter);
		}
	}

	CHECK_COM_RESULT_RETURN(backupcom->FreeWriterStatus());

	return !has_error;
}
#endif //ENABLE_VSS
#endif //_WIN32

#ifdef _WIN32
std::string IndexThread::GetErrorHResErrStr(HRESULT res)
{
	switch(res)
	{
	case E_INVALIDARG:
		return "E_INVALIDARG";
	case E_OUTOFMEMORY:
		return "E_OUTOFMEMORY";
	case E_UNEXPECTED:
		return "E_UNEXPECTED";
	case E_ACCESSDENIED:
		return "E_ACCESSDENIED";
	case VSS_E_OBJECT_NOT_FOUND:
		return "VSS_E_OBJECT_NOT_FOUND";
	case VSS_E_PROVIDER_VETO:
		return "VSS_E_PROVIDER_VETO";
	case VSS_E_UNEXPECTED_PROVIDER_ERROR:
		return "VSS_E_UNEXPECTED_PROVIDER_ERROR";
	case VSS_E_BAD_STATE:
		return "VSS_E_BAD_STATE";
	case VSS_E_SNAPSHOT_SET_IN_PROGRESS:
		return "VSS_E_SNAPSHOT_SET_IN_PROGRESS";
	case VSS_E_MAXIMUM_NUMBER_OF_VOLUMES_REACHED:
		return "VSS_E_MAXIMUM_NUMBER_OF_VOLUMES_REACHED";
	case VSS_E_MAXIMUM_NUMBER_OF_SNAPSHOTS_REACHED:
		return "VSS_E_MAXIMUM_NUMBER_OF_SNAPSHOTS_REACHED";
	case VSS_E_PROVIDER_NOT_REGISTERED:
		return "VSS_E_PROVIDER_NOT_REGISTERED";
	case VSS_E_VOLUME_NOT_SUPPORTED:
		return "VSS_E_VOLUME_NOT_SUPPORTED";
	case VSS_E_VOLUME_NOT_SUPPORTED_BY_PROVIDER:
		return "VSS_E_VOLUME_NOT_SUPPORTED_BY_PROVIDER";
	};
	return "UNDEF";
}
#endif

std::string IndexThread::lookup_shadowcopy(int sid)
{
#ifdef _WIN32
#ifndef SERVER_ONLY
	std::vector<SShadowCopy> scs=cd->getShadowcopies();

	for(size_t i=0;i<scs.size();++i)
	{
		if(scs[i].id==sid)
		{
			IVssBackupComponents *backupcom=NULL; 
			CHECK_COM_RESULT_RELEASE_S(CreateVssBackupComponents(&backupcom));
			CHECK_COM_RESULT_RELEASE_S(backupcom->InitializeForBackup());
			CHECK_COM_RESULT_RELEASE_S(backupcom->SetContext(VSS_CTX_APP_ROLLBACK) );
			VSS_SNAPSHOT_PROP snap_props; 
			CHECK_COM_RESULT_RELEASE_S(backupcom->GetSnapshotProperties(scs[i].vssid, &snap_props));
			std::string ret=Server->ConvertToUTF8(snap_props.m_pwszSnapshotDeviceObject);
			VssFreeSnapshotProperties(&snap_props);
			backupcom->Release();
			return ret;
		}
	}
#endif
#endif
	return "";
}

SCDirs* IndexThread::getSCDir(const std::wstring path)
{
	std::map<std::wstring, SCDirs*>& scdirs_server = scdirs[starttoken];
	std::map<std::wstring, SCDirs*>::iterator it=scdirs_server.find(path);
	if(it!=scdirs_server.end())
	{
		return it->second;
	}
	else
	{
		SCDirs *nd=new SCDirs;
		scdirs_server.insert(std::pair<std::wstring, SCDirs*>(path, nd) );

		nd->running=false;

		return nd;
	}
}

IFileServ *IndexThread::getFileSrv(void)
{
	IScopedLock lock(filesrv_mutex);
	return filesrv;
}

void IndexThread::execute_prebackup_hook(void)
{
#ifdef _WIN32
	system(Server->ConvertToUTF8(L"\""+Server->getServerWorkingDir()+L"\\prefilebackup.bat\"").c_str());
#else
	system("/etc/urbackup/prefilebackup");
#endif
}

void IndexThread::execute_postindex_hook(void)
{
#ifdef _WIN32
	system(Server->ConvertToUTF8(L"\""+Server->getServerWorkingDir()+L"\\postfileindex.bat\"").c_str());
#else
	system("/etc/urbackup/postfileindex");
#endif
}

void IndexThread::execute_postbackup_hook(void)
{
#ifdef _WIN32
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(STARTUPINFO) );
	memset(&pi, 0, sizeof(PROCESS_INFORMATION) );
	si.cb=sizeof(STARTUPINFO);
	if(!CreateProcessW(L"C:\\Windows\\system32\\cmd.exe", (LPWSTR)(L"cmd.exe /C \""+Server->getServerWorkingDir()+L"\\postfilebackup.bat\"").c_str(), NULL, NULL, false, NORMAL_PRIORITY_CLASS|CREATE_NO_WINDOW, NULL, NULL, &si, &pi) )
	{
		Server->Log("Executing postfilebackup.bat failed: "+nconvert((int)GetLastError()), LL_INFO);
	}
	else
	{
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
#else
	pid_t pid1;
	pid1 = fork();
	if( pid1==0 )
	{
		setsid();
		pid_t pid2;
		pid2 = fork();
		if(pid2==0)
		{
			char *a1=(char*)"/etc/urbackup/postfilebackup";
			char* const argv[]={ a1, NULL };
			execv(a1, argv);
			Server->Log("Error in execv /etc/urbackup/postfilebackup: "+nconvert(errno), LL_INFO);
			exit(1);
		}
		else
		{
			exit(1);
		}
	}
	else
	{
		int status;
		waitpid(pid1, &status, 0);
	}
#endif
}

std::wstring IndexThread::sanitizePattern(const std::wstring &p)
{
	std::wstring ep=trim(p);
	std::wstring nep;
	nep.reserve(ep.size()*2);
	for(size_t j=0;j<ep.size();++j)
	{
		wchar_t ch=ep[j];
		if(ch=='/')
		{
			if(os_file_sep()==L"\\")
			{
				nep+=L"\\\\";
			}
			else
			{
				nep+=os_file_sep();
			}
		}
		else if(ch=='\\' && j+1<ep.size() && ep[j+1]=='\\')
		{
			if(os_file_sep()==L"\\")
			{
				nep+=L"\\\\";
			}
			else
			{
				nep+=os_file_sep();
			}
			++j;
		}
		else if(ch=='\\' && ( j+1>=ep.size() || (ep[j+1]!='[' ) ) )
		{
			if(os_file_sep()==L"\\")
				nep+=L"\\\\";
			else
				nep+=os_file_sep();
		}
		else
		{
			nep+=ch;
		}
	}
	return nep;
}

void IndexThread::readPatterns(bool &pattern_changed, bool update_saved_patterns)
{
	ISettingsReader *curr_settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");
	exlude_dirs.clear();
	if(curr_settings!=NULL)
	{	
		std::wstring val;
		if(curr_settings->getValue(L"exclude_files", &val) || curr_settings->getValue(L"exclude_files_def", &val) )
		{
			if(val!=cd->getOldExcludePattern())
			{
				pattern_changed=true;
				if(update_saved_patterns)
				{
					cd->updateOldExcludePattern(val);
				}
			}

			std::vector<std::wstring> toks;
			Tokenize(val, toks, L";");
			exlude_dirs=toks;
#ifdef _WIN32
			for(size_t i=0;i<exlude_dirs.size();++i)
			{
				strupper(&exlude_dirs[i]);
			}
#endif
			for(size_t i=0;i<exlude_dirs.size();++i)
			{
				if(exlude_dirs[i].find('\\')==std::wstring::npos
					&& exlude_dirs[i].find('/')==std::wstring::npos
					&& exlude_dirs[i].find('*')==std::wstring::npos )
				{
					exlude_dirs[i]=L"*/"+trim(exlude_dirs[i]);
				}
			}
			for(size_t i=0;i<exlude_dirs.size();++i)
			{
				exlude_dirs[i]=sanitizePattern(exlude_dirs[i]);
			}
			
			addFileExceptions();
		}
		else
		{
			addFileExceptions();
		}

		if(curr_settings->getValue(L"include_files", &val) || curr_settings->getValue(L"include_files_def", &val) )
		{
			if(val!=cd->getOldIncludePattern())
			{
				pattern_changed=true;
				if(update_saved_patterns)
				{
					cd->updateOldIncludePattern(val);
				}
			}

			std::vector<std::wstring> toks;
			Tokenize(val, toks, L";");
			include_dirs=toks;
#ifdef _WIN32
			for(size_t i=0;i<include_dirs.size();++i)
			{
				strupper(&include_dirs[i]);
			}
#endif
			for(size_t i=0;i<include_dirs.size();++i)
			{
				include_dirs[i]=sanitizePattern(include_dirs[i]);
			}
			include_depth.resize(include_dirs.size());
			for(size_t i=0;i<include_dirs.size();++i)
			{
				std::wstring ip=include_dirs[i];
				if(ip.find(L"*")==ip.size()-1 || ip.find(L"*")==std::string::npos)
				{
					int depth=0;
					for(size_t j=0;j<ip.size();++j)
					{
						if(ip[j]=='/')
							++depth;
						else if(ip[j]=='\\' && j+1<ip.size() && ip[j+1]=='\\')
						{
							++j;
							++depth;
						}
					}
					include_depth[i]=depth;
				}
				else
				{
					include_depth[i]=-1;
				}
			}
			include_prefix.resize(include_dirs.size());
			for(size_t i=0;i<include_dirs.size();++i)
			{
				size_t f1=include_dirs[i].find_first_of(L":");
				size_t f2=include_dirs[i].find_first_of(L"[");
				size_t f3=include_dirs[i].find_first_of(L"*");
				while(f2>0 && f2!=std::string::npos && include_dirs[i][f2-1]=='\\')
					f2=include_dirs[i].find_first_of(L"[", f2);

				size_t f=(std::min)((std::min)(f1,f2), f3);

				if(f!=std::string::npos)
				{
					if(f>0)
					{
						include_prefix[i]=include_dirs[i].substr(0, f);
					}
				}
				else
				{
					include_prefix[i]=include_dirs[i];
				}

				std::wstring nep;
				for(size_t j=0;j<include_prefix[i].size();++j)
				{
					wchar_t ch=include_prefix[i][j];
					if(ch=='/')
						nep+=os_file_sep();
					else if(ch=='\\' && j+1<include_prefix[i].size() && include_prefix[i][j+1]=='\\')
					{
						nep+=os_file_sep();
						++j;
					}
					else
					{
					        nep+=ch;
					}
				}			
				
				include_prefix[i]=nep;
			}

		}
		Server->destroy(curr_settings);
	}
	else
	{
		addFileExceptions();
	}
}

bool amatch(const wchar_t *str, const wchar_t *p);

bool IndexThread::isExcluded(const std::wstring &path)
{
	std::wstring wpath=path;
#ifdef _WIN32
	strupper(&wpath);
#endif
	for(size_t i=0;i<exlude_dirs.size();++i)
	{
		if(!exlude_dirs[i].empty())
		{
			bool b=amatch(wpath.c_str(), exlude_dirs[i].c_str());
			if(b)
			{
				return true;
			}
		}
	}
	return false;
}

bool IndexThread::isIncluded(const std::wstring &path, bool *adding_worthless)
{
	std::wstring wpath=path;
#ifdef _WIN32
	strupper(&wpath);
#endif
	int wpath_level=0;
	if(adding_worthless!=NULL)
	{
		for(size_t i=0;i<wpath.size();++i)
		{
			if(wpath[i]=='/')
				++wpath_level;
			else if(wpath[i]=='\\')
				++wpath_level;
			else if(i==wpath.size()-1)
				++wpath_level;
		}
		*adding_worthless=true;
	}
	bool has_pattern=false;
	for(size_t i=0;i<include_dirs.size();++i)
	{
		if(!include_dirs[i].empty())
		{
			has_pattern=true;
			bool b=amatch(wpath.c_str(), include_dirs[i].c_str());
			if(b)
			{
				return true;
			}
			if(adding_worthless!=NULL)
			{
				if( include_depth[i]==-1 )
				{
					*adding_worthless=false;
				}
				else
				{
					bool has_prefix=(wpath.find(include_prefix[i])==0);
					if( has_prefix )
					{
						if( wpath_level<=include_depth[i])
						{
							*adding_worthless=false;
						}
					}
				}
			}
		}
	}
	return !has_pattern;
}

void IndexThread::start_filesrv(void)
{
	std::wstring name;
	if(Server->getServerParameter("restore_mode")=="true")
	{
		name=L"##restore##"+convert(Server->getTimeSeconds())+convert(Server->getRandomNumber()%10000);
		writestring(Server->ConvertToUTF8(name), "clientname.txt");
	}
	else
	{
		ISettingsReader *curr_settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");
		if(curr_settings!=NULL)
		{
			std::wstring val;
			if(curr_settings->getValue(L"computername", &val))
			{
				if(!val.empty())
				{
					name=val;
				}
			}
			Server->destroy(curr_settings);
		}
	}

	unsigned int curr_tcpport = tcpport;
	unsigned int curr_udpport = udpport;
	std::string s_tcpport=Server->getServerParameter("fileserv_tcpport");
	if(!s_tcpport.empty())
		curr_tcpport=atoi(s_tcpport.c_str());
	std::string s_udpport=Server->getServerParameter("fileserv_udpport");
	if(!s_udpport.empty())
		curr_udpport=atoi(s_udpport.c_str());

	filesrv=((IFileServFactory*)(Server->getPlugin(Server->getThreadID(), filesrv_pluginid)))->createFileServ(curr_tcpport, curr_udpport, name);
	filesrv->shareDir(L"urbackup", Server->getServerWorkingDir()+L"/urbackup/data");

	ServerIdentityMgr::setFileServ(filesrv);
	ServerIdentityMgr::loadServerIdentities();
}

void IndexThread::shareDir(const std::wstring& token, std::wstring name, const std::wstring &path)
{
	if(!token.empty())
	{
		name = token + L"|" +name;
	}
	IScopedLock lock(filesrv_mutex);
	filesrv_share_dirs[name]=path;
}

void IndexThread::removeDir(const std::wstring& token, std::wstring name)
{
	if(!token.empty())
	{
		name = token + L"|" +name;
	}
	IScopedLock lock(filesrv_mutex);
	std::map<std::wstring, std::wstring>::iterator it=filesrv_share_dirs.find(name);
	if(it!=filesrv_share_dirs.end())
	{
		filesrv_share_dirs.erase(it);
	}
}

std::wstring IndexThread::getShareDir(const std::wstring &name)
{
	IScopedLock lock(filesrv_mutex);
	return filesrv_share_dirs[name];
}

void IndexThread::share_dirs()
{
	IScopedLock lock(filesrv_mutex);
	for(std::map<std::wstring, std::wstring>::iterator it=filesrv_share_dirs.begin();it!=filesrv_share_dirs.end();++it)
	{
		std::wstring dir=it->first;
		filesrv->shareDir(dir, it->second);
	}
}

void IndexThread::unshare_dirs()
{
	IScopedLock lock(filesrv_mutex);
	for(std::map<std::wstring, std::wstring>::iterator it=filesrv_share_dirs.begin();it!=filesrv_share_dirs.end();++it)
	{
		std::wstring dir=it->first;
		filesrv->removeDir(dir);
	}
}

void IndexThread::doStop(void)
{
	CWData wd;
	wd.addUChar(8);
	wd.addVoidPtr(NULL);
	msgpipe->Write(wd.getDataPtr(), wd.getDataSize());
}

void IndexThread::modifyFilesInt(std::wstring path, const std::vector<SFileAndHash> &data)
{
	size_t add_size=path.size()*sizeof(wchar_t)+sizeof(std::wstring);
	for(size_t i=0;i<data.size();++i)
	{
		add_size+=data[i].name.size()*sizeof(wchar_t);
		add_size+=sizeof(SFileAndHash);
		add_size+=data[i].hash.size();
	}
	add_size+=sizeof(std::vector<SFile>);

	modify_file_buffer_size+=add_size;

	modify_file_buffer.push_back(std::pair<std::wstring, std::vector<SFileAndHash> >(path, data) );

	if( modify_file_buffer_size>max_modify_file_buffer_size)
	{
		commitModifyFilesBuffer();
	}
}

void IndexThread::commitModifyFilesBuffer(void)
{
	db->BeginTransaction();
	for(size_t i=0;i<modify_file_buffer.size();++i)
	{
		cd->modifyFiles(modify_file_buffer[i].first, modify_file_buffer[i].second);
	}
	db->EndTransaction();

	modify_file_buffer.clear();
	modify_file_buffer_size=0;
}

std::wstring IndexThread::removeDirectorySeparatorAtEnd(const std::wstring& path)
{
	wchar_t path_sep=os_file_sep()[0];
	if(!path.empty() && path[path.size()-1]==path_sep )
	{
		return path.substr(0, path.size()-1);
	}
	return path;
}

std::string IndexThread::getSHA256(const std::wstring& fn)
{
	sha256_ctx ctx;
	sha256_init(&ctx);

	IFile * f=Server->openFile(os_file_prefix(fn), MODE_READ_SEQUENTIAL_BACKUP);

	if(f==NULL)
	{
		return std::string();
	}

	char buffer[32768];
	unsigned int r;
	while( (r=f->Read(buffer, 32768))>0)
	{
		sha256_update(&ctx, reinterpret_cast<const unsigned char*>(buffer), r);

		if(IdleCheckerThread::getPause())
		{
			Server->wait(5000);
		}
	}

	Server->destroy(f);

	unsigned char dig[32];
	sha256_final(&ctx, dig);

	return bytesToHex(dig, 32);
}

std::string IndexThread::getSHA512Binary(const std::wstring& fn)
{
	Server->Log(L"Calculating SHA512 Hash for file \""+fn+L"\"", LL_DEBUG);
	sha512_ctx ctx;
	sha512_init(&ctx);

	IFile * f=Server->openFile(os_file_prefix(fn), MODE_READ_SEQUENTIAL_BACKUP);

	if(f==NULL)
	{
		return std::string();
	}

	char buffer[32768];
	unsigned int r;
	while( (r=f->Read(buffer, 32768))>0)
	{
		sha512_update(&ctx, reinterpret_cast<const unsigned char*>(buffer), r);

		if(IdleCheckerThread::getPause())
		{
			Server->wait(5000);
		}
	}

	Server->destroy(f);

	std::string ret;
	ret.resize(64);
	sha512_final(&ctx, reinterpret_cast<unsigned char*>(const_cast<char*>(ret.c_str())));
	return ret;
}

void IndexThread::VSSLog(const std::string& msg, int loglevel)
{
	Server->Log(msg, loglevel);
	if(loglevel>LL_DEBUG)
	{
		vsslog.push_back(std::make_pair(msg, loglevel));
	}
}

void IndexThread::VSSLog(const std::wstring& msg, int loglevel)
{
	Server->Log(msg, loglevel);
	if(loglevel>LL_DEBUG)
	{
		vsslog.push_back(std::make_pair(Server->ConvertToUTF8(msg), loglevel));
	}
}

#ifdef _WIN32
namespace
{
	LONG GetStringRegKey(HKEY hKey, const std::wstring &strValueName, std::wstring &strValue, const std::wstring &strDefaultValue)
	{
		strValue = strDefaultValue;
		WCHAR szBuffer[8192];
		DWORD dwBufferSize = sizeof(szBuffer);
		ULONG nError;
		nError = RegQueryValueExW(hKey, strValueName.c_str(), 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
		if (ERROR_SUCCESS == nError)
		{
			strValue.resize(dwBufferSize/sizeof(wchar_t));
			memcpy(const_cast<wchar_t*>(strValue.c_str()), szBuffer, dwBufferSize);
		}
		return nError;
	}
}
#endif

void IndexThread::addFileExceptions(void)
{
#ifdef _WIN32
	exlude_dirs.push_back(sanitizePattern(L"C:\\HIBERFIL.SYS"));

	HKEY hKey;
	LONG lRes = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", 0, KEY_READ, &hKey);
	if(lRes != ERROR_SUCCESS)
		return;

	std::wstring tfiles;
	lRes = GetStringRegKey(hKey, L"ExistingPageFiles", tfiles, L"");
	if(lRes != ERROR_SUCCESS)
		return;

	std::vector<std::wstring> toks;
	std::wstring sep;
	sep.resize(1);
	sep[0]=0;
	Tokenize(tfiles, toks, sep);
	for(size_t i=0;i<toks.size();++i)
	{
		toks[i]=trim(toks[i].c_str());

		if(toks[i].empty())
			continue;

		toks[i]=trim(toks[i]);

		if(toks[i].find(L"\\??\\")==0)
		{
			toks[i].erase(0, 4);
		}

		strupper(&toks[i]);

		exlude_dirs.push_back(sanitizePattern(toks[i]));
	}
#endif
}

void IndexThread::handleHardLinks(const std::wstring& bpath, const std::wstring& vsspath)
{
#ifdef _WIN32
	std::wstring prefixedbpath=os_file_prefix(bpath);
	std::wstring tvolume;
	tvolume.resize(prefixedbpath.size()+100);
	DWORD cchBufferLength=static_cast<DWORD>(tvolume.size());
	BOOL b=GetVolumePathNameW(prefixedbpath.c_str(), &tvolume[0], cchBufferLength);
	if(!b)
	{
		VSSLog(L"Error getting volume path for "+bpath, LL_WARNING);
		return;
	}

	std::wstring tvssvolume;
	tvssvolume.resize(vsspath.size()+100);
	cchBufferLength=static_cast<DWORD>(tvssvolume.size());
	b=GetVolumePathNameW(vsspath.c_str(), &tvssvolume[0], cchBufferLength);
	if(!b)
	{
		VSSLog(L"Error getting volume path for "+vsspath, LL_WARNING);
		return;
	}

	std::wstring vssvolume=tvssvolume.c_str();

	std::wstring volume=strlower(tvolume.c_str());

	if(volume.find(L"\\\\?\\")==0)
		volume.erase(0, 4);

	std::vector<SMDir> additional_changed_dirs;

	std::wstring prev_path;

	for(size_t i=0;i<changed_dirs.size();++i)
	{
		std::wstring vsstpath;
		{
			std::wstring tpath=changed_dirs[i].name;

			if(tpath.find(volume)!=0)
			{
				continue;
			}
			vsstpath=vssvolume+tpath.substr(volume.size());
		}

		if(!prev_path.empty() && prev_path==vsstpath)
		{
			continue;
		}
		else
		{
			prev_path = vsstpath;
		}

		bool has_error;
		std::vector<SFile> files = getFiles(os_file_prefix(vsstpath), &has_error, false, false);

		if(has_error)
		{
			VSSLog(L"Cannot open directory "+vsstpath+L" to handle hard links", LL_DEBUG);
		}

		for(size_t i=0;i<files.size();++i)
		{
			if(files[i].isdir)
			{
				continue;
			}

			std::wstring fn=vsstpath+files[i].name;
			HANDLE hFile = CreateFileW(os_file_prefix(fn).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

			if(hFile==INVALID_HANDLE_VALUE)
			{
				VSSLog(L"Cannot open file "+fn+L" to read the file attributes", LL_INFO);
			}
			else
			{
				BY_HANDLE_FILE_INFORMATION fileInformation;
				BOOL b=GetFileInformationByHandle(hFile, &fileInformation);

				if(!b)
				{
					VSSLog(L"Error getting file information of "+fn, LL_INFO);
					CloseHandle(hFile);
				}
				else if(fileInformation.nNumberOfLinks>1)
				{
					CloseHandle(hFile);

					std::wstring outBuf;
					DWORD stringLength=4096;
					outBuf.resize(stringLength);
					HANDLE hFn=FindFirstFileNameW(os_file_prefix(fn).c_str(), 0, &stringLength, &outBuf[0]);

					if(hFn==INVALID_HANDLE_VALUE && GetLastError()==ERROR_MORE_DATA)
					{
						outBuf.resize(stringLength);
						hFn=FindFirstFileNameW(os_file_prefix(fn).c_str(), 0, &stringLength, &outBuf[0]);
					}

					if(hFn==INVALID_HANDLE_VALUE)
					{
						VSSLog(L"Error reading hard link names of "+fn, LL_INFO);
					}
					else
					{
						std::wstring ndir=strlower(ExtractFilePath(std::wstring(outBuf.begin(), outBuf.begin()+stringLength)))+os_file_sep();
						if(ndir[0]=='\\')
							ndir=volume+ndir.substr(1);
						else
							ndir=volume+ndir;

						std::vector<SMDir>::iterator it_dir=std::lower_bound(changed_dirs.begin(), changed_dirs.end(), SMDir(0, ndir));
						if(it_dir==changed_dirs.end() || (*it_dir).name!=ndir)
						{
							additional_changed_dirs.push_back(SMDir(0, ndir));
						}

						do
						{
							b = FindNextFileNameW(hFn, &stringLength, &outBuf[0]);

							if(!b && GetLastError()==ERROR_MORE_DATA)
							{
								outBuf.resize(stringLength);
								b = FindNextFileNameW(hFn, &stringLength, &outBuf[0]);
							}

							if(!b && GetLastError()!=ERROR_HANDLE_EOF)
							{
								VSSLog(L"Error reading (2) hard link names of "+fn, LL_INFO);
							}
							else
							{
								std::wstring ndir=strlower(ExtractFilePath(std::wstring(outBuf.begin(), outBuf.begin()+stringLength)))+os_file_sep();
								if(ndir[0]=='\\')
									ndir=volume+ndir.substr(1);
								else
									ndir=volume+ndir;
						
								std::vector<SMDir>::iterator it_dir=std::lower_bound(changed_dirs.begin(), changed_dirs.end(), SMDir(0, ndir));
								if(it_dir==changed_dirs.end() || (*it_dir).name!=ndir)
								{
									additional_changed_dirs.push_back(SMDir(0, ndir));
								}
							}
						}
						while(b);

						FindClose(hFn);
					}
				}
				else
				{
					CloseHandle(hFile);
				}
			}
		}
	}

	changed_dirs.insert(changed_dirs.end(), additional_changed_dirs.begin(), additional_changed_dirs.end());
	std::sort(changed_dirs.begin(), changed_dirs.end());
#endif
}

std::string IndexThread::escapeListName( const std::string& listname )
{
	std::string ret;
	ret.reserve(listname.size());
	for(size_t i=0;i<listname.size();++i)
	{
		if(listname[i]=='"')
		{
			ret+="\\\"";
		}
		else if(listname[i]=='\\')
		{
			ret+="\\\\";
		}
		else
		{
			ret+=listname[i];
		}
	}
	return ret;
}
