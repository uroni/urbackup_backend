#include "pluginmgr.h"
#include "LuaInterpreter.h"

IPlugin * LuaPluginMgr::createPluginInstance(str_map & params)
{
	return new LuaInterpreter;
}

void LuaPluginMgr::destroyPluginInstance(IPlugin * plugin)
{
	delete reinterpret_cast<LuaInterpreter*>(plugin);
}
