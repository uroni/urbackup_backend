#include <string>
#include "Interface/Thread.h"

class CLoadbalancerClient : public IThread
{
public:
	CLoadbalancerClient(std::string pLB, unsigned short pLBPort, int pWeight, unsigned short pServerport);
	void operator()(void);
	
private:
	std::string lb;
	unsigned short lbport;
	int weight;
	unsigned short serverport;
};
