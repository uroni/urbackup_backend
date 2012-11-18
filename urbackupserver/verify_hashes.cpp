#include "../Interface/Database.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "database.h"
#include "../stringtools.h"
#include <iostream>
#include <fstream>
#include "../urbackupcommon/sha2/sha2.h"
#include "../urbackupcommon/os_functions.h"
#include <memory.h>

const _u32 c_read_blocksize=4096;
const size_t draw_segments=30;
const size_t c_speed_size=15;
const size_t c_max_l_length=80;

void draw_progress(std::wstring curr_fn, _i64 curr_verified, _i64 verify_size)
{
	static _i64 last_progress_bytes=0;
	static unsigned int last_time=0;
	static size_t max_line_length=0;

	unsigned int passed_time=Server->getTimeMS()-last_time;
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
	IFile *f=Server->openFile(fp, MODE_READ);
	if( f==NULL )
	{
		Server->Log(L"Error opening file \""+fp+L"\"", LL_ERROR);
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

	int cid=0;
	int backupid=0;
	std::string filter;
	std::string wfilter;

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

	if(filter.empty()) wfilter=" WHERE";
	else	wfilter=filter+" AND";

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

	IQuery *q_get_files=db->Prepare("SELECT rowid, fullpath, shahash FROM files"+wfilter+" rowid>? ORDER BY rowid ASC LIMIT 1000000");

	bool is_okay=true;

	do
	{
		q_get_files->Bind(crowid);
		res=q_get_files->Read();
		q_get_files->Reset();

		for(size_t i=0;i<res.size();++i)
		{
			crowid=(std::max)(crowid, watoi64(res[i][L"rowid"]));
			if(! verify_file( res[i], curr_verified, verify_size) )
			{
				v_failure << "Verification of \"" << Server->ConvertToUTF8(res[i][L"fullpath"]) << "\" failed\r\n";
				is_okay=false;
			}
		}
	}
	while(!res.empty());
	
	if(v_failure.is_open() && is_okay)
	{
		v_failure.close();
		Server->deleteFile(v_output_fn);
	}
	
	std::cout << std::endl;

	return is_okay;
}