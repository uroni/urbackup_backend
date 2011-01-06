#include "IPipedProcessFactory.h"

class PipedProcessFactory : public IPipedProcessFactory
{
public:
	virtual IPipedProcess* createProcess(const std::string &cmdline);
};