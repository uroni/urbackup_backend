#pragma once

#include "../../Interface/PluginMgr.h"

class BtrfsPluginMgr : public IPluginMgr
{
public:
	IPlugin* createPluginInstance(str_map& params);
	void destroyPluginInstance(IPlugin* plugin);
};
