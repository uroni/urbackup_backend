/*
* all.c -- Lua core, libraries and interpreter in a single file
*/

#define luaall_c

#include "lapi.c"
#include "lcode.c"
#include "ldebug.c"
#include "ldo.c"
#include "ldump.c"
#include "lfunc.c"
#include "lgc.c"
#include "llex.c"
#include "lmem.c"
#include "lobject.c"
#include "lopcodes.c"
#include "lparser.c"
#include "lstate.c"
#include "lstring.c"
#include "ltable.c"
#include "ltm.c"
#include "lundump.c"
#include "lvm.c"
#include "lzio.c"

#include "lauxlib.c"
#include "lbaselib.c"
#include "ldblib.c"
#include "liolib.c"
#include "linit.c"
#include "lmathlib.c"
#include "loadlib.c"
#include "loslib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lcorolib.c"
#include "lutf8lib.c"
#include "lctype.c"

static const luaL_Reg syslib_custom[] = {
	{ "clock",     os_clock },
	{ "date",      os_date },
	{ "difftime",  os_difftime },
	{ "time",      os_time },
	{ NULL, NULL }
};


LUAMOD_API int luaopen_os_custom(lua_State *L) {
	luaL_newlib(L, syslib_custom);
	return 1;
}