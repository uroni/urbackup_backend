#pragma once

#include "../Interface/Thread.h"
#include "server_log.h"

class ClientMain;

class PhashLoad : public IThread
{
public:
	PhashLoad(ClientMain* client_main,
		int clientid, logid_t logid,
		std::string async_id);

	~PhashLoad();

	void operator()();

	bool getHash(int64 file_id, std::string& hash);

	bool hasError();

private:
	bool has_error;
	bool eof;
	ClientMain* client_main;
	int clientid;
	logid_t logid;
	std::string async_id;
	IFile* phash_file;
	int64 phash_file_pos;
};
