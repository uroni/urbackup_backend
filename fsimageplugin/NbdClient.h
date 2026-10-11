#pragma once
#include "../Interface/CustomClient.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include <vector>

class NbdService;

enum EClientState
{
	ClientState_Handshake,
	ClientState_ClientFlags,
	ClientState_Options,
	ClientState_Data
};

class NbdClient : public ICustomClient
{
public:
	NbdClient(NbdService* service, IFile* backing_file, bool read_only);
	~NbdClient();

	virtual void Init( THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName, const IClientWakeup* wakeup );

	virtual int Run();

	virtual void ReceivePackets();

	bool handleRequests();

private:
	bool handleClientFlags();
	bool handleOption();
	bool sendOptionReply(unsigned int opt, unsigned int reply_type, const char* data, size_t data_size);
	bool sendExportInfo(unsigned int opt);
	unsigned short transmissionFlags();
	void consumeRecvBuf(size_t n);

	NbdService* service;

	bool shutdown;
	bool was_client;
	IPipe* pipe;

	EClientState state;
	bool no_zeroes;

	IFile* backing_file;
	bool read_only;

	size_t recv_buf_off;
	std::vector<char> recv_buf;

	std::vector<char> response_buf;
};
