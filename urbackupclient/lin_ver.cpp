#include "lin_ver.h"
#include <stdio.h>
#include <map>
#include "../stringtools.h"

namespace
{
	std::string run_cmd(const std::string& cmd)
	{
		FILE* f=popen(cmd.c_str(), "r");
		if(f!=NULL)
		{
			std::string data;
			char buf[1024];
			while(!feof(f))
			{
				if(fgets(buf, 1024, f))
				{
					data+=buf;
				}				
			}
			pclose(f);
			return data;
		}
		return "";
	}

	std::map<std::string, std::string> read_kv(const std::string& cmd)
	{
		std::string data = run_cmd(cmd);

		std::map<std::string, std::string> ret;
		int lc = linecount(data);
		for(int i = 0; i < lc; ++i)
		{
			std::string line = getline(i+1, data);

			std::string key = strlower(trim(getuntil(":", line)));
			std::string value = trim(getafter(":", line));

			ret[key]=value;
		}
		
		return ret;
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
	
	std::map<std::string, std::string> read_kv_file(const std::string& fn)
	{
		std::string data = getFile(fn);

		std::map<std::string, std::string> ret;
		int lc = linecount(data);
		for(int i = 0; i < lc; ++i)
		{
			std::string line = getline(i+1, data);

			std::string key = strlower(trim(getuntil("=", line)));
			std::string value = unquote_value(trim(getafter("=", line)));

			ret[key]=value;
		}
		
		return ret;
	}
}

std::string get_lin_os_version()
{
	std::string kernel = trim(run_cmd("uname -r"));
	std::string arch = trim(run_cmd("uname -m"));
	std::string description;
#ifndef __APPLE__
	std::map<std::string, std::string> m = read_kv("lsb_release -a");
	description = m["description"];
	
	if(description.empty())
	{	
		if(FileExists("/etc/redhat-release"))
		{
			description = trim(getFile("/etc/redhat-release"));
		}
		else if(FileExists("/etc/os-release"))
		{
			m = read_kv_file("/etc/os-release");
			description = m["name"] + " " + m["version"];			
		}
		else if(FileExists("/etc/debian_version"))
		{
			description = "Debian " + trim(getFile("/etc/debian_version"));
		}
	}
#else
	description = "Mac OS X "+trim(run_cmd("sw_vers -productVersion"));
#endif
	return  description + (kernel.empty()?"":("; Kernel " + kernel + (arch.empty()?"":(" " + arch))));
}