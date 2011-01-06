#include "../Interface/PluginMgr.h"

class CCryptoPluginMgr : public IPluginMgr
{
public:
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};
