#include "SambaService.h"
#include "../Interface/Thread.h"
#include "../Interface/Pipe.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "ClientService.h"

namespace
{
	class StreamInput : public IThread
	{
		IPipe* pipe;
		IPipe* smbPipe;
	public:
		StreamInput(IPipe* pipe, IPipe* smbPipe)
			: pipe(pipe), smbPipe(smbPipe)
		{ }

		void operator()()
		{
			std::unique_ptr<StreamInput> freeThis(this);

			size_t read;
			char buffer[32768];
			while ((read = smbPipe->Read(buffer, sizeof(buffer))) > 0)
			{
				if (!pipe->Write(buffer, read))
					break;
			}

			pipe->shutdown();
			smbPipe->shutdown();
		}
	};
}

ICustomClient* SambaServiceFactory::createClient()
{
	return new SambaService();
}

void SambaServiceFactory::destroyClient(ICustomClient* pClient)
{
	delete static_cast<SambaService*>(pClient);
}

void SambaService::Init(THREAD_ID pTID, IPipe* pPipe, const std::string& pEndpointName)
{
	pipe = pPipe;
	state = State::Init;
	smbPipe.reset();
	readTicket = ILLEGAL_THREADPOOL_TICKET;
}

bool SambaService::wantReceive()
{
	return state == State::Running;
}

bool SambaService::Run(IRunOtherCallback* run_other)
{
	if (state == State::Shutdown)
	{
		if (!Server->getThreadPool()->isRunning(readTicket))
			return false;
		return true;
	}

	if (state == State::Init)
	{
		smbPipe.reset(ClientConnector::getRemoteConnection(std::string(), 10000, ConnectionTypeSamba));
		if (!smbPipe)
			return false;

		state = State::Running;
		readTicket = Server->getThreadPool()->execute(new StreamInput(pipe, smbPipe.get()), "smb read");
	}

	return true;
}

void SambaService::ReceivePackets(IRunOtherCallback* run_other)
{
	char buffer[32768];
	const auto read = pipe->Read(buffer, sizeof(buffer));
	if (read == 0)
	{
		state = State::Shutdown;
		pipe->shutdown();
		smbPipe->shutdown();
		return;
	}

	if (!smbPipe->Write(buffer, read))
	{
		state = State::Shutdown;
		pipe->shutdown();
		smbPipe->shutdown();
		return;
	}
}