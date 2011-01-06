#include "../Interface/PluginMgr.h"

class CPychartPluginMgr : public IPluginMgr
{
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};