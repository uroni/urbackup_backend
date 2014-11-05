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

bool verify_file(db_single_result &res, _i64 &curr_verified, _i64 verify_size)
{
	std::wstring fp=res[L"fullpath"];
	IFile *f=Server->openFile(os_file_prefix(fp), MODE_READ);
	if( f==NULL )
	{
		Server->Log(L"Error opening file \""+fp+L"\"", LL_ERROR);
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

		if(!backupname.empty())
		{
			if(backupname=="last")
			{
				q=db->Prepare("SELECT id,path FROM backups WHERE clientid=? ORDER BY backuptime DESC LIMIT 1");
				q->Bind(cid);
				res=q->Read();
				if(!res.empty())
				{
					backupid=watoi(res[0][L"id"]);
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
				q=db->Prepare("SELECT id FROM backups WHERE path=? AND clientid=?");
				q->Bind(backupname);
				q->Bind(cid);
				res=q->Read();
				if(!res.empty())
				{
					backupid=watoi(res[0][L"id"]);
				}
				else
				{
					Server->Log("Backup \""+backupname+"\" not found", LL_ERROR);
					
					return false;
				}
			}

			filter+=" AND backupid="+nconvert(backupid);
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

	IQuery *q_get_files=db->Prepare("SELECT id, fullpath, shahash, filesize FROM files"+filter);

	bool is_okay=true;

	IDatabaseCursor* cursor = q_get_files->Cursor();

	std::vector<int64> todelete;

	db_single_result res_single;
	while(cursor->next(res_single))
	{
		if(! verify_file( res_single, curr_verified, verify_size) )
		{
			v_failure << "Verification of \"" << Server->ConvertToUTF8(res_single[L"fullpath"]) << "\" failed\r\n";
			is_okay=false;

			if(delete_failed)
			{
				todelete.push_back(watoi64(res_single[L"id"]));
			}
		}
	}
	
	if(v_failure.is_open() && is_okay)
	{
		v_failure.close();
		Server->deleteFile(v_output_fn);
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