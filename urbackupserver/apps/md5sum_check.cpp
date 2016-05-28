#include "../../Interface/Server.h"
#include "../../Interface/File.h"
#include <fstream>
#include <iostream>
#include "../../stringtools.h"
#include "../../md5.h"
#include "../../urbackupcommon/os_functions.h"

int md5sum_check()
{
	std::string md5sum_file = Server->getServerParameter("md5sum_file");

	std::fstream in(md5sum_file.c_str(), std::ios::in | std::ios::binary);

	if (!in.is_open())
	{
		Server->Log("Error opening md5sum_file \"" + md5sum_file + "\"", LL_ERROR);
		return 1;
	}

	std::string check_workingdir = Server->getServerParameter("check_workingdir");

	if (!check_workingdir.empty()
		&& check_workingdir[check_workingdir.size()-1]!=os_file_sep()[0])
	{
		check_workingdir += os_file_sep();
	}

	std::vector<std::string> check_failed_fns;
	std::map<std::string, size_t> failure_msgs;
	size_t n_files = 0;
	for (std::string line; std::getline(in, line);)
	{
		line = trim(line);

		if (line.empty())
			continue;

		std::string md5sum = strlower(getuntil("  ", line));
		std::string fn = getafter("  ", line);

#ifdef _WIN32
		fn = greplace("/", "\\", fn);
#endif
		fn = check_workingdir + fn;

		std::auto_ptr<IFile> f(Server->openFile(os_file_prefix(fn), MODE_READ_SEQUENTIAL_BACKUP));

		if (f.get() == NULL)
		{
			std::string errmsg = os_last_error_str();
			Server->Log("Error opening file \"" + fn + "\" for calculating md5sum. " + os_last_error_str(), LL_ERROR);
			check_failed_fns.push_back(fn+" (Open failure. "+ errmsg+")");
			++failure_msgs["Open failure. " + errmsg];
			continue;
		}

		std::cout << "Checking file \"" << fn << "\": ";

		MD5 md5;
		std::vector<char> buf;
		buf.resize(32768);

		bool has_read_error = false;
		_u32 rc;
		while ((rc = f->Read(buf.data(), static_cast<_u32>(buf.size()), &has_read_error)) > 0)
		{
			md5.update(reinterpret_cast<unsigned char*>(buf.data()), static_cast<_u32>(rc));
		}

		if (has_read_error)
		{
			std::string errmsg = os_last_error_str();
			Server->Log("Error while reading from file \"" + fn + "\" for creating md5sums. " + errmsg, LL_ERROR);
			check_failed_fns.push_back(fn+" (Read failure. "+ errmsg +")");
			++failure_msgs["Read failure. " + errmsg];
		}

		md5.finalize();

		char* hd = md5.hex_digest();
		std::string hex_dig(hd);
		delete[] hd;

		if (hex_dig != md5sum)
		{
			std::cout << "FAILED" << std::endl;
			check_failed_fns.push_back(fn);
			++failure_msgs["Content wrong"];
		}
		else
		{
			std::cout << "OK" << std::endl;
			++n_files;
		}
	}

	if (!check_failed_fns.empty())
	{
		std::cerr << "~~~~~~~~ SUMMARY ~~~~~~~~~~~" << std::endl;
		for (size_t i = 0; i < check_failed_fns.size(); ++i)
		{
			std::cerr << "FAILED: " << check_failed_fns[i] << std::endl;
		}

		std::cerr << "Check failed for " << check_failed_fns.size() << " files (successful: "  << n_files << " files)" << std::endl;

		for (std::map<std::string, size_t>::iterator it = failure_msgs.begin();
			it != failure_msgs.end(); ++it)
		{
			std::cerr << it->second << " files failed with: " << it->first << std::endl;
		}

		return 2;
	}
	else
	{
		std::cout << "Check successful for " << n_files << " files. No failures." << std::endl;
	}

	return 0;
}