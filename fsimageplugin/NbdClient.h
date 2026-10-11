#pragma once
#include "../Interface/CustomClient.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include <vector>

class NbdService;

enum EClientState
{
	ClientState_Handshake,
	ClientState_Data
};

class NbdClient : public ICustomClient
{
public:
	NbdClient(NbdService* service, IFile* backing_file);
	~NbdClient();

	virtual void Init( THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName, const IClientWakeup* wakeup );

	virtual int Run();

	virtual void ReceivePackets();

	bool handleRequests();

private:

	NbdService* service;

	bool shutdown;
	bool was_client;
	IPipe* pipe;

	EClientState state;

	IFile* backing_file;

	size_t recv_buf_off;
	std::vector<char> recv_buf;

	std::vector<char> response_buf;
};
