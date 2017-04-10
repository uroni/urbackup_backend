#pragma once
#include "../Interface/Plugin.h"
#include <string>

class ILuaInterpreter : public IPlugin
{
public:
	class IEMailFunction
	{
	public:
		virtual bool mail(const std::string& send_to, const std::string& subject, const std::string& message) = 0;
	};

	struct SInterpreterFunctions
	{
		SInterpreterFunctions()
			: mail_func(NULL) {}

		IEMailFunction* mail_func;
	};

	virtual std::string compileScript(const std::string& script) = 0;
	virtual int64 runScript(const std::string& script, const str_map& params, int64& ret2, std::string& state, const SInterpreterFunctions& funcs) = 0;
};