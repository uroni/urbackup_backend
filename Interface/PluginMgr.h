#include "Types.h"
#include "Plugin.h"
#include "Object.h"

class IPluginMgr : public IObject
{
public:
	virtual IPlugin *createPluginInstance(str_map &params)=0;
	virtual void destroyPluginInstance(IPlugin *plugin)=0;
};
