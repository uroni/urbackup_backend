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
}

std::string get_lin_os_version()
{
	std::string kernel = trim(run_cmd("uname -r"));
	std::string arch = trim(run_cmd("uname -m"));
	std::map<std::string, std::string> m = read_kv("lsb_release -a");
	return m["description"] + (kernel.empty()?"":("; Kernel " + kernel + (arch.empty()?"":(" " + arch))));
}