#include "../Interface/PluginMgr.h"

class CUrlPluginMgr : public IPluginMgr
{
public:
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};