/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2017 Martin Raiber
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

#include <iostream>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "Connector.h"
#include "../stringtools.h"
#include "../tclap/CmdLine.h"
#include "json/json.h"
#include "../urbackupcommon/os_functions.h"

#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#include "../config.h"
#include <time.h>
#include <sys/time.h>
#define PWFILE VARDIR "/urbackup/pw.txt"
#define PWFILE_CHANGE VARDIR "/urbackup/pw_change.txt"
#else
#include <Windows.h>
#define PACKAGE_VERSION "$version_full_numeric$"
#define VARDIR ""
#define PWFILE "pw.txt"
#define PWFILE_CHANGE "pw_change.txt"
#endif

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

void wait(unsigned int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep(ms * 1000);
#endif
}

int64 getTimeMS()
{
#ifdef _WIN32
	return GetTickCount64();
#else
	//return (unsigned int)(((double)clock()/(double)CLOCKS_PER_SEC)*1000.0);
	/*
	boost::xtime xt;
	boost::xtime_get(&xt, boost::TIME_UTC);
	static boost::int_fast64_t start_t=xt.sec;
	xt.sec-=start_t;
	unsigned int t=xt.sec*1000+(unsigned int)((double)xt.nsec/1000000.0);
	return t;*/
	/*timeval tp;
	gettimeofday(&tp, NULL);
	static long start_t=tp.tv_sec;
	tp.tv_sec-=start_t;
	return tp.tv_sec*1000+tp.tv_usec/1000;
	*/
#ifdef __APPLE__
	clock_serv_t cclock;
	mach_timespec_t mts;
	host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
	clock_get_time(cclock, &mts);
	mach_port_deallocate(mach_task_self(), cclock);
	return static_cast<int64>(mts.tv_sec) * 1000 + mts.tv_nsec / 1000000;
#else
	timespec tp;
	if (clock_gettime(CLOCK_MONOTONIC, &tp) != 0)
	{
		timeval tv;
		gettimeofday(&tv, NULL);
		static long start_t = tv.tv_sec;
		tv.tv_sec -= start_t;
		return tv.tv_sec * 1000 + tv.tv_usec / 1000;
	}
	return static_cast<int64>(tp.tv_sec) * 1000 + tp.tv_nsec / 1000000;
#endif //__APPLE__
#endif
}

const std::string cmdline_version = PACKAGE_VERSION;

void show_version()
{
	std::cout << "UrBackup Client Controller v" << cmdline_version << std::endl;
	std::cout << "Copyright (C) 2011-2019 Martin Raiber" << std::endl;
	std::cout << "This is free software; see the source for copying conditions. There is NO"<< std::endl;
	std::cout << "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."<< std::endl;
}

void action_help(std::string cmd)
{
	std::cout << std::endl;
	std::cout << "USAGE:" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " [--help] [--version] <command> [<args>]" << std::endl;
	std::cout << std::endl;
	std::cout << "Get specific command help with " << cmd << " <command> --help" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " start" << std::endl;
	std::cout << "\t\t" "Start an incremental/full image/file backup" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " status" << std::endl;
	std::cout << "\t\t" "Get current backup status" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " browse" << std::endl;
	std::cout << "\t\t" "Browse backups and files/folders in backups" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " restore-start" << std::endl;
	std::cout << "\t\t" "Restore files/folders from backup" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " set-settings" << std::endl;
	std::cout << "\t\t" "Set backup settings" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " reset-keep" << std::endl;
	std::cout << "\t\t" "Reset keeping files during incremental backups" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " add-backupdir" << std::endl;
	std::cout << "\t\t" "Add new directory to backup set" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " list-backupdirs" << std::endl;
	std::cout << "\t\t" "List directories that are being backed up" << std::endl;
	std::cout << std::endl;
	std::cout << "\t" << cmd << " remove-backupdir" << std::endl;
	std::cout << "\t\t" "Remove directory from backup set" << std::endl;
	std::cout << std::endl;
}

const size_t c_speed_size = 15;
const size_t c_max_l_length = 80;

size_t get_terminal_width()
{
#ifndef _WIN32
	struct winsize w;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0)
	{
		return c_max_l_length;
	}
	else
	{
		return w.ws_col;
	}
#else
	return c_max_l_length;
#endif
}

void draw_progress(int pc_done, double speed_bpms, int64 done_bytes, int64 total_bytes, std::string details, int detail_pc)
{
	static size_t max_line_length = 0;

	size_t term_width = get_terminal_width();
	size_t draw_segments = term_width / 3;

	size_t segments = (size_t)(pc_done / 100.*draw_segments);

	std::string toc = "\r[";
	for (size_t i = 0; i<draw_segments; ++i)
	{
		if (i<segments)
		{
			toc += "=";
		}
		else if (i == segments)
		{
			toc += ">";
		}
		else
		{
			toc += " ";
		}
	}
	std::string speed_str = PrettyPrintSpeed(static_cast<size_t>(speed_bpms*1000.0));
	while (speed_str.size()<c_speed_size)
		speed_str += " ";
	std::string pcdone = convert(pc_done);
	if (pcdone.size() == 1)
		pcdone = " " + pcdone;

	if (!details.empty() && detail_pc >= 0)
	{
		std::string detailpc_s = convert(detail_pc);
		if (detailpc_s.size() == 1)
			detailpc_s = " " + detailpc_s;

		details += " " + detailpc_s + "%";
	}

	toc += "] " + pcdone + "% ";
	if (total_bytes >= 0)
	{
		toc += PrettyPrintBytes(done_bytes) + "/" + PrettyPrintBytes(total_bytes) + " ";
	}

	if (speed_bpms > 0)
	{
		toc += "at " + speed_str + " ";
	}

	if (!details.empty())
	{
		toc += details;
	}


	if (toc.size() >= term_width)
		toc = toc.substr(0, term_width);

	if (toc.size()>max_line_length)
		max_line_length = toc.size();

	max_line_length = (std::min)(max_line_length, term_width);

	while (toc.size()<max_line_length)
		toc += " ";

	std::cout << toc;
	std::cout.flush();
}

typedef int(*action_fun)(std::vector<std::string> args);

class PwClientCmd
{
public:
	PwClientCmd(TCLAP::CmdLine& cmd, bool change)
		: cmd(cmd),
		pw_file_arg("p", "pw-file",
		"Use password in file",
		false, change ? PWFILE_CHANGE : PWFILE, "path", cmd),
		client_arg("c", "client",
		"Start backup on this client",
		false, "127.0.0.1", "hostname/IP", cmd),
		change(change)
	{

	}

	void wait(int64 maxtimems)
	{
		int64 starttime = getTimeMS();
		do
		{
			if (FileExists(pw_file_arg.getValue()))
			{
				return;
			}
			::wait(100);
		} while (getTimeMS() - starttime < maxtimems);
	}

	bool set()
	{
		if (change)
		{
			Connector::setPWFileChange(pw_file_arg.getValue());
		}
		else
		{
			Connector::setPWFile(pw_file_arg.getValue());
		}
		
		Connector::setClient(client_arg.getValue());

		if (trim(getFile(pw_file_arg.getValue())).empty())
		{
			if (errno != 0)
			{
				perror("urbackupclientctl");
			}
			std::cerr << "Cannot read backend password from " << pw_file_arg.getValue() << std::endl;
			return false;
		}
		else
		{
			return true;
		}
	}

private:
	TCLAP::CmdLine& cmd;
	
	bool change;
	TCLAP::ValueArg<std::string> pw_file_arg;
	TCLAP::ValueArg<std::string> client_arg;
};

std::vector<int64> get_current_processes()
{
	SStatusDetails sd = Connector::getStatusDetails();

	std::vector<int64> ret;

	for (size_t i = 0; i < sd.running_processes.size(); ++i)
	{
		ret.push_back(sd.running_processes[i].process_id);
	}

	return ret;
}

const std::string spinner = "|/-\\";

int64 wait_for_new_process(std::string type, const std::vector<int64>& current_processes)
{
	int tries = 60;

	std::string message  = "Waiting for server to start backup... ";

	for (int i = 0; i < tries; ++i)
	{
		int tries = 20;
		SStatusDetails sd = Connector::getStatusDetails();

		while (!sd.ok && tries>0)
		{
			--tries;
			wait(100);
			sd = Connector::getStatusDetails();
		}

		if (!sd.ok)
		{
			return 0;
		}

		for (size_t j = 0; j < sd.running_processes.size(); ++j)
		{
			if (sd.running_processes[j].action == type)
			{
				if (std::find(current_processes.begin(), current_processes.end(), sd.running_processes[j].process_id) == current_processes.end())
				{
					if (i > 0)
					{
						std::cout << "\r" << message << "done" << std::endl;
					}
					return sd.running_processes[j].process_id;
				}
			}
		}

		std::cout << "\r" << message << spinner[i%spinner.size()];
		std::cout.flush();

		wait(1000);
	}

	std::cout << "\r" << message << "done" << std::endl;
	return 0;
}

int follow_status(bool restore, int64 process_id)
{
	bool found_once = false;

	size_t preparing_idx = 0;
	size_t waiting_for_id_idx = 0;

	std::string waiting_msg;
	std::string preparing_msg;

	while (true)
	{
		int tries = 20;
		SStatusDetails status = Connector::getStatusDetails();

		while (!status.ok && tries>0)
		{
			--tries;
			wait(100);
			status = Connector::getStatusDetails();
		}

		if (!status.ok)
		{
			std::cerr << "Could not get status from backend" << std::endl;
			return 3;
		}

		bool found = false;

		for (size_t i = 0; i < status.running_processes.size(); ++i)
		{
			SRunningProcess& proc = status.running_processes[i];
			if (status.running_processes[i].process_id == process_id)
			{
				if (!found_once && waiting_for_id_idx>0)
				{
					std::cout << "\r" << waiting_msg << " done" << std::endl;
				}

				found_once = true;

				if (proc.percent_done < 0)
				{
					if (restore)
					{
						preparing_msg = "Preparing restore... ";
						std::cout << "\r" << preparing_msg << spinner[preparing_idx%spinner.size()];
					}
					else
					{
						preparing_msg = "Preparing... ";
						std::cout << "\r" << preparing_msg << spinner[preparing_idx%spinner.size()];
					}

					std::cout.flush();					

					++preparing_idx;
				}
				else
				{
					if (preparing_idx > 0)
					{
						std::cout << "\r" << preparing_msg << "done" << std::endl;
						preparing_idx = 0;
					}
					draw_progress(proc.percent_done, proc.speed_bpms, proc.done_bytes, proc.total_bytes, proc.details, proc.detail_pc);
				}

				found = true;
				break;
			}
		}

		if (!found)
		{
			for (size_t i = 0; i < status.finished_processes.size(); ++i)
			{
				if (status.finished_processes[i].id == process_id)
				{
					if (status.finished_processes[i].success)
					{
						std::cout << std::endl;
						if (restore)
						{
							std::cout << "Restore completed successfully." << std::endl;
						}
						else
						{
							std::cout << "Completed successfully." << std::endl;
						}
						return 0;
					}
					else
					{
						std::cout << std::endl;
						if (restore)
						{
							std::cerr << "Restore failed." << std::endl;
						}
						else
						{
							std::cerr << "Failed." << std::endl;
						}
						return 4;
					}
				}
			}

			if (!found_once)
			{
				if (restore)
				{
					waiting_msg = "Starting restore. Waiting for backup server... ";
					std::cout << "\r" << waiting_msg << spinner[waiting_for_id_idx%spinner.size()];;
				}
				else
				{
					waiting_msg = "Waiting for process to become available... ";
					std::cout << "\r" << waiting_msg << spinner[waiting_for_id_idx%spinner.size()];;
				}

				std::cout.flush();

				++waiting_for_id_idx;
			}
		}

		wait(1000);
	}
}

int action_start(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Start an incremental/full image/file backup", ' ', cmdline_version);

	TCLAP::SwitchArg incr_backup("i", "incremental", "Start incremental backup");
	TCLAP::SwitchArg full_backup("f", "full", "Start full backup");

	cmd.xorAdd(incr_backup, full_backup);

#ifdef _WIN32
	TCLAP::SwitchArg file_backup("l", "file", "Start file backup");
	TCLAP::SwitchArg image_backup("m", "image", "Start image backup");

	cmd.xorAdd(file_backup, image_backup);
#endif

	TCLAP::SwitchArg non_blocking_arg("b", "non-blocking",
		"Do not show backup progress and block till the backup is finished but return immediately after starting it", cmd);

	TCLAP::ValueArg<std::string> virtual_client_arg("v", "virtual-client",
		"Virtual client name",
		false, "", "client name", cmd);

	PwClientCmd pw_client_cmd(cmd, false);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	std::vector<int64> current_processes = get_current_processes();

	std::string type;
	int rc;
#ifdef _WIN32
	if(file_backup.getValue())
	{
#endif
		type = full_backup.getValue() ? "FULL" : "INCR";

		rc = Connector::startBackup(virtual_client_arg.getValue(), full_backup.getValue());
#ifdef _WIN32
	}
	else
	{
		type = full_backup.getValue() ? "FULLI" : "INCRI";

		rc = Connector::startImage(virtual_client_arg.getValue(), full_backup.getValue());
	}
#endif

	if(rc==2)
	{
		std::cerr << "Backup is already running" << std::endl;
		return 2;
	}
	else if(rc==1)
	{
		if (non_blocking_arg.getValue())
		{
			std::cout << "Backup started" << std::endl;
			return 0;
		}
		else
		{
			int64 new_process = wait_for_new_process(type, current_processes);

			if (new_process == 0)
			{
				std::cerr << "Timeout while waiting for server to start backup" << std::endl;
				return 4;
			}

			return follow_status(false, new_process);
		}
	}
	else if(rc==3)
	{
		std::cerr << "Error starting backup. No backup server found." << std::endl;
		return 3;
	}
	else
	{
		std::cerr << "Error starting backup." << std::endl;
		return 1;		
	}
}

int action_status(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Get current backup status", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, false);

	TCLAP::ValueArg<int> follow_arg("f", "follow",
		"Follow proccess status",
		false, 0, "process id", cmd);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	if (follow_arg.getValue() == 0)
	{
		std::string status = Connector::getStatusDetailsRaw();
		if (!status.empty())
		{
			std::cout << status << std::endl;
			return 0;
		}
		else
		{
			std::cerr << "Error getting status" << std::endl;
			return 1;
		}
	}
	else
	{
		return follow_status(false, follow_arg.getValue());
	}	
}

int action_browse(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Browse backups and files/folders in backups", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, false);

	TCLAP::ValueArg<std::string> backupid_arg("b", "backupid",
		"Backupid of backup in which to browse files/folders or \"last\" for last complete backup",
		false, "", "id", cmd);

	TCLAP::ValueArg<std::string> path_arg("d", "path",
		"Path of folder/file to which to browse",
		false, "", "path", cmd);

	TCLAP::ValueArg<std::string> virtual_client_arg("v", "virtual-client",
		"Virtual client name",
		false, "", "client name", cmd);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	if(path_arg.getValue().empty() && !backupid_arg.isSet())
	{
		Connector::EAccessError access_error;
		std::string filebackups = Connector::getFileBackupsList(virtual_client_arg.getValue(), access_error);

		if(!filebackups.empty())
		{
			std::cout << filebackups << std::endl;
			return 0;
		}
		else
		{
			if(access_error==Connector::EAccessError_NoServer)
			{
				std::cerr << "Error getting file backups. No backup server found." << std::endl;
				return 2;
			}
			else if (access_error == Connector::EAccessError_NoTokens)
			{
				std::cerr << "No file backup access tokens found. Did you run a file backup yet?" << std::endl;
				return 3;
			}
			else
			{
				std::cerr << "Error getting file backups" << std::endl;
				return 1;
			}			
		}
	}
	else
	{
		int* pbackupid = NULL;
		int backupid = 0;
		if(backupid_arg.isSet())
		{
			if (backupid_arg.getValue() != "last"
				&& convert(atoi(backupid_arg.getValue().c_str())) != backupid_arg.getValue())
			{
				std::cerr << "Not a valid backupid: \"" << backupid_arg.getValue() << "\"" << std::endl;
				return 3;
			}

			if (backupid_arg.getValue() != "last")
			{
				backupid = atoi(backupid_arg.getValue().c_str());
			}
			pbackupid = &backupid;
		}
		Connector::EAccessError access_error;
		std::string filelist = Connector::getFileList(path_arg.getValue(), pbackupid, virtual_client_arg.getValue(), access_error);

		if(!filelist.empty())
		{
			std::cout << filelist << std::endl;
			return 0;
		}
		else
		{
			if (access_error == Connector::EAccessError_NoServer)
			{
				std::cerr << "Error getting file list. No backup server found." << std::endl;
				return 2;
			}
			else if (access_error == Connector::EAccessError_NoTokens)
			{
				std::cerr << "No file backup access tokens found. Did you run a file backup yet?" << std::endl;
				return 3;
			}
			else
			{
				std::cerr << "Error getting file list" << std::endl;
				return 1;
			}
		}
	}
}

int wait_for_restore(std::string restore_info)
{
	Json::Value root;
	Json::Reader reader;

	if (!reader.parse(restore_info, root, false))
	{
		return 1;
	}

	if (root.get("ok", false) == false)
	{
		std::cerr << "Error starting restore. Errorcode: " << root.get("err", -1).asInt() << std::endl;
		return 2;
	}

	int64 process_id = root["process_id"].asInt64();

	return follow_status(true, process_id);
}

std::string remove_ending_slash(const std::string& path)
{
	if (path.size() > 1
		&& path[path.size() - 1] == os_file_sep()[0])
	{
		return path.substr(0, path.size() - 1);
	}
	
	return path;
}

int action_start_restore(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Restore files/folders from backup", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, false);

	TCLAP::ValueArg<std::string> backupid_arg("b", "backupid",
		"Backupid of backup from which to restore files/folders or \"last\" for last complete backup",
		true, "", "id", cmd);

	TCLAP::ValueArg<std::string> path_arg("d", "path",
		"Path of folder/file to restore",
		false, "", "path", cmd);

	TCLAP::MultiArg<std::string> map_from_arg("m", "map-from",
		"Map from local output path of folders/files to a different local path",
		false, "path", cmd);

	TCLAP::MultiArg<std::string> map_to_arg("t", "map-to",
		"Map to local output path of folders/files to a different local path",
		false, "path", cmd);

	TCLAP::SwitchArg no_remove_arg("n", "no-remove",
		"Do not remove files/directories not in backup", cmd);

	TCLAP::SwitchArg consider_other_fs_arg("o", "consider-other-fs",
		"Consider other file systems when removing files/directories not in backup", cmd);

	TCLAP::SwitchArg non_blocking_arg("l", "non-blocking",
		"Do not show restore progress and block till the restore is finished but return immediately after starting it", cmd);

	TCLAP::SwitchArg no_follow_symlinks("s", "no-follow-symlinks",
		"Do not follow symlinks outside of restored path during restore", cmd);

	TCLAP::ValueArg<std::string> virtual_client_arg("v", "virtual-client",
		"Virtual client name",
		false, "", "client name", cmd);

	cmd.parse(args);

	if (map_from_arg.getValue().size() != map_to_arg.getValue().size())
	{
		std::cerr << "There need to be an equal amount of -m/--map-from and -t/--map-to arguments" << std::endl;
		return 2;
	}

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	if (backupid_arg.getValue() != "last"
		&& convert(atoi(backupid_arg.getValue().c_str())) != backupid_arg.getValue())
	{
		std::cerr << "Not a valid backupid: \"" << backupid_arg.getValue() << "\"" << std::endl;
		return 2;
	}

	std::vector<SPathMap> path_map;
	for (size_t i = 0; i < map_from_arg.getValue().size(); ++i)
	{
		SPathMap new_pm;
		new_pm.source = remove_ending_slash(map_from_arg.getValue()[i]);
		new_pm.target = remove_ending_slash(map_to_arg.getValue()[i]);

		if (new_pm.source == os_file_sep()
			&& new_pm.target != os_file_sep())
		{
			new_pm.target += os_file_sep();
		}

		if (new_pm.target == os_file_sep()
			&& new_pm.source != os_file_sep())
		{
			new_pm.target = std::string();
		}

		path_map.push_back(new_pm);
	}

	int backupid = 0;
	if (backupid_arg.getValue() != "last")
	{
		backupid = atoi(backupid_arg.getValue().c_str());
	}

	Connector::EAccessError access_error;
	std::string restore_info = Connector::startRestore(path_arg.getValue(), backupid, virtual_client_arg.getValue(),
		path_map, access_error, !no_remove_arg.getValue(), !consider_other_fs_arg.getValue(),
		!no_follow_symlinks.getValue());

	if(!restore_info.empty())
	{
		if (non_blocking_arg.getValue())
		{
			std::cout << restore_info << std::endl;
			return 0;
		}
		else
		{
			return wait_for_restore(restore_info);
		}
	}
	else
	{
		if(access_error == Connector::EAccessError_NoServer)
		{
			std::cerr << "Error starting restore. No backup server found." << std::endl;
			return 2;
		}
		else if (access_error == Connector::EAccessError_NoTokens)
		{
			std::cerr << "Error starting restore. No file backup access tokens found. Did you run a file backup yet?" << std::endl;
			return 3;
		}
		else
		{
			std::cerr << "Error starting restore" << std::endl;
			return 1;
		}
	}
}

int action_set_settings(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Set backup settings", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, true);

	TCLAP::MultiArg<std::string> key_arg("k", "key",
		"Key of the setting to set",
		false, "setting key", cmd);

	TCLAP::MultiArg<std::string> value_arg("v", "value",
		"New value to set the setting to",
		false, "setting value", cmd);

	cmd.parse(args);

	if (key_arg.getValue().size() != value_arg.getValue().size())
	{
		std::cerr << "There need to be an equal amount of -k/--key and -v/--value arguments" << std::endl;
		return 2;
	}

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	std::string s_settings;
	for (size_t i = 0; i < key_arg.getValue().size(); ++i)
	{
		s_settings += key_arg.getValue()[i] + "=" + value_arg.getValue()[i] + "\n";
	}

	s_settings += "keep_old_settings=true\n";

	bool no_perm;
	bool b = Connector::updateSettings(s_settings, no_perm);

	if (!b)
	{
		if (no_perm)
		{
			std::cerr << "Error setting settings. Client is not allowed to change settings." << std::endl;
		}
		else
		{
			std::cerr << "Error setting settings." << std::endl;
		}
		return 1;
	}
	else
	{
		return 0;
	}
}

int action_reset_keep(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Reset keeping files during incremental backups", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, true);

	TCLAP::ValueArg<std::string> virtual_client_arg("v", "virtual-client",
		"Virtual client name",
		false, "", "client name", cmd);

	TCLAP::ValueArg<std::string> backup_folder_arg("b", "backup-folder",
		"Backup folder name",
		false, "", "folder name", cmd);

	TCLAP::ValueArg<int> group_arg("g", "backup-group",
		"Backup group index",
		false, 0, "group index", cmd);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	std::string ret = Connector::resetKeep(virtual_client_arg.getValue(), backup_folder_arg.getValue(), group_arg.getValue());

	if (ret == "OK")
	{
		return 0;
	}
	else if (ret == "err_virtual_client_not_found")
	{
		std::cerr << "Error: Virtual client not found" << std::endl;
		return 4;
	}
	else if (ret == "err_backup_folder_not_found")
	{
		std::cerr << "Error: Backup folder not found" << std::endl;
		return 5;
	}
	else
	{
		std::cerr << "Error: " << ret << std::endl;
		return 6;
	}
}

std::string removeChars(std::string in)
{
	char illegalchars[] = { '*', ':', '/' , '\\' };
	std::string ret;
	for (size_t i = 0; i<in.size(); ++i)
	{
		bool found = false;
		for (size_t j = 0; j<sizeof(illegalchars) / sizeof(illegalchars[0]); ++j)
		{
			if (illegalchars[j] == in[i])
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			ret += in[i];
		}
	}
	return ret;
}

bool findPathName(const std::vector<SBackupDir>& dirs, const std::string &pn)
{
	for (size_t i = 0; i<dirs.size(); ++i)
	{
		if (dirs[i].name == pn)
		{
			return true;
		}
	}
	return false;
}

std::string getDefaultDirname(const std::vector<SBackupDir>& dirs, const std::string &path)
{
	std::string dirname = removeChars(ExtractFileName(path));

	if (dirname.empty())
		dirname = "rootfs";

	if (findPathName(dirs, dirname))
	{
		for (int k = 0; k<100; ++k)
		{
			if (!findPathName(dirs, dirname + "_" + convert(k)))
			{
				dirname = dirname + "_" + convert(k);
				break;
			}
		}
	}

	return dirname;
}

int action_add_backupdir(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Add new directory to backup set", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, true);

	TCLAP::ValueArg<std::string> virtual_client_arg("v", "virtual-client",
		"Virtual client name",
		false, "", "client name", cmd);

	TCLAP::ValueArg<std::string> name_arg("n", "name",
		"Backup directory name",
		false, "", "name", cmd);

	TCLAP::ValueArg<std::string> path_arg("d", "path",
		"Backup path",
		true, "", "path", cmd);

	TCLAP::ValueArg<int> group_arg("g", "backup-group",
		"Backup group index",
		false, 0, "group index", cmd);

	TCLAP::SwitchArg optional_arg("o", "optional",
		"Do not fail backup if path does not exist",
		cmd);

	TCLAP::SwitchArg no_follow_symlinks_arg("f", "no-follow-symlinks",
		"Do not follow symbolic links outside of backup path",
		cmd);

	TCLAP::SwitchArg symlinks_required_arg("r", "require-symlinks",
		"Fail backup if symbolic link targets do not exist",
		cmd);

	TCLAP::SwitchArg one_filesystem_arg("x", "one-filesystem",
		"Do not cross filesystem boundary during backup",
		cmd);

	TCLAP::SwitchArg require_snapshot_arg("s", "require-snapshot",
		"Fail backup if snapshot of backup path cannot be created",
		cmd);

	TCLAP::SwitchArg separate_hashes_arg("a", "separate-hashes",
		"Do not share local hashes with other virtual clients",
		cmd);

	TCLAP::SwitchArg keep_arg("k", "keep",
		"Keep deleted files and directories during incremental backups. DO NOT USE",
		cmd);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	std::string flags;

	if (optional_arg.getValue())
	{
		if (!flags.empty()) flags += ",";
		flags += "optional";
	}

	if (!no_follow_symlinks_arg.getValue())
	{
		if (!flags.empty()) flags += ",";
		flags += "follow_symlinks";
	}

	if (!symlinks_required_arg.getValue())
	{
		if (!flags.empty()) flags += ",";
		flags += "symlinks_optional";
	}

	if (one_filesystem_arg.getValue())
	{
		if (!flags.empty()) flags += ",";
		flags += "one_filesystem";
	}

	if (require_snapshot_arg.getValue())
	{
		if (!flags.empty()) flags += ",";
		flags += "require_snapshot";
	}

	if (!separate_hashes_arg.getValue())
	{
		if (!flags.empty()) flags += ",";
		flags += "share_hashes";
	}

	if (keep_arg.getValue())
	{
		if (!flags.empty()) flags += ",";
		flags += "keep";
	}

	std::vector<SBackupDir> backup_dirs = Connector::getSharedPaths(true);

	if (Connector::hasError())
	{
		std::cerr << "Error retrieving current backup directories from backend" << std::endl;
		return 1;
	}

	SBackupDir new_dir;
	new_dir.path = path_arg.getValue();
	if (name_arg.getValue().empty())
	{
		new_dir.name = getDefaultDirname(backup_dirs, new_dir.path);
	}
	else
	{
		new_dir.name = name_arg.getValue();
	}

	new_dir.group = group_arg.getValue();

	new_dir.flags = flags;

	new_dir.virtual_client = virtual_client_arg.getValue();
	
	backup_dirs.push_back(new_dir);
	
	if (!Connector::saveSharedPaths(backup_dirs))
	{
		std::cerr << "Error adding new backup path via backend" << std::endl;
		return 2;
	}
	else
	{
		return 0;
	}
}

void ouput_val(std::string val, size_t max_size)
{
	if (val.size() > max_size)
	{
		val = val.substr(0, max_size);
	}
	std::cout << val;
	for (size_t i = val.size(); i < max_size; ++i)
	{
		std::cout << ' ';
	}
}

void display_table(const std::vector<std::vector<std::string> >& rows)
{
	if (rows.empty())
	{
		return;
	}

	const size_t val_gap = 1;

	std::vector<size_t> max_size;
	max_size.resize(rows[0].size());

	for (size_t i = 0; i < rows[0].size(); ++i)
	{
		for (size_t j = 0; j < rows.size(); ++j)
		{
			max_size[i] = (std::max)(rows[j][i].size(), max_size[i]);
		}
	}

	for (size_t i = 0; i < rows[0].size(); ++i)
	{
		ouput_val(rows[0][i], max_size[i]+ val_gap);
	}

	std::cout << std::endl;

	for (size_t i = 0; i < rows[0].size(); ++i)
	{
		std::cout << std::string(max_size[i], '-');
		std::cout << std::string(val_gap, ' ');
	}

	std::cout << std::endl;

	for (size_t i = 1; i < rows.size(); ++i)
	{
		for (size_t j = 0; j < rows[i].size(); ++j)
		{
			ouput_val(rows[i][j], max_size[j] + val_gap);
		}
		std::cout << std::endl;
	}
}

int action_list_backupdirs(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("List directories that are being backed up", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, false);

	TCLAP::ValueArg<std::string> virtual_client_arg("v", "virtual-client",
		"Display only for virtual with name",
		false, "", "client name", cmd);

	TCLAP::SwitchArg raw_arg("r", "raw",
		"Return raw JSON output", cmd);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	if (raw_arg.getValue())
	{
		std::string ret = Connector::getSharedPathsRaw();
		if (ret.empty())
		{
			std::cerr << "Error retrieving current backup directories from backend" << std::endl;
			return 1;
		}
		std::cout << ret;
		std::cout.flush();
		return 0;
	}

	std::vector<SBackupDir> backup_dirs = Connector::getSharedPaths(false);

	if (Connector::hasError())
	{
		std::cerr << "Error retrieving current backup directories from backend" << std::endl;
		return 1;
	}

	if (backup_dirs.empty())
	{
		std::cout << "No directories are being backed up" << std::endl;
		return 0;
	}

	bool has_virtual_client = false;
	bool has_group = false;

	for (size_t i = 0; i < backup_dirs.size(); ++i)
	{
		if (!backup_dirs[i].virtual_client.empty())
		{
			has_virtual_client = true;
		}
		if (backup_dirs[i].group != 0)
		{
			has_group = true;
		}
	}

	std::vector<std::vector<std::string> > tab;

	std::vector<std::string> tab_header;
	tab_header.push_back("PATH");
	tab_header.push_back("NAME");
	if (has_group)
	{
		tab_header.push_back("GROUP");
	}
	if (has_virtual_client)
	{
		tab_header.push_back("VIRTUAL CLIENT");
	}
	tab_header.push_back("FLAGS");

	tab.push_back(tab_header);

	for (size_t i = 0; i < backup_dirs.size(); ++i)
	{
		std::vector<std::string> row;
		row.push_back(backup_dirs[i].path);

		if (backup_dirs[i].name.empty())
		{
			backup_dirs[i].name = getDefaultDirname(backup_dirs, backup_dirs[i].path);
		}

		row.push_back(backup_dirs[i].name);

		if (has_group)
		{
			row.push_back(convert(backup_dirs[i].group));
		}

		if (has_virtual_client)
		{
			if (backup_dirs[i].virtual_client.empty())
			{
				row.push_back("-");
			}
			else
			{
				row.push_back(backup_dirs[i].virtual_client);
			}
		}

		row.push_back(backup_dirs[i].flags);

		tab.push_back(row);
	}

	display_table(tab);

	return 0;
}

int action_remove_backupdir(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Remove directory from backup set", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, true);

	TCLAP::ValueArg<std::string> name_arg("n", "name",
		"Backup directory name",
		false, "", "name");

	TCLAP::ValueArg<std::string> path_arg("d", "path",
		"Backup path",
		true, "", "path");

	cmd.xorAdd(name_arg, path_arg);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	std::vector<SBackupDir> backup_dirs = Connector::getSharedPaths(true);

	if (Connector::hasError())
	{
		std::cerr << "Error retrieving current backup directories from backend" << std::endl;
		return 1;
	}

	bool del_ok = false;

	for (size_t i = 0; i < backup_dirs.size();)
	{
		if (!name_arg.getValue().empty()
			&& backup_dirs[i].name == name_arg.getValue())
		{
			backup_dirs.erase(backup_dirs.begin() + i);
			del_ok = true;
		}
		else if (!path_arg.getValue().empty()
			&& backup_dirs[i].path == path_arg.getValue())
		{
			backup_dirs.erase(backup_dirs.begin() + i);
			del_ok = true;
		}
		else
		{
			++i;
		}
	}

	if (!del_ok)
	{
		std::cerr << "Backup directory to remove not found" << std::endl;
		return 1;
	}

	if (!Connector::saveSharedPaths(backup_dirs))
	{
		std::cerr << "Error removing backup directory via backend" << std::endl;
		return 2;
	}
	else
	{
		return 0;
	}
}

int action_wait_for_backend(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Wait for backend to become available", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, false);

	TCLAP::ValueArg<int> time_arg("t", "time",
		"Max time in seconds to wait",
		false, 60, "seconds", cmd);

	cmd.parse(args);

	pw_client_cmd.wait(time_arg.getValue() * 1000);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	int64 starttime = getTimeMS();
	do
	{
		int64 thistime = getTimeMS();
		std::string d = Connector::getStatusRawNoWait();
		if (!Connector::hasError()
			&& !d.empty())
		{
			return 0;
		}
		if (getTimeMS() - thistime < 30)
		{
			wait(100);
		}
	} while (getTimeMS() - starttime < time_arg.getValue() * 1000);

	std::cerr << "Could not connect to backend in specified time" << std::endl;
	return 1;
}

int main(int argc, char *argv[])
{
	if(argc==0)
	{
		std::cerr << "Not enough arguments (zero arguments) -- no program name" << std::endl;
		return 1;
	}

#ifdef _WIN32
	HMODULE hModule = GetModuleHandleW(NULL);
	if (hModule != INVALID_HANDLE_VALUE)
	{
		WCHAR path[MAX_PATH];
		if (GetModuleFileNameW(hModule, path, MAX_PATH) != 0)
		{
			SetCurrentDirectoryW(path);
		}
	}
#endif

	std::vector<std::string> actions;
	std::vector<action_fun> action_funs;
	actions.push_back("start");
	action_funs.push_back(action_start);
	actions.push_back("status");
	action_funs.push_back(action_status);
	actions.push_back("browse");
	action_funs.push_back(action_browse);
	actions.push_back("restore-start");
	action_funs.push_back(action_start_restore);
	actions.push_back("set-settings");
	action_funs.push_back(action_set_settings);
	actions.push_back("reset-keep");
	action_funs.push_back(action_reset_keep);
	actions.push_back("add-backupdir");
	action_funs.push_back(action_add_backupdir);
	actions.push_back("list-backupdirs");
	action_funs.push_back(action_list_backupdirs);
	actions.push_back("remove-backupdir");
	action_funs.push_back(action_remove_backupdir);
	actions.push_back("wait-for-backend");
	action_funs.push_back(action_wait_for_backend);

	bool has_help=false;
	bool has_version=false;
	size_t action_idx=std::string::npos;
	std::vector<std::string> args;
	args.push_back(argv[0]);
	for(int i=1;i<argc;++i)
	{
		std::string arg = argv[i];

		if(arg=="--help" || arg=="-h")
		{
			has_help=true;
		}

		if(arg=="--version")
		{
			has_version=true;
		}

		if(!arg.empty() && arg[0]=='-')
		{
			args.push_back(arg);
			continue;
		}

		bool found_action=false;
		for(size_t j=0;j<actions.size();++j)
		{
			if(next(actions[j], 0, arg))
			{
				if(action_idx!=std::string::npos)
				{
					action_help(argv[0]);
					exit(1);
				}
				action_idx=j;
				found_action=true;
			}
		}

		if(!found_action)
		{
			args.push_back(arg);
		}
	}

	if(action_idx==std::string::npos)
	{
		if(has_help)
		{
			action_help(argv[0]);
			exit(1);
		}
		if(has_version)
		{
			show_version();
			exit(1);
		}
		action_help(argv[0]);
		exit(1);
	}

	try
	{
		args[0]+=" "+actions[action_idx];
		int rc = action_funs[action_idx](args);
		return rc;
	}
	catch (TCLAP::ArgException &e)
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
}

