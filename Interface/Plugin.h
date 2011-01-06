#ifndef INTERFACE_PLUGIN_H
#define INTERFACE_PLUGIN_H

#include "Object.h"
#include "Types.h"

class IPlugin : public IObject
{
public:
	/**
	* Gets called if Server->ReloadPlugin() is executed
	**/
	virtual bool Reload(void){ return true; }

	/**
	* Only for PerThread Plugins. Gets called before the Plugin Instance is returned.
	**/
	virtual void Reset(void){}
};


#endif //INTERFACE_PLUGIN_H
