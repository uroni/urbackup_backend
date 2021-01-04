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
#include "../tclap/CmdLine.h"
#include <vector>
#include "../stringtools.h"
#include <stdlib.h>
#ifndef _WIN32
#include "config.h"
#include "../Server.h"
#include "../Interface/SettingsReader.h"
#include <memory>
#else
#define PACKAGE_VERSION "unknown"
#define VARDIR ""
#define SYSCONFDIR ""
#define DATADIR ""
#endif

const std::string cmdline_version = PACKAGE_VERSION;

void show_version()
{
	std::cout << "UrBackup Client Backend v" << cmdline_version << std::endl;
	std::cout << "Copyright (C) 2011-2019 Martin Raiber" << std::endl;
	std::cout << "This is free software; see the source for copying conditions. There is NO"<< std::endl;
	std::cout << "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."<< std::endl;
}

int real_main(int argc, char* argv[])
#ifndef _WIN32
	;
#else
{return 1;}
#endif

int run_real_main(std::vector<std::string> args)
{
	char** argv = new char*[args.size()];
	for(size_t i=0;i<args.size();++i)
	{
		argv[i] = const_cast<char*>(args[i].c_str());
	}

	int rc = real_main(static_cast<int>(args.size()), argv);
	delete[] argv;
	return rc;
}

std::string unquote_value(std::string val)
{
	val = trim(val);
	if(val[0]=='"')
	{
		size_t last_pos = val.find_last_of('"');
		if(last_pos!=0)
		{
			val=val.substr(1, last_pos-1);
		}
	}
	else if(val[0]=='\'')
	{
		size_t last_pos = val.find_last_of('\'');
		if(last_pos!=0)
		{
			val=val.substr(1, last_pos-1);
		}
	}
	return val;
}

#ifndef _WIN32
void read_config_file(std::string fn, std::vector<std::string>& real_args)
{
	if (!FileExists(fn))
	{
		std::cout << "Config file at " << fn << " does not exist. Ignoring." << std::endl;
		return;
	}

	bool destroy_server=false;
	if(Server==NULL)
	{
		Server = new CServer;
		destroy_server=true;
	}

	{
		std::auto_ptr<ISettingsReader> settings(Server->createFileSettingsReader(fn));
		std::string val;
		if(settings->getValue("LOGFILE", &val))
		{
			val = unquote_value(val);

			if(!val.empty())
			{
				if(val[0]!='/')
				{
					val = "/var/log/"+val;
				}
				real_args.push_back("--logfile");
				real_args.push_back(val);
			}
		}
		if(settings->getValue("LOGLEVEL", &val))
		{
			val = trim(unquote_value(val));

			if(!val.empty())
			{
				real_args.push_back("--loglevel");
				real_args.push_back(val);
			}
		}
		if(settings->getValue("DAEMON_TMPDIR", &val))
		{
			std::string tmpdir = unquote_value(val);
			if(!tmpdir.empty())
			{
				if(setenv("TMPDIR", tmpdir.c_str(), 1)!=0)
				{
					std::cout << "Error setting TMPDIR" << std::endl;
					exit(1);
				}
			}
		}
		if(settings->getValue("RESTORE", &val))
		{
			val = trim(unquote_value(val));

			if(!val.empty())
			{
				real_args.push_back("--allow_restore");
				real_args.push_back(strlower(val));
			}
		}
		if (settings->getValue("INTERNET_ONLY", &val))
		{
			val = trim(unquote_value(val));

			if (!val.empty())
			{
				real_args.push_back("--internet_only_mode");
				real_args.push_back(strlower(val));
			}
		}
		if (settings->getValue("LOG_ROTATE_FILESIZE", &val))
		{
			val = trim(unquote_value(val));

			if (!val.empty())
			{
				real_args.push_back("--rotate-filesize");
				real_args.push_back(strlower(val));
			}
		}
		if (settings->getValue("LOG_ROTATE_NUM", &val))
		{
			val = trim(unquote_value(val));

			if (!val.empty())
			{
				real_args.push_back("--rotate-numfiles");
				real_args.push_back(strlower(val));
			}
		}
		if (settings->getValue("CAPATH", &val))
		{
			val = trim(unquote_value(val));

			if (!val.empty())
			{
				real_args.push_back("--capath");
				real_args.push_back(val);
			}
		}
	}	

	if(destroy_server)
	{
		delete static_cast<CServer*>(Server);
	}
}
#endif

#ifndef _WIN32
int restoreclient_main(int argc, char* argv[]);
#endif

int main(int argc, char* argv[])
{
	if(argc==0)
	{
		std::cout << "Not enough arguments (zero arguments) -- no program name" << std::endl;
		return 1;
	}

#ifndef _WIN32
	if (ExtractFileName(argv[0]) == "urbackuprestoreclient")
	{
		return restoreclient_main(argc, argv);
	}
#endif

	for(size_t i=1;i<argc;++i)
	{
		std::string arg = argv[i];

		if(arg=="--version")
		{
			show_version();
			return 0;
		}
	}

	if (argc > 0 &&
		std::string(argv[1]) == "--internal")
	{
		std::vector<std::string> real_args;

		real_args.push_back(argv[0]);

		for (size_t i = 2; i < argc; ++i)
		{
			real_args.push_back(argv[i]);
		}

		return run_real_main(real_args);
	}

	try
	{
		TCLAP::CmdLine cmd("Run UrBackup Client Backend", ' ', cmdline_version);

		TCLAP::ValueArg<std::string> logfile_arg("l", "logfile",
			"Specifies the log file name",
			false, "/var/log/urbackupclient.log", "path", cmd);

		std::vector<std::string> loglevels;
		loglevels.push_back("debug");
		loglevels.push_back("info");
		loglevels.push_back("warn");
		loglevels.push_back("error");

		TCLAP::ValuesConstraint<std::string> loglevels_constraint(loglevels);
		TCLAP::ValueArg<std::string> loglevel_arg("v", "loglevel",
			"Specifies the log level",
			false, "info", &loglevels_constraint, cmd);

		TCLAP::SwitchArg daemon_arg("d", "daemon", "Daemonize process", cmd, false);

		TCLAP::SwitchArg internet_only_arg("i", "internet-only", "Only connect to backup servers via internet", cmd, false);

		TCLAP::ValueArg<std::string> pidfile_arg("w", "pidfile",
			"Save pid of daemon in file",
			false, "/var/run/urbackup_srv.pid", "path", cmd);

		TCLAP::SwitchArg no_console_time_arg("t", "no-consoletime",
			"Do not print time when logging to console",
			cmd, false);

#if defined(__APPLE__)
		int default_rotate_filesize = 20971520;
		int default_rotate_numfiles = 10;
#else
		int default_rotate_filesize = 0;
		int default_rotate_numfiles = 0;
#endif

		TCLAP::ValueArg<int> rotate_filesize_arg("g", "rotate-filesize",
			"Maximum size of log file before rotation",
			false, default_rotate_filesize, "bytes", cmd);

		TCLAP::ValueArg<int> rotate_num_files_arg("j", "rotate-num-files",
			"Max number of log files during rotation",
			false, default_rotate_numfiles, "num", cmd);

#ifndef _WIN32
		TCLAP::ValueArg<std::string> config_arg("c", "config",
			"Read configuration parameters from config file",
			false, "", "path", cmd);

		TCLAP::ValueArg<std::string> capath_arg("", "capath",
			"Path to a certificate authority bundle file or directory",
			false, "", "path", cmd);
#endif

		std::vector<std::string> restore_arg_vals;
		restore_arg_vals.push_back("client-confirms");
		restore_arg_vals.push_back("server-confirms");
		restore_arg_vals.push_back("disabled");

		TCLAP::ValuesConstraint<std::string> restore_arg_constraint(restore_arg_vals);
		TCLAP::ValueArg<std::string> restore_arg("r", "restore",
			"Specifies if restores are allowed and where they have to be confirmed",
			false, "client-confirms", &restore_arg_constraint, cmd);


		std::vector<std::string> backup_args_vals;
		restore_arg_vals.push_back("incr-file");
		restore_arg_vals.push_back("if");
		restore_arg_vals.push_back("full-file");
		restore_arg_vals.push_back("ff");
#ifdef _WIN32
		restore_arg_vals.push_back("incr-image");
		restore_arg_vals.push_back("ii");
		restore_arg_vals.push_back("full-image");
		restore_arg_vals.push_back("fi");
#endif
		TCLAP::ValuesConstraint<std::string> backup_arg_constraint(restore_arg_vals);
		TCLAP::ValueArg<std::string> backup_arg("b", "backup",
			"Connects to server, runs backup, then exits",
			false, "", &backup_arg_constraint, cmd);

		TCLAP::ValueArg<int> backup_start_timeout("", "backup-start-timeout",
			"Maximum amount of time to wait for a backup to start (when run with -b/--backup) before the backup is stopped with an error",
			false, 5 * 60, "seconds", cmd);

		TCLAP::ValueArg<int> backup_timeout("", "backup-timeout",
			"Maximum amount of time to wait for a backup to continue after the connection has been lost (when run with -b/--backup) before the backup is stopped with an error",
			false, 5 * 60, "seconds", cmd);

		std::vector<std::string> real_args;
		real_args.push_back(argv[0]);

		cmd.parse(argc, argv);


#ifndef _WIN32
		if(!config_arg.getValue().empty())
		{
			read_config_file(config_arg.getValue(), real_args);
		}
#endif
		real_args.push_back("--no-server");
		real_args.push_back("--workingdir");
		real_args.push_back(VARDIR);
		real_args.push_back("--script_path");
#if defined(__APPLE__)
		// ./UrBackup\ Client.app/Contents/MacOS/bin/urbackupclientctl../../share/urbackup/scripts
		std::string datadir = ExtractFilePath(ExtractFilePath(argv[0])) + "/share/urbackup/scripts";
		real_args.push_back( datadir + ":" SYSCONFDIR "/urbackup/scripts");
#else
		real_args.push_back( DATADIR "/urbackup/scripts:" SYSCONFDIR "/urbackup/scripts");
#endif
		real_args.push_back("--pidfile");
		real_args.push_back(pidfile_arg.getValue());
		if(std::find(real_args.begin(), real_args.end(), "--logfile")==real_args.end())
		{
			real_args.push_back("--logfile");
			real_args.push_back(logfile_arg.getValue());
		}
		if(std::find(real_args.begin(), real_args.end(), "--loglevel")==real_args.end())
		{
			real_args.push_back("--loglevel");
			real_args.push_back(loglevel_arg.getValue());
		}
		if(daemon_arg.getValue())
		{
			real_args.push_back("--daemon");
		}
		if(no_console_time_arg.getValue())
		{
			real_args.push_back("--log_console_no_time");
		}
		if(std::find(real_args.begin(), real_args.end(), "--internet_only_mode") == real_args.end()
			&& internet_only_arg.getValue())
		{
			real_args.push_back("--internet_only_mode");
			real_args.push_back("true");
		}
		if(std::find(real_args.begin(), real_args.end(), "--allow_restore")==real_args.end())
		{
			real_args.push_back("--allow_restore");
			real_args.push_back(restore_arg.getValue());
		}
		if (rotate_filesize_arg.getValue() > 0
			&& std::find(real_args.begin(), real_args.end(), "--rotate-filesize") == real_args.end())
		{
			real_args.push_back("--rotate-filesize");
			real_args.push_back(convert(rotate_filesize_arg.getValue()));
		}
		if(rotate_num_files_arg.getValue() > 0
			&& std::find(real_args.begin(), real_args.end(), "--rotate-numfiles") == real_args.end())
		{
			real_args.push_back("--rotate-numfiles");
			real_args.push_back(convert(rotate_num_files_arg.getValue()));
		}

#ifndef _WIN32
		if (!capath_arg.getValue().empty()
			&& std::find(real_args.begin(), real_args.end(), "--capath") == real_args.end())
		{
			real_args.push_back("--capath");
			real_args.push_back(capath_arg.getValue());
		}
#endif
		if (!backup_arg.getValue().empty())
		{
			std::string bi = backup_arg.getValue();
			if (bi == "ff")
				bi = "full-file";
			else if (bi == "if")
				bi = "incr-file";
			else if (bi == "ii")
				bi = "incr-image";
			else if (bi == "fi")
				bi = "full-image";
			real_args.push_back("--backup_immediate");
			real_args.push_back(bi);
		}
		real_args.push_back("--backup_immediate_start_timeout");
		real_args.push_back(convert(backup_start_timeout.getValue()));
		real_args.push_back("--backup_immediate_timeout");
		real_args.push_back(convert(backup_timeout.getValue()));
		
		return run_real_main(real_args);
	}
	catch (TCLAP::ArgException &e)
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
}

#ifndef _WIN32
int restoreclient_main(int argc, char* argv[])
{
	if (argc == 0)
	{
		std::cout << "Not enough arguments (zero arguments) -- no program name" << std::endl;
		return 1;
	}

	for (size_t i = 1; i<argc; ++i)
	{
		std::string arg = argv[i];

		if (arg == "--version")
		{
			show_version();
			return 0;
		}
	}

	try
	{
		TCLAP::CmdLine cmd("Run UrBackup Restore Client", ' ', cmdline_version);

		TCLAP::ValueArg<std::string> logfile_arg("l", "logfile",
			"Specifies the log file name",
			false, "/var/log/urbackup.log", "path", cmd);

		std::vector<std::string> loglevels;
		loglevels.push_back("debug");
		loglevels.push_back("info");
		loglevels.push_back("warn");
		loglevels.push_back("error");

		TCLAP::ValuesConstraint<std::string> loglevels_constraint(loglevels);
		TCLAP::ValueArg<std::string> loglevel_arg("v", "loglevel",
			"Specifies the log level",
			false, "info", &loglevels_constraint, cmd);

		TCLAP::SwitchArg daemon_arg("d", "daemon", "Daemonize process", cmd, false);

		TCLAP::SwitchArg internet_only_arg("i", "internet-only", "Only connect to backup servers via internet", cmd, false);

		TCLAP::ValueArg<std::string> pidfile_arg("w", "pidfile",
			"Save pid of daemon in file",
			false, "/var/run/urbackup_srv.pid", "path", cmd);

		TCLAP::SwitchArg no_console_time_arg("t", "no-consoletime",
			"Do not print time when logging to console",
			cmd, false);

		TCLAP::ValueArg<std::string> config_arg("c", "config",
			"Read configuration parameters from config file",
			false, "", "path", cmd);

		TCLAP::ValueArg<std::string> restore_mbr_arg("m", "restore-mbr",
			"MBR file to restore to out_device",
			false, "", "path");

		TCLAP::ValueArg<std::string> mbr_info_arg("j", "mbr-info",
			"Show contents of .mbr file",
			false, "", "path");

		TCLAP::ValueArg<std::string> out_device_arg("o", "out-device",
			"Device file to restore to",
			false, "", "path", cmd);

		TCLAP::ValueArg<std::string> restore_image_arg("r", "restore-image",
			"Image file to restore to out_device",
			false, "", "path");

		TCLAP::SwitchArg restore_wizard_arg("e", "restore-wizard", "Start restore wizard");
		TCLAP::SwitchArg restore_client_arg("n", "restore-client", "Start restore client");

		TCLAP::ValueArg<std::string> ping_server_arg("p", "ping-server",
			"Ping server to notify it of client",
			false, "", "IP/hostname");

		TCLAP::SwitchArg image_download_progress_arg("q", "image-download-progress",
			"Return image download progress for piping to dialog");

		TCLAP::SwitchArg image_download_progress_decorate_arg("z", "image-download-progress-decorate",
			"More verbose printing of image download progress",
			cmd, false);

		TCLAP::SwitchArg image_download_arg("s", "image-download",
			"Start a image download");

		TCLAP::SwitchArg mbr_download_arg("b", "mbr-download",
			"Start a image download");

		TCLAP::ValueArg<std::string> download_token_arg("k", "download-token",
			"Token to use to start image/MBR download",
			false, "", "token", cmd);

		TCLAP::ValueArg<std::string> mbr_read_arg("u", "mbr-read",
			"Read MBR/GPT from device",
			false, "", "path");

		std::vector<TCLAP::Arg*> xorArgs;
		xorArgs.push_back(&restore_mbr_arg);
		xorArgs.push_back(&mbr_info_arg);
		xorArgs.push_back(&restore_image_arg);
		xorArgs.push_back(&restore_wizard_arg);
		xorArgs.push_back(&restore_client_arg);
		xorArgs.push_back(&ping_server_arg);
		xorArgs.push_back(&image_download_progress_arg);
		xorArgs.push_back(&mbr_download_arg);
		xorArgs.push_back(&image_download_arg);
		xorArgs.push_back(&mbr_read_arg);		

		cmd.xorAdd(xorArgs);

		cmd.parse(argc, argv);

		std::vector<std::string> real_args;
		real_args.push_back(argv[0]);

		if (!config_arg.getValue().empty())
		{
			read_config_file(config_arg.getValue(), real_args);
		}
		
		real_args.push_back("--pidfile");
		real_args.push_back(pidfile_arg.getValue());
		if (std::find(real_args.begin(), real_args.end(), "--logfile") == real_args.end())
		{
			real_args.push_back("--logfile");
			real_args.push_back(logfile_arg.getValue());
		}
		if (std::find(real_args.begin(), real_args.end(), "--loglevel") == real_args.end())
		{
			real_args.push_back("--loglevel");
			real_args.push_back(loglevel_arg.getValue());
		}
		if (daemon_arg.getValue())
		{
			real_args.push_back("--daemon");
		}
		if (no_console_time_arg.getValue())
		{
			real_args.push_back("--log_console_no_time");
		}
		if (internet_only_arg.getValue())
		{
			real_args.push_back("--internet_only_mode");
			real_args.push_back("true");
		}
		real_args.push_back("--allow_restore");
		real_args.push_back("server-confirms");
		if (restore_mbr_arg.isSet())
		{
			if (!out_device_arg.isSet())
			{
				std::cout << "You need to specify a device to which restore the MBR via --out-device" << std::endl;
				return 1;
			}
			
			real_args.push_back("--no-server");
			real_args.push_back("--restore");
			real_args.push_back("true");
			real_args.push_back("--restore_cmd");
			real_args.push_back("write_mbr");
			real_args.push_back("--mbr_filename");
			real_args.push_back(restore_mbr_arg.getValue());
			real_args.push_back("--out_device");
			real_args.push_back(out_device_arg.getValue());
		}
		else if (mbr_info_arg.isSet())
		{
			real_args.push_back("--no-server");
			real_args.push_back("--restore");
			real_args.push_back("true");
			real_args.push_back("--restore_cmd");
			real_args.push_back("mbrinfo");
			real_args.push_back("--mbr_filename");
			real_args.push_back(mbr_info_arg.getValue());
		}
		else if (restore_image_arg.isSet())
		{
			real_args.push_back("--no-server");
			real_args.push_back("--vhdcopy_in");
			real_args.push_back(restore_image_arg.getValue());
			real_args.push_back("--vhdcopy_out");
			real_args.push_back(out_device_arg.getValue());
		}
		else if (restore_wizard_arg.isSet())
		{
			real_args.push_back("--no-server");
			real_args.push_back("--restore_wizard");
			real_args.push_back("true");
		}
		else if (restore_client_arg.isSet())
		{
			real_args.push_back("--no-server");
			real_args.push_back("--restore_mode");
			real_args.push_back("true");
		}
		else if (ping_server_arg.isSet())
		{
			real_args.push_back("--no-server");
			real_args.push_back("--restore");
			real_args.push_back("true");
			real_args.push_back("--restore_cmd");
			real_args.push_back("ping_server");
			real_args.push_back("--ping_server");
			real_args.push_back(ping_server_arg.getValue());
		}
		else if (image_download_progress_arg.isSet())
		{
			real_args.push_back("--no-server");
			real_args.push_back("--restore");
			real_args.push_back("true");
			real_args.push_back("--restore_cmd");
			real_args.push_back("download_progress");
			if (image_download_progress_decorate_arg.getValue())
			{
				real_args.push_back("--decorate");
				real_args.push_back("1");
			}
		}
		else if (image_download_arg.isSet()
			|| mbr_download_arg.isSet() )
		{
			if (!out_device_arg.isSet())
			{
				std::cout << "You need to specify a device/file to which to download via --out-device" << std::endl;
				return 1;
			}

			if (!download_token_arg.isSet())
			{
				std::cout << "You need to specify a token to use via --download-token" << std::endl;
				return 1;
			}

			real_args.push_back("--no-server");
			real_args.push_back("--restore");
			real_args.push_back("true");
			real_args.push_back("--restore_cmd");
			if(mbr_download_arg.isSet())
				real_args.push_back("download_mbr");
			else
				real_args.push_back("download_image");
			real_args.push_back("--restore_token");
			real_args.push_back(download_token_arg.getValue());
			real_args.push_back("--restore_out");
			real_args.push_back(out_device_arg.getValue());
		}
		else if (mbr_read_arg.isSet())
		{
			real_args.push_back("--no-server");
			real_args.push_back("--restore");
			real_args.push_back("true");
			real_args.push_back("--restore_cmd");
			real_args.push_back("read_mbr");
			real_args.push_back("--device_fn");
			real_args.push_back(mbr_read_arg.getValue());
		}

		return run_real_main(real_args);
	}
	catch (TCLAP::ArgException &e)
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
}
#endif