#include "../Interface/Thread.h"

class ServerUpdate : public IThread
{
public:
	ServerUpdate(void);

	void operator()(void);
};