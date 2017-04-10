#pragma once

#include "../Interface/PluginMgr.h"

class LuaPluginMgr : public IPluginMgr
{
public:
	IPlugin *createPluginInstance(str_map &params);
	void destroyPluginInstance(IPlugin *plugin);
};
