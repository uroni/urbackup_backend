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
#include <algorithm>
#include <fstream>
#include <stdlib.h>


bool IdleCheckerThread::idle=false;
bool IdleCheckerThread::pause=false;

extern PLUGIN_ID filesrv_pluginid;

const unsigned int idletime=60000;
const unsigned int nonidlesleeptime=500;
const unsigned short tcpport=35621;
const unsigned short udpport=35622;
const unsigned int shadowcopy_timeout=24*60*60*1000;

#define ENABLE_VSS

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

IndexThread::IndexThread(void)
{
	if(filelist_mutex==NULL)
		filelist_mutex=Server->createMutex();
	if(msgpipe==NULL)
		msgpipe=Server->createMemoryPipe();

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
	CHECK_COM_RESULT(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	CHECK_COM_RESULT(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY, NULL, EOAC_NONE, NULL));
#endif
#ifdef _WIN32
#ifdef THREAD_MODE_BACKGROUND_BEGIN
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
#endif

	if(Server->getPlugin(Server->getThreadID(), filesrv_pluginid))
	{
		std::wstring name;
		if(Server->getServerParameter("restore_mode")=="true")
		{
			name=L"##restore##"+convert(Server->getTimeSeconds())+convert(rand()%10000);
		}
		filesrv=((IFileServFactory*)(Server->getPlugin(Server->getThreadID(), filesrv_pluginid)))->createFileServ(tcpport, udpport, name);
		filesrv->shareDir("urbackup", Server->getServerWorkingDir()+L"/urbackup/data");

		ServerIdentityMgr::setFileServ(filesrv);
		ServerIdentityMgr::loadServerIdentities();
	}
	else
	{
		filesrv=NULL;
		Server->Log("Error starting fileserver", LL_ERROR);
	}

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	db->Write("CREATE TEMPORARY TABLE files_tmp (num NUMERIC, data BLOB, name TEXT);");

	cd=new ClientDAO(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT));
	
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
			//incr backup
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
			DirectoryWatcherThread::update();
#endif
			execute_prebackup_hook();
			indexDirs();
			contractor->Write("done");
		}
		else if(action==1)
		{
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
			contractor->Write("done");
		}
		else if(action==2) // create shadowcopy
		{
			std::string scdir;
			data.getStr(&scdir);
			SCDirs *scd=getSCDir(Server->ConvertToUnicode(scdir));

			unsigned char fileserv;
			bool hfs=data.getUChar(&fileserv);
			
			if(scd->running==true && Server->getTimeMS()-scd->starttime<=shadowcopy_timeout)
			{
				contractor->Write("done-"+nconvert(scd->save_id)+"-"+Server->ConvertToUTF8(scd->target));
				scd->sccount++;
			}
			else if(scd->running==false || Server->getTimeMS()-scd->starttime>shadowcopy_timeout)
			{
				if(scd->running==true)
				{
					Server->Log("Removing shadowcopy \""+scd->dir+"\" because of timeout...", LL_INFO);
					bool b=release_shadowcopy(*scd);
					if(!b)
					{
#ifdef _WIN32
						Server->Log("Deleting shadowcopy of \""+scd->dir+"\" failed.", LL_ERROR);
#endif
					}
				}

				scd->dir=scdir;
				scd->sccount=1;
				scd->save_id=-1;
				scd->starttime=Server->getTimeMS();
				if(hfs && fileserv==0)
				{
					scd->target=Server->ConvertToUnicode(scd->dir);
					scd->fileserv=false;
				}
				else
				{
					scd->target=filesrv->getShareDir(scd->dir);
					scd->fileserv=true;
				}

				Server->Log("Creating shadowcopy of \""+scd->dir+"\"...", LL_INFO);
				bool b=start_shadowcopy(*scd);
				Server->Log("done.", LL_INFO);
				if(!b)
				{
					contractor->Write("failed");
#ifdef _WIN32
					Server->Log("Creating shadowcopy of \""+scd->dir+"\" failed.", LL_ERROR);
#endif
				}
				else
				{
					contractor->Write("done-"+nconvert(scd->save_id)+"-"+Server->ConvertToUTF8(scd->target));
					scd->running=true;
				}
			}
		}
		else if(action==3) // remove shadowcopy
		{
			std::string scdir;
			data.getStr(&scdir);
			SCDirs *scd=getSCDir(Server->ConvertToUnicode(scdir));

			if(scd->running==false )
			{
				if(!release_shadowcopy(*scd))
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
				scd->sccount--;
				if(scd->sccount<=0)
				{
					bool b=release_shadowcopy(*scd);
					if(!b)
					{
						contractor->Write("failed");
#ifdef _WIN32
						Server->Log("Deleting shadowcopy of \""+scd->dir+"\" failed.", LL_ERROR);
#endif
					}
					else
					{
						contractor->Write("done");
					}
					scd->running=false;
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
		}
#endif
	}
}

void IndexThread::indexDirs(void)
{
	updateDirs();
#ifdef _WIN32
	cd->restoreSavedChangedDirs();
	//Invalidate cache
	DirectoryWatcherThread::update();
	changed_dirs=cd->getChangedDirs();

	cd->restoreSavedDelDirs();
	std::vector<std::wstring> deldirs=cd->getDelDirs();
	for(size_t i=0;i<deldirs.size();++i)
	{
		cd->removeDeletedDir(deldirs[i]);
	}
#endif
	std::sort(changed_dirs.begin(), changed_dirs.end());

	{
		std::fstream outfile("urbackup/data/filelist_new.ub", std::ios::out|std::ios::binary);
		for(size_t i=0;i<backup_dirs.size();++i)
		{
			SCDirs *scd=getSCDir(widen(backup_dirs[i].tname));
			if(scd->running)
			{
				if(!release_shadowcopy(*scd))
				{
					Server->Log("Releasing shadowcopy failed", LL_ERROR);
				}
			}
			scd->dir=backup_dirs[i].tname;
			scd->sccount=0;
			scd->save_id=-1;
			scd->starttime=Server->getTimeMS();
			scd->target=filesrv->getShareDir(backup_dirs[i].tname);
			scd->fileserv=true;

			std::wstring mod_path=backup_dirs[i].path;

			Server->Log("Creating shadowcopy of \""+scd->dir+"\" in indexDirs()", LL_INFO);
			bool onlyref;
			bool b=start_shadowcopy(*scd, &onlyref);
			Server->Log("done.", LL_INFO);
			if(!b)
			{
#ifdef _WIN32
				Server->Log("Creating shadowcopy of \""+scd->dir+"\" failed in indexDirs().", LL_ERROR);
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
				DirectoryWatcherThread::update();
				Server->wait(10000);
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

			Server->Log("Indexing \""+backup_dirs[i].tname+"\"...", LL_INFO);
			index_c_db=0;
			index_c_fs=0;
			index_c_db_update=0;
			outfile << "d\"" << backup_dirs[i].tname << "\"\n";
			//db->Write("BEGIN IMMEDIATE;");
			last_transaction_start=Server->getTimeMS();
			initialCheck( backup_dirs[i].path, mod_path, outfile);
			//db->EndTransaction();
			outfile << "d\"..\"\n";
			Server->Log("Indexing of \""+backup_dirs[i].tname+"\" done. "+nconvert(index_c_fs)+" filesystem lookups "+nconvert(index_c_db)+" db lookups and "+nconvert(index_c_db_update)+" db updates" , LL_INFO);
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
}

void IndexThread::initialCheck(const std::wstring &orig_dir, const std::wstring &dir, std::fstream &outfile)
{
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

	std::vector<SFile> files=getFilesProxy(orig_dir, dir);
	
	for(size_t i=0;i<files.size();++i)
	{
		if( !files[i].isdir )
		{
			outfile << "f\"" << Server->ConvertToUTF8(files[i].name) << "\" " << files[i].size << " " << files[i].last_modified << "\n";
		}
	}

	for(size_t i=0;i<files.size();++i)
	{
		if( files[i].isdir )
		{
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
			filesrv->shareDir(backup_dirs[i].tname, backup_dirs[i].path);
	}
}

std::vector<SFile> IndexThread::getFilesProxy(const std::wstring &orig_path, const std::wstring &path)
{
#ifndef _WIN32
	return getFiles(path);
#else
	bool found=std::binary_search(changed_dirs.begin(), changed_dirs.end(), orig_path);
	std::vector<SFile> tmp;
	if(found)
	{
		++index_c_fs;
		tmp=getFiles(path);
		if(cd->hasFiles(orig_path) )
		{
			++index_c_db_update;
			cd->modifyFiles(orig_path, tmp);
		}
		else
			cd->addFiles(orig_path, tmp);
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
			tmp=getFiles(orig_path);
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

bool IndexThread::start_shadowcopy(SCDirs &dir, bool *onlyref)
{
#ifdef _WIN32
#ifdef ENABLE_VSS	
	if(dir.ref!=NULL && dir.ref->backupcom==NULL)
	{
		release_shadowcopy(dir);
	}

	WCHAR volume_path[MAX_PATH]; 
	BOOL ok = GetVolumePathNameW(dir.target.c_str(), volume_path, MAX_PATH);
	if(!ok)
	{
		Server->Log("GetVolumePathName(dir.target, volume_path, MAX_PATH) failed", LL_ERROR);
		return false;
	}

	std::wstring wpath=volume_path;

	if(dir.ref==NULL || !dir.ref->ok)
	{
		for(size_t i=0;i<sc_refs.size();++i)
		{
			if(sc_refs[i]->target==wpath && sc_refs[i]->ok)
			{
				if(Server->getTimeSeconds()-sc_refs[i]->starttime>shadowcopy_timeout/1000)
				{
					for(std::map<std::wstring, SCDirs*>::iterator it=scdirs.begin();it!=scdirs.end();++it)
					{
						Server->Log("Removing reference because of reference timeout", LL_WARNING);
						release_shadowcopy(*it->second);
					}
				}
				else
				{
					dir.ref=sc_refs[i];
					dir.ref->rcount++;

					dir.orig_target=dir.target;
					dir.target.erase(0,wpath.size());

					dir.target=dir.ref->volpath+os_file_sep()+dir.target;
					if(dir.fileserv)
					{
						filesrv->shareDir(dir.dir, dir.target);
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

		dir.ref=new SCRef;
		dir.ref->rcount=1;
		dir.ref->starttime=Server->getTimeSeconds();
		dir.ref->target=wpath;
		sc_refs.push_back(dir.ref);
	}

	if(dir.ref->rcount!=1)
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

	CHECK_COM_RESULT_RELEASE(backupcom->StartSnapshotSet(&dir.ref->ssetid));

	CHECK_COM_RESULT_RELEASE(backupcom->AddToSnapshotSet(volume_path, GUID_NULL, &dir.ref->ssetid) );

	CHECK_COM_RESULT_RELEASE(backupcom->SetBackupState(FALSE, FALSE, VSS_BT_FULL, FALSE)); 
	
	CHECK_COM_RESULT_RELEASE(backupcom->PrepareForBackup(&pb_result));
	wait_for(pb_result);

	CHECK_COM_RESULT_RELEASE(backupcom->DoSnapshotSet(&pb_result));
	wait_for(pb_result);

	VSS_SNAPSHOT_PROP snap_props; 
    CHECK_COM_RESULT_RELEASE(backupcom->GetSnapshotProperties(dir.ref->ssetid, &snap_props));

	dir.orig_target=dir.target;
	dir.target.erase(0,wpath.size());
	dir.ref->volpath=(std::wstring)snap_props.m_pwszSnapshotDeviceObject;
	dir.target=dir.ref->volpath+os_file_sep()+dir.target;
	if(dir.fileserv)
	{
		filesrv->shareDir(dir.dir, dir.target);
	}

	SShadowCopy tsc;
	tsc.vssid=snap_props.m_SnapshotId;
	tsc.ssetid=snap_props.m_SnapshotSetId;
	tsc.target=dir.orig_target;
	tsc.path=(std::wstring)snap_props.m_pwszSnapshotDeviceObject;
	tsc.orig_target=dir.orig_target;
	tsc.filesrv=dir.fileserv;
	tsc.tname=widen(dir.dir);
	dir.save_id=cd->addShadowcopy(tsc);
	dir.ref->save_id=dir.save_id;
	dir.ref->ok=true;

	Server->Log(L"Shadowcopy path: "+tsc.path);

	dir.ref->ssetid=snap_props.m_SnapshotId;

	VssFreeSnapshotProperties(&snap_props);

	dir.ref->backupcom=backupcom;
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

bool IndexThread::release_shadowcopy(SCDirs &dir)
{
#ifdef _WIN32
#ifdef ENABLE_VSS
	if(dir.ref!=NULL && dir.ref->backupcom!=NULL && dir.fileserv)
	{
		filesrv->shareDir(dir.dir, dir.orig_target);
	}

	bool ok=false;

	if(dir.ref!=NULL && dir.ref->backupcom!=NULL)
	{
		if(dir.ref->rcount<=1)
		{
			IVssBackupComponents *backupcom=dir.ref->backupcom;
			IVssAsync *pb_result;
			CHECK_COM_RESULT_RELEASE(backupcom->BackupComplete(&pb_result));
			wait_for(pb_result);

			Server->Log(L"Deleting shadowcopy for path \""+dir.target+L"\" -2", LL_INFO);
			
			if(dir.ref->save_id!=-1)
			{
				cd->deleteShadowcopy(dir.ref->save_id);
			}
#ifndef VSS_XP
#ifndef VSS_S03
			if(dir.fileserv)
			{
				filesrv->shareDir(dir.dir, dir.orig_target);
			}

			LONG dels; 
			GUID ndels; 
			CHECK_COM_RESULT_RELEASE(backupcom->DeleteSnapshots(dir.ref->ssetid, VSS_OBJECT_SNAPSHOT, TRUE, 
				&dels, &ndels));

			if(dels==0)
			{
				Server->Log("Deleting shadowcopy failed.", LL_ERROR);
			}
			else
			{
				ok=true;
			}
#endif
#endif
#if defined(VSS_XP) || defined(VSS_S03)
			ok=true;
#endif

			backupcom->Release();
			dir.ref->backupcom=NULL;

			
		}
		--dir.ref->rcount;
	}
	else if(dir.ref!=NULL)
	{
		--dir.ref->rcount;
	}

	{
		std::vector<SShadowCopy> scs=cd->getShadowcopies();

		bool found=false;

		for(size_t i=0;i<scs.size();++i)
		{
			SCDirs *scd=getSCDir(scs[i].tname);
			if( (dir.save_id!=-1 && scs[i].id!=dir.save_id && dir.orig_target==scs[i].orig_target) || ( dir.ref==NULL && (scd->running==false || scd->save_id!=scs[i].id ) ) )
			{
				found=true;
				break;
			}
		}

#ifndef VSS_XP
#ifndef VSS_S03
		if(found)
		{
			IVssBackupComponents *backupcom=NULL; 
			CHECK_COM_RESULT_RELEASE(CreateVssBackupComponents(&backupcom));
			CHECK_COM_RESULT_RELEASE(backupcom->InitializeForBackup());
			CHECK_COM_RESULT_RELEASE(backupcom->SetContext(VSS_CTX_APP_ROLLBACK) );
			
			for(size_t i=0;i<scs.size();++i)
			{
				SCDirs *scd=getSCDir(scs[i].target);
				if( (dir.save_id!=-1 && scs[i].id!=dir.save_id && dir.orig_target==scs[i].orig_target) || (dir.ref==NULL && (scd->running==false || scd->save_id!=scs[i].id ) ) )
				{
					if(scd->ref==NULL || scd->ref->rcount<=1)
					{
						Server->Log(L"Deleting shadowcopy for path \""+scs[i].path+L"\"", LL_INFO);
						if(scs[i].filesrv)
						{
							filesrv->shareDir(wnarrow(scs[i].tname), scs[i].orig_target);
						}
						LONG dels; 
						GUID ndels; 
						CHECK_COM_RESULT(backupcom->DeleteSnapshots(scs[i].vssid, VSS_OBJECT_SNAPSHOT, TRUE, 
							&dels, &ndels));
						cd->deleteShadowcopy(scs[i].id);
						if(dels>0)
							ok=true;
					}
					if(scd->ref!=NULL)
						--scd->ref->rcount;
				}
			}

			backupcom->Release();
		}
#endif
#endif
#if defined(VSS_XP) || defined(VSS_S03)
		if(found)
		{
			for(size_t i=0;i<scs.size();++i)
			{
				if(scs[i].target==dir.target || ( dir.ref!=NULL && dir.ref->backupcom==NULL ) )
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
				for(std::map<std::wstring, SCDirs*>::iterator it=scdirs.begin();it!=scdirs.end();++it)
				{
					if(it->second->ref==sc_refs[i])
					{
						it->second->ref=NULL;
					}
				}
				delete sc_refs[i];
				sc_refs.erase(sc_refs.begin()+i);
				r=true;
				break;
			}
		}
	}

	
	if(dir.ref!=NULL && dir.ref->backupcom==NULL)
	{
		Server->Log("dir.backupcom=NULL in IndexThread::release_shadowcopy", LL_DEBUG);
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
		nd->save_id=-1;
		nd->sccount=0;

		return nd;
	}
}

IFileServ *IndexThread::getFileSrv(void)
{
	return filesrv;
}

void IndexThread::execute_prebackup_hook(void)
{
#ifdef _WIN32
	system(Server->ConvertToUTF8(Server->getServerWorkingDir()+L"\\prefilebackup.bat").c_str());
#endif
}