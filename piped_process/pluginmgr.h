#include "../Interface/PluginMgr.h"

class CPipedProcessPluginMgr : public IPluginMgr
{
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};
