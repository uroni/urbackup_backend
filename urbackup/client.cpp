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

#include "client.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../Interface/SettingsReader.h"
#ifdef _WIN32
#include "DirectoryWatcherThread.h"
#endif
#include "../stringtools.h"
#include "fileclient/data.h"
#include "database.h"
#include "ServerIdentityMgr.h"
#include "ClientService.h"
#include <algorithm>
#include <fstream>
#include <stdlib.h>


volatile bool IdleCheckerThread::idle=false;
volatile bool IdleCheckerThread::pause=false;
volatile bool IndexThread::stop_index=false;
std::map<std::wstring, std::wstring> IndexThread::filesrv_share_dirs;

extern PLUGIN_ID filesrv_pluginid;

const unsigned int idletime=60000;
const unsigned int nonidlesleeptime=500;
const unsigned short tcpport=35621;
const unsigned short udpport=35622;
const unsigned int shadowcopy_timeout=7*24*60*60*1000;

#ifndef SERVER_ONLY
#define ENABLE_VSS
#endif

#define CHECK_COM_RESULT_RELEASE(x) { HRESULT r; if( (r=(x))!=S_OK ){ Server->Log( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); if(backupcom!=NULL){backupcom->AbortBackup();backupcom->Release();} return false; }}
#define CHECK_COM_RESULT_RELEASE_S(x) { HRESULT r; if( (r=(x))!=S_OK ){ Server->Log( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); if(backupcom!=NULL){backupcom->AbortBackup();backupcom->Release();} return ""; }}
#define CHECK_COM_RESULT(x) { HRESULT r; if( (r=(x))!=S_OK ){ Server->Log( #x+(std::string)" failed: EC="+GetErrorHResErrStr(r), LL_ERROR); }}

void IdleCheckerThread::operator()(void)
{
	int lx,ly;
	int x,y;
	getMousePos(x,y);
	lx=x;
	ly=y;

	unsigned int last_move=Server->getTimeMS();

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
{
	if(filelist_mutex==NULL)
		filelist_mutex=Server->createMutex();
	if(msgpipe==NULL)
		msgpipe=Server->createMemoryPipe();
	if(filesrv_mutex==NULL)
		filesrv_mutex=Server->createMutex();

	contractor=NULL;

	dwt=NULL;
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
#ifdef _WIN32
#ifndef SERVER_ONLY
	CHECK_COM_RESULT(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	CHECK_COM_RESULT(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY, NULL, EOAC_NONE, NULL));
#endif
#endif
#ifdef _WIN32
#ifdef THREAD_MODE_BACKGROUND_BEGIN
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
#endif

	if(Server->getPlugin(Server->getThreadID(), filesrv_pluginid))
	{
		start_filesrv();
	}
	else
	{
		filesrv=NULL;
		Server->Log("Error starting fileserver", LL_ERROR);
	}

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	db->Write("CREATE TEMPORARY TABLE files_tmp (num NUMERIC, data BLOB, name TEXT);");

	cd=new ClientDAO(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT));

#ifdef _WIN32
#ifdef ENABLE_VSS
	cleanup_saved_shadowcopies();
#endif
#endif
	
	//indexDirs();

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
		}
		msgpipe->Read(&msg);
		CRData data(&msg);
		char action;
		data.getChar(&action);
		data.getVoidPtr((void**)&contractor);
		if(action==0)
		{
			data.getStr(&starttoken);

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
				Server->Log("Deleting files... GAP found...", LL_INFO);

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
				if(!stop_index)
				{
					contractor->Write("done");
				}
				else
				{
					contractor->Write("error - stop_index 2");
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
			data.getStr(&starttoken);

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
				IQuery *q=db->Prepare("DELETE FROM files", false);
				q->Write();
				db->destroyQuery(q);
			}
			execute_prebackup_hook();
			indexDirs();
			execute_postindex_hook();
			if(!stop_index)
			{
				contractor->Write("done");
			}
			else
			{
				contractor->Write("error");
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
				if(start_shadowcopy(scd, NULL, false, std::vector<SCRef*>(), image_backup==1?true:false))
				{
					contractor->Write("done-"+nconvert(scd->ref->save_id)+"-"+Server->ConvertToUTF8(scd->target));
				}
				else
				{
					Server->Log(L"Getting shadowcopy of \""+scd->dir+L"\" failed.", LL_ERROR);
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

				Server->Log(L"Creating shadowcopy of \""+scd->dir+L"\"...", LL_INFO);
				bool b=start_shadowcopy(scd, NULL, false, std::vector<SCRef*>(), image_backup==0?false:true);
				Server->Log("done.", LL_INFO);
				if(!b || scd->ref==NULL)
				{
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

			if(scd->running==false )
			{
				int save_id=-1;
				data.getInt(&save_id);
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
				bool b=release_shadowcopy(scd, false, image_backup==1?true:false);
				if(!b)
				{
					contractor->Write("failed");
#ifdef _WIN32
					Server->Log(L"Deleting shadowcopy of \""+scd->dir+L"\" failed.", LL_ERROR);
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
				std::wstring msg=L"A"+Server->ConvertToUnicode( dir );
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
				std::wstring msg=L"D"+Server->ConvertToUnicode( dir );
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
	}
}

void IndexThread::indexDirs(void)
{
	readExcludePattern();
	updateDirs();
#ifdef _WIN32
	cd->restoreSavedChangedDirs();
	//Invalidate cache
	DirectoryWatcherThread::update_and_wait();
	changed_dirs=cd->getChangedDirs();

	cd->restoreSavedDelDirs();
	std::vector<std::wstring> deldirs=cd->getDelDirs();
	for(size_t i=0;i<deldirs.size();++i)
	{
		cd->removeDeletedDir(deldirs[i]);
	}
#endif
	std::sort(changed_dirs.begin(), changed_dirs.end());

	std::vector<SCRef*> past_refs;

	{
		std::fstream outfile("urbackup/data/filelist_new.ub", std::ios::out|std::ios::binary);
		for(size_t i=0;i<backup_dirs.size();++i)
		{
			SCDirs *scd=getSCDir(backup_dirs[i].tname);
			if(!scd->running)
			{
				scd->dir=backup_dirs[i].tname;
				scd->starttime=Server->getTimeSeconds();
				scd->target=getShareDir(backup_dirs[i].tname);
				scd->orig_target=scd->target;
				scd->fileserv=true;
			}

			std::wstring mod_path=backup_dirs[i].path;

			Server->Log(L"Creating shadowcopy of \""+scd->dir+L"\" in indexDirs()", LL_INFO);
			bool onlyref=true;
			bool b=start_shadowcopy(scd, &onlyref, true, past_refs);
			Server->Log("done.", LL_INFO);

			if(!b)
			{
#ifdef _WIN32
				Server->Log(L"Creating shadowcopy of \""+scd->dir+L"\" failed in indexDirs().", LL_ERROR);
#endif
			}
			else
			{
				mod_path=scd->target;
				scd->running=true;
			}
#ifdef _WIN32
			if(!b || !onlyref)
			{
				past_refs.push_back(scd->ref);
				DirectoryWatcherThread::update_and_wait();
				Server->wait(1000);
				std::vector<std::wstring> acd=cd->getChangedDirs(false);
				changed_dirs.insert(changed_dirs.end(), acd.begin(), acd.end() );
				std::sort(changed_dirs.begin(), changed_dirs.end());

				std::vector<std::wstring> deldirs=cd->getDelDirs(false);
				for(size_t i=0;i<deldirs.size();++i)
				{
					cd->removeDeletedDir(deldirs[i]);
				}
			}
#endif

			Server->Log(L"Indexing \""+backup_dirs[i].tname+L"\"...", LL_INFO);
			index_c_db=0;
			index_c_fs=0;
			index_c_db_update=0;
			outfile << "d\"" << Server->ConvertToUTF8(backup_dirs[i].tname) << "\"\n";
			//db->Write("BEGIN IMMEDIATE;");
			last_transaction_start=Server->getTimeMS();
			initialCheck( backup_dirs[i].path, mod_path, outfile, true);
			if(stop_index)
			{
				for(size_t k=0;k<backup_dirs.size();++k)
				{
					SCDirs *scd=getSCDir(backup_dirs[k].tname);
					release_shadowcopy(scd);
				}
				
				outfile.close();
				removeFile(L"urbackup/data/filelist_new.ub");
				return;
			}
			//db->EndTransaction();
			outfile << "d\"..\"\n";
			Server->Log(L"Indexing of \""+backup_dirs[i].tname+L"\" done. "+convert(index_c_fs)+L" filesystem lookups "+convert(index_c_db)+L" db lookups and "+convert(index_c_db_update)+L" db updates" , LL_INFO);		
		}
	}

	cd->copyFromTmpFiles();

#ifdef _WIN32
	cd->deleteSavedChangedDirs();
	cd->deleteSavedDelDirs();
#endif

	{
		IScopedLock lock(filelist_mutex);
		removeFile(L"urbackup/data/filelist.ub");
		moveFile(L"urbackup/data/filelist_new.ub", L"urbackup/data/filelist.ub");
		Server->wait(1000);
	}

	share_dirs(starttoken);
}

void IndexThread::initialCheck(const std::wstring &orig_dir, const std::wstring &dir, std::fstream &outfile, bool first)
{
	Server->Log(L"Indexing "+dir, LL_DEBUG);
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
		return;
	}

	std::vector<SFile> files=getFilesProxy(orig_dir, dir, !first);
	
	for(size_t i=0;i<files.size();++i)
	{
		if( !files[i].isdir )
		{
			if( isExcluded(orig_dir+os_file_sep()+files[i].name) || isExcluded(dir+os_file_sep()+files[i].name) )
			{
				continue;
			}
			outfile << "f\"" << Server->ConvertToUTF8(files[i].name) << "\" " << files[i].size << " " << files[i].last_modified << "\n";
		}
	}

	for(size_t i=0;i<files.size();++i)
	{
		if( files[i].isdir )
		{
			if( isExcluded(orig_dir+os_file_sep()+files[i].name) || isExcluded(dir+os_file_sep()+files[i].name) )
			{
				continue;
			}

			outfile << "d\"" << Server->ConvertToUTF8(files[i].name) << "\"\n";
			initialCheck(orig_dir+os_file_sep()+files[i].name, dir+os_file_sep()+files[i].name, outfile);
			outfile << "d\"..\"\n";
		}
	}
}

void IndexThread::readBackupDirs(void)
{
	backup_dirs=cd->getBackupDirs();

	for(size_t i=0;i<backup_dirs.size();++i)
	{
		if(filesrv!=NULL)
			shareDir(backup_dirs[i].tname, backup_dirs[i].path);
	}
}

std::vector<SFile> IndexThread::getFilesProxy(const std::wstring &orig_path, const std::wstring &path, bool use_db)
{
#ifndef _WIN32
	return getFiles(path);
#else
	bool found=std::binary_search(changed_dirs.begin(), changed_dirs.end(), orig_path);
	std::vector<SFile> tmp;
	if(found || use_db==false)
	{
		++index_c_fs;

		std::wstring tpath=path;
		if(path.size()<2 || (path[0]!='\\' && path[1]!='\\' ) )
			tpath=L"\\\\?\\"+path;

		tmp=getFiles(tpath);
		if(use_db)
		{
			if(cd->hasFiles(orig_path) )
			{
				++index_c_db_update;
				cd->modifyFiles(orig_path, tmp);
			}
			else
			{
				cd->addFiles(orig_path, tmp);
			}
		}
		return tmp;
	}
	else
	{	
		if( cd->getFiles(orig_path, tmp) )
		{
			++index_c_db;
			return tmp;
		}
		else
		{
			++index_c_fs;

			std::wstring tpath=path;
			if(path.size()<2 || (path[0]!='\\' && path[1]!='\\' ) )
				tpath=L"\\\\?\\"+path;

			tmp=getFiles(tpath);
			cd->addFiles(orig_path, tmp);
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
		Server->Log("res!=VSS_S_ASYNC_FINISHED CCOM fail", LL_ERROR);
		vsasync->Release();
		return false;
	}
	vsasync->Release();
	return true;
}
#endif

bool IndexThread::start_shadowcopy(SCDirs *dir, bool *onlyref, bool restart_own, std::vector<SCRef*> no_restart_refs, bool for_imagebackup)
{
#ifdef _WIN32
#ifdef ENABLE_VSS
	cleanup_saved_shadowcopies();

	WCHAR volume_path[MAX_PATH]; 
	BOOL ok = GetVolumePathNameW(dir->orig_target.c_str(), volume_path, MAX_PATH);
	if(!ok)
	{
		Server->Log("GetVolumePathName(dir.target, volume_path, MAX_PATH) failed", LL_ERROR);
		return false;
	}

	std::wstring wpath=volume_path;

	{
		for(size_t i=0;i<sc_refs.size();++i)
		{
			if(sc_refs[i]->target==wpath && sc_refs[i]->ok)
			{
				bool do_restart=true;
				for(size_t k=0;k<no_restart_refs.size();++k)
				{
					if(no_restart_refs[k]==sc_refs[i])
					{
						do_restart=false;
						break;
					}
				}
				bool only_own_tokens=true;
				for(size_t k=0;k<sc_refs[i]->starttokens.size();++k)
				{
					if( sc_refs[i]->starttokens[k]!=starttoken && (Server->getTimeSeconds()-ClientConnector::getLastTokenTime(sc_refs[i]->starttokens[k]))<3600)
					{
						only_own_tokens=false;
						break;
					}
				}
				if(Server->getTimeSeconds()-sc_refs[i]->starttime>shadowcopy_timeout/1000 || (do_restart==true && restart_own==true && only_own_tokens==true ) )
				{
					if( only_own_tokens==true)
					{
						Server->Log("Removing reference because restart own was specified and only own tokens are present", LL_WARNING);
					}
					else
					{
						Server->Log("Removing reference because of reference timeout", LL_WARNING);
					}

					std::vector<std::wstring> m_keys;
					for(std::map<std::wstring, SCDirs*>::iterator lit=scdirs.begin();lit!=scdirs.end();++lit)
					{
						m_keys.push_back(lit->first);
					}
					SCRef *curr=sc_refs[i];
					for(size_t k=0;k<m_keys.size();++k)
					{
						std::map<std::wstring, SCDirs*>::iterator it=scdirs.find(m_keys[k]);
						if(it!=scdirs.end() && it->second->ref==curr)
						{
							Server->Log(L"Releasing "+it->first+L" orig_target="+it->second->orig_target+L" target="+it->second->target, LL_DEBUG);
							release_shadowcopy(it->second, false, -1, dir);
						}
					}
					break;
				}
				else
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

					Server->Log(L"orig_target="+dir->orig_target+L" volpath="+dir->ref->volpath, LL_DEBUG);

					dir->target=dir->orig_target;
					dir->target.erase(0,wpath.size());

					dir->target=dir->ref->volpath+os_file_sep()+dir->target;
					if(dir->fileserv)
					{
						shareDir(dir->dir, dir->target);
					}

					if(for_imagebackup && dir->ref->save_id!=-1)
					{
						cd->modShadowcopyRefCount(dir->ref->save_id, 1);
					}

					if(onlyref!=NULL)
					{
						*onlyref=true;
					}

					Server->Log("Shadowcopy already present.", LL_INFO);
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
		Server->Log("Error rcount!=1", LL_ERROR);
	}

	IVssBackupComponents *backupcom=NULL; 
	CHECK_COM_RESULT_RELEASE(CreateVssBackupComponents(&backupcom));

	CHECK_COM_RESULT_RELEASE(backupcom->InitializeForBackup());

#ifndef VSS_XP
#ifndef VSS_S03
	CHECK_COM_RESULT_RELEASE(backupcom->SetContext(VSS_CTX_APP_ROLLBACK) );
#endif
#endif

	IVssAsync *pb_result;
	
    CHECK_COM_RESULT_RELEASE(backupcom->GatherWriterMetadata(&pb_result)); 
	wait_for(pb_result);

	/*UINT num_writers; 
    CHECK_COM_RESULT(backupcom->GetWriterMetadataCount(&num_writers));
	for(UINT i=0;i<num_writers;++i)
	{
		IVssExamineWriterMetadata *writer_metadata;
		GUID id;
		CHECK_COM_RESULT(backupcom->GetWriterMetadata(i, &id, &writer_metadata)); 

		UINT ifiles;
        UINT efiles; 
        UINT comps;
		CHECK_COM_RESULT(writer_metadata->GetFileCounts(&ifiles, &efiles, &comps));

		for(UINT j=0;j<comps;++j)
		{
			IVssWMComponent *comp;
			CHECK_COM_RESULT(writer_metadata->GetComponent(j, &comp));

			PVSSCOMPONENTINFO comp_info; 
			CHECK_COM_RESULT(comp->GetComponentInfo(&comp_info));
		}
	}*/

	CHECK_COM_RESULT_RELEASE(backupcom->StartSnapshotSet(&dir->ref->ssetid));

	CHECK_COM_RESULT_RELEASE(backupcom->AddToSnapshotSet(volume_path, GUID_NULL, &dir->ref->ssetid) );

	CHECK_COM_RESULT_RELEASE(backupcom->SetBackupState(FALSE, FALSE, VSS_BT_FULL, FALSE)); 
	
	CHECK_COM_RESULT_RELEASE(backupcom->PrepareForBackup(&pb_result));
	wait_for(pb_result);

	CHECK_COM_RESULT_RELEASE(backupcom->DoSnapshotSet(&pb_result));
	wait_for(pb_result);

	VSS_SNAPSHOT_PROP snap_props; 
    CHECK_COM_RESULT_RELEASE(backupcom->GetSnapshotProperties(dir->ref->ssetid, &snap_props));

	dir->target.erase(0,wpath.size());
	dir->ref->volpath=(std::wstring)snap_props.m_pwszSnapshotDeviceObject;
	dir->target=dir->ref->volpath+os_file_sep()+dir->target;
	if(dir->fileserv)
	{
		shareDir(dir->dir, dir->target);
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
	if(for_imagebackup)
	{
		tsc.refs=1;
	}
	dir->ref->save_id=cd->addShadowcopy(tsc);
	dir->ref->ok=true;

	Server->Log(L"Shadowcopy path: "+tsc.path);

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
			CHECK_COM_RESULT_RELEASE(backupcom->BackupComplete(&pb_result));
			wait_for(pb_result);

			Server->Log(L"Deleting shadowcopy for path \""+dir->target+L"\" -2", LL_INFO);
			
			if(dir->ref->save_id!=-1)
			{
				cd->deleteShadowcopy(dir->ref->save_id);
			}

#ifndef VSS_XP
#ifndef VSS_S03
#ifndef SERVER_ONLY

			LONG dels; 
			GUID ndels; 
			CHECK_COM_RESULT_RELEASE(backupcom->DeleteSnapshots(dir->ref->ssetid, VSS_OBJECT_SNAPSHOT, TRUE, 
				&dels, &ndels));

			if(dels==0)
			{
				Server->Log("Deleting shadowcopy failed.", LL_ERROR);
			}
			else
			{
				ok=true;
			}
			has_dels=true;
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
					Server->Log(L"Removing shadowcopy entry for path \""+scs[i].path+L"\"", LL_INFO);
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
				Server->Log(L"Deleting Shadowcopy for dir \""+sc_refs[i]->target+L"\"", LL_INFO);
				bool c=true;
				while(c)
				{
					c=false;
					for(std::map<std::wstring, SCDirs*>::iterator it=scdirs.begin();it!=scdirs.end();++it)
					{
						if(it->second->ref==sc_refs[i])
						{
							if(it->second->fileserv)
							{
								shareDir(it->second->dir, it->second->orig_target);
							}
							it->second->target=it->second->orig_target;

							it->second->ref=NULL;
							if(dontdel==NULL || it->second!=dontdel )
							{
								delete it->second;
								scdirs.erase(it);
								c=true;
								break;
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

bool IndexThread::cleanup_saved_shadowcopies(void)
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
			if(f2==true && (scs[i].refs<=0 || scs[i].passedtime>shadowcopy_timeout/1000))
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
				if( f2==true && (scs[i].refs<=0 || scs[i].passedtime>shadowcopy_timeout/1000))
				{
					Server->Log(L"Deleting shadowcopy for path \""+scs[i].path+L"\"", LL_INFO);
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
	std::map<std::wstring, SCDirs*>::iterator it=scdirs.find(path);
	if(it!=scdirs.end())
	{
		return it->second;
	}
	else
	{
		SCDirs *nd=new SCDirs;
		scdirs.insert(std::pair<std::wstring, SCDirs*>(path, nd) );

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
#endif
}

void IndexThread::execute_postindex_hook(void)
{
#ifdef _WIN32
	system(Server->ConvertToUTF8(L"\""+Server->getServerWorkingDir()+L"\\postfileindex.bat\"").c_str());
#endif
}

void IndexThread::readExcludePattern(void)
{
	ISettingsReader *curr_settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");
	exlude_dirs.clear();
	if(curr_settings!=NULL)
	{	
		std::wstring val;
		if(curr_settings->getValue(L"exclude_files", &val))
		{
			std::vector<std::wstring> toks;
			Tokenize(val, toks, L";");
			exlude_dirs=toks;
		}
		Server->destroy(curr_settings);
	}
}

bool amatch(const wchar_t *str, const wchar_t *p);

bool IndexThread::isExcluded(const std::wstring &path)
{
	for(size_t i=0;i<exlude_dirs.size();++i)
	{
		if(!exlude_dirs[i].empty())
		{
			bool b=amatch(path.c_str(), exlude_dirs[i].c_str());
			if(b)
			{
				return true;
			}
		}
	}
	return false;
}

void IndexThread::start_filesrv(void)
{
	std::wstring name;
	if(Server->getServerParameter("restore_mode")=="true")
	{
		name=L"##restore##"+convert(Server->getTimeSeconds())+convert(rand()%10000);
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
	filesrv=((IFileServFactory*)(Server->getPlugin(Server->getThreadID(), filesrv_pluginid)))->createFileServ(tcpport, udpport, name);
	filesrv->shareDir(L"urbackup", Server->getServerWorkingDir()+L"/urbackup/data");

	ServerIdentityMgr::setFileServ(filesrv);
	ServerIdentityMgr::loadServerIdentities();
}

void IndexThread::shareDir(const std::wstring &name, const std::wstring &path)
{
	IScopedLock lock(filesrv_mutex);
	filesrv_share_dirs[name]=path;
}

void IndexThread::removeDir(const std::wstring &name)
{
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

void IndexThread::share_dirs(const std::string &token)
{
	IScopedLock lock(filesrv_mutex);
	for(std::map<std::wstring, std::wstring>::iterator it=filesrv_share_dirs.begin();it!=filesrv_share_dirs.end();++it)
	{
		std::wstring dir=it->first;
		if(!token.empty())
			dir=widen(token)+L"|"+dir;

		filesrv->shareDir(dir, it->second);
	}
}

void IndexThread::unshare_dirs(const std::string &token)
{
	IScopedLock lock(filesrv_mutex);
	for(std::map<std::wstring, std::wstring>::iterator it=filesrv_share_dirs.begin();it!=filesrv_share_dirs.end();++it)
	{
		std::wstring dir=it->first;
		if(!token.empty())
			dir=widen(token)+L"|"+dir;

		filesrv->removeDir(dir);
	}
}