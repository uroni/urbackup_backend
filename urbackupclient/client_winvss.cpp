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

#include "client.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../Interface/SettingsReader.h"
#include "file_permissions.h"
#include "DirectoryWatcherThread.h"
#include "../urbackupcommon/json.h"
#include <assert.h>

#define CHECK_COM_RESULT_RELEASE(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog(#x+(std::string)" failed. VSS error code "+GetErrorHResErrStr(r), LL_ERROR); printProviderInfo(r); if(backupcom!=NULL){backupcom->AbortBackup();backupcom->Release();} return false; }}
#define CHECK_COM_RESULT_RETURN(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog( #x+(std::string)" failed. VSS error code "+GetErrorHResErrStr(r), LL_ERROR); printProviderInfo(r); return false; }}
#define CHECK_COM_RESULT_RELEASE_S(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog( #x+(std::string)" failed. VSS error code "+GetErrorHResErrStr(r), LL_ERROR); printProviderInfo(r); if(backupcom!=NULL){backupcom->AbortBackup();backupcom->Release();} return ""; }}
#define CHECK_COM_RESULT(x) { HRESULT r; if( (r=(x))!=S_OK ){ VSSLog( #x+(std::string)" failed. VSS error code "+GetErrorHResErrStr(r), LL_ERROR); printProviderInfo(r); }}
#define CHECK_COM_RESULT_OK(x, ok) { HRESULT r; if( (r=(x))!=S_OK ){ ok=false; VSSLog( #x+(std::string)" failed .VSS error code "+GetErrorHResErrStr(r), LL_ERROR); printProviderInfo(r); }}
#define CHECK_COM_RESULT_OK_HR(x, ok, r) { if( (r=(x))!=S_OK ){ ok=false; VSSLog( #x+(std::string)" failed. VSS error code "+GetErrorHResErrStr(r), LL_ERROR); printProviderInfo(r); }}

namespace
{
	std::string sortHex(UINT i)
	{
		UINT bi = big_endian(i);
		std::string ret= bytesToHex(reinterpret_cast<unsigned char*>(&bi), sizeof(bi));
		strupper(&ret);
		return ret;
	}
}

namespace
{
	template<typename T>
	class ReleaseIUnknown
	{
	public:
		ReleaseIUnknown(T*& unknown)
			: unknown(unknown) {}

		~ReleaseIUnknown() {
			if (unknown != NULL) {
				unknown->Release();
			}
		}

	private:
		T*& unknown;
	};

#define TOKENPASTE2(x, y) x ## y
#define TOKENPASTE(x, y) TOKENPASTE2(x, y)

#define SCOPED_DECLARE_RELEASE_IUNKNOWN(t, x) t* x = NULL; ReleaseIUnknown<t> TOKENPASTE(ReleaseIUnknown_,__LINE__) (x)

	class FreeBStr
	{
	public:
		FreeBStr(BSTR& bstr)
			: bstr(bstr)
		{}

		~FreeBStr() {
			if (bstr != NULL) {
				SysFreeString(bstr);
			}
		}
	private:
		BSTR& bstr;
	};

#define SCOPED_DECLARE_FREE_BSTR(x) BSTR x = NULL; FreeBStr TOKENPASTE(FreeBStr_, __LINE__) (x)

	class FreeComponentInfo
	{
	public:
		FreeComponentInfo(IVssWMComponent* wmComponent, PVSSCOMPONENTINFO& componentInfo)
			: wmComponent(wmComponent), componentInfo(componentInfo)
		{}

		~FreeComponentInfo() {
			if (wmComponent != NULL && componentInfo != NULL) {
				wmComponent->FreeComponentInfo(componentInfo);
			}
		}
	private:
		IVssWMComponent* wmComponent;
		PVSSCOMPONENTINFO componentInfo;
	};

#define SCOPED_DECLARE_FREE_COMPONENTINFO(c, i) PVSSCOMPONENTINFO i = NULL; FreeComponentInfo TOKENPASTE(FreeComponentInfo_,__LINE__) (c, i);

	std::string convert(VSS_ID id)
	{
		WCHAR GuidStr[128] = {};
		int rc = StringFromGUID2(id, GuidStr, 128);
		if (rc > 0)
		{
			return Server->ConvertFromWchar(std::wstring(GuidStr, rc - 1));
		}
		return std::string();
	}

	class ScopedFreeVssInstance
	{
	public:
		ScopedFreeVssInstance(SVssInstance* instance)
			: instance(instance)
		{}

		~ScopedFreeVssInstance()
		{
			if (instance->refcount == 0)
			{
				delete instance;
			}
		}

	private:
		SVssInstance* instance;
	};
}

void IndexThread::clearContext(SShadowCopyContext& context)
{
	if (context.backupcom != NULL)
	{
		removeBackupcomReferences(context.backupcom);
		context.backupcom->Release();
		context.backupcom = NULL;
	}
}

bool IndexThread::wait_for(IVssAsync *vsasync, const std::string& error_prefix)
{
	if (vsasync == NULL)
	{
		VSSLog("vsasync is NULL", LL_ERROR);
		return false;
	}

	CHECK_COM_RESULT(vsasync->Wait());

	HRESULT res;
	CHECK_COM_RESULT(vsasync->QueryStatus(&res, NULL));

	while (res == VSS_S_ASYNC_PENDING)
	{
		CHECK_COM_RESULT(vsasync->Wait());

		CHECK_COM_RESULT(vsasync->QueryStatus(&res, NULL));
	}

	if (res != VSS_S_ASYNC_FINISHED)
	{
		VSSLog(error_prefix+". Error code "+ GetErrorHResErrStr(res), LL_ERROR);
		if (res == VSS_E_INSUFFICIENT_STORAGE)
		{
			VSSLog("Likely cause and solution: The system or snapshot provider does not have enough free storage space on the volumes"
				" to be backed up to create snapshots. Please free up some space and then try again.", LL_ERROR);
		}
		vsasync->Release();
		return false;
	}
	vsasync->Release();
	return true;
}

bool IndexThread::checkErrorAndLog(BSTR pbstrWriter, VSS_WRITER_STATE pState, HRESULT pHrResultFailure, std::string& errmsg, int loglevel, bool* retryable_error)
{
#define FAIL_STATE(x) case x: { state=#x; failure=true; } break
#define OK_STATE(x) case x: { state=#x; ok_state=true; } break

	std::string state;
	bool failure = false;
	bool ok_state = false;
	switch (pState)
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

	std::string err;
	bool has_error = false;
#define HR_ERR(x) case x: { err=#x; has_error=true; } break
	switch (pHrResultFailure)
	{
	case S_OK: { err = "S_OK"; } break;
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

	std::string writerName;
	if (pbstrWriter)
		writerName = Server->ConvertFromWchar(pbstrWriter);
	else
		writerName = "(NULL)";

	if (failure 
		|| (has_error && (!ok_state || pHrResultFailure!= VSS_E_WRITERERROR_RETRYABLE) ) )
	{
		const std::string erradd = ". UrBackup will continue with the backup but the associated data may not be consistent.";
		std::string nerrmsg = "Writer " + writerName + " has failure state " + state + " with error " + err;
		if (retryable_error && pHrResultFailure == VSS_E_WRITERERROR_RETRYABLE)
		{
			loglevel = LL_INFO;
			*retryable_error = true;
		}
		else if(loglevel<LL_ERROR)
		{
			nerrmsg += erradd;
		}
		VSSLog(nerrmsg, loglevel);
		errmsg += nerrmsg;
		return false;
	}
	else
	{
		VSSLog("Writer " + writerName + " has failure state " + state + " with error " + err + ".", LL_DEBUG);
	}

	return true;
}


bool IndexThread::check_writer_status(IVssBackupComponents *backupcom, std::string& errmsg, int loglevel, bool* retryable_error)
{
	IVssAsync *pb_result;
	CHECK_COM_RESULT_RETURN(backupcom->GatherWriterStatus(&pb_result));

	if (!wait_for(pb_result, "Gathering writer status failed"))
	{
		VSSLog("Error while waiting for result from GatherWriterStatus", LL_ERROR);
		return false;
	}

	UINT nWriters;
	CHECK_COM_RESULT_RETURN(backupcom->GetWriterStatusCount(&nWriters));

	VSSLog("Number of Writers: " + convert(nWriters), LL_DEBUG);

	bool has_error = false;
	for (UINT i = 0; i<nWriters; ++i)
	{
		VSS_ID pidInstance;
		VSS_ID pidWriter;
		BSTR pbstrWriter;
		VSS_WRITER_STATE pState;
		HRESULT pHrResultFailure;

		bool ok = true;
		CHECK_COM_RESULT_OK(backupcom->GetWriterStatus(i,
			&pidInstance,
			&pidWriter,
			&pbstrWriter,
			&pState,
			&pHrResultFailure), ok);

		if (ok)
		{
			if (!checkErrorAndLog(pbstrWriter, pState, pHrResultFailure, errmsg, loglevel, retryable_error))
			{
				has_error = true;
			}

			SysFreeString(pbstrWriter);
		}
	}

	CHECK_COM_RESULT_RETURN(backupcom->FreeWriterStatus());

	return !has_error;
}

std::string IndexThread::GetErrorHResErrStr(HRESULT res)
{
#define CASE_VSS_ERROR(x) case x: return #x
	switch (res)
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
	CASE_VSS_ERROR(VSS_E_INSUFFICIENT_STORAGE);
	CASE_VSS_ERROR(VSS_E_FLUSH_WRITES_TIMEOUT);
	CASE_VSS_ERROR(VSS_E_HOLD_WRITES_TIMEOUT);
	CASE_VSS_ERROR(VSS_E_NESTED_VOLUME_LIMIT);
	CASE_VSS_ERROR(VSS_E_REBOOT_REQUIRED);
	CASE_VSS_ERROR(VSS_E_TRANSACTION_FREEZE_TIMEOUT);
	CASE_VSS_ERROR(VSS_E_TRANSACTION_THAW_TIMEOUT);
	};
#undef CASE_VSS_ERROR
	return "UNDEF("+convert((int64)res)+")";
}

void IndexThread::printProviderInfo(HRESULT res)
{
	if (res != VSS_E_UNEXPECTED_PROVIDER_ERROR)
	{
		return;
	}

	std::string data;
	if (os_popen("vssadmin list providers", data) == 0)
	{
		std::vector<std::string> lines;
		TokenizeMail(data, lines, "\n");

		if (lines.size() > 3)
		{
			VSSLog("VSS provider information:", LL_ERROR);

			for (size_t i = 3; i < lines.size(); ++i)
			{
				std::string cl = trim(lines[i]);
				if (!cl.empty())
				{
					VSSLog(cl, LL_ERROR);
				}
			}
		}
	}
}

std::string IndexThread::lookup_shadowcopy(int sid)
{
	std::vector<SShadowCopy> scs = cd->getShadowcopies();

	for (size_t i = 0; i<scs.size(); ++i)
	{
		if (scs[i].id == sid)
		{
			IVssBackupComponents *backupcom = NULL;
			CHECK_COM_RESULT_RELEASE_S(CreateVssBackupComponents(&backupcom));
			CHECK_COM_RESULT_RELEASE_S(backupcom->InitializeForBackup());
			CHECK_COM_RESULT_RELEASE_S(backupcom->SetContext(VSS_CTX_APP_ROLLBACK));
			VSS_SNAPSHOT_PROP snap_props = {};
			CHECK_COM_RESULT_RELEASE_S(backupcom->GetSnapshotProperties(scs[i].vssid, &snap_props));
			std::string ret = Server->ConvertFromWchar(snap_props.m_pwszSnapshotDeviceObject);
			VssFreeSnapshotProperties(&snap_props);
			backupcom->Release();
			return ret;
		}
	}
	return "";
}

bool IndexThread::start_shadowcopy_components(VSS_ID& ssetid, bool* has_active_transaction)
{
	std::string wpath;
	SCDirs dir;
	dir.ref = new SCRef;
	dir.ref->starttime = Server->getTimeSeconds();
	dir.ref->target = wpath;
	dir.ref->clientsubname = index_clientsubname;
	dir.ref->for_imagebackup = false;
	bool only_ref_val = false;
	bool* only_ref=&only_ref_val;
	bool ret = start_shadowcopy_win(&dir, wpath, false, true, only_ref, has_active_transaction);

	if (ret)
	{
		ssetid = dir.ref->ssetid;
	}

	return ret;
}

bool IndexThread::start_shadowcopy_win(SCDirs * dir, std::string &wpath, bool for_imagebackup, bool with_components, bool * &onlyref,
	bool* has_active_transaction)
{
	const char* crash_consistent_explanation = "This means the files open by this application (e.g. databases) will be backed up in a crash consistent "
		"state instead of a properly shutdown state. Properly written applications can recover from system crashes or power failures.";

	dir->ref->cbt = false;

	std::string wpath_lower = strlower(wpath);

	std::vector<SCRef> additional_refs;
	std::vector<std::string> selected_vols;

	const int max_tries = 3;
	int tries = max_tries;
	bool retryable_error = true;
	IVssBackupComponents *backupcom = NULL;
	while (tries>0 && retryable_error)
	{
		CHECK_COM_RESULT_RELEASE(CreateVssBackupComponents(&backupcom));

		if (!backupcom)
		{
			VSSLog("backupcom is NULL", LL_ERROR);
			return false;
		}

		CHECK_COM_RESULT_RELEASE(backupcom->InitializeForBackup());

		CHECK_COM_RESULT_RELEASE(backupcom->SetBackupState(with_components ? true : false, 
			with_components ? false : true,
			with_components ? VSS_BT_FULL : VSS_BT_COPY, false));

		IVssAsync *pb_result;

		CHECK_COM_RESULT_RELEASE(backupcom->GatherWriterMetadata(&pb_result));
		if (!wait_for(pb_result, "Gathering writer status failed"))
		{
			backupcom->AbortBackup();
			backupcom->Release();
			return false;
		}

#ifndef VSS_XP
#ifndef VSS_S03
		CHECK_COM_RESULT_RELEASE(backupcom->SetContext(VSS_CTX_APP_ROLLBACK));
#endif
#endif

		selected_vols.clear();

		if (with_components)
		{
			if (!selectVssComponents(backupcom, selected_vols))
			{
				VSSLog("Selecting components to backup failed", LL_ERROR);
				backupcom->Release();
				backupcom = NULL;
				return false;
			}

			if (vss_all_components.empty())
			{
				VSSLog("Selected no components to backup", LL_INFO);
				backupcom->Release();
				backupcom = NULL;
				return true;
			}
		}

		if (!wpath.empty()
			&& std::find(selected_vols.begin(), selected_vols.end(), wpath_lower)
			    == selected_vols.end())
		{
			selected_vols.push_back(wpath_lower);
		}

		std::vector<std::string> norm_selected_vols = selected_vols;
		for (size_t i = 0; i < norm_selected_vols.size(); ++i)
		{
			normalizeVolume(norm_selected_vols[i]);
		}

		for (size_t i = 0; i < norm_selected_vols.size(); ++i)
		{
			std::string& norm_vol = norm_selected_vols[i];
			std::vector<std::string> add_vols = getSnapshotGroup(norm_vol, for_imagebackup);
			for (size_t j = 0; j < add_vols.size(); ++j)
			{
				if (std::find(norm_selected_vols.begin(), norm_selected_vols.end(), add_vols[j])
					== norm_selected_vols.end())
				{
					norm_selected_vols.push_back(add_vols[j]);
					selected_vols.push_back(add_vols[j]+"\\");
				}
			}
		}

		std::string errmsg;

		retryable_error = false;
		check_writer_status(backupcom, errmsg, LL_WARNING, &retryable_error);

		bool b_ok = true;
		int tries_snapshot_set = 5;
		while (b_ok == true)
		{
			HRESULT r;
			CHECK_COM_RESULT_OK_HR(backupcom->StartSnapshotSet(&dir->ref->ssetid), b_ok, r);
			if (b_ok)
			{
				break;
			}

			if (b_ok == false && tries_snapshot_set >= 0 && r == VSS_E_SNAPSHOT_SET_IN_PROGRESS)
			{
				VSSLog("Retrying starting shadow copy in 30s", LL_WARNING);
				b_ok = true;
				--tries_snapshot_set;
			}
			Server->wait(30000);
		}

		if (!b_ok)
		{
			CHECK_COM_RESULT_RELEASE(backupcom->StartSnapshotSet(&dir->ref->ssetid));
		}

		additional_refs.clear();
		for (size_t i = 0; i < selected_vols.size(); ++i)
		{
			SCRef nref;
			nref.starttime = Server->getTimeSeconds();
			nref.target = selected_vols[i];
			nref.clientsubname = index_clientsubname;
			nref.for_imagebackup = for_imagebackup;
			nref.ssetid = dir->ref->ssetid;
			nref.backupcom = backupcom;
			additional_refs.push_back(nref);
		}

		for(size_t i=0;i<selected_vols.size();++i)
		{
			if (tries == max_tries)
			{
				if (selected_vols[i] == wpath_lower)
				{
					dir->ref->cbt = prepareCbt(selected_vols[i]);
				}
				else
				{
					additional_refs[i].cbt = prepareCbt(selected_vols[i]);
				}
			}

			CHECK_COM_RESULT_RELEASE(backupcom->AddToSnapshotSet(&(Server->ConvertToWchar(selected_vols[i])[0]), GUID_NULL, &additional_refs[i].volid));

			if (selected_vols[i] == wpath_lower)
			{
				dir->ref->volid = additional_refs[i].volid;
			}
		}



		CHECK_COM_RESULT_RELEASE(backupcom->PrepareForBackup(&pb_result));
		if (!wait_for(pb_result, "Preparing backup failed"))
		{
			backupcom->AbortBackup();
			backupcom->Release();
			return false;
		}

		retryable_error = false;
		check_writer_status(backupcom, errmsg, LL_WARNING, &retryable_error);

		CHECK_COM_RESULT_RELEASE(backupcom->DoSnapshotSet(&pb_result));
		if (!wait_for(pb_result, "Starting snapshot set failed"))
		{
			backupcom->AbortBackup();
			backupcom->Release();
			return false;
		}

		retryable_error = false;

		bool snapshot_ok = false;
		if (tries>1)
		{
			snapshot_ok = check_writer_status(backupcom, errmsg, with_components ? LL_ERROR : LL_WARNING, &retryable_error);
		}
		else
		{
			snapshot_ok = check_writer_status(backupcom, errmsg, with_components ? LL_ERROR : LL_WARNING, NULL);
		}
		--tries;
		if (!snapshot_ok && !retryable_error)
		{
			if (!with_components)
			{
				VSSLog("Writer is in error state during snapshot creation. Writer data may not be consistent. " + std::string(crash_consistent_explanation), LL_WARNING);
			}
			else
			{
				VSSLog("Writer is in error state during snapshot creation.", LL_ERROR);
				backupcom->AbortBackup();
				backupcom->Release();
				return false;
			}
			break;
		}
		else if (!snapshot_ok)
		{
			if (tries == 0)
			{
				if (!with_components)
				{
					VSSLog("Creating snapshot failed after three tries. Giving up. Writer data may not be consistent. " + std::string(crash_consistent_explanation), LL_WARNING);
				}
				else
				{
					VSSLog("Creating snapshot failed after three tries. Giving up.", LL_ERROR);
					backupcom->AbortBackup();
					backupcom->Release();
					return false;
				}
				break;
			}
			else
			{
				VSSLog("Snapshotting failed because of Writer. Retrying in 30s...", LL_WARNING);
			}
			bool bcom_ok = true;
			CHECK_COM_RESULT_OK(backupcom->BackupComplete(&pb_result), bcom_ok);
			if (bcom_ok)
			{
				wait_for(pb_result, "Completing backup with error status failed");
			}

#ifndef VSS_XP
#ifndef VSS_S03
			if (bcom_ok)
			{
				LONG dels;
				GUID ndels;
				CHECK_COM_RESULT_OK(backupcom->DeleteSnapshots(dir->ref->ssetid, VSS_OBJECT_SNAPSHOT, TRUE,
					&dels, &ndels), bcom_ok);

				if (dels == 0)
				{
					VSSLog("Deleting shadowcopy failed.", LL_ERROR);
				}
			}
#endif
#endif
			backupcom->Release();
			backupcom = NULL;

			Server->wait(30000);
		}
		else
		{
			break;
		}
	}

	for (size_t i = 0; i < additional_refs.size(); ++i)
	{
		VSS_SNAPSHOT_PROP snap_props = {};
		CHECK_COM_RESULT_RELEASE(backupcom->GetSnapshotProperties(additional_refs[i].volid, &snap_props));

		if (snap_props.m_pwszSnapshotDeviceObject == NULL)
		{
			VSSLog("GetSnapshotProperties did not return a volume path", LL_ERROR);
			if (backupcom != NULL) { backupcom->AbortBackup(); backupcom->Release(); }
			return false;
		}

		if (wpath_lower == selected_vols[i])
		{
			dir->target.erase(0, wpath.size());
			dir->ref->volpath = Server->ConvertFromWchar(snap_props.m_pwszSnapshotDeviceObject);
			dir->starttime = Server->getTimeSeconds();
			dir->target = dir->ref->volpath + os_file_sep() + dir->target;
			if (dir->fileserv)
			{
				shareDir(starttoken, dir->dir, dir->target);
			}
		}
		else
		{
			additional_refs[i].volpath = Server->ConvertFromWchar(snap_props.m_pwszSnapshotDeviceObject);
		}

		SShadowCopy tsc;
		tsc.vssid = snap_props.m_SnapshotId;
		tsc.ssetid = snap_props.m_SnapshotSetId;

		if (wpath_lower == selected_vols[i])
		{
			tsc.target = dir->orig_target;
			tsc.orig_target = dir->orig_target;
			tsc.filesrv = dir->fileserv;
			tsc.tname = dir->dir;
		}
		else
		{
			tsc.target = selected_vols[i];
			tsc.orig_target = selected_vols[i];
		}

		tsc.vol = selected_vols[i];
		tsc.path = Server->ConvertFromWchar(snap_props.m_pwszSnapshotDeviceObject);
		tsc.starttoken = starttoken;
		tsc.clientsubname = index_clientsubname;
		if (for_imagebackup)
		{
			tsc.refs = 1;
		}
		if (wpath_lower == selected_vols[i])
		{
			dir->ref->save_id = cd->addShadowcopy(tsc);
			dir->ref->ok = true;
			dir->ref->backupcom = backupcom;

			if (with_components)
			{
				dir->ref->with_writers = true;
			}

		}
		else
		{
			additional_refs[i].save_id = cd->addShadowcopy(tsc);
			additional_refs[i].ok = true;
			additional_refs[i].backupcom = backupcom;

			if (with_components)
			{
				additional_refs[i].with_writers = true;
			}
		}

		if (has_active_transaction!=NULL)
		{
			HANDLE hVolume = CreateFileW((Server->ConvertToWchar(tsc.path)+L"\\").c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);

			if (hVolume != INVALID_HANDLE_VALUE)
			{
				TXFS_TRANSACTION_ACTIVE_INFO trans_active= {};
				DWORD ret_bytes;
				BOOL b = DeviceIoControl(hVolume, FSCTL_TXFS_TRANSACTION_ACTIVE, NULL, 0, &trans_active,
					sizeof(trans_active), &ret_bytes, NULL);

				if (b && trans_active.TransactionsActiveAtSnapshot)
				{
					Server->Log("Shadow copy has active NTFS transaction", LL_INFO);
					*has_active_transaction = true;
				}

				CloseHandle(hVolume);
			}
		}

		VSSLog("Shadowcopy path: " + tsc.path, LL_DEBUG);

		VssFreeSnapshotProperties(&snap_props);
				
		if (onlyref != NULL)
		{
			*onlyref = false;
		}

		if (wpath_lower != selected_vols[i])
		{
			SCRef* nref = new SCRef;
			*nref = additional_refs[i];
			sc_refs.push_back(nref);
		}
	}

	return true;
}

bool IndexThread::deleteShadowcopyWin(SCDirs *dir)
{
	for (size_t i = 0; i < sc_refs.size(); ++i)
	{
		if (sc_refs[i] != dir->ref
			&& sc_refs[i]->ssetid == dir->ref->ssetid)
		{
			dir->ref->backupcom = NULL;
			return true;
		}
	}

	bool ok = true;

	IVssBackupComponents *backupcom = dir->ref->backupcom;

	for (std::map<std::string, SVssInstance*>::iterator it = vss_name_instances.begin();
		it != vss_name_instances.end();)
	{
		std::map<std::string, SVssInstance*>::iterator it_curr = it;
		++it;

		if (it_curr->second->backupcom == backupcom)
		{
			ok = false;
			--it_curr->second->refcount;

			for (size_t i = 0; i < it_curr->second->parents.size(); ++i)
			{
				++it_curr->second->parents[i]->issues;
			}

			if (it_curr->second->refcount == 0)
			{
				HRESULT hr = it_curr->second->backupcom->SetBackupSucceeded(it_curr->second->instanceId,
					it_curr->second->writerId, it_curr->second->componentType,
					it_curr->second->logicalPath.empty() ? NULL : Server->ConvertToWchar(it_curr->second->logicalPath).c_str(),
					Server->ConvertToWchar(it_curr->second->componentName).c_str(),
					false);

				if (hr != S_OK)
				{
					VSSLog("Error setting component \"" + it_curr->second->componentName + "\" with logical path \"" + it_curr->second->logicalPath +
						"\" to failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
				}

				for (size_t i = 0; i < it_curr->second->parents.size(); ++i)
				{
					SVssInstance* parent = it_curr->second->parents[i];

					assert(parent->refcount > 0);
					--parent->refcount;

					if (parent->refcount == 0)
					{
						hr = parent->backupcom->SetBackupSucceeded(parent->instanceId,
							parent->writerId, parent->componentType,
							parent->logicalPath.empty() ? NULL : Server->ConvertToWchar(parent->logicalPath).c_str(),
							Server->ConvertToWchar(parent->componentName).c_str(),
							false);

						if (hr != S_OK)
						{
							VSSLog("Error setting component \"" + parent->componentName + "\" with logical path \"" + parent->logicalPath +
								"\" to failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
						}

						delete parent;
					}
				}

				delete it_curr->second;
			}			

			vss_name_instances.erase(it_curr);
		}
	}

	IVssAsync *pb_result;
	bool bcom_ok = true;
	CHECK_COM_RESULT_OK(backupcom->BackupComplete(&pb_result), bcom_ok);
	if (bcom_ok)
	{
		if (!wait_for(pb_result, "Completing backup failed"))
		{
			ok = false;
		}
	}

	std::string errmsg;
	if (!check_writer_status(backupcom, errmsg, LL_ERROR, NULL))
	{
		if (dir->ref->with_writers)
		{
			ok = false;
		}
	}

	if (dir->ref->with_writers)
	{
		SCOPED_DECLARE_FREE_BSTR(xml);
		HRESULT hr = backupcom->SaveAsXML(&xml);

		if (hr == S_OK)
		{
			std::string component_config_dir = "urbackup\\windows_components_config\\" + conv_filename(starttoken);
			writestring(Server->ConvertFromWchar(xml), component_config_dir + "\\backupcom.xml");
		}
	}

#ifndef VSS_XP
#ifndef VSS_S03
	if (bcom_ok)
	{
		LONG dels;
		GUID ndels;
		CHECK_COM_RESULT_OK(backupcom->DeleteSnapshots(dir->ref->ssetid, VSS_OBJECT_SNAPSHOT_SET, TRUE,
			&dels, &ndels), bcom_ok);

		if (dels == 0)
		{
			VSSLog("Deleting shadowcopy failed.", LL_ERROR);
			ok = false;
		}
	}
#endif
#endif

	removeBackupcomReferences(backupcom);

	backupcom->Release();
	dir->ref->backupcom = NULL;

	return ok;
}

void IndexThread::initVss()
{
	CHECK_COM_RESULT(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	CHECK_COM_RESULT(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IDENTIFY, NULL, EOAC_NONE, NULL));
}

bool IndexThread::deleteSavedShadowCopyWin(SShadowCopy& scs, SShadowCopyContext& context)
{
	IVssBackupComponents *backupcom = NULL;
	if (context.backupcom == NULL)
	{
		CHECK_COM_RESULT_RELEASE(CreateVssBackupComponents(&backupcom));
		CHECK_COM_RESULT_RELEASE(backupcom->InitializeForBackup());
		CHECK_COM_RESULT_RELEASE(backupcom->SetContext(VSS_CTX_APP_ROLLBACK));
	}
	else
	{
		backupcom = context.backupcom;
		context.backupcom = NULL;
	}

	std::vector<SShadowCopy> ascs = cd->getShadowcopies();
	bool set_last = true;
	for (size_t i = 0; i < ascs.size(); ++i)
	{
		if (ascs[i].vssid != scs.vssid
			&& ascs[i].ssetid == scs.ssetid)
		{
			set_last = false;
			break;
		}
	}

	LONG dels;
	GUID ndels;
	CHECK_COM_RESULT(backupcom->DeleteSnapshots(set_last ? scs.ssetid : scs.vssid, 
		set_last ? VSS_OBJECT_SNAPSHOT_SET : VSS_OBJECT_SNAPSHOT, TRUE,
		&dels, &ndels));
	cd->deleteShadowcopy(scs.id);

	context.backupcom = backupcom;

	if (dels>0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool IndexThread::getVssSettings()
{
	std::string settings_fn = "urbackup/data/settings.cfg";
	std::auto_ptr<ISettingsReader> curr_settings(Server->createFileSettingsReader(settings_fn));
	vss_select_components.clear();
	vss_select_all_components = false;
	bool select_default_components = false;
	bool ret = false;
	if (curr_settings.get() != NULL)
	{
		std::string val;
		if (!curr_settings->getValue("vss_select_components", &val)
			 && !curr_settings->getValue("vss_select_components_def", &val))
		{
			val = "default=1";
		}

		if (!val.empty())
		{
			str_map comps;
			ParseParamStrHttp(val, &comps, false);

			vss_select_components.clear();

			for (str_map::iterator it = comps.begin();
				it != comps.end(); ++it)
			{
				if (next(it->first, 0, "writer_"))
				{
					std::string idx = getafter("writer_", it->first);

					SComponent component;

					component.componentName = comps["name_" + idx];
					component.logicalPath = comps["path_" + idx];
					HRESULT hr = IIDFromString(Server->ConvertToWchar(comps["writer_" + idx]).c_str(), &component.writerId);

					if (hr==S_OK)
					{
						vss_select_components.push_back(component);
						ret = true;
					}
				}
			}

			if (!comps["all"].empty()
				&& comps["all"] != "0")
			{
				vss_select_all_components = true;
			}

			if (!comps["default"].empty()
				&& comps["default"] != "0")
			{
				select_default_components = true;
			}
		}
	}

	if (!vss_select_all_components
		&& select_default_components)
	{
		const char* default_writer_ids[] = {
			"{a65faa63-5ea8-4ebc-9dbd-a0c4db26912a}", //MS SQL Server 2014
			"{76fe1ac4-15f7-4bcd-987e-8e1acb462fb7}" //MS Exchange 2010
		};

		for (size_t i = 0; i < sizeof(default_writer_ids) / sizeof(default_writer_ids[0]); ++i)
		{
			SComponent component;
			HRESULT hr = IIDFromString(Server->ConvertToWchar(default_writer_ids[i]).c_str(), &component.writerId);
			if (hr == S_OK)
			{
				vss_select_components.push_back(component);
				ret = true;
			}
		}
	}

	return ret;
}

bool IndexThread::selectVssComponents(IVssBackupComponents *backupcom
	, std::vector<std::string>& selected_vols)
{
	UINT nwriters;
	CHECK_COM_RESULT_RETURN(backupcom->GetWriterMetadataCount(&nwriters));

	std::vector<SComponent> required_dependencies;
	vss_all_components.clear();
	vss_explicitly_selected_components.clear();

	bool added_at_least_one_component;
	do
	{
		added_at_least_one_component = false;

		for (UINT i = 0; i < nwriters; ++i)
		{
			VSS_ID writerInstance;
			SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssExamineWriterMetadata, writerMetadata);
			HRESULT hr = backupcom->GetWriterMetadata(i, &writerInstance, &writerMetadata);

			if (hr != S_OK)
			{
				VSSLog("Getting metadata of writer " + convert(i) + " failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
				continue;
			}

			VSS_ID instanceId;
			VSS_ID writerId;
			SCOPED_DECLARE_FREE_BSTR(writerName);
			VSS_USAGE_TYPE usageType;
			VSS_SOURCE_TYPE sourceType;
			hr = writerMetadata->GetIdentity(&instanceId, &writerId, &writerName, &usageType, &sourceType);

			if (hr != S_OK)
			{
				VSSLog("Getting identity of writer " + convert(i) + " failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
				continue;
			}

			//Always skip System writer and ASR writer
			if (convert(writerId) == "{E8132975-6F93-4464-A53E-1050253AE220}"
				|| convert(writerId) == "{BE000CBE-11FE-4426-9C58-531AA6355FC4}" )
			{
				continue;
			}

			std::string writerNameStr = Server->ConvertFromWchar(writerName);

			UINT nIncludeFiles, nExcludeFiles, nComponents;
			hr = writerMetadata->GetFileCounts(&nIncludeFiles, &nExcludeFiles, &nComponents);
			if (hr != S_OK)
			{
				VSSLog("GetFileCounts of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
				continue;
			}

			for (UINT j = 0; j < nComponents; ++j)
			{
				SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMComponent, wmComponent);
				hr = writerMetadata->GetComponent(j, &wmComponent);
				if (hr != S_OK)
				{
					VSSLog("Error getting component " + convert(j) + " of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
					continue;
				}

				SCOPED_DECLARE_FREE_COMPONENTINFO(wmComponent, componentInfo);
				hr = wmComponent->GetComponentInfo(&componentInfo);
				if (hr != S_OK)
				{
					VSSLog("Error getting component info " + convert(j) + " of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
					continue;
				}

				std::string componentNameStr = Server->ConvertFromWchar(componentInfo->bstrComponentName);

				std::string logicalPathStr;
				if (componentInfo->bstrLogicalPath != NULL)
				{
					logicalPathStr = Server->ConvertFromWchar(componentInfo->bstrLogicalPath);
				}

				SComponent currComponent;
				currComponent.componentName = componentNameStr;
				currComponent.logicalPath = logicalPathStr;
				currComponent.writerId = writerId;

				bool already_selected = std::find(vss_all_components.begin(), vss_all_components.end(),
					currComponent)!= vss_all_components.end();

				bool already_added = already_selected;

				if (!already_selected)
				{
					for (size_t k = 0; k < vss_explicitly_selected_components.size(); ++k)
					{
						std::string logicalPathFull = vss_explicitly_selected_components[k].logicalPath;
						if (logicalPathFull.empty())
						{
							logicalPathFull = vss_explicitly_selected_components[k].componentName;
						}
						else
						{
							logicalPathFull += "\\" + vss_explicitly_selected_components[k].componentName;
						}

						if (vss_explicitly_selected_components[k].writerId == writerId
							&& next(logicalPathStr, 0, logicalPathFull))
						{
							already_selected = true;
							break;
						}
					}
				}

				bool added_curr_component = false;

				bool backup_component = vss_select_all_components;
				if (!backup_component)
				{
					for (size_t k = 0; k < vss_select_components.size(); ++k)
					{
						if (vss_select_components[k].writerId == writerId
							&& vss_select_components[k].logicalPath.empty()
							&& vss_select_components[k].componentName.empty())
						{
							backup_component = true;
							added_curr_component = true;
						}
						else if (vss_select_components[k].writerId == writerId)
						{
							backup_component = true;
						}
					}
				}

				if (!already_selected
					&& backup_component)
				{
					if (!componentInfo->bSelectable)
					{
						hr = backupcom->AddComponent(instanceId, writerId, componentInfo->type, componentInfo->bstrLogicalPath, componentInfo->bstrComponentName);

						if (hr != S_OK)
						{
							VSSLog("Error adding component \"" + componentNameStr + "\" of writer \"" + writerNameStr + "\" to backup (1). VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
						}
						else
						{
							added_curr_component = true;
						}
					}
					else
					{
						if (added_curr_component
							|| std::find(vss_select_components.begin(), vss_select_components.end(),
							currComponent) != vss_select_components.end())
						{
							hr = backupcom->AddComponent(instanceId, writerId, componentInfo->type, componentInfo->bstrLogicalPath, componentInfo->bstrComponentName);

							if (hr != S_OK)
							{
								VSSLog("Error adding component \"" + componentNameStr + "\" of writer \"" + writerNameStr + "\" to backup (1). VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							}
							else
							{
								added_curr_component = true;
								vss_explicitly_selected_components.push_back(currComponent);
							}							
						}
					}
				}

				if (added_curr_component
					|| (!already_added && already_selected) )
				{
					added_at_least_one_component = true;

					vss_all_components.push_back(currComponent);

					std::vector<SComponent>::iterator
						it = std::find(required_dependencies.begin(), required_dependencies.end(),
							currComponent);

					if (it != required_dependencies.end())
					{
						required_dependencies.erase(it);
					}

					for (UINT k = 0; k < componentInfo->cDependencies; ++k)
					{
						SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMDependency, wmDependency);
						hr = wmComponent->GetDependency(k, &wmDependency);

						if (hr != S_OK)
						{
							VSSLog("Error getting component dependency " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							continue;
						}

						SCOPED_DECLARE_FREE_BSTR(componentName);
						hr = wmDependency->GetComponentName(&componentName);

						if (hr != S_OK)
						{
							VSSLog("Error getting component name of dependency " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							continue;
						}

						SCOPED_DECLARE_FREE_BSTR(logicalPath);
						hr = wmDependency->GetLogicalPath(&logicalPath);

						if (hr != S_OK)
						{
							VSSLog("Error getting logical path of dependency " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							continue;
						}

						SComponent newDependency;
						newDependency.componentName = Server->ConvertFromWchar(componentName);
						if (logicalPath != NULL)
						{
							newDependency.logicalPath = Server->ConvertFromWchar(logicalPath);
						}

						hr = wmDependency->GetWriterId(&newDependency.writerId);

						if (hr != S_OK)
						{
							VSSLog("Error getting writer id of dependency " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							continue;
						}

						required_dependencies.push_back(newDependency);
					}

					for (UINT k = 0; k < componentInfo->cFileCount; ++k)
					{
						SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMFiledesc, wmFile);
						hr = wmComponent->GetFile(k, &wmFile);

						if (hr != S_OK)
						{
							VSSLog("Error getting component file desc " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							continue;
						}

						if (!addFilespecVol(wmFile, selected_vols))
						{
							VSSLog("Error adding file desc " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed.", LL_ERROR);
						}
					}

					for (UINT k = 0; k < componentInfo->cDatabases; ++k)
					{
						SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMFiledesc, wmFile);
						hr = wmComponent->GetDatabaseFile(k, &wmFile);

						if (hr != S_OK)
						{
							VSSLog("Error getting database file desc " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							continue;
						}

						if (!addFilespecVol(wmFile, selected_vols))
						{
							VSSLog("Error adding database file desc " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed.", LL_ERROR);
						}
					}

					for (UINT k = 0; k < componentInfo->cLogFiles; ++k)
					{
						SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMFiledesc, wmFile);
						hr = wmComponent->GetDatabaseLogFile(k, &wmFile);

						if (hr != S_OK)
						{
							VSSLog("Error getting database log file desc " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
							continue;
						}

						if (!addFilespecVol(wmFile, selected_vols))
						{
							VSSLog("Error adding database log file desc " + convert(k) + " of component \"" +
								componentNameStr + "\" of writer \"" + writerNameStr + "\" failed.", LL_ERROR);
						}
					}
				}
			}
		}
	} while (!required_dependencies.empty() && added_at_least_one_component);

	if (!required_dependencies.empty())
	{
		for (size_t i = 0; i < required_dependencies.size(); ++i)
		{			
			VSSLog("Unresolved VSS component dependency writer id " + convert(required_dependencies[i].writerId) +
				" component name \"" + required_dependencies[i].componentName + "\" logical path \"" +
				required_dependencies[i].logicalPath + "\"", LL_ERROR);
		}
	}

	return true;
}

bool IndexThread::addFilespecVol(IVssWMFiledesc* wmFile, std::vector<std::string>& selected_vols)
{
	SCOPED_DECLARE_FREE_BSTR(bPath);
	HRESULT hr = wmFile->GetPath(&bPath);

	if (hr != S_OK)
	{
		VSSLog("Getting path of file spec failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
		return false;
	}

	SCOPED_DECLARE_FREE_BSTR(bAltPath);
	wmFile->GetAlternateLocation(&bAltPath);

	BSTR tpath = bAltPath != NULL ? bAltPath : bPath;

	std::vector<wchar_t> tpathRepl(32768);

	DWORD rc = ExpandEnvironmentStringsW(tpath, tpathRepl.data(), static_cast<DWORD>(tpathRepl.size()));

	if (rc >= tpathRepl.size() || rc == 0)
	{
		VSSLog("Replacing environment strings in path \""+ Server->ConvertFromWchar(tpath) + "\" failed", LL_ERROR);
		return false;
	}

	std::string vol = strlower(getVolPath(Server->ConvertFromWchar(tpathRepl.data())));

	if (vol.empty())
	{
		VSSLog("Could not determine volume of path \"" + Server->ConvertFromWchar(tpath) + "\"", LL_ERROR);
		return false;
	}

	if (std::find(selected_vols.begin(), selected_vols.end(), vol) == selected_vols.end())
	{
		selected_vols.push_back(vol);
	}

	return true;
}

std::string IndexThread::getVolPath(const std::string& bpath)
{
	std::string prefixedbpath = os_file_prefix(bpath);
	std::wstring tvolume;
	tvolume.resize(prefixedbpath.size() + 100);
	DWORD cchBufferLength = static_cast<DWORD>(tvolume.size());
	BOOL b = GetVolumePathNameW(Server->ConvertToWchar(prefixedbpath).c_str(), &tvolume[0], cchBufferLength);
	if (!b)
	{
		return std::string();
	}

	std::string volume = Server->ConvertFromWchar(tvolume.c_str());
	std::string volume_lower = strlower(volume);

	if (volume.find("\\\\?\\") == 0
		&& volume_lower.find("\\\\?\\globalroot") != 0
		&& volume_lower.find("\\\\?\\volume") != 0)
	{
		volume.erase(0, 4);
	}

	return volume;
}

bool IndexThread::indexVssComponents(VSS_ID ssetid, bool use_db, const std::vector<SCRef*>& past_refs, std::fstream &outfile)
{
	IVssBackupComponents* backupcom = NULL;
	for (size_t i = 0; i < sc_refs.size(); ++i)
	{
		if (sc_refs[i]->ssetid == ssetid)
		{
			backupcom = sc_refs[i]->backupcom;
			break;
		}
	}

	if (backupcom == NULL)
	{
		VSSLog("Could not find backupcom for snapshot set " + convert(ssetid), LL_ERROR);
		return false;
	}

	for (std::map<std::string, SVssInstance*>::iterator it = vss_name_instances.begin();
		it != vss_name_instances.end();)
	{
		if (!next(it->first, 0, starttoken + "|"))
		{
			++it;
			continue;
		}

		--it->second->refcount;

		if (it->second->refcount == 0)
		{
			for (size_t i = 0; i < it->second->parents.size(); ++i)
			{
				--it->second->parents[i]->refcount;
				if (it->second->parents[i]->refcount == 0)
				{
					delete it->second->parents[i];
				}
			}

			delete it->second;
		}

		std::map<std::string, SVssInstance*>::iterator delit = it++;
		vss_name_instances.erase(delit);
	}

	std::string component_config_dir = "urbackup\\windows_components_config\\" + conv_filename(starttoken);
	std::string components_dir = "urbackup\\windows_components";

	if (os_directory_exists(component_config_dir))
	{
		if (!os_remove_nonempty_dir(component_config_dir))
		{
			VSSLog("Error removing directory \""+ component_config_dir+"\". " + os_last_error_str(), LL_ERROR);
			return false;
		}
	}

	if (!os_create_dir_recursive(component_config_dir))
	{
		VSSLog("Error creating directory \""+ component_config_dir+"\". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	writestring("", component_config_dir + "\\backupcom.xml");

	if (!change_file_permissions_admin_only(component_config_dir))
	{
		VSSLog("Error setting permissions of directory \""+ component_config_dir+"\" to admin only. " + os_last_error_str(), LL_ERROR);
		return false;
	}

	if (!os_directory_exists(components_dir))
	{
		if (!os_create_dir_recursive(components_dir))
		{
			VSSLog("Error creating directory \"" + components_dir + "\". " + os_last_error_str(), LL_ERROR);
			return false;
		}
	}

	if (!os_directory_exists(components_dir + os_file_sep() + "dummy"))
	{
		if (!os_create_dir(components_dir + os_file_sep() + "dummy"))
		{
			VSSLog("Error creating directory \"" + components_dir + os_file_sep() + "dummy" + "\". " + os_last_error_str(), LL_ERROR);
			return false;
		}
	}

	shareDir(starttoken, "windows_components_config", Server->getServerWorkingDir() + os_file_sep() + component_config_dir);
	shareDir(starttoken, "windows_components", Server->getServerWorkingDir() + os_file_sep() + components_dir);

	UINT nwriters;
	CHECK_COM_RESULT_RETURN(backupcom->GetWriterMetadataCount(&nwriters));

	std::vector<std::string> component_config_files;
	std::vector<int64> component_config_file_size;

	JSON::Object info_json;
	JSON::Array selected_components_json;

	std::string pretty_symlink_struct = "d\"windows_components\"\n";

	for (UINT i = 0; i < nwriters; ++i)
	{
		VSS_ID writerInstance;
		SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssExamineWriterMetadata, writerMetadata);
		HRESULT hr = backupcom->GetWriterMetadata(i, &writerInstance, &writerMetadata);

		if (hr != S_OK)
		{
			VSSLog("Getting metadata of writer " + convert(i) + " failed while indexing. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
			return false;
		}

		VSS_ID instanceId;
		VSS_ID writerId;
		SCOPED_DECLARE_FREE_BSTR(writerName);
		VSS_USAGE_TYPE usageType;
		VSS_SOURCE_TYPE sourceType;
		hr = writerMetadata->GetIdentity(&instanceId, &writerId, &writerName, &usageType, &sourceType);

		if (hr != S_OK)
		{
			VSSLog("Getting identity of writer " + convert(i) + " failed while indexing. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
			return false;
		}

		std::string writerNameStr = Server->ConvertFromWchar(writerName);

		UINT nIncludeFiles, nExcludeFiles, nComponents;
		hr = writerMetadata->GetFileCounts(&nIncludeFiles, &nExcludeFiles, &nComponents);
		if (hr != S_OK)
		{
			VSSLog("GetFileCounts of writer \"" + writerNameStr + "\" failed while indexing. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
			return false;
		}

		bool has_component = false;

		std::string writerNameStrShort = writerNameStr;
		if (writerNameStrShort.size() > 100)
		{
			writerNameStrShort = writerNameStr.substr(0, 100);
		}

		std::string curr_dir = conv_filename(writerNameStrShort) + "_" + convert(writerId);
		std::string named_path_base = ".symlink_"+ curr_dir;

		std::string pretty_symlink_struct_writer = "d\"" + sortHex(i) + "_" + conv_filename(writerNameStrShort) + "\"\n";
		std::string pretty_struct_base = components_dir + os_file_sep() + sortHex(i) + "_" + conv_filename(writerNameStrShort);
		if (!os_directory_exists(pretty_struct_base))
		{
			os_create_dir(pretty_struct_base);
		}

		std::vector<std::string> exclude_files;
		std::vector<SVssInstance*> component_vss_instances;

		for (UINT j = 0; j < nComponents; ++j)
		{
			SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMComponent, wmComponent);
			hr = writerMetadata->GetComponent(j, &wmComponent);
			if (hr != S_OK)
			{
				VSSLog("Error getting component " + convert(j) + " of writer \"" + writerNameStr + "\" failed while indexing. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
				return false;
			}

			SCOPED_DECLARE_FREE_COMPONENTINFO(wmComponent, componentInfo);
			hr = wmComponent->GetComponentInfo(&componentInfo);
			if (hr != S_OK)
			{
				VSSLog("Error getting component info " + convert(j) + " of writer \"" + writerNameStr + "\" failed while indexing. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
				return false;
			}

			std::string componentNameStr = Server->ConvertFromWchar(componentInfo->bstrComponentName);

			std::string logicalPathStr;
			if (componentInfo->bstrLogicalPath != NULL)
			{
				logicalPathStr = Server->ConvertFromWchar(componentInfo->bstrLogicalPath);
			}

			std::string componentNameStrShort = componentNameStr;
			if (componentNameStrShort.size() > 100)
			{
				componentNameStrShort = componentNameStr.substr(0, 100);
			}

			std::string logicalPathHash;
			if (!logicalPathStr.empty())
			{
				logicalPathHash = "_" + Server->GenerateHexMD5(logicalPathStr);
			}

			curr_dir = sortHex(j) + "_" + conv_filename(componentNameStrShort);
			std::string named_path = named_path_base + logicalPathHash + "_" + conv_filename(componentNameStrShort);

			SComponent currComponent;
			currComponent.componentName = componentNameStr;
			currComponent.logicalPath = logicalPathStr;
			currComponent.writerId = writerId;

			if (std::find(vss_all_components.begin(),
				vss_all_components.end(), currComponent)!=vss_all_components.end())
			{
				if (!has_component)
				{
					if (!getExcludedFiles(writerMetadata, nExcludeFiles, exclude_files))
					{
						VSSLog("Error getting excluded files of writer \"" + writerNameStr + "\" failed while indexing.", LL_ERROR);
						return false;
					}
				}

				JSON::Object currComponentJson;
				currComponentJson.set("writerId", convert(writerId));
				currComponentJson.set("logicalPath", logicalPathStr);
				currComponentJson.set("componentName", componentNameStr);
				selected_components_json.add(currComponentJson);

				SVssInstance* vssInstance = new SVssInstance;
				ScopedFreeVssInstance freeVssInstance(vssInstance);

				vssInstance->refcount = 0;
				vssInstance->issues = 0;
				vssInstance->componentName = componentNameStr;
				vssInstance->logicalPath = logicalPathStr;
				vssInstance->writerId = writerId;
				vssInstance->instanceId = instanceId;
				vssInstance->backupcom = backupcom;
				vssInstance->componentType = componentInfo->type;
				vssInstance->set_succeeded = std::find(vss_explicitly_selected_components.begin(),
					vss_explicitly_selected_components.end(), currComponent) != vss_explicitly_selected_components.end();

				std::string pretty_struct_component_path = pretty_struct_base + os_file_sep() + curr_dir;
				if (!os_directory_exists(pretty_struct_component_path))
				{
					os_create_dir(pretty_struct_component_path);
				}

				pretty_symlink_struct_writer += "d\"" + curr_dir + "\"\n";

				has_component = true;
				const uint64 symlink_mask = 0x7000000000000000ULL;
				std::string symlink_change_indicator = convert(symlink_mask);

				for (UINT k = 0; k < componentInfo->cFileCount; ++k)
				{
					SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMFiledesc, wmFile);
					hr = wmComponent->GetFile(k, &wmFile);

					if (hr != S_OK)
					{
						VSSLog("Error getting component file desc " + convert(k) + " of component \"" +
							componentNameStr + "\" of writer \"" + writerNameStr + "\" failed while indexing. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
						return false;
					}

					if (!os_directory_exists(pretty_struct_component_path +os_file_sep()+"files"+ sortHex(k)))
					{
						bool is_dir = true;
						os_link_symbolic(os_file_prefix(Server->getServerWorkingDir() + os_file_sep() + components_dir + os_file_sep() + "dummy"),
							os_file_prefix(Server->getServerWorkingDir() + os_file_sep() + pretty_struct_component_path + os_file_sep() + "files" + sortHex(k)), NULL, &is_dir);
					}

					pretty_symlink_struct_writer += "d\"files" + sortHex(k) + "\" 0 "+ symlink_change_indicator+"#special=1&sym_target="+ EscapeParamString(named_path + "_files" + sortHex(k))+"\nu\n";
					if (!addFiles(wmFile, ssetid, past_refs, named_path+"_files"+ sortHex(k), use_db, exclude_files, outfile))
					{
						VSSLog("Error indexing files " + convert(k) + " of component \"" +
							componentNameStr + "\" of writer \"" + writerNameStr + "\".", LL_ERROR);
						return false;
					}

					vss_name_instances[starttoken + "|" + named_path + "_files" + sortHex(k)] = vssInstance;
					++vssInstance->refcount;
				}

				for (UINT k = 0; k < componentInfo->cDatabases; ++k)
				{
					SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMFiledesc, wmFile);
					hr = wmComponent->GetDatabaseFile(k, &wmFile);

					if (hr != S_OK)
					{
						VSSLog("Error getting database file desc " + convert(k) + " of component \"" +
							componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
						return false;
					}

					if (!os_directory_exists(os_file_prefix(pretty_struct_component_path + os_file_sep() + "database" + sortHex(k))))
					{
						bool is_dir = true;
						os_link_symbolic(os_file_prefix(Server->getServerWorkingDir() + os_file_sep() + components_dir + os_file_sep() + "dummy"),
							os_file_prefix(Server->getServerWorkingDir() + os_file_sep() + pretty_struct_component_path + os_file_sep() + "database" + sortHex(k)), NULL, &is_dir);
					}

					pretty_symlink_struct_writer += "d\"database" + sortHex(k) + "\" 0 " + symlink_change_indicator + "#special=1&sym_target=" + EscapeParamString(named_path + "_database" + sortHex(k)) + "\nu\n";
					if (!addFiles(wmFile, ssetid, past_refs, named_path+"_database"+ sortHex(k), use_db, exclude_files, outfile))
					{
						VSSLog("Error indexing database file " + sortHex(k) + " of component \"" +
							componentNameStr + "\" of writer \"" + writerNameStr + "\".", LL_ERROR);
						return false;
					}

					vss_name_instances[starttoken + "|" + named_path + "_database" + sortHex(k)] = vssInstance;
					++vssInstance->refcount;
				}

				for (UINT k = 0; k < componentInfo->cLogFiles; ++k)
				{
					SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMFiledesc, wmFile);
					hr = wmComponent->GetDatabaseLogFile(k, &wmFile);

					if (hr != S_OK)
					{
						VSSLog("Error getting database log file desc " + convert(k) + " of component \"" +
							componentNameStr + "\" of writer \"" + writerNameStr + "\" failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
						return false;
					}

					if (!os_directory_exists(pretty_struct_component_path + os_file_sep() + "database_log" + convert(k)))
					{
						bool is_dir = true;
						os_link_symbolic(os_file_prefix(Server->getServerWorkingDir() + os_file_sep() + components_dir + os_file_sep() + "dummy"),
							os_file_prefix(Server->getServerWorkingDir() + os_file_sep() + pretty_struct_component_path + os_file_sep() + "database_log" + sortHex(k)), NULL, &is_dir);
					}

					pretty_symlink_struct_writer += "d\"database_log" + sortHex(k) + "\" 0 " + symlink_change_indicator + "#special=1&sym_target=" + EscapeParamString(named_path + "_database_log" + sortHex(k)) + "\nu\n";
					if (!addFiles(wmFile, ssetid, past_refs, named_path+"_database_log"+ sortHex(k), use_db, exclude_files, outfile))
					{
						VSSLog("Error indexing database log file " + sortHex(k) + " of component \"" +
							componentNameStr + "\" of writer \"" + writerNameStr + "\".", LL_ERROR);
						return false;
					}

					vss_name_instances[starttoken + "|" + named_path + "_database_log" + sortHex(k)] = vssInstance;
					++vssInstance->refcount;
				}

				std::string completeLogicalPathStr = logicalPathStr;
				if (completeLogicalPathStr.empty())
				{
					completeLogicalPathStr = componentNameStr;
				}
				else
				{
					completeLogicalPathStr += "\\" + componentNameStr;
				}

				for (std::vector<SVssInstance*>::iterator it = component_vss_instances.begin();
					it != component_vss_instances.end(); ++it)
				{
					std::string logicalPath = (*it)->logicalPath;

					if (logicalPath.empty())
					{
						logicalPath = (*it)->componentName;
					}
					else
					{
						logicalPath += "\\" + (*it)->componentName;
					}

					if (*it != vssInstance
						&& completeLogicalPathStr != logicalPath )
					{
						if (next(completeLogicalPathStr, 0, logicalPath))
						{
							bool already_present = false;
							for (size_t k = 0; k < vssInstance->parents.size(); ++k)
							{
								if (*vssInstance->parents[k] == *(*it))
								{
									already_present = true;
								}
							}
							if (!already_present)
							{
								vssInstance->parents.push_back(*it);
								++(*it)->refcount;
							}
						}
						else if(next(logicalPath, 0, completeLogicalPathStr))
						{
							bool already_present = false;
							for (size_t k = 0; k < (*it)->parents.size(); ++k)
							{
								if (*(*it)->parents[k] == *vssInstance)
								{
									already_present = true;
								}
							}
							if (!already_present)
							{
								(*it)->parents.push_back(vssInstance);
								++vssInstance->refcount;
							}
						}
					}
				}

				++vssInstance->refcount;
				component_vss_instances.push_back(vssInstance);

				pretty_symlink_struct_writer += "u\n";
			}
		}

		for (size_t j = 0; j < component_vss_instances.size(); ++j)
		{
			assert(component_vss_instances[j]->refcount>0);
			--component_vss_instances[j]->refcount;
			if (component_vss_instances[j]->refcount==0)
			{
				delete component_vss_instances[j];
			}
		}

		if (has_component)
		{
			SCOPED_DECLARE_FREE_BSTR(componentAsXml);
			hr = writerMetadata->SaveAsXML(&componentAsXml);

			if (hr != S_OK)
			{
				VSSLog("Getting component config XML of writer \"" + writerNameStr + "\" failed while indexing. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
				return false;
			}

			std::string utf8Xml = Server->ConvertFromWchar(componentAsXml);
			if (!write_file_only_admin(utf8Xml, component_config_dir + os_file_sep() + convert(writerId) + ".xml"))
			{
				VSSLog("Error writing component config to \"" + component_config_dir + os_file_sep() + convert(writerId) + ".xml\". " + os_last_error_str(), LL_ERROR);
				return false;
			}

			component_config_files.push_back(convert(writerId) + ".xml");
			component_config_file_size.push_back(utf8Xml.size());

			pretty_symlink_struct += pretty_symlink_struct_writer+"u\n";
		}
	}

	pretty_symlink_struct += "u\n";

	addFromLastUpto("windows_components", true, 0, false, outfile);
	outfile << pretty_symlink_struct;

	addFromLastUpto("windows_components_config", true, 0, false, outfile);
	outfile << "d\"windows_components_config\" 0 0#orig_path="+EscapeParamString("C:\\windows_components_config")+"\n";
	for (size_t i = 0; i < component_config_files.size(); ++i)
	{
		int64 rnd = Server->getTimeSeconds() << 32 | Server->getRandomNumber();
		if (rnd > 0) rnd *= -1;
		outfile << "f\"" << component_config_files[i] << "\" " << component_config_file_size[i] << " " << rnd << "\n";
	}

	info_json.set("selected_components", selected_components_json);
	std::string selected_components_data = info_json.stringify(false);

	int64 rnd = Server->getTimeSeconds() << 32 | Server->getRandomNumber();
	if (rnd > 0) rnd *= -1;
	outfile << "f\"backupcom.xml\" 0 " << rnd << "\n";
	outfile << "f\"info.json\" " << selected_components_data.size() << " " << rnd << "\n";

	if (!write_file_only_admin(selected_components_data, component_config_dir + os_file_sep() + "info.json"))
	{
		VSSLog("Error writing component info to \"" + component_config_dir + os_file_sep() + "info.json\". " + os_last_error_str(), LL_ERROR);
		return false;
	}

	outfile << "u\n";

	return true;
}

std::string IndexThread::expandPath(BSTR pathStr)
{
	std::vector<wchar_t> tpathRepl(MAX_PATH);

	DWORD rc = ExpandEnvironmentStringsW(pathStr, tpathRepl.data(), static_cast<DWORD>(tpathRepl.size()));

	if (rc >= tpathRepl.size() || rc == 0)
	{
		tpathRepl.resize(32768);
		VSSLog("Replacing environment strings in path \"" + Server->ConvertFromWchar(pathStr) + "\" failed", LL_ERROR);
		return std::string();
	}

	rc = ExpandEnvironmentStringsW(pathStr, tpathRepl.data(), static_cast<DWORD>(tpathRepl.size()));

	if (rc >= tpathRepl.size() || rc == 0)
	{
		VSSLog("Replacing environment strings in path \"" + Server->ConvertFromWchar(pathStr) + "\" failed", LL_ERROR);
		return std::string();
	}

	return Server->ConvertFromWchar(tpathRepl.data());
}

void IndexThread::removeBackupcomReferences(IVssBackupComponents * backupcom)
{
	for (std::map<std::string, SVssInstance*>::iterator it = vss_name_instances.begin();
		it != vss_name_instances.end();)
	{
		std::map<std::string, SVssInstance*>::iterator it_curr = it;
		++it;

		if (it_curr->second->backupcom == backupcom)
		{
			--it_curr->second->refcount;

			if (it_curr->second->refcount == 0)
			{
				for (size_t i = 0; i < it_curr->second->parents.size(); ++i)
				{
					--it_curr->second->parents[i]->refcount;
					if (it_curr->second->parents[i]->refcount == 0)
					{
						delete it_curr->second->parents[i];
					}
				}

				delete it_curr->second;
			}

			vss_name_instances.erase(it_curr);
		}
	}
}

bool IndexThread::addFiles(IVssWMFiledesc* wmFile, VSS_ID ssetid, const std::vector<SCRef*>& past_refs, std::string named_prefix, bool use_db,
	const std::vector<std::string>& exclude_files, std::fstream &outfile)
{
	SCOPED_DECLARE_FREE_BSTR(bPath);
	HRESULT hr = wmFile->GetPath(&bPath);

	if (hr != S_OK)
	{
		VSSLog("Getting path of file spec failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
		return false;
	}

	SCOPED_DECLARE_FREE_BSTR(bAltPath);
	wmFile->GetAlternateLocation(&bAltPath);

	BSTR tpath = bAltPath != NULL ? bAltPath : bPath;

	std::string path = removeDirectorySeparatorAtEnd(strlower(expandPath(tpath)));

	std::string vol = strlower(getVolPath(path));

	std::string orig_path;
	if (bAltPath != NULL)
	{
		orig_path = removeDirectorySeparatorAtEnd(expandPath(bPath));
	}

	if (vol.empty())
	{
		VSSLog("Could not determine volume of path \"" + path + "\"", LL_ERROR);
		return false;
	}

	if (!next(path, 0, vol))
	{
		VSSLog("Path \""+path+"\" does not start with volume \"" + vol + "\"", LL_ERROR);
		return false;
	}

	std::string volpath;
	std::string vssvolume;
	for (size_t i = 0; i < sc_refs.size(); ++i)
	{
		if (sc_refs[i]->ssetid == ssetid
			&& sc_refs[i]->target == vol)
		{
			volpath = add_trailing_slash(sc_refs[i]->volpath) + path.substr(vol.size());
			vssvolume = sc_refs[i]->volpath;
			break;
		}
	}

	if (volpath.empty())
	{
		VSSLog("Cannot find VSS snapshot for volume \"" + vol + "\"", LL_ERROR);
		return false;
	}

	bool found_backupdir = false;
	for (size_t i = 0; i<backup_dirs.size(); ++i)
	{
		if (backup_dirs[i].group != index_group
			&& backup_dirs[i].group!= c_group_vss_components)
			continue;

		if (backup_dirs[i].symlinked
			&& !(index_flags & EBackupDirFlag_FollowSymlinks))
			continue;

		std::string bpath = addDirectorySeparatorAtEnd(backup_dirs[i].path);
		std::string bpath_wo_slash;

		bpath = strlower(bpath);
		bpath_wo_slash = removeDirectorySeparatorAtEnd(bpath);

		if (path == bpath_wo_slash
			|| next(path, 0, bpath))
		{
			if (backup_dirs[i].group == c_group_vss_components)
			{
				backup_dirs[i].symlinked_confirmed = true;
			}
		
			found_backupdir = true;
			break;
		}
	}

	if (!found_backupdir)
	{
		SBackupDir backup_dir;

		cd->addBackupDir(named_prefix, path, 0, index_flags, c_group_vss_components, 0);

		backup_dir.id = static_cast<int>(db->getLastInsertID());

		if (dwt != NULL)
		{
			std::string msg = "A" + path;
			dwt->getPipe()->Write(msg);
		}

		backup_dir.group = c_group_vss_components;
		backup_dir.flags = index_flags;
		backup_dir.path = path;
		backup_dir.tname = named_prefix;
		backup_dir.symlinked = false;
		backup_dir.symlinked_confirmed = true;
		backup_dir.server_default = index_server_default;

		backup_dirs.push_back(backup_dir);
	}

	SCDirs *scd = getSCDir(named_prefix, index_clientsubname, false);
	if (!scd->running)
	{
		scd->dir = named_prefix;
		scd->starttime = Server->getTimeSeconds();
		scd->target = path;
		scd->orig_target = scd->target;
	}
	scd->fileserv = true;

	bool onlyref = true;
	bool stale_shadowcopy = false;
	bool shadowcopy_not_configured = false;
	if (!start_shadowcopy(scd, &onlyref, true, true, past_refs, false, &stale_shadowcopy, &shadowcopy_not_configured))
	{
		VSSLog("Starting shadow copy for \"" + named_prefix + "\" failed.", LL_ERROR);
		return false;
	}

	if (!onlyref)
	{
		VSSLog("Shadow copy for \"" + named_prefix + "\" could not be referenced", LL_ERROR);
		return false;
	}

	if (stale_shadowcopy)
	{
		VSSLog("Shadow copy for \"" + named_prefix + "\" is stale", LL_ERROR);
		return false;
	}

	if (shadowcopy_not_configured)
	{
		VSSLog("Shadow copy for \"" + named_prefix + "\" is not configured", LL_ERROR);
		return false;
	}

	scd->running = true;

	shareDir(starttoken, named_prefix, volpath);
		
	normalizeVolume(vssvolume);

	SCOPED_DECLARE_FREE_BSTR(filespec);
	hr = wmFile->GetFilespec(&filespec);
	if (hr != S_OK)
	{
		VSSLog("Getting file spec failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
		return false;
	}

	bool dir_recurse = false;
	hr = wmFile->GetRecursive(&dir_recurse);
	if (hr != S_OK)
	{
		VSSLog("Getting recursion flag for file spec failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
		return false;
	}

	std::vector<int> include_depth;
	std::vector<std::string> include_prefix;
	std::vector<SIndexInclude> include_files = parseIncludePatterns(named_prefix + "\\" + Server->ConvertFromWchar(filespec) + ";"
		+ named_prefix + "\\*\\" + Server->ConvertFromWchar(filespec));

	if (path.find("127d0a1d-4ef2-11d1-8608-00c04fc295ee") != std::string::npos)
	{
		int abc = 5;
	}

	openCbtHdatFile(scd->ref, named_prefix, vol);

	initialCheck(vol, vssvolume, path, volpath, named_prefix, outfile, true, index_flags, use_db, false, 0,
		dir_recurse, false, exclude_files, include_files, orig_path);

	return true;
}

bool IndexThread::getExcludedFiles(IVssExamineWriterMetadata* writerMetadata, UINT nExcludedFiles, std::vector<std::string>& exclude_files)
{
	for (UINT k = 0; k < nExcludedFiles; ++k)
	{
		SCOPED_DECLARE_RELEASE_IUNKNOWN(IVssWMFiledesc, wmFile);
		HRESULT hr = writerMetadata->GetExcludeFile(k, &wmFile);
		if (hr != S_OK)
		{
			VSSLog("Error getting exluded file " + convert(k) + " from writer metadata", LL_ERROR);
			return false;
		}

		SCOPED_DECLARE_FREE_BSTR(bPath);
		hr = wmFile->GetPath(&bPath);

		if (hr != S_OK)
		{
			VSSLog("Getting path of file excluded spec failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
			return false;
		}

		SCOPED_DECLARE_FREE_BSTR(bAltPath);
		wmFile->GetAlternateLocation(&bAltPath);

		BSTR tpath = bAltPath != NULL ? bAltPath : bPath;

		std::vector<wchar_t> tpathRepl(32768);

		DWORD rc = ExpandEnvironmentStringsW(tpath, tpathRepl.data(), static_cast<DWORD>(tpathRepl.size()));

		if (rc >= tpathRepl.size() || rc == 0)
		{
			VSSLog("Replacing environment strings in path \"" + Server->ConvertFromWchar(tpath) + "\" failed", LL_ERROR);
			return false;
		}

		std::string path = Server->ConvertFromWchar(tpathRepl.data());

		SCOPED_DECLARE_FREE_BSTR(filespec);
		hr = wmFile->GetFilespec(&filespec);
		if (hr != S_OK)
		{
			VSSLog("Getting excluded file spec failed. VSS error code " + GetErrorHResErrStr(hr), LL_ERROR);
			return false;
		}

		if (path.empty())
		{
			path = Server->ConvertFromWchar(filespec);
		}
		else
		{
			if (path.size() > 0 &&
				(path[path.size() - 1] == '\\' ||
					path[path.size() - 1] == '/'))
			{
				path += Server->ConvertFromWchar(filespec);
			}
			else
			{
				path += os_file_sep() + Server->ConvertFromWchar(filespec);
			}
		}

		strupper(&path);

		exclude_files.push_back(sanitizePattern(path));
	}

	return true;
}

void IndexThread::removeUnconfirmedVssDirs()
{
	for (size_t i = 0; i<backup_dirs.size();)
	{
		if (backup_dirs[i].group == c_group_vss_components)
		{
			if (!backup_dirs[i].symlinked_confirmed)
			{
				VSSLog("Removing unconfirmed VSS path \"" + backup_dirs[i].tname + "\" to \"" + backup_dirs[i].path, LL_INFO);

				if (dwt != NULL)
				{
					std::string msg = "D" + backup_dirs[i].path;
					dwt->getPipe()->Write(msg);
				}

				cd->delBackupDir(backup_dirs[i].id);

				removeDir(starttoken, backup_dirs[i].tname);

				if (filesrv != NULL)
				{
					filesrv->removeDir(backup_dirs[i].tname, starttoken);
				}

				backup_dirs.erase(backup_dirs.begin() + i);

				continue;
			}
		}
		++i;
	}
}
