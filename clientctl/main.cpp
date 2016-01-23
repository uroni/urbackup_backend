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

#ifndef _WIN32
#include "../config.h"
#define PWFILE VARDIR "/urbackup/pw.txt"
#define PWFILE_CHANGE VARDIR "/urbackup/pw_change.txt"
#else
#define PACKAGE_VERSION "$version_full_numeric$"
#define VARDIR ""
#define PWFILE "pw.txt"
#define PWFILE_CHANGE "pw_change.txt"
#endif

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

int action_start(std::vector<std::string> args)
{
	TCLAP::CmdLine cmd("Start an incremental/full image/file backup", ' ', cmdline_version);

	TCLAP::SwitchArg incr_backup("i", "incremental", "Start incremental backup");
	TCLAP::SwitchArg full_backup("f", "full", "Start full backup");

	cmd.xorAdd(incr_backup, full_backup);

	TCLAP::SwitchArg file_backup("l", "file", "Start file backup");
	TCLAP::SwitchArg image_backup("m", "image", "Start image backup");

	cmd.xorAdd(file_backup, image_backup);

	PwClientCmd pw_client_cmd(cmd, false);

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	int rc;
	if(file_backup.getValue())
	{
		rc = Connector::startBackup(full_backup.getValue());
	}
	else
	{
		rc = Connector::startImage(full_backup.getValue());
	}

	if(rc==2)
	{
		std::cerr << "Backup is already running" << std::endl;
		return 2;
	}
	else if(rc==1)
	{
		std::cout << "Backup started" << std::endl;
		return 0;
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

	cmd.parse(args);

	if (!pw_client_cmd.set())
	{
		return 3;
	}

	std::string status = Connector::getStatusDetail();
	if(!status.empty())
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
		path_map, no_server, !no_remove_arg.getValue(), consider_other_fs_arg.getValue());

	if(!restore_info.empty())
	{
		std::cout << restore_info << std::endl;
		return 0;
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

