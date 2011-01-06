#include "CustomClient.h"

class IService
{
public:
	virtual ICustomClient* createClient()=0;
	virtual void destroyClient( ICustomClient * pClient)=0;
};
