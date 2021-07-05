/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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
#include "RansomwareCanary.h"
#include <thread>
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../urbackupcommon/glob.h"
#include "../common/miniz.h"
#include "clientdao.h"
#include "database.h"
#ifdef _WIN32
#include <aclapi.h>
#endif

struct SOwner
{
	bool has_owner = false;
#ifdef _WIN32
	PSID Owner = nullptr;
	PSECURITY_DESCRIPTOR sec_d = nullptr;

	~SOwner() {
		if (sec_d != nullptr)
			LocalFree(sec_d);
	}
#endif
};

static bool getOwner(const std::string& fn, SOwner& owner)
{
#ifdef _WIN32
	PSID newOwner = nullptr;
	PSECURITY_DESCRIPTOR new_sec_d = nullptr;
	DWORD rc = GetNamedSecurityInfoW(Server->ConvertToWchar(os_file_prefix(fn)).c_str(),
		SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &newOwner, NULL, NULL,
		NULL, &new_sec_d);

	if (rc == ERROR_SUCCESS)
	{
		if (owner.sec_d != nullptr)
			LocalFree(owner.sec_d);

		owner.Owner = newOwner;
		owner.sec_d = new_sec_d;
		owner.has_owner = true;
	}
	else if (new_sec_d != nullptr)
	{
		LocalFree(new_sec_d);
	}
	return rc == ERROR_SUCCESS;
#else
	//TODO: Implement
	return false;
#endif
}

#ifdef _WIN32
HRESULT ModifyPrivilege(
	IN LPCTSTR szPrivilege,
	IN BOOL fEnable)
{
	HRESULT hr = S_OK;
	TOKEN_PRIVILEGES NewState;
	LUID             luid;
	HANDLE hToken = NULL;

	if (!OpenProcessToken(GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
		&hToken))
	{
		return ERROR_FUNCTION_FAILED;
	}

	if (!LookupPrivilegeValue(NULL,
		szPrivilege,
		&luid))
	{
		CloseHandle(hToken);
		return ERROR_FUNCTION_FAILED;
	}


	NewState.PrivilegeCount = 1;
	NewState.Privileges[0].Luid = luid;
	NewState.Privileges[0].Attributes =
		(fEnable ? SE_PRIVILEGE_ENABLED : 0);

	if (!AdjustTokenPrivileges(hToken,
		FALSE,
		&NewState,
		0,
		NULL,
		NULL))
	{
		hr = ERROR_FUNCTION_FAILED;
	}

	CloseHandle(hToken);

	return hr;
}
#endif

static bool setOwner(const std::string& fn, const SOwner& owner)
{
#ifndef _WIN32
	//TODO: Implement
	return false;
#else
	if (!owner.has_owner || owner.Owner == nullptr)
		return false;

	HRESULT hr = ModifyPrivilege(SE_TAKE_OWNERSHIP_NAME, TRUE);
	if (!SUCCEEDED(hr))
		return false;

	std::wstring wfn = Server->ConvertToWchar(os_file_prefix(fn)).c_str();
	DWORD rc = SetNamedSecurityInfoW(&wfn[0],
		SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, owner.Owner, NULL, NULL,
		NULL);

	return rc == ERROR_SUCCESS;
#endif
}

static size_t zipWrite(void* pOpaque, mz_uint64 file_ofs, const void* pBuf, size_t n)
{
	IFsFile* out_file = reinterpret_cast<IFsFile*>(pOpaque);
	return out_file->Write(file_ofs, reinterpret_cast<const char*>(pBuf), n);
}

static void setupRansomwareCanaryFile(const std::string& curr_path, const std::string& fn_prefix,
	const std::string& server_token, SOwner& owner)
{
	std::string out_fn = os_file_prefix(curr_path + fn_prefix + "-" + server_token + ".docx");
	if (os_get_file_type(out_fn) != 0)
	{
		Server->Log("Ransomware canary at \"" + out_fn + "\" already exists");
		return;
	}

	if (!curr_path.empty())
	{
		std::vector<SFile> sib_files = getFiles(curr_path.substr(0, curr_path.size()-1));

		for (SFile& sib_file : sib_files)
		{
			if (getOwner(curr_path + sib_file.name, owner))
				break;
		}
	}

	const std::string canary_path = Server->getServerWorkingDir() + os_file_sep() +
		"urbackup" + os_file_sep() + "canary.docx";

	mz_zip_archive canary_doc = {};
	if (!mz_zip_reader_init_file(&canary_doc, canary_path.c_str(), 0))
	{
		Server->Log("Error opening canary.docx file for extraction", LL_ERROR);
		return;
	}

	ScopedDeleteFn delete_fn(out_fn+".new");
	std::unique_ptr<IFsFile> out_file(Server->openFile(out_fn+".new", MODE_WRITE));

	if (out_file.get() == nullptr)
	{
		Server->Log("Error opening canary out at \"" + out_fn + ".new\". " + os_last_error_str(), LL_ERROR);
		return;
	}

	mz_zip_archive canary_doc_out = {};
	canary_doc_out.m_pWrite = zipWrite;
	canary_doc_out.m_pIO_opaque = out_file.get();
	if (!mz_zip_writer_init(&canary_doc_out, 0))
	{
		Server->Log("Error init zip at \"" + out_fn + ".new\"", LL_ERROR);
		return;
	}

	for (unsigned int i = 0, num_files = mz_zip_reader_get_num_files(&canary_doc); i < num_files; ++i)
	{
		std::vector<char> buf(100);
		mz_zip_reader_get_filename(&canary_doc, i, buf.data(), buf.size());
		std::string curr_fn = buf.data();

		if (curr_fn == "word/document.xml")
		{
			size_t fsize;
			void* buf = mz_zip_reader_extract_to_heap(&canary_doc, i, &fsize, 0);

			if (buf == nullptr)
			{
				Server->Log("Error extracting word/document.xml", LL_ERROR);
				return;
			}

			std::string bdata(reinterpret_cast<char*>(buf), fsize);

			std::string uuid;
			uuid.resize(16);
			Server->secureRandomFill(&uuid[0], 16);

			bdata = greplace("$RAND$", bytesToHex(uuid), bdata);

			if (!mz_zip_writer_add_mem(&canary_doc_out, curr_fn.c_str(),
				bdata.data(), bdata.size(), MZ_DEFAULT_COMPRESSION))
			{
				Server->Log("Error adding modified word/document.xml to doc", LL_ERROR);
				return;
			}
		}
		else
		{
			if (!mz_zip_writer_add_from_zip_reader(&canary_doc_out, &canary_doc, i))
			{
				Server->Log("Error adding file \"" + curr_fn + "\" to canary out doc", LL_ERROR);
				return;
			}
		}		
	}

	if (!mz_zip_writer_finalize_archive(&canary_doc_out))
	{
		Server->Log("Error finalizing archive \"" + out_fn + "\"", LL_ERROR);
		return;
	}

	mz_zip_writer_end(&canary_doc_out);
	mz_zip_reader_end(&canary_doc);

	delete_fn.release();
	out_file->Sync();
	out_file.reset();

	if (!os_rename_file(out_fn + ".new", out_fn))
	{
		Server->Log("Error renaming canary " + out_fn + ".new", LL_ERROR);
		return;
	}

	setOwner(out_fn, owner);
}

static std::string getBackupPath(const std::string& name, int facet, int group)
{
	ClientDAO clientdao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT));

	std::vector<SBackupDir> backupdirs = clientdao.getBackupDirs();

	for (SBackupDir& bd: backupdirs)
	{
		if (bd.facet != facet || bd.group != group)
			continue;

		if (bd.tname == name)
			return bd.path;
	}

	return std::string();
}

static void setupRansomwareCanariesPath(const std::string& curr_path, const std::vector<std::string> path_components, size_t idx,
	const std::string& server_token, int facet, int group, SOwner& owner)
{
	if (idx >= path_components.size())
		return;

	bool last_comp = idx + 1 >= path_components.size();
	std::string comp = path_components[idx];

	bool create = false;

	if (comp[0] == '^'
		&& comp.find("*") == std::string::npos)
	{
		create = true;
		comp = comp.substr(1);
	}

	if (idx == 0)
	{
		comp = getBackupPath(comp, facet, group);

		if (comp.empty())
			return;
	}

	if (!last_comp 
		&& comp.find("*") != std::string::npos)
	{
		std::vector<SFile> files = getFiles(curr_path);
		for (size_t i = 0; i < files.size(); ++i)
		{
			if (files[i].isdir
				&& amatch(files[i].name.c_str(), comp.c_str()))
			{
				getOwner(curr_path + files[i].name, owner);

				setupRansomwareCanariesPath(curr_path + files[i].name + os_file_sep(),
					path_components, idx + 1, server_token, facet, group, owner);
			}
		}
	}
	else
	{
		if (!create
			&& !last_comp)
		{
			getOwner(curr_path + comp, owner);
		}

		if (!last_comp &&
			create &&
			!os_directory_exists(os_file_prefix(curr_path + comp)))
		{
			if (!os_create_dir(os_file_prefix(curr_path + comp)))
			{
				Server->Log("Error creating directory \"" + curr_path + comp +
					"\" for ransomware canary. " + os_last_error_str(), LL_ERROR);
			}
			setOwner(curr_path + comp, owner);
		}
		else if (last_comp)
		{
			setupRansomwareCanaryFile(curr_path,
				comp, server_token, owner);
		}

		if(!last_comp)
			setupRansomwareCanariesPath(curr_path + comp + os_file_sep(), path_components,
				idx + 1, server_token, facet, group, owner);
	}
}

static void setupRansomwareCanariesInt(const std::string& ransomware_canary_paths, const std::string& server_token,
	int facet, int group)
{
	std::vector<std::string> paths;
	Tokenize(ransomware_canary_paths, paths, ";");

	for (std::string path : paths)
	{
		std::vector<std::string> path_components;
		Tokenize(path, path_components, "/");

		if (!path_components.empty())
		{
			SOwner owner;
			setupRansomwareCanariesPath(std::string(), path_components, 0, server_token, facet, group, owner);
		}
	}
}

void setupRansomwareCanaries(const std::string& ransomware_canary_paths, const std::string& server_token,
	int facet, int group)
{
	std::thread t([ransomware_canary_paths, server_token, facet, group]()
		{
			setupRansomwareCanariesInt(ransomware_canary_paths, server_token, facet, group);
		});

	t.detach();
}
