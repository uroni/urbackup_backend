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
#endif

const std::string cmdline_version = PACKAGE_VERSION;

void show_version()
{
	std::cout << "UrBackup Client Backend v" << cmdline_version << std::endl;
	std::cout << "Copyright (C) 2011-2016 Martin Raiber" << std::endl;
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
			val = unquote_value(val);

			if(!val.empty())
			{
				real_args.push_back("--loglevel");
				real_args.push_back(unquote_value(val));
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
			val = unquote_value(val);

			if(!val.empty())
			{
				real_args.push_back("--allow_restore");
				real_args.push_back(strlower(unquote_value(val)));
			}
		}
	}	

	if(destroy_server)
	{
		delete Server;
	}
}
#endif


int main(int argc, char* argv[])
{
	if(argc==0)
	{
		std::cout << "Not enough arguments (zero arguments) -- no program name" << std::endl;
		return 1;
	}

	for(size_t i=1;i<argc;++i)
	{
		std::string arg = argv[i];

		if(arg=="--version")
		{
			show_version();
			return 0;
		}
	}

	try
	{
		TCLAP::CmdLine cmd("Run UrBackup Client Backend", ' ', cmdline_version);

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

#ifndef _WIN32
		TCLAP::ValueArg<std::string> config_arg("c", "config",
			"Read configuration parameters from config file",
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

		cmd.parse(argc, argv);

		std::vector<std::string> real_args;
		real_args.push_back(argv[0]);

#ifndef _WIN32
		if(!config_arg.getValue().empty())
		{
			read_config_file(config_arg.getValue(), real_args);
		}
#endif
		real_args.push_back("--workingdir");
		real_args.push_back(VARDIR);
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
		if(internet_only_arg.getValue())
		{
			real_args.push_back("--internet_only_mode");
			real_args.push_back("true");
		}
		if(std::find(real_args.begin(), real_args.end(), "--allow_restore")==real_args.end())
		{
			real_args.push_back("--allow_restore");
			real_args.push_back(restore_arg.getValue());
		}
		return run_real_main(real_args);
	}
	catch (TCLAP::ArgException &e)
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}
}