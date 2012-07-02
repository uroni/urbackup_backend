#include "../Interface/PluginMgr.h"

class CFileServPluginMgr : public IPluginMgr
{
public:
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};