#include "LuaInterpreter.h"
#include "src/lua.hpp"
#include "../Interface/Server.h"
#include "../common/data.h"
#include <assert.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.h"
#include "../stringtools.h"

extern "C" {
	LUAMOD_API int luaopen_os_custom(lua_State *L);
}

namespace
{
#include "lua/dkjson_lua.h"

	std::string get_lua(const unsigned char* data, size_t data_len)
	{
		size_t out_len;
		void* cdata = tinfl_decompress_mem_to_heap(data, data_len, &out_len, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32);
		if (cdata == NULL)
		{
			return std::string();
		}

		std::string ret(reinterpret_cast<char*>(cdata), reinterpret_cast<char*>(cdata) + out_len);
		mz_free(cdata);
		return ret;
	}
}

namespace
{
	class ScopedLuaState
	{
		lua_State* state;
	public:
		ScopedLuaState(lua_State* state)
			: state(state)
		{}

		~ScopedLuaState() {
			lua_close(state);
		}
	};

#define LUA_SERIALIZE_STOP 100

	bool unserialize_val(lua_State* state, char t, CRData& data)
	{
		switch (t)
		{
		case LUA_TNIL:
		{
			lua_pushnil(state);
		}	break;
		case LUA_TBOOLEAN:
		{
			int64 b;
			if (!data.getVarInt(&b))
				return false;
			lua_pushboolean(state, static_cast<int>(b));
		}	break;
		case LUA_TNUMBER:
		{
			double num;
			if (!data.getDouble(&num))
				return false;
			lua_pushnumber(state, num);
		}	break;
		case LUA_TSTRING:
		{
			std::string str;
			if (!data.getStr2(&str))
				return false;
			lua_pushstring(state, str.c_str());
		}	break;
		default:
			return false;
		}

		return true;
	}

	bool unserialize_table(lua_State* state, CRData& data)
	{
		lua_newtable(state);

		char t;
		while (data.getChar(&t))
		{
			if (t == LUA_SERIALIZE_STOP)
				return true;

			if (!unserialize_val(state, t, data))
			{
				return false;
			}

			if (t == LUA_TNUMBER || t == LUA_TSTRING)
			{
				char t2;
				if (!data.getChar(&t2))
					return false;

				if (t2 == LUA_TTABLE)
				{
					if (!unserialize_table(state, data))
						return false;
				}
				else if (!unserialize_val(state, t2, data))
				{
					return false;
				}

				lua_rawset(state, -3);
			}
			else
			{
				lua_rawset(state, -2);
			}
		}

		return false;
	}

	bool unserialize_table(lua_State* state, const std::string& data_in)
	{
		CRData data(data_in.data(), data_in.size());
		return unserialize_table(state, data);
	}

	bool serialize_val(lua_State* state, int idx, CWData& data)
	{
		if (lua_isboolean(state, idx))
		{
			data.addChar(LUA_TBOOLEAN);
			data.addVarInt(lua_toboolean(state, idx));
		}
		else if (lua_isnil(state, idx))
		{
			data.addChar(LUA_TNIL);
		}
		else if (lua_isstring(state, idx))
		{
			data.addChar(LUA_TSTRING);
			data.addString2(lua_tostring(state, idx));
		}
		else if (lua_isnumber(state, idx))
		{
			data.addChar(LUA_TNUMBER);
			data.addDouble(lua_tonumber(state, idx));
		}
		else
		{
			return false;
		}

		return true;
	}

	bool serialize_table(lua_State* state, CWData& data)
	{
		for (lua_pushnil(state); lua_next(state, -2) != 0; lua_pop(state, 1))
		{
			if (!serialize_val(state, -2, data))
				return false;

			if (lua_istable(state, -1))
			{
				data.addChar(LUA_TTABLE);
				if (!serialize_table(state, data))
					return false;
			}
			else if (!serialize_val(state, -1, data))
			{
				return false;
			}
		}

		data.addChar(LUA_SERIALIZE_STOP);

		return true;
	}

	std::string serialize_table(lua_State* state)
	{
		CWData data;
		if (!serialize_table(state, data))
			return std::string();

		return std::string(data.getDataPtr(), data.getDataSize());
	}

	int lua_write(lua_State *L, const void *p, size_t sz, void *ud)
	{
		try
		{
			std::string* output = reinterpret_cast<std::string*>(ud);

			output->insert(output->end(), reinterpret_cast<const char*>(p), reinterpret_cast<const char*>(p) + sz);
		}
		catch (...)
		{
			return 1;
		}

		return 0;
	}

	static const luaL_Reg loadedlibs[] = {
		{ "_G", luaopen_base },
		{ LUA_TABLIBNAME, luaopen_table },
		{ LUA_STRLIBNAME, luaopen_string },
		{ LUA_MATHLIBNAME, luaopen_math },
		{ LUA_UTF8LIBNAME, luaopen_utf8 },
		{ LUA_OSLIBNAME, luaopen_os_custom},
		{ NULL, NULL }
	};


	LUALIB_API void luaL_openlibs_custom(lua_State *L) {
		const luaL_Reg *lib;
		/* "require" functions from 'loadedlibs' and set results to global table */
		for (lib = loadedlibs; lib->func; lib++) {
			luaL_requiref(L, lib->name, lib->func, 1);
			lua_pop(L, 1);  /* remove lib */
		}
	}

	ILuaInterpreter::SInterpreterFunctions* get_funcs(lua_State* L)
	{
		lua_getglobal(L, "_g_funcs");
		ILuaInterpreter::SInterpreterFunctions* ret 
			= reinterpret_cast<ILuaInterpreter::SInterpreterFunctions*>(lua_touserdata(L, -1));
		lua_pop(L, 1);

		if (ret == NULL)
		{
			luaL_error(L, "Internal functions not present");
		}

		return ret;
	}

	int l_mail(lua_State *L) {
		std::string to = luaL_checkstring(L, 1);
		std::string subj = luaL_checkstring(L, 2);
		std::string msg = luaL_checkstring(L, 3);

		ILuaInterpreter::SInterpreterFunctions* funcs = get_funcs(L);

		bool b = funcs->mail_func->mail(to, subj, msg);

		lua_pushboolean(L, b ? 1 : 0);
		return 1;
	}

	int l_download(lua_State *L)
	{
		std::string url = luaL_checkstring(L, 1);
		std::string http_proxy;
		if (lua_gettop(L) > 1)
		{
			http_proxy = luaL_checkstring(L, 2);
		}
		ILuaInterpreter::SInterpreterFunctions* funcs = get_funcs(L);

		std::string errmsg;
		std::string ret = funcs->url_func->downloadString(url, http_proxy, &errmsg);

		if (ret.empty())
		{
			lua_pushnil(L);
			lua_pushlstring(L, errmsg.c_str(), errmsg.size());
		}
		else
		{
			lua_pushlstring(L, ret.c_str(), ret.size());
			lua_pushnil(L);
		}
		return 2;
	}

	void set_param(lua_State* state, const ILuaInterpreter::Param& param)
	{
		if (param.tag == ILuaInterpreter::Param::PARAM_VEC)
		{
			lua_newtable(state);
			for (ILuaInterpreter::Param::params_map::const_iterator it = param.u.params->begin();
				it!=param.u.params->end();++it)
			{
				set_param(state, it->first);
				set_param(state, it->second);
				lua_rawset(state, -3);
			}
		}
		else if (param.tag == ILuaInterpreter::Param::PARAM_STR)
		{
			lua_pushstring(state, param.u.str->c_str());
		}
		else if (param.tag == ILuaInterpreter::Param::PARAM_NUM)
		{
			lua_pushnumber(state, param.u.num);
		}
		else if (param.tag == ILuaInterpreter::Param::PARAM_INT)
		{
			lua_pushinteger(state, param.u.i);
		}
		else if (param.tag == ILuaInterpreter::Param::PARAM_BOOL)
		{
			lua_pushboolean(state, param.u.b);
		}
		else
		{
			assert(false);
		}
	}

	int l_require(lua_State* state)
	{
		std::string name = luaL_checkstring(state, 1);
		std::string code;
		if (name == "dkjson")
		{
			code = get_lua(dkjson_lua_z, dkjson_lua_z_len);
			//code = getFile("luaplugin/lua/dkjson.lua");
		}
		if (code.empty())
		{
			return 0;
		}
		else
		{
			luaL_loadbuffer(state, code.data(), code.size(), name.c_str());
		}
		lua_call(state, 0, LUA_MULTRET);
		return 1;
	}
}

std::string LuaInterpreter::compileScript(const std::string & script)
{
	lua_State* state = luaL_newstate();
	if (state == NULL)
	{
		return std::string();
	}

	ScopedLuaState scoped_state(state);

	int rc = luaL_loadbuffer(state, script.c_str(), script.size(), "script");
	if (rc) {
		Server->Log(std::string("Error loading lua script: ") + lua_tostring(state, -1), LL_ERROR);
		return std::string();
	}

	std::string ret;
	rc = lua_dump(state, lua_write, &ret, 1);
	if (rc)
	{
		Server->Log(std::string("Error dumping lua state"), LL_ERROR);
		return std::string();
	}

	return ret;
}

int64 LuaInterpreter::runScript(const std::string& script, const ILuaInterpreter::Param& params, int64& ret2,
	std::string& state_data, std::string& global_data, const ILuaInterpreter::SInterpreterFunctions& funcs)
{
	ret2 = -1;

	lua_State* state = luaL_newstate();
	if (state == NULL)
	{
		return -1;
	}

	ScopedLuaState scoped_state(state);

	luaL_openlibs_custom(state);

	int rc = luaL_loadbuffer(state, script.c_str(), script.size(), "script");
	if (rc) {
		Server->Log(std::string("Error loading lua script: ") + lua_tostring(state, -1), LL_ERROR);
		return -1;
	}

	set_param(state, params);
	lua_setglobal(state, "params");

	lua_pushlightuserdata(state, const_cast<ILuaInterpreter::SInterpreterFunctions*>(&funcs));
	lua_setglobal(state, "_g_funcs");

	lua_pushcfunction(state, l_mail);
	lua_setglobal(state, "mail");
	lua_pushcfunction(state, l_require);
	lua_setglobal(state, "require");
	lua_pushcfunction(state, l_download);
	lua_setglobal(state, "download");

	if (state_data.empty())
	{
		lua_newtable(state);
	}
	else if (!unserialize_table(state, state_data))
	{
		Server->Log("Error unserializing state data", LL_ERROR);
		return -1;
	}

	lua_setglobal(state, "state");

	if (global_data.empty())
	{
		lua_newtable(state);
	}
	else if (!unserialize_table(state, global_data))
	{
		Server->Log("Error unserializing global data", LL_ERROR);
		return -1;
	}

	lua_setglobal(state, "global");

	rc = lua_pcall(state, 0, LUA_MULTRET, 0);
	if (rc) {
		Server->Log(std::string("Error running lua script: ") + lua_tostring(state, -1), LL_ERROR);
		return -1;
	}

	if (lua_gettop(state) > 1)
	{
		ret2 = lua_tointeger(state, -1);
		lua_pop(state, 1);
	}
	int64 ret = lua_tointeger(state, -1);
	lua_pop(state, 1);

	lua_getglobal(state, "state");

	state_data = serialize_table(state);
	if (state_data.empty())
	{
		Server->Log("Error serializing state data", LL_WARNING);
	}

	lua_getglobal(state, "global");

	global_data = serialize_table(state);
	if (global_data.empty())
	{
		Server->Log("Error serializing global data", LL_WARNING);
	}
	
	return ret;
}
