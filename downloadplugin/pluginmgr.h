#include "../Interface/PluginMgr.h"

class CDownloadPluginMgr : public IPluginMgr
{
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};
