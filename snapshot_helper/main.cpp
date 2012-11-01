#include <string>
#include <iostream>
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>

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
			if(!os_create_hardlink(dst+os_file_sep()+curr_files[i].name, src+os_file_sep()+curr_files[i].name, false) )
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
#ifdef _WIN32
	return trim(getFile("backupfolder"));
#else
	return trim(getFile("/etc/urbackup/backupfolder"));
#endif
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
	int rc=system("btrfs subvolume create \""+subvolume_folder+"\"");
	return rc==0;
#endif
}

bool create_snapshot(std::string snapshot_src, std::string snapshot_dst)
{
#ifdef _WIN32
	return CopyFolder(widen(snapshot_src), widen(snapshot_dst));
#else
	int rc=system("btrfs subvolume snapshot \""+snapshot_src+"\" \""+snapshot_dst+"\"");
	return rc==0;
#endif
}

bool remove_subvolume(std::string subvolume_folder)
{
#ifdef _WIN32
	return os_remove_nonempty_dir(widen(subvolume_folder));
#else
	int rc=system("btrfs subvolume delete \""+subvolume_folder+"\"");
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
				return 1;
			}

			if(!create_snapshot(clientdir+os_file_sepn()+"A", clientdir+os_file_sepn()+"B") )
			{
				std::cout << "Creating test snapshot failed" << std::endl;
				return 1;
			}

			if(!remove_subvolume(clientdir+os_file_sepn()+"A") )
			{
				std::cout << "Removing subvolume A failed" << std::endl;
				return 1;
			}

			if(!remove_subvolume(clientdir+os_file_sepn()+"B") )
			{
				std::cout << "Removing subvolume B failed" << std::endl;
				return 1;
			}

			if(!os_remove_dir(clientdir))
			{
				std::cout << "Removing test clientdir failed" << std::endl;
				return 1;
			}
		}
		else
		{
			std::cout << "Creating test clientdir failed" << std::endl;
			return 1;
		}
		return 0;
	}
	else
	{
		std::cout << "Command not found" << std::endl;
		return 1;
	}
}