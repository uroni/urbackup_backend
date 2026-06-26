#include <string>
#include <iostream>
#include <vector>
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#ifdef __linux__
#include <linux/loop.h>
#endif
#include <errno.h>
#include <sys/ioctl.h>
extern char **environ;

namespace
{
std::vector<std::string> get_child_env_store()
{
	std::vector<std::string> env;

	const char* whitelist[] = {
		"LIBGUESTFS_BACKEND",
		"LIBGUESTFS_BACKEND_SETTINGS",
		"LIBGUESTFS_PATH",
		"LIBGUESTFS_CACHEDIR",
		"LIBGUESTFS_TMPDIR",
		"LIBGUESTFS_DEBUG",
		"LIBGUESTFS_TRACE",
		"TMPDIR",
		NULL
	};

	for(size_t i=0; whitelist[i]!=NULL; ++i)
	{
		const char* val = getenv(whitelist[i]);
		if(val!=NULL)
		{
			env.push_back(std::string(whitelist[i])+"="+val);
		}
	}

	return env;
}

char** get_child_env(std::vector<std::string>& env_store, std::vector<char*>& env_ptrs)
{
	env_ptrs.clear();

	for(size_t i=0; i<env_store.size(); ++i)
	{
		env_ptrs.push_back(const_cast<char*>(env_store[i].c_str()));
	}

	env_ptrs.push_back(NULL);
	return env_ptrs.data();
}

bool elevate_child_to_root()
{
	if(geteuid()==0)
	{
		if(setgid(0)!=0)
		{
			return false;
		}

		if(setuid(0)!=0)
		{
			return false;
		}
	}

	return true;
}
}

#define DEF_Server
#include "../Server.h"
#include "../config.h"

#ifndef LOOP_SET_DIRECT_IO
#define LOOP_SET_DIRECT_IO	0x4C08
#endif
#ifndef LOOP_CTL_GET_FREE
#define LOOP_CTL_GET_FREE       0x4C82
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#define LOCAL_LO_FLAGS_AUTOCLEAR 4

IServer *Server;

const char* mdconfig_path = "/sbin/mdconfig";
const char* umount_path = "/sbin/umount";


std::string getBackupfolderPath()
{
	std::string fn=trim(getFile("/etc/urbackup/backupfolder"));
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
		std::vector<std::string> env_store = get_child_env_store();
		std::vector<char*> env_ptrs;
		environ = get_child_env(env_store, env_ptrs);

		if(!elevate_child_to_root())
		{
			exit(126);
		}
		
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
		std::vector<std::string> env_store = get_child_env_store();
		std::vector<char*> env_ptrs;
		environ = get_child_env(env_store, env_ptrs);

		if(!elevate_child_to_root())
		{
			exit(126);
		}
		
		close(pipefd[0]);
		
		if(dup2(pipefd[1], 1)==-1)
		{
			return -1;
		}
		
		if(dup2(pipefd[1], 2)==-1)
		{
			return -1;
		}
		
		close(pipefd[1]);
		
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



bool path_exists_lstat(const std::string& path)
{
	struct stat st;
	return lstat(path.c_str(), &st)==0;
}

#ifdef __linux__
void cleanup_fuse_mountpoint(const std::string& path, bool guestmount)
{
	if(guestmount)
	{
		exec_wait("guestunmount", true, path.c_str(), NULL);
	}

	exec_wait("fusermount", true, "-u", path.c_str(), NULL);
	exec_wait("fusermount", true, "-u", "-z", path.c_str(), NULL);
	exec_wait(umount_path, true, path.c_str(), NULL);
	exec_wait(umount_path, true, "-l", path.c_str(), NULL);
}
#endif

bool ensure_mount_dir(const std::string& path, const std::string& what)
{
	errno = 0;
	if(os_directory_exists(path))
	{
		return true;
	}

	int exists_errno = errno;

#ifdef __linux__
	if(exists_errno==EACCES || exists_errno==ENOTCONN || path_exists_lstat(path))
	{
		cleanup_fuse_mountpoint(path, what=="mountpoint");

		errno = 0;
		if(os_directory_exists(path))
		{
			return true;
		}

		if(path_exists_lstat(path))
		{
			if(rmdir(path.c_str())!=0 && errno!=ENOENT)
			{
				std::cerr << "Error removing stale " << what << " at \"" << path << "\". Err: " << errno << std::endl;
				return false;
			}
		}
	}
#endif

	errno = 0;
	if(os_create_dir(path))
	{
		return true;
	}

	if(errno==EEXIST)
	{
		return os_directory_exists(path) || path_exists_lstat(path);
	}

	std::cerr << "Error creating " << what << " at \"" << path << "\". Err: " << errno << std::endl;
	return false;
}

std::string find_urbackupsrv_cmd()
{
	static std::string urbackupsrv_cmd;
	
	if(!urbackupsrv_cmd.empty())
	{
		return urbackupsrv_cmd;
	}
	
	if(exec_wait(BINDIR "/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd = BINDIR "/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/usr/local/bin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/usr/local/bin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	if(exec_wait("/sbin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/sbin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/bin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/bin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/usr/sbin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/usr/sbin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else if(exec_wait("/usr/bin/urbackupsrv", false, "--version", NULL)==1)
	{
		urbackupsrv_cmd="/usr/bin/urbackupsrv";
		return urbackupsrv_cmd;
	}
	else
	{
		urbackupsrv_cmd="urbackupsrv";
		return urbackupsrv_cmd;
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

bool ubuntu_guestmount_fix()
{
#ifdef __linux__
	//workaround for https://bugs.launchpad.net/ubuntu/+source/linux/+bug/759725
	if(getFile("/etc/os-release").find("NAME=\"Ubuntu\"")!=std::string::npos)
	{
		std::vector<SFile> files = getFiles("/boot");
		for(size_t i=0;i<files.size();++i)
		{
			if(!files[i].isdir 
				&& files[i].name.find("vmlinuz")==0)
			{
				if(chmod(("/boot/"+files[i].name).c_str(), S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IROTH)!=0)
					return false;
			}
		}
	}
#endif //__linux__
	return true;
}

#ifdef __linux__
bool mount_linux_loop(const std::string& imagepath, int partition, int64 offset, int64 length)
{
	int loopc = open64("/dev/loop-control", O_RDWR|O_CLOEXEC);
	if(loopc==-1)
	{
		exec_wait("modprobe", true, "loop", NULL);
		loopc = open64("/dev/loop-control", O_RDWR|O_CLOEXEC);
	}
	
	if(loopc==-1)
	{
		std::cout << "Error opening loop control. Err: " << errno << std::endl;
	}
	
	bool dio_available=true;
	int bd = open64(imagepath.c_str(), O_RDONLY|O_CLOEXEC|O_DIRECT);
	if(bd==-1)
	{
		bd = open64(imagepath.c_str(), O_RDONLY|O_CLOEXEC);
		if(bd==-1)
		{
			std::cerr << "Error opening backing file " << imagepath << std::endl;
			if(loopc!=-1)
				close(loopc);
			return false;
		}
		dio_available=false;
	}
	
	int last_loopd=-1;
	int devnum = -1;
	int last_devnum = -1;
	int loopd = -1;
	bool set_success=false;
	do
	{
		int next_devnum=-1;
		if(loopc!=-1)
		{
			next_devnum = ioctl(loopc, LOOP_CTL_GET_FREE);
		}
		if(next_devnum<0)
		{
			++devnum;
			std::cout << "Error getting free loop device. Err: " << errno << std::endl;
		}
		else
		{
			devnum = next_devnum;
		}
		
		if(devnum==last_devnum)
		{
			std::cerr << "Getting same loop device after EBUSY. Stopping." << std::endl;
			break;
		}
		
		last_devnum = devnum;
		
		loopd = open(("/dev/loop"+convert(devnum)).c_str(), O_RDWR|O_CLOEXEC);
		if(loopd==-1)
		{
			std::cerr << "Error opening loop device " << imagepath << std::endl;
			break;
		}
		
		int rc = ioctl(loopd, LOOP_SET_FD, bd);
		if(rc)
		{
			if(errno!=EBUSY)
			{
				std::cerr << "Error setting loop device fd. Err: " << errno << std::endl;
				break;
			}
			else
			{
				close(loopd);
				loopd=-1;
			}
		}
		else
		{
			set_success=true;
		}			
	} while(!set_success);
	
	if(loopc!=-1)
		close(loopc);
	close(bd);
	
	if(!set_success)
	{		
		if(loopd!=-1)
		{
			close(loopd);
		}
		return false;
	}
	
	loop_info64 linfo = {};
	if(offset==-1)
	{
		linfo.lo_offset = 512*1024;
	}
	else
	{
		linfo.lo_offset = offset;
		linfo.lo_sizelimit = length;
	}
	linfo.lo_flags = LO_FLAGS_READ_ONLY|LOCAL_LO_FLAGS_AUTOCLEAR;
	int rc = ioctl(loopd, LOOP_SET_STATUS64, &linfo);
	if(rc)
	{
		std::cerr << "Error setting loop device status. Err: " << errno << std::endl;
		close(loopd);
		return false;
	}
	
	if(dio_available)
	{
		unsigned long dio = 1;
		rc = ioctl(loopd, LOOP_SET_DIRECT_IO, &dio);
		if(rc)
		{
			std::cerr << "Error setting loop device to direct io. Err: " << errno << std::endl;
		}
	}
	
	std::string mountpoint = ExtractFilePath(imagepath)+"_mnt";
	
	if(partition>=0)
	{
		mountpoint+=convert(partition);
	}
	
	if(!os_directory_exists(mountpoint)
		&& !os_create_dir(mountpoint))
	{
		std::cerr << "Error creating mountpoint at \"" << mountpoint << "\". Err: " << errno << std::endl;
		close(loopd);
		return false;
	}

	chown_dir(mountpoint);
	chown_dir("/dev/loop"+convert(devnum));
	
	passwd* user_info = getpwnam("urbackup");
	std::string uid = "uid=0";
	std::string gid = "gid=0";
	if(user_info)
	{
		uid="uid="+convert(user_info->pw_uid);
		gid="gid="+convert(user_info->pw_gid);
	}
	
	ubuntu_guestmount_fix();
	
	std::cout << "Guestmount..." << std::endl;
	std::string stdout;
	bool mount_ok=true;
	if(exec_wait("guestmount", stdout, "-r", "-n", "--format=raw", "-a", ("/dev/loop"+convert(devnum)).c_str(),
				"-o", "kernel_cache",
				"-o", uid.c_str(),
				"-o", gid.c_str(),
				"-o", "allow_root",
				"-m", "/dev/sda",
				mountpoint.c_str(), NULL) )
	{
		mount_ok=false;
		std::cout << stdout;
		size_t qpos = stdout.find("Did you mean to mount one of these filesystems");
		if(qpos!=std::string::npos)
		{
			stdout=stdout.substr(qpos);
			stdout=getafter("guestmount:", stdout);
			while(getbetween("(", ")", stdout)=="swap")
			{
				stdout=getafter("guestmount:", stdout);
			}
			stdout=trim(getuntil("(", stdout));
			std::cout << "Guestmount with dev " << stdout << std::endl;
			
			mount_ok=true;
			if(exec_wait("guestmount", true, "-r", "-n", "--format=raw", "-a", ("/dev/loop"+convert(devnum)).c_str(),
				"-o", "kernel_cache",
				"-o", uid.c_str(),
				"-o", gid.c_str(),
				"-o", "allow_root",
				"-m", stdout.c_str(),
				mountpoint.c_str(), NULL) )
			{
				mount_ok=false;
			}
		}
	
		if(!mount_ok)
		{
			close(loopd);
			os_remove_dir(mountpoint);
			return false;
		}
	}
	
	std::cout << stdout;

	close(loopd);
	return true;
}
#else
bool mount_mdconfig(const std::string& imagepath, int partition)
{
	std::string mountpoint = ExtractFilePath(imagepath)+"_mnt";
	std::string unitpath = ExtractFilePath(imagepath)+"_unit";
	
	if(partition>=0)
	{
		mountpoint+=convert(partition);
		unitpath+=convert(partition);
	}

	if(partition==-1) partition=0;	

	if(os_directory_exists(mountpoint)) 
	{
		std::cout << "unmounting..." << std::endl;
		exec_wait(umount_path, true, mountpoint.c_str(), NULL);
		
		std::string unit = getFile(unitpath);
		if(!unit.empty())
		{
			std::cout << "Removing md" << unit << std::endl;
			exec_wait(mdconfig_path, true, "-d", "-u", unit.c_str(), NULL);
		}
		unlink(unitpath.c_str());
	}
	
	if(!os_directory_exists(mountpoint)
		&& !os_create_dir(mountpoint))
	{
		std::cerr << "Error creating mountpoint at \"" << mountpoint << "\". Err: " << errno << std::endl;
		return false;
	}

	chown_dir(mountpoint);
	
	for(size_t i=0;i<1024;++i)
	{
		int rc = exec_wait(mdconfig_path, true, "-a", "-t", "vnode", "-f", imagepath.c_str(), "-o", "readonly", "-u", convert(i).c_str(), NULL);
		if(rc==0)
		{
			std::cout << "Found free md unit " << i << std::endl;
			writestring(convert(i), unitpath.c_str());
			
			std::cout << "Loading kernel module..." << std::endl;
			exec_wait("/sbin/kldload", true, "fuse.ko", NULL);
			
			std::cout << "Mounting /dev/md"+convert(i)+"s"+convert(partition+1)<<" at " << mountpoint << " ..." << std::endl;
			if(system(("ntfs-3g -o ro /dev/md"+convert(i)+"s"+convert(partition+1)+" \""+ mountpoint+"\"").c_str())==0)
			{
				return true;
			}
			else
			{
				std::cout << "Mounting failed. Removing md device..." << std::endl;
				exec_wait(mdconfig_path, true, "-d", "-u", convert(i).c_str(), NULL);
				unlink(unitpath.c_str());
				return false;
			}
		}
	}
	
	std::cout << "No free mdconfig unit found." << std::endl;
	
	return false;
}
#endif

bool mount_image(const std::string& imagepath, int partition, int64 offset, int64 length)
{
	std::string ext = findextension(imagepath);

	if(ext=="raw")
	{
#ifdef __linux__
		return mount_linux_loop(imagepath, partition, offset, length);
#else
		return mount_mdconfig(imagepath, partition);
#endif
	}
	else
	{
		std::string mountpoint = ExtractFilePath(imagepath)+"/contents";
		
		if(partition>=0)
		{
			mountpoint+=convert(partition);
		}

		if(os_directory_exists(mountpoint) || errno==EACCES || errno==ENOTCONN ) 
		{
			exec_wait("guestunmount", true, mountpoint.c_str(), NULL);
		}
	
		if(!ensure_mount_dir(mountpoint, "mountpoint"))
		{
			return false;
		}

		chown_dir(mountpoint);
		
		std::string devpoint = ExtractFilePath(imagepath)+"/device";
		
		if(partition>=0)
		{
			devpoint+=convert(partition);
		}

#ifdef __linux__
		if(os_directory_exists(devpoint) || errno==EACCES || errno==ENOTCONN)
		{
			cleanup_fuse_mountpoint(devpoint, false);
			os_remove_dir(devpoint);
		}
#endif
		
		std::string mount_options="";
		passwd* user_info = getpwnam("urbackup");
		if(user_info)
		{
			mount_options+="uid="+convert(user_info->pw_uid)+",gid="+convert(user_info->pw_gid)+",allow_other";
		}
		
		ubuntu_guestmount_fix();
		
		std::cerr << "Mounting VHD without guestmount device export" << std::endl;
		std::string mount_stdout;
		int mount_rc = exec_wait(find_urbackupsrv_cmd(), mount_stdout, "mount-vhd", "-f", imagepath.c_str(), "-m", mountpoint.c_str(), "-o", mount_options.c_str(), NULL);
		std::cout << mount_stdout;
		
		if(mount_rc)
		{
			std::cout << "UrBackup mount process returned non-zero return code" << std::endl;
#ifdef __linux__
			cleanup_fuse_mountpoint(mountpoint, true);
			cleanup_fuse_mountpoint(devpoint, false);
#endif
			os_remove_dir(mountpoint);
			os_remove_dir(devpoint);
			return false;
		}
		
		return true;
	}
}

bool unmount_image(const std::string& imagepath, int partition)
{
	std::string ext = findextension(imagepath);
	std::string mountpoint;
	if(ext=="raw")
	{
		mountpoint = ExtractFilePath(imagepath)+"_mnt";
	}
	else
	{
		mountpoint = ExtractFilePath(imagepath)+"/contents";
	}
	
	if(partition>=0)
	{
		mountpoint+=convert(partition);
	}

	std::cout << "Mountpoint: " << mountpoint << std::endl;

	bool ret=true;

	if(os_directory_exists(mountpoint) || errno==EACCES || errno==ENOTCONN)
	{
#ifdef __linux__
		std::cout << "Guestunmount..." << std::endl;
		if(exec_wait("guestunmount", true, mountpoint.c_str(), NULL))
		{
			std::cerr << "Unmounting \"" << mountpoint << "\" failed." << std::endl;
			ret = false;
		}
#else
		if(exec_wait(umount_path, true, mountpoint.c_str(), NULL))
		{
			exec_wait(umount_path, true, "-f", mountpoint.c_str(), NULL);
			std::cerr << "Unmounting \"" << mountpoint << "\" failed." << std::endl;
			ret = false;
		}
#endif
		
		os_remove_dir(mountpoint);
	}
	
#ifndef __linux__
	std::string unitpath = ExtractFilePath(imagepath)+"_unit";
	if(partition>=0)
	{
		unitpath+=convert(partition);
	}	
	std::string unit = getFile(unitpath);
	if(!unit.empty())
	{
		std::cout << "Removing md device " << unit << std::endl;
		exec_wait(mdconfig_path, true, "-d", "-u", unit.c_str(), NULL);
	}
	unlink(unitpath.c_str());
#endif
	
	std::string devpoint = ExtractFilePath(imagepath)+"/device";
	if(partition>=0)
	{
		devpoint+=convert(partition);
	}
	
	if(ext!="raw" && (os_directory_exists(devpoint) || errno==EACCES || errno==ENOTCONN) )
	{
		if(exec_wait("fusermount", true, "-u", devpoint.c_str(), NULL))
		{
			exec_wait("fusermount", true, "-u", "-z", devpoint.c_str(), NULL);
			exec_wait(umount_path, true, devpoint.c_str(), NULL);
		}
		
		os_remove_dir(devpoint);
	}
	
	return ret;
}

int main(int argc, char *argv[])
{
	if(argc<2)
	{
		std::cout << "Not enough parameters" << std::endl;
		return 1;
	}

	std::string backupfolder=getBackupfolderPath();
	
	if(backupfolder.empty())
	{	
		std::cout << "Backupfolder not set" << std::endl;
		return 1;
	}
	
	if(seteuid(0)!=0)
	{
		std::cout << "Cannot become root user" << std::endl;
		return 1;
	}

	std::string cmd = argv[1];
	if(cmd=="mount")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for mount" << std::endl;
			return 1;
		}

#ifdef __FreeBSD__
		//system() on FreeBSD seems to not close file descriptors
		for (int fd=3; fd<10000; fd++) close(fd);
#endif
		std::string clientname=handleFilename(argv[2]);
		std::string name=handleFilename(argv[3]);
		std::string imagename=handleFilename(argv[4]);
		
		int partition=-1;
		int64 offset=-1;
		int64 length=0;
		if(argc>7)
		{
			partition = watoi(argv[5]);
			offset=watoi64(argv[6]);
			length=watoi64(argv[7]);
		}

		return mount_image(backupfolder + os_file_sep() + clientname + os_file_sep() + name + os_file_sep() + imagename, partition, offset, length)?0:1;
	}
	else if(cmd=="umount")
	{
		if(argc<5)
		{
			std::cout << "Not enough parameters for umount" << std::endl;
			return 1;
		}

		std::string clientname=handleFilename(argv[2]);
		std::string name=handleFilename(argv[3]);
		std::string imagename=handleFilename(argv[4]);
		
		int partition=-1;
		
		if(argc>5)
		{
			partition=watoi(argv[5]);
		}

		return unmount_image(backupfolder + os_file_sep() + clientname + os_file_sep() + name + os_file_sep() + imagename, partition)?0:1;
	}
	else if(cmd=="test")
	{
#ifndef WITH_FUSEPLUGIN
		std::cerr << "TEST FAILED: Please compile with mountvhd (./configure --with-mountvhd)" << std::endl;
		return 1;
#endif
#if defined(__linux__)
		if(exec_wait("guestmount", false, "--version", NULL)!=0)
		{
			std::cerr << "TEST FAILED: guestmount is missing (libguestfs-tools)" << std::endl;
			return 1;
		}
#elif defined(__FreeBSD__)
		if(exec_wait(mdconfig_path, false, "-l", NULL)!=0)
		{
			std::cerr << "TEST FAILED: mdconfig not present" << std::endl;
			return 1;
		}
#else
		std::cerr << "TEST FAILED: Not FreeBSD or Linux" << std::endl;
		return 1;
#endif
		std::cout << "MOUNT TEST OK" << std::endl;
		return 0;
	}
	else
	{
		std::cout << "Command not found" << std::endl;
		return 1;
	}
}

