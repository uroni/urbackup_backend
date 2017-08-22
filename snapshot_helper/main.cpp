#include <string>
#include <string>
#include <iostream>
#include <vector>
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
extern char **environ;
#endif

#define DEF_Server
#include "../Server.h"

const int mode_btrfs=0;
const int mode_zfs=1;
const int mode_zfs_file=2

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


std::string getBackupfolderPath(int mode)
{
	std::string fn_name;
	if(mode==mode_btrfs)
	{
		fn_name = "backupfolder";
	}
	else if(mode==mode_zfs)
	{
		fn_name = "dataset";
	}
	else if(mode==mode_zfs_file)
	{
		fn_name = "dataset_file";
	}
	else
	{
		return std::string();
	}

	std::string fn;
#ifdef _WIN32
	fn=trim(getFile(fn_name));
#else
	fn=trim(getFile("/etc/urbackup/"+fn_name));
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

#ifndef _WIN32
int exec_wait(const std::string& path, bool keep_stdout, ...)
{
	va_list vl;
	va_start(vl, keep_stdout);
	
	std::vector<char*> args;
	args.push_back(const_cast<char*>(path.c_str()));
	
	while(true)
	{
		const char* p = va_arg(vl, const char*);
		if(p==NULL) break;
		args.push_back(const_cast<char*>(p));
	}
	va_end(vl);
	
	args.push_back(NULL);
	
	pid_t child_pid = fork();
	
	if(child_pid==0)
	{
		environ = new char*[1];
		*environ=NULL;
		
		if(!keep_stdout)
		{
			int nullfd = open("/dev/null", O_WRONLY);
			
			if(nullfd!=-1)
			{
				if(dup2(nullfd, 1)==-1)
				{
					return -1;
				}
				
				if(dup2(nullfd, 2)==-1)
				{
					return -1;
				}
			}
			else
			{
				return -1;
			}
		}
		
		int rc = execvp(path.c_str(), args.data());
		exit(rc);
	}
	else
	{
		int status;
		waitpid(child_pid, &status, 0);
		if(WIFEXITED(status))
		{
			return WEXITSTATUS(status);
		}
		else
		{
			return -1;
		}
	}
}

int exec_wait(const std::string& path, std::string& stdout, ...)
{
	va_list vl;
	va_start(vl, stdout);
	
	std::vector<char*> args;
	args.push_back(const_cast<char*>(path.c_str()));
	
	while(true)
	{
		const char* p = va_arg(vl, const char*);
		if(p==NULL) break;
		args.push_back(const_cast<char*>(p));
	}
	va_end(vl);
	
	args.push_back(NULL);
	
	int pipefd[2];
	if (pipe(pipefd) == -1)
	{
		return -1;
	}
	
	pid_t child_pid = fork();
	
	if(child_pid==0)
	{
		environ = new char*[1];
		*environ=NULL;
		
		close(pipefd[0]);
		
		if(dup2(pipefd[1], 1)==-1)
		{
			return -1;
		}
		
		int rc = execvp(path.c_str(), args.data());
		exit(rc);
	}
	else
	{
		close(pipefd[1]);
		
		char buf[512];
		int r;
		while( (r=read(pipefd[0], buf, 512))>0)
		{
			stdout.insert(stdout.end(), buf, buf+r);
		}
		
		close(pipefd[0]);
	
		int status;
		waitpid(child_pid, &status, 0);
		if(WIFEXITED(status))
		{
			return WEXITSTATUS(status);
		}
		else
		{
			return -1;
		}
	}
}

bool chown_dir(const std::string& dir)
{
	passwd* user_info = getpwnam("urbackup");
	if(user_info)
	{
		int rc = chown(dir.c_str(), user_info->pw_uid, user_info->pw_gid);
		return rc!=-1;
	}
	return false;
}

std::string find_btrfs_cmd()
{
	static std::string btrfs_cmd;
	
	if(!btrfs_cmd.empty())
	{
		return btrfs_cmd;
	}
	
	if(exec_wait("btrfs", false, "--version", NULL)==0)
	{
		btrfs_cmd="btrfs";
		return btrfs_cmd;
	}
	else if(exec_wait("/sbin/btrfs", false, "--version", NULL)==0)
	{
		btrfs_cmd="/sbin/btrfs";
		return btrfs_cmd;
	}
	else if(exec_wait("/bin/btrfs", false, "--version", NULL)==0)
	{
		btrfs_cmd="/bin/btrfs";
		return btrfs_cmd;
	}
	else if(exec_wait("/usr/sbin/btrfs", false, "--version", NULL)==0)
	{
		btrfs_cmd="/usr/sbin/btrfs";
		return btrfs_cmd;
	}
	else if(exec_wait("/usr/bin/btrfs", false, "--version", NULL)==0)
	{
		btrfs_cmd="/usr/bin/btrfs";
		return btrfs_cmd;
	}
	else
	{
		btrfs_cmd="btrfs";
		return btrfs_cmd;
	}
}

std::string find_zfs_cmd()
{
	static std::string zfs_cmd;
	
	if(!zfs_cmd.empty())
	{
		return zfs_cmd;
	}
	
	if(exec_wait("zfs", false, "--version", NULL)==2)
	{
		zfs_cmd="zfs";
		return zfs_cmd;
	}
	else if(exec_wait("/sbin/zfs", false, "--version", NULL)==2)
	{
		zfs_cmd="/sbin/zfs";
		return zfs_cmd;
	}
	else if(exec_wait("/bin/zfs", false, "--version", NULL)==2)
	{
		zfs_cmd="/bin/zfs";
		return zfs_cmd;
	}
	else if(exec_wait("/usr/sbin/zfs", false, "--version", NULL)==2)
	{
		zfs_cmd="/usr/sbin/zfs";
		return zfs_cmd;
	}
	else if(exec_wait("/usr/bin/zfs", false, "--version", NULL)==2)
	{
		zfs_cmd="/usr/bin/zfs";
		return zfs_cmd;
	}
	else
	{
		zfs_cmd="zfs";
		return zfs_cmd;
	}
}
#endif

bool create_subvolume(int mode, std::string subvolume_folder)
{
#ifdef _WIN32
	return os_create_dir(subvolume_folder);
#else
	if(mode==mode_btrfs)
	{
		int rc=exec_wait(find_btrfs_cmd(), true, "subvolume", "create", subvolume_folder.c_str(), NULL);
		chown_dir(subvolume_folder);
		return rc==0;
	}
	else if(mode==mode_zfs)
	{
		int rc=exec_wait(find_zfs_cmd(), true, "create", "-p", subvolume_folder.c_str(), NULL);
		chown_dir(subvolume_folder);
		return rc==0;
	}
	return false;
#endif
}

bool get_mountpoint(int mode, std::string subvolume_folder)
{
#ifdef _WIN32
	std::cout << subvolume_folder << std::endl;
	return true;
#else
	if(mode==mode_btrfs)
	{
		std::cout << subvolume_folder << std::endl;
		return true;
	}
	else if(mode==mode_zfs)
	{
		int rc=exec_wait(find_zfs_cmd(), true, "get", "-H", "-o", "value", "mountpoint", subvolume_folder.c_str(), NULL);
		return rc==0;
	}
	return false;
#endif
}

bool create_snapshot(int mode, std::string snapshot_src, std::string snapshot_dst)
{
#ifdef _WIN32
	return CopyFolder(widen(snapshot_src), widen(snapshot_dst));
#else
	if(mode==mode_btrfs)
	{
		int rc=exec_wait(find_btrfs_cmd(), true, "subvolume", "snapshot", snapshot_src.c_str(), snapshot_dst.c_str(), NULL);
		chown_dir(snapshot_dst);
		return rc==0;
	}
	else if(mode==mode_zfs)
	{
		int rc=exec_wait(find_zfs_cmd(), true, "clone", (snapshot_src+"@ro").c_str(), snapshot_dst.c_str(), NULL);
		chown_dir(snapshot_dst);
		return rc==0;
	}
	return false;
#endif
}

bool is_subvolume(int mode, std::string subvolume_folder)
{
#ifdef _WIN32
	return true;
#else
	if(mode==mode_btrfs)
	{
		int rc=exec_wait(find_btrfs_cmd(), false, "subvolume", "list", subvolume_folder.c_str(), NULL);
		return rc==0;
	}
	else if(mode==mode_zfs)
	{
		int rc=exec_wait(find_zfs_cmd(), false, "list", subvolume_folder.c_str(), NULL);
		return rc==0;
	}
	return false;
#endif
}

bool promote_dependencies(const std::string& snapshot, std::vector<std::string>& dependencies)
{
	std::cout << "Searching for origin " << snapshot << std::endl;
	
	std::string snap_data;
	int rc = exec_wait(find_zfs_cmd(), snap_data, "list", "-H", "-o", "name", NULL);
	if(rc!=0)
		return false;
	
	std::vector<std::string> snaps;
	TokenizeMail(snap_data, snaps, "\n");
		
	std::string snap_folder = ExtractFilePath(snapshot);
	for(size_t i=0;i<snaps.size();++i)
	{	
		if( !next(trim(snaps[i]), 0, snap_folder)
			|| trim(snaps[i]).size()<=snap_folder.size() )
			continue;
		
		std::string stdout;
		std::string subvolume_folder = snaps[i];
		int rc=exec_wait(find_zfs_cmd(), stdout, "get", "-H", "-o", "value", "origin", subvolume_folder.c_str(), NULL);
		if(rc==0)
		{
			stdout=trim(stdout);
			
			if(stdout==snapshot)
			{	
				std::cout << "Origin is " << subvolume_folder << std::endl;
				
				if(exec_wait(find_zfs_cmd(), true, "promote", subvolume_folder.c_str(), NULL)!=0)
				{
					return false;
				}
				
				dependencies.push_back(subvolume_folder);
			}
		}
	}
	
	return true;
}

bool remove_subvolume(int mode, std::string subvolume_folder, bool quiet=false)
{
#ifdef _WIN32
	return os_remove_nonempty_dir(widen(subvolume_folder));
#else
	if(mode==mode_btrfs)
	{
		int compat_rc = exec_wait(find_btrfs_cmd(), false, "subvolume", "delete", "-c", NULL);
		
		if(compat_rc==1)
		{
			compat_rc = exec_wait("/bin/sh", false, "-c", (find_btrfs_cmd() +
				" subvolume delete -c 2>&1 | grep \"ERROR: error accessing '-c'\"").c_str(), NULL);
			if(compat_rc==0)
			{
				compat_rc=12;
			}
		}
		
		int rc;
		if(compat_rc==12)
		{
			rc=exec_wait(find_btrfs_cmd(), !quiet, "subvolume", "delete", subvolume_folder.c_str(), NULL);
		}
		else
		{
			rc=exec_wait(find_btrfs_cmd(), !quiet, "subvolume", "delete", "-c", subvolume_folder.c_str(), NULL);
		}
		return rc==0;
	}
	else if(mode==mode_zfs)
	{
		exec_wait(find_zfs_cmd(), false, "destroy", (subvolume_folder+"@ro").c_str(), NULL);
		int rc = exec_wait(find_zfs_cmd(), false, "destroy", subvolume_folder.c_str(), NULL);
		if(rc!=0)
		{
			std::cout << "Destroying subvol " << subvolume_folder << " failed. Promoting dependencies..." << std::endl;
			std::string rename_name = ExtractFileName(subvolume_folder);
			if(exec_wait(find_zfs_cmd(), true, "rename", (subvolume_folder+"@ro").c_str(), (subvolume_folder+"@"+rename_name).c_str(), NULL)!=0
				&& is_subvolume(mode, subvolume_folder+"@ro") )
			{
				return false;
			}
			
			std::vector<std::string> dependencies;
			if(!promote_dependencies(subvolume_folder+"@"+rename_name, dependencies))
			{
				return false;
			}

			rc = exec_wait(find_zfs_cmd(), true, "destroy", subvolume_folder.c_str(), NULL);
			
			if(rc==0)
			{
				for(size_t i=0;i<dependencies.size();++i)
				{
					if(is_subvolume(mode, dependencies[i]+"@"+rename_name))
					{
						rc = exec_wait(find_zfs_cmd(), true, "destroy", (dependencies[i]+"@"+rename_name).c_str(), NULL);
						if(rc!=0)
						{
							break;
						}
					}
				}
			}
		}
		
		return rc==0;
	}
	return false;
#endif
}

bool make_readonly(int mode, std::string subvolume_folder)
{
#ifdef _WIN32
	return false;
#else
	if(mode==mode_btrfs)
	{
		int rc=exec_wait(find_btrfs_cmd(), true, "property", "set", "-ts", subvolume_folder.c_str(), "ro", "true", NULL);
		return rc==0;
	}
	else if(mode==mode_zfs)
	{
		int rc=exec_wait(find_zfs_cmd(), true, "snapshot", (subvolume_folder+"@ro").c_str(), NULL);
		return rc==0;
	}
	return false;
#endif
}

int zfs_test()
{
	std::cout << "Testing for zfs..." << std::endl;
				
	if(getBackupfolderPath(mode_zfs).empty())
	{
		std::cout << "TEST FAILED: Dataset is not set via /etc/urbackup/dataset" << std::endl;
		return 1;
	}
	
	std::string clientdir=getBackupfolderPath(mode_zfs)+os_file_sep()+"testA54hj5luZtlorr494";
	
	if(create_subvolume(mode_zfs, clientdir)
		&& remove_subvolume(mode_zfs, clientdir) )
	{
		std::cout << "ZFS TEST OK" << std::endl;
		
		clientdir=getBackupfolderPath(mode_zfs_file)+os_file_sep()+"testA54hj5luZtlorr494";
		
		if(getBackupfolderPath(mode_zfs_file).empty())
		{
			return 10 + mode_zfs;
		}
		else if(create_subvolume(mode_zfs, clientdir)
			&& remove_subvolume(mode_zfs, clientdir))
		{
			return 10 + mode_zfs_file;
		}
	
		return 10 + mode_zfs;
	}
	else
	{
		std::cout << "TEST FAILED: Creating test zfs volume \"" << clientdir << "\" failed" << std::endl;
	}
	return 1;
}

int main(int argc, char *argv[])
{
	if(argc<2)
	{
		std::cout << "Not enough parameters" << std::endl;
		return 1;
	}
	
	std::string cmd;
	int mode = 0;
	if((std::string)argv[1]!="test")
	{
		if(argc<3)
		{
			std::cout << "Not enough parameters" << std::endl;
			return 1;
		}
		cmd=argv[2];
		mode=atoi(argv[1]);
	}
	else
	{
		cmd=argv[1];
	}

	std::string backupfolder=getBackupfolderPath(mode);
	
	if(backupfolder.empty())
	{	
		if(mode==mode_btrfs)
		{
			std::cout << "Backupfolder not set" << std::endl;
		}
		else if(mode==mode_zfs)
		{
			std::cout << "ZFS image dataset not set" << std::endl;
		}
		else if(mode==mode_zfs_file)
		{
			std::cout << "ZFS file dataset not set" << std::endl;
		}
		else
		{
			std::cout << "Unknown mode: " << mode << std::endl;
		}
		return 1;
	}
	
	if(cmd!="test" && mode==mode_zfs_file)
	{
		mode=mode_zfs;
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
		if(argc<5)
		{
			std::cout << "Not enough parameters for create" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[3]);
		std::string name=handleFilename(argv[4]);

		std::string subvolume_folder=backupfolder+os_file_sep()+clientname+os_file_sep()+name;
		
		return create_subvolume(mode, subvolume_folder)?0:1;
	}
	else if(cmd=="mountpoint")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for mountpoint" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[3]);
		std::string name=handleFilename(argv[4]);

		std::string subvolume_folder=backupfolder+os_file_sep()+clientname+os_file_sep()+name;
		
		return get_mountpoint(mode, subvolume_folder)?0:1;
	}
	else if(cmd=="snapshot")
	{
		if(argc<6)
		{
			std::cout << "Not enough parameters for snapshot" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[3]);
		std::string src_name=handleFilename(argv[4]);
		std::string dst_name=handleFilename(argv[5]);

		std::string subvolume_src_folder=backupfolder+os_file_sep()+clientname+os_file_sep()+src_name;
		std::string subvolume_dst_folder=backupfolder+os_file_sep()+clientname+os_file_sep()+dst_name;

		return create_snapshot(mode, subvolume_src_folder, subvolume_dst_folder)?0:1;
	}
	else if(cmd=="remove")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for remove" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[3]);
		std::string name=handleFilename(argv[4]);

		std::string subvolume_folder=backupfolder+os_file_sep()+clientname+os_file_sep()+name;
		
		return remove_subvolume(mode, subvolume_folder)?0:1;
	}
	else if(cmd=="test")
	{
		std::cout << "Testing for btrfs..." << std::endl;
		std::string clientdir=backupfolder+os_file_sep()+"testA54hj5luZtlorr494";
		
		bool create_dir_rc=os_create_dir(clientdir);
		if(!create_dir_rc)
		{	
			remove_subvolume(mode_zfs, clientdir, true);
			remove_subvolume(mode_btrfs, clientdir+os_file_sep()+"A", true);
			remove_subvolume(mode_btrfs, clientdir+os_file_sep()+"B", true);
			os_remove_dir(clientdir);
		}
		create_dir_rc = create_dir_rc || os_create_dir(clientdir);
		if(create_dir_rc)
		{	
			if(!create_subvolume(mode_btrfs, clientdir+os_file_sep()+"A") )
			{
				std::cout << "TEST FAILED: Creating test btrfs subvolume failed" << std::endl;
				os_remove_dir(clientdir);
				
				return zfs_test();
			}
			
			bool suc=true;

			if(!create_snapshot(mode_btrfs, clientdir+os_file_sep()+"A", clientdir+os_file_sep()+"B") )
			{
				std::cout << "TEST FAILED: Creating test snapshot failed" << std::endl;
				suc=false;
			}
			
			if(suc)
			{			
				writestring("test", clientdir+os_file_sep()+"A"+os_file_sep()+"test");
				
				if(!os_create_hardlink(clientdir+os_file_sep()+"B"+os_file_sep()+"test", clientdir+os_file_sep()+"A"+os_file_sep()+"test", true, NULL))
				{
					std::cout << "TEST FAILED: Creating cross sub-volume reflink failed. Need Linux kernel >= 3.6." << std::endl;
					suc=false;
				}
				else
				{
					if(getFile(clientdir+os_file_sep()+"B"+os_file_sep()+"test")!="test")
					{
						std::cout << "TEST FAILED: Cannot read reflinked file" << std::endl;
						suc=false;
					}
				}
			}

			if(!remove_subvolume(mode_btrfs, clientdir+os_file_sep()+"A") )
			{
				std::cout << "TEST FAILED: Removing subvolume A failed" << std::endl;
				suc=false;
			}

			if(!remove_subvolume(mode_btrfs, clientdir+os_file_sep()+"B") )
			{
				std::cout << "TEST FAILED: Removing subvolume B failed" << std::endl;
				suc=false;
			}

			if(!os_remove_dir(clientdir))
			{
				std::cout << "TEST FAILED: Removing test clientdir failed" << std::endl;
				return 1;
			}
			
			if(!suc)
			{
				return 1;
			}
		}
		else
		{
			std::cout << "TEST FAILED: Creating test clientdir \"" << clientdir << "\" failed" << std::endl;
						
			return zfs_test();
		}
		std::cout << "BTRFS TEST OK" << std::endl;
		return 10 + mode_btrfs;
	}
	else if(cmd=="issubvolume")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for issubvolume" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[3]);
		std::string name=handleFilename(argv[4]);

		std::string subvolume_folder=backupfolder+os_file_sep()+clientname+os_file_sep()+name;
		
		return is_subvolume(mode, subvolume_folder)?0:1;
	}
	else if(cmd=="makereadonly")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for makereadonly" << std::endl;
			return 1;
		}
		
		std::string clientname=handleFilename(argv[3]);
		std::string name=handleFilename(argv[4]);

		std::string subvolume_folder=backupfolder+os_file_sep()+clientname+os_file_sep()+name;
		
		return make_readonly(mode, subvolume_folder)?0:1;
	}
	else
	{
		std::cout << "Command not found" << std::endl;
		return 1;
	}
}

