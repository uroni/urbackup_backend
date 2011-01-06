#include "../Interface/Plugin.h"

#include "IPychart.h"

class IPychartFactory : public IPlugin
{
public:
	virtual IPychart * getPychart(void)=0;
};