#pragma once
#include "../Interface/PluginMgr.h"

class ImagePluginMgr : public IPluginMgr
{
public:
	virtual IPlugin* createPluginInstance(str_map& params) override;

	virtual void destroyPluginInstance(IPlugin* plugin) override;

};
