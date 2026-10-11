#pragma once
#include "../Interface/Service.h"
#include "../Interface/File.h"
#include "../Interface/Mutex.h"

class NbdService : public IService
{
public:
	NbdService(IFile* backing_file, bool read_only=false);
	~NbdService();

	virtual ICustomClient* createClient();

	virtual void destroyClient( ICustomClient * pClient );

	bool acquireClient();
	void releaseClient();

private:
	IFile* backing_file;
	bool read_only;

	IMutex* client_mutex;
	bool has_client;
};
