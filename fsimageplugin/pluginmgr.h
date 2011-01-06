#include "../Interface/PluginMgr.h"

class CImagePluginMgr : public IPluginMgr
{
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};
