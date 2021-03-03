#include "pluginmgr.h"
#include "BtrfsFactory.h"

IPlugin* BtrfsPluginMgr::createPluginInstance(str_map& params)
{
	return new BtrfsFactory;
}

void BtrfsPluginMgr::destroyPluginInstance(IPlugin* plugin)
{
	delete static_cast<BtrfsFactory*>(plugin);
}
