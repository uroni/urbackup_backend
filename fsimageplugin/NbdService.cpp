#include "NbdService.h"
#include "NbdClient.h"
#include "../Interface/Server.h"


NbdService::NbdService( IFile* backing_file, bool read_only )
	: backing_file(backing_file), read_only(read_only), client_mutex(Server->createMutex()), has_client(false)
{

}

NbdService::~NbdService()
{
	Server->destroy(client_mutex);
}


ICustomClient* NbdService::createClient()
{
	return new NbdClient(this, backing_file, read_only);
}

void NbdService::destroyClient( ICustomClient * pClient )
{
	delete pClient;
}

bool NbdService::acquireClient()
{
	IScopedLock lock(client_mutex);

	if(has_client)
	{
		return false;
	}

	has_client=true;
	return true;
}

void NbdService::releaseClient()
{
	IScopedLock lock(client_mutex);
	has_client=false;
}
