#include "../Interface/PluginMgr.h"

class CFileServPluginMgr : public IPluginMgr
{
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};