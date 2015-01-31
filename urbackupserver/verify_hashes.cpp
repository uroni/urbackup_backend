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

#include "../Interface/Database.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../Interface/DatabaseCursor.h"
#include "database.h"
#include "../stringtools.h"
#include <iostream>
#include <fstream>
#include "../urbackupcommon/sha2/sha2.h"
#include "../urbackupcommon/os_functions.h"
#include <memory.h>
#include <memory>
#include "dao/ServerBackupDao.h"
#include "FileIndex.h"
#include "create_files_index.h"
#include "server_hash.h"
#include "serverinterface/helper.h"

const _u32 c_read_blocksize=4096;
const size_t draw_segments=30;
const size_t c_speed_size=15;
const size_t c_max_l_length=80;

void draw_progress(std::wstring curr_fn, _i64 curr_verified, _i64 verify_size)
{
	static _i64 last_progress_bytes=0;
	static int64 last_time=0;
	static size_t max_line_length=0;

	int64 passed_time=Server->getTimeMS()-last_time;
	if(passed_time>1000)
	{
		_i64 new_bytes=curr_verified-last_progress_bytes;

		float pc_done=(float)curr_verified/(float)verify_size;

		size_t segments=(size_t)(pc_done*draw_segments);

		std::string toc="\r[";
		for(size_t i=0;i<draw_segments;++i)
		{
			if(i<segments)
			{
				toc+="=";
			}
			else if(i==segments)
			{
				toc+=">";
			}
			else
			{
				toc+=" ";
			}
		}
		std::string speed_str=PrettyPrintSpeed((size_t)((new_bytes*1000)/passed_time));
		while(speed_str.size()<c_speed_size)
			speed_str+=" ";
		std::string pcdone=nconvert((int)(pc_done*100.f));
		if(pcdone.size()==1)
			pcdone=" "+pcdone;

		toc+="] "+pcdone+"% "+speed_str+" "+Server->ConvertToUTF8(curr_fn);
		
		if(toc.size()>=c_max_l_length)
		    toc=toc.substr(0, c_max_l_length);
		
		if(toc.size()>max_line_length)
		    max_line_length=toc.size();
		    
		while(toc.size()<max_line_length)
		    toc+=" ";

		std::cout << toc;
		std::cout.flush();

		last_progress_bytes=curr_verified;
		last_time=Server->getTimeMS();
	}
}

bool verify_file(db_single_result &res, _i64 &curr_verified, _i64 verify_size, bool& missing)
{
	std::wstring fp=res[L"fullpath"];
	IFile *f=Server->openFile(os_file_prefix(fp), MODE_READ);
	if( f==NULL )
	{
		Server->Log(L"Error opening file \""+fp+L"\"", LL_ERROR);
		missing = true;
		return false;
	}

	if(watoi64(res[L"filesize"])!=f->Size())
	{
		Server->Log(L"Filesize of \""+fp+L"\" is wrong", LL_ERROR);
		return false;
	}

	std::wstring f_name=ExtractFileName(fp);

	sha512_ctx shactx;
	sha512_init(&shactx);

	_u32 r;
	char buf[c_read_blocksize];
	int64 curr_verified_start=curr_verified;
	do
	{
		r=f->Read(buf, c_read_blocksize);
		if(r>0)
		{
			sha512_update(&shactx, (unsigned char*) buf, r);
		}
		curr_verified+=r;

		draw_progress(f_name, curr_verified, verify_size);
	}
	while(r>0);
	
	Server->destroy(f);

	if(curr_verified-curr_verified_start!=watoi64(res[L"filesize"]))
	{
		Server->Log(L"Could not read all bytes of file \""+fp+L"\"", LL_ERROR);
		return false;
	}

	const unsigned char * db_sha=(unsigned char*)res[L"shahash"].c_str();
	unsigned char calc_dig[64];
	sha512_final(&shactx, calc_dig);

	if(memcmp(db_sha, calc_dig, 64)!=0)
	{
		Server->Log(L"Hash of \""+fp+L"\" is wrong", LL_ERROR);
		return false;
	}

	return true;
}

bool verify_hashes(std::string arg)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	std::string working_dir=Server->ConvertToUTF8(Server->getServerWorkingDir());
	std::string v_output_fn=working_dir+os_file_sepn()+"urbackup"+os_file_sepn()+"verification_result.txt";
	std::fstream v_failure;
	v_failure.open(v_output_fn.c_str(), std::ios::out|std::ios::binary);
	if( !v_failure.is_open() )
		Server->Log("Could not open \""+v_output_fn+"\" for writing", LL_ERROR);
	else
		Server->Log("Writing verification results to \""+v_output_fn+"\"", LL_INFO);

	std::string clientname;
	std::string backupname;

	if(arg!="true")
	{
		if(arg.find("/")==std::string::npos)
		{
			clientname=arg;
		}
		else
		{
			clientname=getuntil("/", arg);
			backupname=getafter("/", arg);
			
		}
	}

	bool delete_failed = Server->getServerParameter("delete_verify_failed")=="true";

	int cid=0;
	int backupid=0;
	std::string filter;

	if(!clientname.empty())
	{
		if(clientname!="*")
		{
			IQuery *q=db->Prepare("SELECT id FROM clients WHERE name=?");
			q->Bind(clientname);
			db_results res=q->Read();
			if(!res.empty())
			{
				cid=watoi(res[0][L"id"]);
			}
			else
			{
				Server->Log("Client \""+clientname+"\" not found", LL_ERROR);
				return false;
			}

			filter=" WHERE clientid="+nconvert(cid);
		}
		else
		{
			filter+=" WHERE 1=1";
		}
		

		if(!backupname.empty())
		{
			std::string backupid_filter;
			if(backupname=="last")
			{
				if(clientname!="*")
				{
					IQuery* q=db->Prepare("SELECT id,path FROM backups WHERE clientid=? AND complete=1 ORDER BY backuptime DESC LIMIT 1");
					q->Bind(cid);
					db_results res=q->Read();
					if(!res.empty())
					{
						backupid_filter="= "+wnarrow(res[0][L"id"]);
						Server->Log(L"Last backup: "+res[0][L"path"], LL_INFO);
					}
					else
					{
						Server->Log("Last backup not found", LL_ERROR);
						return false;
					}
				}
				else
				{
					backupid_filter=" IN (SELECT id FROM backups b WHERE NOT EXISTS (SELECT id FROM backups c WHERE complete=1 AND c.backuptime > b.backuptime AND b.clientid=c.clientid))";
				}
				
			}
			else if(backupname=="*")
			{
				backupid_filter=" IN (SELECT id FROM backups WHERE clientid="+nconvert(cid)+")";
			}
			else
			{		
				IQuery* q=db->Prepare("SELECT id FROM backups WHERE path=? AND clientid=?");
				q->Bind(backupname);
				q->Bind(cid);
				db_results res=q->Read();
				if(!res.empty())
				{
					backupid_filter="= "+wnarrow(res[0][L"id"]);
				}
				else
				{
					Server->Log("Backup \""+backupname+"\" not found", LL_ERROR);
					
					return false;
				}
			}

			if(backupname!="*" || clientname!="*")
			{
				filter+=" AND backupid "+backupid_filter;
			}
		}
	}

	std::cout << "Calculating filesize..." << std::endl;
	IQuery *q_num_files=db->Prepare("SELECT SUM(filesize) AS c FROM files"+filter);
	db_results res=q_num_files->Read();
	if(res.empty())
	{
		Server->Log("Error during filesize calculation.", LL_ERROR);
		return false;
	}

	_i64 verify_size=watoi64(res[0][L"c"]);
	_i64 curr_verified=0;

	std::cout << "To be verified: " << PrettyPrintBytes(verify_size) << " of files" << std::endl;

	_i64 crowid=0;

	IQuery *q_get_files=db->Prepare("SELECT id, fullpath, shahash, filesize FROM files"+filter, false);

	bool is_okay=true;

	IDatabaseCursor* cursor = q_get_files->Cursor();

	std::vector<int64> todelete;
	std::vector<int64> missing_files;

	db_single_result res_single;
	while(cursor->next(res_single))
	{
		bool is_missing=false;
		if(! verify_file( res_single, curr_verified, verify_size, is_missing) )
		{
			if(!is_missing)
			{
				v_failure << "Verification of \"" << Server->ConvertToUTF8(res_single[L"fullpath"]) << "\" failed\r\n";
				is_okay=false;

				if(delete_failed)
				{
					todelete.push_back(watoi64(res_single[L"id"]));
				}
			}
			else
			{
				missing_files.push_back(watoi64(res_single[L"id"]));
			}			
		}
	}
	
	if(v_failure.is_open() && is_okay)
	{
		v_failure.close();
		Server->deleteFile(v_output_fn);
	}

	db->destroyQuery(q_get_files);

	IQuery* q_get_file = db->Prepare("SELECT id, fullpath, shahash, filesize FROM files WHERE id=?");

	std::cout << missing_files.size() << " could not be opened during verification. Checking now if they have been deleted from the database..." << std::endl;

	for(size_t i=0;i<missing_files.size();++i)
	{
		q_get_file->Bind(missing_files[i]);
		db_results res = q_get_file->Read();
		q_get_file->Reset();
		
		if(!res.empty())
		{
			bool is_missing=false;
			db_single_result& res_single = res[0];
			if(!verify_file(res_single, curr_verified, verify_size, is_missing))
			{
				v_failure << "Verification of file \"" << Server->ConvertToUTF8(res_single[L"fullpath"]) << "\" failed (during rechecking previously missing files)\r\n";
				is_okay=false;

				if(delete_failed)
				{
					todelete.push_back(watoi64(res_single[L"id"]));
				}
			}
		}
	}
	
	std::cout << std::endl;

	if(delete_failed)
	{
		std::cout << "Deleting " << todelete.size() << " file entries with failed verification from database..." << std::endl;

		SStartupStatus status;
		if(!create_files_index(status))
		{
			std::cout << "Error opening file index -1" << std::endl;
		}
		else
		{
			ServerBackupDao backupdao(db);
			std::auto_ptr<FileIndex> fileindex(create_lmdb_files_index());

			if(fileindex.get()==NULL)
			{
				std::cout << "Error opening file index -2" << std::endl;
			}
			else
			{

				for(size_t i=0;i<todelete.size();++i)
				{
					BackupServerHash::deleteFileSQL(backupdao, *fileindex, todelete[i]);
				}

				std::cout << "done." << std::endl;
			}
		}		
	}
	

	return is_okay;
}