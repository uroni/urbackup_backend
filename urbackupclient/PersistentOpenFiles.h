#pragma once

#include <string>
#include <map>
#include <vector>

#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../common/data.h"

const char PERSIST_ADD=0;
const char PERSIST_REMOVE=1;

class PersistentOpenFiles
{
public:
	
	PersistentOpenFiles();

	void add(const std::wstring& fn);

	void remove(const std::wstring& fn);

	std::vector<std::wstring> get();

	bool flushf();

private:

	bool flushf_int(bool allow_cycle);

	bool load();

	void addf(const std::wstring& fn, unsigned int id);

	void removef(unsigned int id, size_t fn_size);	

	bool cycle();

	CWData wdata;

	IFile* persistf;

	std::map<std::wstring, unsigned int> open_files;

	size_t bytes_written;

	size_t bytes_deleted;

	unsigned int curr_id;
};