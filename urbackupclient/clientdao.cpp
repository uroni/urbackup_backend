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

#include "clientdao.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include <memory.h>

ClientDAO::ClientDAO(IDatabase *pDB)
{
	db=pDB;
	prepareQueries();
}

void ClientDAO::prepareQueries(void)
{
	q_get_files=db->Prepare("SELECT data,num FROM files WHERE name=?", false);
	q_add_files=db->Prepare("INSERT INTO files_tmp (name, num, data) VALUES (?,?,?)", false);
	q_get_dirs=db->Prepare("SELECT name, path, id, optional, continuous FROM backupdirs", false);
	q_remove_all=db->Prepare("DELETE FROM files", false);
	q_get_changed_dirs=db->Prepare("SELECT id, name FROM mdirs WHERE name GLOB ? UNION SELECT id, name FROM mdirs_backup WHERE name GLOB ?", false);
	q_remove_changed_dirs=db->Prepare("DELETE FROM mdirs GLOB ?", false);
	q_modify_files=db->Prepare("UPDATE files SET data=?, num=? WHERE name=?", false);
	q_has_files=db->Prepare("SELECT count(*) AS num FROM files WHERE name=?", false);
	q_insert_shadowcopy=db->Prepare("INSERT INTO shadowcopies (vssid, ssetid, target, path, tname, orig_target, filesrv, vol, starttime, refs, starttoken) VALUES (?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, ?, ?)", false);
	q_get_shadowcopies=db->Prepare("SELECT id, vssid, ssetid, target, path, tname, orig_target, filesrv, vol, (strftime('%s','now') - strftime('%s', starttime)) AS passedtime, refs, starttoken FROM shadowcopies", false);
	q_remove_shadowcopies=db->Prepare("DELETE FROM shadowcopies WHERE id=?", false);
	q_save_changed_dirs=db->Prepare("INSERT OR REPLACE INTO mdirs_backup SELECT id, name FROM mdirs WHERE name GLOB ?", false);
	q_delete_saved_changed_dirs=db->Prepare("DELETE FROM mdirs_backup", false);
	q_copy_from_tmp_files=db->Prepare("INSERT INTO files (num, data, name) SELECT num, data, name FROM files_tmp", false);
	q_delete_tmp_files=db->Prepare("DELETE FROM files_tmp", false);
	q_has_changed_gap=db->Prepare("SELECT name FROM mdirs WHERE name GLOB '##-GAP-##*'", false);
	q_get_del_dirs=db->Prepare("SELECT name FROM del_dirs WHERE name GLOB ? UNION SELECT name FROM del_dirs_backup WHERE name GLOB ?", false);
	q_del_del_dirs=db->Prepare("DELETE FROM del_dirs WHERE name GLOB ?", false);
	q_copy_del_dirs=db->Prepare("INSERT INTO del_dirs_backup SELECT name FROM del_dirs WHERE name GLOB ?", false);
	q_del_del_dirs_copy=db->Prepare("DELETE FROM del_dirs_backup", false);
	q_remove_del_dir=db->Prepare("DELETE FROM files WHERE name GLOB ?", false);
	q_get_shadowcopy_refcount=db->Prepare("SELECT refs FROM shadowcopies WHERE id=?", false);
	q_set_shadowcopy_refcount=db->Prepare("UPDATE shadowcopies SET refs=? WHERE id=?", false);
	q_save_changed_files=db->Prepare("INSERT OR REPLACE INTO mfiles_backup SELECT dir_id,name FROM mfiles WHERE dir_id=?", false);
	q_remove_changed_files=db->Prepare("DELETE FROM mfiles WHERE dir_id=?", false);
	q_delete_saved_changed_files=db->Prepare("DELETE FROM mfiles_backup", false);
	q_has_changed_file=db->Prepare("SELECT dir_id FROM mfiles_backup WHERE dir_id=? AND name=? UNION SELECT dir_id FROM mfiles WHERE dir_id=? AND name=?", false);
	q_get_changed_files=db->Prepare("SELECT name FROM mfiles_backup WHERE dir_id=? UNION SELECT name FROM mfiles WHERE dir_id=?", false);
	q_get_pattern=db->Prepare("SELECT tvalue FROM misc WHERE tkey=?", false);
	q_insert_pattern=db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES (?, ?)", false);
	q_update_pattern=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey=?", false);
	prepareQueriesGen();
}

void ClientDAO::destroyQueries(void)
{
	db->destroyQuery(q_get_files);
	db->destroyQuery(q_add_files);
	db->destroyQuery(q_get_dirs);
	db->destroyQuery(q_remove_all);
	db->destroyQuery(q_get_changed_dirs);
	db->destroyQuery(q_remove_changed_dirs);
	db->destroyQuery(q_modify_files);
	db->destroyQuery(q_has_files);
	db->destroyQuery(q_insert_shadowcopy);
	db->destroyQuery(q_get_shadowcopies);
	db->destroyQuery(q_remove_shadowcopies);
	db->destroyQuery(q_save_changed_dirs);
	db->destroyQuery(q_delete_saved_changed_dirs);
	db->destroyQuery(q_copy_from_tmp_files);
	db->destroyQuery(q_delete_tmp_files);
	db->destroyQuery(q_has_changed_gap);
	db->destroyQuery(q_get_del_dirs);
	db->destroyQuery(q_del_del_dirs);
	db->destroyQuery(q_copy_del_dirs);
	db->destroyQuery(q_del_del_dirs_copy);
	db->destroyQuery(q_remove_del_dir);
	db->destroyQuery(q_get_shadowcopy_refcount);
	db->destroyQuery(q_set_shadowcopy_refcount);
	db->destroyQuery(q_save_changed_files);
	db->destroyQuery(q_remove_changed_files);
	db->destroyQuery(q_delete_saved_changed_files);
	db->destroyQuery(q_has_changed_file);
	db->destroyQuery(q_get_changed_files);
	db->destroyQuery(q_get_pattern);
	db->destroyQuery(q_insert_pattern);
	db->destroyQuery(q_update_pattern);
	destroyQueriesGen();
}

//@-SQLGenSetup
void ClientDAO::prepareQueriesGen(void)
{
	q_updateShadowCopyStarttime=NULL;
	q_updateFileAccessToken=NULL;
	q_getFileAccessTokens=NULL;
	q_getFileAccessTokenId=NULL;
}

//@-SQLGenDestruction
void ClientDAO::destroyQueriesGen(void)
{
	db->destroyQuery(q_updateShadowCopyStarttime);
	db->destroyQuery(q_updateFileAccessToken);
	db->destroyQuery(q_getFileAccessTokens);
	db->destroyQuery(q_getFileAccessTokenId);
}

void ClientDAO::restartQueries(void)
{
	destroyQueries();
	prepareQueries();
}

bool ClientDAO::getFiles(std::wstring path, std::vector<SFileAndHash> &data)
{
	q_get_files->Bind(path);
	db_results res=q_get_files->Read();
	q_get_files->Reset();
	if(res.size()==0)
		return false;

	std::wstring &qdata=res[0][L"data"];

	if(qdata.empty())
		return true;

	int num=watoi(res[0][L"num"]);
	char *ptr=(char*)&qdata[0];
	while(ptr-(char*)&qdata[0]<num)
	{
		SFileAndHash f;
		unsigned short ss;
		memcpy(&ss, ptr, sizeof(unsigned short));
		ptr+=sizeof(unsigned short);
		std::string tmp;
		tmp.resize(ss);
		memcpy(&tmp[0], ptr, ss);
		f.name=Server->ConvertToUnicode(tmp);
		ptr+=ss;
		memcpy(&f.size, ptr, sizeof(int64));
		ptr+=sizeof(int64);
		memcpy(&f.last_modified, ptr, sizeof(int64));
		ptr+=sizeof(int64);
		char isdir=*ptr;
		++ptr;
		if(isdir==0)
			f.isdir=false;
		else
			f.isdir=true;
		
		unsigned short hashsize;
		memcpy(&hashsize, ptr, sizeof(unsigned short));
		ptr+=sizeof(unsigned short);

		f.hash.resize(hashsize);
		if(hashsize>0)
		{
			memcpy(&f.hash[0], ptr, hashsize);
		}

		ptr+=hashsize;

		unsigned short fpb_size;
		memcpy(&fpb_size, ptr, sizeof(unsigned short));
		ptr+=sizeof(unsigned short);

		f.file_permission_bits.resize(fpb_size);
		if(fpb_size>0)
		{
			memcpy(&f.file_permission_bits[0], ptr, fpb_size);
		}

		ptr+=fpb_size;

		memcpy(&f.last_modified_orig, ptr, sizeof(int64));
		ptr+=sizeof(int64);

		memcpy(&f.created, ptr, sizeof(int64));
		ptr+=sizeof(int64);

		data.push_back(f);
	}
	return true;
}

char * constructData(const std::vector<SFileAndHash> &data, size_t &datasize)
{
	datasize=0;
	std::vector<std::string> utf;
	utf.resize(data.size());
	for(size_t i=0;i<data.size();++i)
	{
		utf[i]=Server->ConvertToUTF8(data[i].name);
		datasize+=utf[i].size();
		datasize+=sizeof(unsigned short);
		datasize+=sizeof(int64)*2;
		++datasize;
		datasize+=sizeof(unsigned short);
		datasize+=data[i].hash.size();
		datasize+=sizeof(unsigned short);
		datasize+=data[i].file_permission_bits.size();
		datasize+=sizeof(int64)*2;
	}
	char *buffer=new char[datasize];
	char *ptr=buffer;
	for(size_t i=0;i<data.size();++i)
	{
		unsigned short ss=(unsigned short)utf[i].size();
		memcpy(ptr, (char*)&ss, sizeof(unsigned short));
		ptr+=sizeof(unsigned short);
		memcpy(ptr, &utf[i][0], ss);
		ptr+=ss;
		memcpy(ptr, (char*)&data[i].size, sizeof(int64));
		ptr+=sizeof(int64);
		memcpy(ptr, (char*)&data[i].last_modified, sizeof(int64));
		ptr+=sizeof(int64);
		char isdir=1;
		if(!data[i].isdir)
			isdir=0;
		*ptr=isdir;
		ptr+=sizeof(char);
		unsigned short hashsize=static_cast<unsigned short>(data[i].hash.size());
		memcpy(ptr, &hashsize, sizeof(hashsize));
		ptr+=sizeof(hashsize);
		memcpy(ptr, data[i].hash.data(), hashsize);
		ptr+=hashsize;
		unsigned short fpb_size=static_cast<unsigned short>(data[i].file_permission_bits.size());
		memcpy(ptr, &fpb_size, sizeof(fpb_size));
		ptr+=sizeof(fpb_size);
		memcpy(ptr, data[i].file_permission_bits.data(), fpb_size);
		ptr+=fpb_size;
		memcpy(ptr, (char*)&data[i].last_modified_orig, sizeof(int64));
		ptr+=sizeof(int64);
		memcpy(ptr, (char*)&data[i].created, sizeof(int64));
		ptr+=sizeof(int64);
	}
	return buffer;
}

void ClientDAO::addFiles(std::wstring path, const std::vector<SFileAndHash> &data)
{
	size_t ds;
	char *buffer=constructData(data, ds);
	q_add_files->Bind(path);
	q_add_files->Bind(ds);
	q_add_files->Bind(buffer, (_u32)ds);
	q_add_files->Write();
	q_add_files->Reset();
	delete []buffer;
}

void ClientDAO::modifyFiles(std::wstring path, const std::vector<SFileAndHash> &data)
{
	size_t ds;
	char *buffer=constructData(data, ds);
	q_modify_files->Bind(buffer, (_u32)ds);
	q_modify_files->Bind(ds);
	q_modify_files->Bind(path);
	q_modify_files->Write();
	q_modify_files->Reset();
	delete []buffer;
}

bool ClientDAO::hasFiles(std::wstring path)
{
	q_has_files->Bind(path);
	db_results res=q_has_files->Read();
	q_has_files->Reset();
	if(res.size()>0)
		return res[0][L"num"]==L"1";
	else
		return false;
}

std::vector<SBackupDir> ClientDAO::getBackupDirs(void)
{
	db_results res=q_get_dirs->Read();
	q_get_dirs->Reset();

	std::vector<SBackupDir> ret;
	for(size_t i=0;i<res.size();++i)
	{
		SBackupDir dir;
		dir.id=watoi(res[i][L"id"]);
		dir.tname=res[i][L"name"];
		dir.path=res[i][L"path"];
		dir.optional=(res[i][L"optional"]==L"1");
		dir.group=watoi(res[i][L"group"]);

		if(dir.tname!=L"*")
			ret.push_back(dir);
	}
	return ret;
}

void ClientDAO::removeAllFiles(void)
{
	q_remove_all->Write();
}

std::vector<SMDir> ClientDAO::getChangedDirs(const std::wstring& path, bool del)
{
	std::vector<SMDir> ret;

	if(del)
	{
		q_save_changed_dirs->Bind(path+os_file_sep()+L"*");
		q_save_changed_dirs->Write();
		q_save_changed_dirs->Reset();
		q_remove_changed_dirs->Bind(path+os_file_sep()+L"*");
		q_remove_changed_dirs->Write();
		q_remove_changed_dirs->Reset();
	}

	q_get_changed_dirs->Bind(path+os_file_sep()+L"*");
	q_get_changed_dirs->Bind(path+os_file_sep()+L"*");
	db_results res=q_get_changed_dirs->Read();
	q_get_changed_dirs->Reset();

	for(size_t i=0;i<res.size();++i)
	{
		ret.push_back(SMDir(watoi64(res[i][L"id"]), res[i][L"name"] ) );
	}
	return ret;
}

void ClientDAO::moveChangedFiles(_i64 dir_id, bool del)
{
	if(del)
	{
		q_save_changed_files->Bind(dir_id);
		q_save_changed_files->Write();
		q_save_changed_files->Reset();
		q_remove_changed_files->Bind(dir_id);
		q_remove_changed_files->Write();
		q_remove_changed_files->Reset();
	}	
}

std::vector<SShadowCopy> ClientDAO::getShadowcopies(void)
{
	db_results res=q_get_shadowcopies->Read();
	q_get_shadowcopies->Reset();
	std::vector<SShadowCopy> ret;
	for(size_t i=0;i<res.size();++i)
	{
		db_single_result &r=res[i];
		SShadowCopy sc;
		sc.id=watoi(r[L"id"]);
		memcpy(&sc.vssid, r[L"vssid"].c_str(), sizeof(GUID) );
		memcpy(&sc.ssetid, r[L"ssetid"].c_str(), sizeof(GUID) );
		sc.target=r[L"target"];
		sc.path=r[L"path"];
		sc.tname=r[L"tname"];
		sc.orig_target=r[L"orig_target"];
		sc.filesrv=r[L"filesrv"]==L"0"?false:true;
		sc.vol=r[L"vol"];
		sc.passedtime=watoi(r[L"passedtime"]);
		sc.refs=watoi(r[L"refs"]);
		sc.starttoken=r[L"starttoken"];
		ret.push_back(sc);
	}
	return ret;
}

int ClientDAO::addShadowcopy(const SShadowCopy &sc)
{
	q_insert_shadowcopy->Bind((char*)&sc.vssid, sizeof(GUID) );
	q_insert_shadowcopy->Bind((char*)&sc.ssetid, sizeof(GUID) );
	q_insert_shadowcopy->Bind(sc.target);
	q_insert_shadowcopy->Bind(sc.path);
	q_insert_shadowcopy->Bind(sc.tname);
	q_insert_shadowcopy->Bind(sc.orig_target);
	q_insert_shadowcopy->Bind(sc.filesrv?1:0);
	q_insert_shadowcopy->Bind(sc.vol);
	q_insert_shadowcopy->Bind(sc.refs);
	q_insert_shadowcopy->Bind(sc.starttoken);
	q_insert_shadowcopy->Write();
	q_insert_shadowcopy->Reset();
	return (int)db->getLastInsertID();
}

int ClientDAO::modShadowcopyRefCount(int id, int m)
{
	q_get_shadowcopy_refcount->Bind(id);
	db_results res=q_get_shadowcopy_refcount->Read();
	q_get_shadowcopy_refcount->Reset();
	if(!res.empty())
	{
		int refs=watoi(res[0][L"refs"]);
		refs+=m;
		q_set_shadowcopy_refcount->Bind(refs);
		q_set_shadowcopy_refcount->Bind(id);
		q_set_shadowcopy_refcount->Write();
		q_set_shadowcopy_refcount->Reset();
		return refs;
	}
	return -1;
}

void ClientDAO::deleteShadowcopy(int id)
{
	q_remove_shadowcopies->Bind(id);
	q_remove_shadowcopies->Write();
	q_remove_shadowcopies->Reset();
}

void ClientDAO::deleteSavedChangedDirs(void)
{
	q_delete_saved_changed_dirs->Write();
	q_delete_saved_changed_dirs->Reset();
}

void ClientDAO::deleteSavedChangedFiles(void)
{
	q_delete_saved_changed_files->Write();
	q_delete_saved_changed_files->Reset();
}

void ClientDAO::copyFromTmpFiles(void)
{
	q_copy_from_tmp_files->Write();
	q_copy_from_tmp_files->Reset();
	q_delete_tmp_files->Write();
	q_delete_tmp_files->Reset();
}

bool ClientDAO::hasChangedGap(void)
{
	db_results res=q_has_changed_gap->Read();
	q_has_changed_gap->Reset();
	return !res.empty();
}

void ClientDAO::deleteChangedDirs(const std::wstring& path)
{
	if(path.empty())
	{
		q_remove_changed_dirs->Bind("*");
	}
	else
	{
		q_remove_changed_dirs->Bind(path+os_file_sep()+L"*");
	}
	q_remove_changed_dirs->Write();
	q_remove_changed_dirs->Reset();
}

std::vector<std::wstring> ClientDAO::getGapDirs(void)
{
	db_results res=q_has_changed_gap->Read();
	q_has_changed_gap->Reset();
	std::vector<std::wstring> ret;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring gap=res[i][L"name"];
		gap.erase(0,9);
		ret.push_back(gap);
	}
	return ret;
}

std::vector<std::wstring> ClientDAO::getDelDirs(const std::wstring& path, bool del)
{
	std::vector<std::wstring> ret;

	if(del)
	{
		q_copy_del_dirs->Bind(path+os_file_sep()+L"*");
		q_copy_del_dirs->Write();
		q_copy_del_dirs->Reset();
		q_del_del_dirs->Bind(path+os_file_sep()+L"*");
		q_del_del_dirs->Write();
		q_del_del_dirs->Reset();
	}

	q_get_del_dirs->Bind(path+os_file_sep()+L"*");
	q_get_del_dirs->Bind(path+os_file_sep()+L"*");
	db_results res=q_get_del_dirs->Read();
	q_get_del_dirs->Reset();

	for(size_t i=0;i<res.size();++i)
	{
		ret.push_back(res[i][L"name"]);
	}
	return ret;
}

void ClientDAO::deleteSavedDelDirs(void)
{
	q_del_del_dirs_copy->Write();
	q_del_del_dirs_copy->Reset();
}

void ClientDAO::removeDeletedDir(const std::wstring &dir)
{
	q_remove_del_dir->Bind(dir+L"*");
	q_remove_del_dir->Write();
	q_remove_del_dir->Reset();
}

bool ClientDAO::hasFileChange(_i64 dir_id, std::wstring fn)
{
	q_has_changed_file->Bind(dir_id);
	q_has_changed_file->Bind(fn);
	q_has_changed_file->Bind(dir_id);
	q_has_changed_file->Bind(fn);
	db_results res=q_has_changed_file->Read();
	q_has_changed_file->Reset();

	return !res.empty();
}

std::vector<std::wstring> ClientDAO::getChangedFiles(_i64 dir_id)
{
	q_get_changed_files->Bind(dir_id);
	q_get_changed_files->Bind(dir_id);
	db_results res=q_get_changed_files->Read();
	q_get_changed_files->Reset();

	std::vector<std::wstring> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i][L"name"];
	}

	return ret;
}

const std::string exclude_pattern_key="exclude_pattern";

std::wstring ClientDAO::getOldExcludePattern(void)
{
	return getMiscValue(exclude_pattern_key);
}

void ClientDAO::updateOldExcludePattern(const std::wstring &pattern)
{
	updateMiscValue(exclude_pattern_key, pattern);
}

const std::string include_pattern_key="include_pattern";

std::wstring ClientDAO::getOldIncludePattern(void)
{
	return getMiscValue(include_pattern_key);
}

void ClientDAO::updateOldIncludePattern(const std::wstring &pattern)
{
	updateMiscValue(include_pattern_key, pattern);
}

std::wstring ClientDAO::getMiscValue(const std::string& key)
{
	q_get_pattern->Bind(key);
	db_results res=q_get_pattern->Read();
	q_get_pattern->Reset();
	if(!res.empty())
	{
		return res[0][L"tvalue"];
	}
	else
	{
		return L"";
	}
}


void ClientDAO::updateMiscValue(const std::string& key, const std::wstring& value)
{
	q_get_pattern->Bind(key);
	db_results res=q_get_pattern->Read();
	q_get_pattern->Reset();
	if(!res.empty())
	{
		q_update_pattern->Bind(value);
		q_update_pattern->Bind(key);
		q_update_pattern->Write();
		q_update_pattern->Reset();
	}
	else
	{
		q_insert_pattern->Bind(key);
		q_insert_pattern->Bind(value);
		q_insert_pattern->Write();
		q_insert_pattern->Reset();
	}
}

/**
* @-SQLGenAccess
* @func void ClientDAO::updateShadowCopyStarttime
* @sql
*    UPDATE shadowcopies SET starttime=CURRENT_TIMESTAMP
*           WHERE id=:id(int)
*/
void ClientDAO::updateShadowCopyStarttime(int id)
{
	if(q_updateShadowCopyStarttime==NULL)
	{
		q_updateShadowCopyStarttime=db->Prepare("UPDATE shadowcopies SET starttime=CURRENT_TIMESTAMP WHERE id=?", false);
	}
	q_updateShadowCopyStarttime->Bind(id);
	q_updateShadowCopyStarttime->Write();
	q_updateShadowCopyStarttime->Reset();
}

/**
* @-SQLGenAccess
* @func void ClientDAO::updateFileAccessToken
* @sql
*    INSERT OR REPLACE INTO fileaccess_tokens
*          (username, token)
*      VALUES
*           (:username(string), :token(string))
*/
void ClientDAO::updateFileAccessToken(const std::wstring& username, const std::wstring& token)
{
	if(q_updateFileAccessToken==NULL)
	{
		q_updateFileAccessToken=db->Prepare("INSERT OR REPLACE INTO fileaccess_tokens (username, token) VALUES (?, ?)", false);
	}
	q_updateFileAccessToken->Bind(username);
	q_updateFileAccessToken->Bind(token);
	q_updateFileAccessToken->Write();
	q_updateFileAccessToken->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SToken> ClientDAO::getFileAccessTokens
* @return int64 id, string username, string token
* @sql
*    SELECT id, username, token
*    FROM fileaccess_tokens
*/
std::vector<ClientDAO::SToken> ClientDAO::getFileAccessTokens(void)
{
	if(q_getFileAccessTokens==NULL)
	{
		q_getFileAccessTokens=db->Prepare("SELECT id, username, token FROM fileaccess_tokens", false);
	}
	db_results res=q_getFileAccessTokens->Read();
	std::vector<ClientDAO::SToken> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi64(res[i][L"id"]);
		ret[i].username=res[i][L"username"];
		ret[i].token=res[i][L"token"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ClientDAO::getFileAccessTokenId
* @return int64 id
* @sql
*    SELECT id
*    FROM fileaccess_tokens WHERE username = :username(string)
*/
ClientDAO::CondInt64 ClientDAO::getFileAccessTokenId(const std::wstring& username)
{
	if(q_getFileAccessTokenId==NULL)
	{
		q_getFileAccessTokenId=db->Prepare("SELECT id FROM fileaccess_tokens WHERE username = ?", false);
	}
	q_getFileAccessTokenId->Bind(username);
	db_results res=q_getFileAccessTokenId->Read();
	q_getFileAccessTokenId->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"id"]);
	}
	return ret;
}

