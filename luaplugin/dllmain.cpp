/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#include <unistd.h>
#endif


#ifndef STATIC_PLUGIN
#define DEF_SERVER
#endif
#include "../Interface/Server.h"

#ifndef STATIC_PLUGIN
IServer *Server;
#else
#include "../StaticPluginRegistration.h"

extern IServer* Server;

#define LoadActions LoadActions_luaplugin
#define UnloadActions UnloadActions_luaplugin
#endif

#include "pluginmgr.h"

#include "LuaInterpreter.h"
#include "../stringtools.h"

LuaPluginMgr* luapluginmgr;

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server = pServer;

	luapluginmgr = new LuaPluginMgr;

	Server->RegisterPluginThreadsafeModel(luapluginmgr, "lua");

#ifndef STATIC_PLUGIN
	Server->Log("Loaded -luaplugin- plugin", LL_INFO);
#endif

	/*std::string test_script = getFile("test.lua");

	LuaInterpreter lua_interpreter;
	std::string script = lua_interpreter.compileScript(test_script);

	str_map params;
	params["foo"] = "bar";
	int64 ret2;
	std::string state;
	lua_interpreter.runScript(script, params, ret2, state);
	lua_interpreter.runScript(script, params, ret2, state);*/
}

DLLEXPORT void UnloadActions(void)
{
}

#ifdef STATIC_PLUGIN
namespace
{
	static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 0);
}
#endif
