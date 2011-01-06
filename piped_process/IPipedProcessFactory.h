#include <string>
#include "IPipedProcess.h"
#include "../Interface/Plugin.h"

class IPipedProcessFactory: public IPlugin
{
public:
	virtual IPipedProcess* createProcess(const std::string &cmdline)=0;
};
