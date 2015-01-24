#pragma once

#include "../Interface/Thread.h"
#include <string>
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../Interface/File.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "../Interface/Database.h"


class RestoreFiles : public IThread, public FileClient::ReconnectionCallback, public FileClientChunked::ReconnectionCallback
{
public:
	RestoreFiles(int restoreid, std::string client_token, std::string server_token) 
		: restoreid(restoreid), client_token(client_token), server_token(server_token), tcpstack(true), filelist_del(NULL), filelist(NULL)
	{

	}

	void operator()();

	virtual IPipe * new_fileclient_connection( );

	void log(const std::string& msg, int loglevel);
	void log(const std::wstring& msg, int loglevel);


private:
	
	bool connectFileClient(FileClient& fc);
	bool downloadFilelist(FileClient& fc);

	int64 calculateDownloadSize();

	bool downloadFiles(FileClient& fc, int64 total_size);
	
	std::auto_ptr<FileClientChunked> createFcChunked();

	int restoreid;

	IFile* filelist;
	ScopedDeleteFile filelist_del;

	std::string client_token;
	std::string server_token;

	CTCPStack tcpstack;

	IDatabase* db;

};