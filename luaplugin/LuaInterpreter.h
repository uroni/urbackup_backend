#pragma once
#include <string>
#include "../Interface/Types.h"
#include "ILuaInterpreter.h"

class LuaInterpreter : public ILuaInterpreter
{
public:
	virtual std::string compileScript(const std::string& script);

	virtual int64 runScript(const std::string& script, const Param& params, int64& ret2,
		std::string& state, std::string& state_mem,
		std::string& global_data,
		std::string& global_data_mem, SInterpreterFunctions& funcs);

};