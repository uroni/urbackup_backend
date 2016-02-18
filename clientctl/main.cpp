/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#include "../config.h"
#define PWFILE VARDIR "/urbackup/pw.txt"
#define PWFILE_CHANGE VARDIR "/urbackup/pw_change.txt"
#else
#include <Windows.h>
#define PACKAGE_VERSION "$version_full_numeric$"
#define VARDIR ""
#define PWFILE "pw.txt"
#define PWFILE_CHANGE "pw_change.txt"
#endif

void wait(unsigned int ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep(ms * 1000);
#endif
}

const std::string cmdline_version = PACKAGE_VERSION;

void show_version()
{
	std::cout << "UrBackup Client Controller v" << cmdline_version << std::endl;
	std::cout << "Copyright (C) 2011-2016 Martin Raiber" << std::endl;
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
	std::cout << "\t" << cmd << " list" << std::endl;
	std::cout << "\t\t" "List backups and files/folders in backups" << std::endl;
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
		toc += speed_str;
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

int64 wait_for_new_process(std::string type, const std::vector<int64>& current_processes)
{
	int tries = 60;

	for (int i = 0; i < tries; ++i)
	{
		SStatusDetails sd = Connector::getStatusDetails();

		for (size_t j = 0; j < sd.running_processes.size(); ++j)
		{
			if (sd.running_processes[j].action == type)
			{
				if (std::find(current_processes.begin(), current_processes.end(), sd.running_processes[j].process_id) == current_processes.end())
				{
					return sd.running_processes[j].process_id;
				}
			}
		}

		std::string spinner = "|/-\\";

		std::cout << "\\rWaiting for server to start backup... " << spinner[i%spinner.size()];
		std::cout.flush();

		wait(1000);
	}

	return 0;
}

int follow_status(bool restore, int64 process_id)
{
	bool waiting_for_id = false;
	bool preparing = false;
	bool found_once = false;

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
				found_once = true;

				if (proc.percent_done < 0)
				{
					if (!preparing)
					{
						preparing = true;
						if (restore)
						{
							std::cout << "Preparing restore..." << std::endl;
						}
						else
						{
							std::cout << "Preparing..." << std::endl;
						}
					}
				}
				else
				{
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

						}
						else
						{
							std::cerr << "Failed." << std::endl;
						}
						return 4;
					}
				}
			}

			if (!found_once && !waiting_for_id)
			{
				waiting_for_id = true;
				if (restore)
				{
					std::cout << "Starting restore. Waiting for backup server..." << std::endl;
				}
				else
				{
					std::cout << "Waiting for process to become available..." << std::endl;
				}
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

	TCLAP::SwitchArg file_backup("l", "file", "Start file backup");
	TCLAP::SwitchArg image_backup("m", "image", "Start image backup");

	cmd.xorAdd(file_backup, image_backup);

	TCLAP::SwitchArg non_blocking_arg("b", "non-blocking",
		"Do not show backup progress and block till the backup is finished but return immediately after starting it", cmd);

	PwClientCmd pw_client_cmd(cmd, false);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	std::vector<int64> current_processes = get_current_processes();

	std::string type;
	int rc;
	if(file_backup.getValue())
	{
		type = full_backup.getValue() ? "FULL" : "INCR";

		rc = Connector::startBackup(full_backup.getValue());
	}
	else
	{
		type = full_backup.getValue() ? "FULLI" : "INCRI";

		rc = Connector::startImage(full_backup.getValue());
	}

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

int action_list(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("List backups and files/folders in backups", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, false);

	TCLAP::ValueArg<int> backupid_arg("b", "backupid",
		"Backupid of backup from which to list files/folders",
		false, 0, "id", cmd);

	TCLAP::ValueArg<std::string> path_arg("d", "path",
		"Path of folder/file of which to list backups/contents",
		false, "", "path", cmd);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	if(path_arg.getValue().empty() && !backupid_arg.isSet())
	{
		bool no_server;
		std::string filebackups = Connector::getFileBackupsList(no_server);

		if(!filebackups.empty())
		{
			std::cout << filebackups << std::endl;
			return 0;
		}
		else
		{
			if(no_server)
			{
				std::cerr << "Error getting file backups. No backup server found." << std::endl;
				return 2;
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
		if(backupid_arg.isSet())
		{
			pbackupid = &backupid_arg.getValue();
		}
		bool no_server;
		std::string filelist = Connector::getFileList(path_arg.getValue(), pbackupid, no_server);

		if(!filelist.empty())
		{
			std::cout << filelist << std::endl;
			return 0;
		}
		else
		{
			if(no_server)
			{
				std::cerr << "Error getting file list. No backup server found." << std::endl;
				return 2;
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
		std::cerr << "Error starting restore. Errorcode: " + root.get("err", -1).asInt() << std::endl;
		return 2;
	}

	int64 process_id = root["process_id"].asInt64();

	return follow_status(true, process_id);
}

int action_start_restore(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Restore files/folders from backup", ' ', cmdline_version);

	PwClientCmd pw_client_cmd(cmd, false);

	TCLAP::ValueArg<int> backupid_arg("b", "backupid",
		"Backupid of backup from which to restore files/folders",
		true, 0, "id", cmd);

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

	std::vector<SPathMap> path_map;
	for (size_t i = 0; i < map_from_arg.getValue().size(); ++i)
	{
		SPathMap new_pm;
		new_pm.source = map_from_arg.getValue()[i];
		new_pm.target = map_to_arg.getValue()[i];
		path_map.push_back(new_pm);
	}

	bool no_server;
	std::string restore_info = Connector::startRestore(path_arg.getValue(), backupid_arg.getValue(),
		path_map, no_server, !no_remove_arg.getValue(), !consider_other_fs_arg.getValue());

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
		if(no_server)
		{
			std::cerr << "Error starting restore. No backup server found." << std::endl;
			return 2;
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

	PwClientCmd pw_client_cmd(cmd, false);

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

int main(int argc, char *argv[])
{
	if(argc==0)
	{
		std::cerr << "Not enough arguments (zero arguments) -- no program name" << std::endl;
		return 1;
	}

	std::vector<std::string> actions;
	std::vector<action_fun> action_funs;
	actions.push_back("start");
	action_funs.push_back(action_start);
	actions.push_back("status");
	action_funs.push_back(action_status);
	actions.push_back("list");
	action_funs.push_back(action_list);
	actions.push_back("restore-start");
	action_funs.push_back(action_start_restore);
	actions.push_back("set-settings");
	action_funs.push_back(action_set_settings);
	actions.push_back("reset-keep");
	action_funs.push_back(action_reset_keep);

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

