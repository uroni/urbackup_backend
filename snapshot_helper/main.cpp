#include <string>
#include <iostream>
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define DEF_Server
#include "../Server.h"


const std::string btrfs_cmd="/sbin/btrfs";

CServer *Server;

#ifdef _WIN32
#include <Windows.h>

bool CopyFolder(std::wstring src, std::wstring dst)
{
	if(!os_create_dir(dst))
		return false;

	std::vector<SFile> curr_files=getFiles(src);
	for(size_t i=0;i<curr_files.size();++i)
	{
		if(curr_files[i].isdir)
		{
			bool b=CopyFolder(src+os_file_sep()+curr_files[i].name, dst+os_file_sep()+curr_files[i].name);
			if(!b)
				return false;
		}
		else
		{
			if(!os_create_hardlink(dst+os_file_sep()+curr_files[i].name, src+os_file_sep()+curr_files[i].name, false, NULL) )
			{
				BOOL b=CopyFileW( (src+os_file_sep()+curr_files[i].name).c_str(), (dst+os_file_sep()+curr_files[i].name).c_str(), FALSE);
				if(!b)
				{
					return false;
				}
			}
		}
	}

	return true;
}
#endif


std::string getBackupfolderPath(void)
{
	std::string fn;
#ifdef _WIN32
	fn=trim(getFile("backupfolder"));
#else
	fn=trim(getFile("/etc/urbackup/backupfolder"));
#endif
	if(fn.find("\n")!=std::string::npos)
		fn=getuntil("\n", fn);
	if(fn.find("\r")!=std::string::npos)
		fn=getuntil("\r", fn);
	
	return fn;
}

std::string handleFilename(std::string fn)
{
	fn=conv_filename(fn);
	if(fn=="..")
	{
		return "";
	}
	return fn;
}

bool create_subvolume(std::string subvolume_folder)
{
#ifdef _WIN32
	return os_create_dir(subvolume_folder);
#else
	int rc=system((btrfs_cmd+" subvolume create \""+subvolume_folder+"\"").c_str());
	system(("chown urbackup:urbackup \""+subvolume_folder+"\"").c_str());
	return rc==0;
#endif
}

bool create_snapshot(std::string snapshot_src, std::string snapshot_dst)
{
#ifdef _WIN32
	return CopyFolder(widen(snapshot_src), widen(snapshot_dst));
#else
	int rc=system((btrfs_cmd+" subvolume snapshot \""+snapshot_src+"\" \""+snapshot_dst+"\"").c_str());
	system(("chown urbackup:urbackup \""+snapshot_dst+"\"").c_str());
	return rc==0;
#endif
}

bool remove_subvolume(std::string subvolume_folder)
{
#ifdef _WIN32
	return os_remove_nonempty_dir(widen(subvolume_folder));
#else
	int rc=system((btrfs_cmd+" subvolume delete \""+subvolume_folder+"\"").c_str());
	return rc==0;
#endif
}	

bool is_subvolume(std::string subvolume_folder)
{
#ifdef _WIN32
	return true;
#else
	int rc=system((btrfs_cmd+" subvolume list \""+subvolume_folder+"\"").c_str());
	return rc==0;
#endif
}	

int main(int argc, char *argv[])
{
	if(argc<2)
	{
		std::cout << "Not enough parameters" << std::endl;
		return 1;
	}

	std::string cmd=argv[1];

	std::string backupfolder=getBackupfolderPath();

	if(backupfolder.empty())
	{
		std::cout << "Backupfolder not set" << std::endl;
		return 1;
	}
	
#ifndef _WIN32
	if(seteuid(0)!=0)
	{
		std::cout << "Cannot become root user" << std::endl;
		return 1;
	}
#endif

	if(cmd=="create")
	{
		if(argc<4)
		{
			std::cout << "Not enough parameters for create" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[2]);
		std::string name=handleFilename(argv[3]);

		std::string subvolume_folder=backupfolder+os_file_sepn()+clientname+os_file_sepn()+name;
		
		return create_subvolume(subvolume_folder)?0:1;
	}
	else if(cmd=="snapshot")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for snapshot" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[2]);
		std::string src_name=handleFilename(argv[3]);
		std::string dst_name=handleFilename(argv[4]);

		std::string subvolume_src_folder=backupfolder+os_file_sepn()+clientname+os_file_sepn()+src_name;
		std::string subvolume_dst_folder=backupfolder+os_file_sepn()+clientname+os_file_sepn()+dst_name;

		return create_snapshot(subvolume_src_folder, subvolume_dst_folder)?0:1;
	}
	else if(cmd=="remove")
	{
		if(argc<4)
		{
			std::cout << "Not enough parameters for remove" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[2]);
		std::string name=handleFilename(argv[3]);

		std::string subvolume_folder=backupfolder+os_file_sepn()+clientname+os_file_sepn()+name;
		
		return remove_subvolume(subvolume_folder)?0:1;
	}
	else if(cmd=="test")
	{
		std::string clientdir=backupfolder+os_file_sepn()+"testA54hj5luZtlorr494";
		if(os_create_dir(clientdir))
		{			
			if(!create_subvolume(clientdir+os_file_sepn()+"A") )
			{
				std::cout << "Creating test subvolume failed" << std::endl;
				os_remove_dir(clientdir);
				return 1;
			}
			
			bool suc=true;

			if(!create_snapshot(clientdir+os_file_sepn()+"A", clientdir+os_file_sepn()+"B") )
			{
				std::cout << "Creating test snapshot failed" << std::endl;
				suc=false;
			}
			
			if(suc)
			{			
				writestring("test", clientdir+os_file_sepn()+"A"+os_file_sepn()+"test");
				
				if(!os_create_hardlink(clientdir+os_file_sepn()+"B"+os_file_sepn()+"test", clientdir+os_file_sepn()+"A"+os_file_sepn()+"test", true, NULL))
				{
					std::cout << "Cross subvolume reflink failed" << std::endl;
					suc=false;
				}
			
				if(getFile(clientdir+os_file_sepn()+"B"+os_file_sepn()+"test")!="test")
				{
					std::cout << "Cannot read reflinked file" << std::endl;
					suc=false;
				}
			}

			if(!remove_subvolume(clientdir+os_file_sepn()+"A") )
			{
				std::cout << "Removing subvolume A failed" << std::endl;
				suc=false;
			}

			if(!remove_subvolume(clientdir+os_file_sepn()+"B") )
			{
				std::cout << "Removing subvolume B failed" << std::endl;
				suc=false;
			}

			if(!os_remove_dir(clientdir))
			{
				std::cout << "Removing test clientdir failed" << std::endl;
				return 1;
			}
			
			if(!suc)
			{
				return 1;
			}
		}
		else
		{
			std::cout << "Creating test clientdir \"" << clientdir << "\" failed" << std::endl;
			return 1;
		}
		return 0;
	}
	else if(cmd=="issubvolume")
	{
		if(argc<4)
		{
			std::cout << "Not enough parameters for issubvolume" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[2]);
		std::string name=handleFilename(argv[3]);

		std::string subvolume_folder=backupfolder+os_file_sepn()+clientname+os_file_sepn()+name;
		
		return is_subvolume(subvolume_folder)?0:1;
	}
	else
	{
		std::cout << "Command not found" << std::endl;
		return 1;
	}
}

