#include "../Interface/Thread.h"

class IFileDownload;

class ServerUpdate : public IThread
{
public:
	ServerUpdate(void);

	void operator()(void);

private:

	bool waitForDownload(IFileDownload *dl);
};