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

	class IUrlFunction
	{
	public:
		virtual std::string downloadString(const std::string& url, const std::string& http_proxy = "",
			std::string *errmsg = NULL) = 0;
	};

	struct SInterpreterFunctions
	{
		SInterpreterFunctions()
			: mail_func(NULL), url_func(NULL) {}

		IEMailFunction* mail_func;
		IUrlFunction* url_func;
	};

	struct Param
	{
		typedef std::map<Param, Param> params_map;

		Param()
			: tag(PARAM_VEC)
		{
			u.params = new params_map();
		}

		Param(const std::string& pstr)
			: tag(PARAM_STR)
		{
			u.str = new std::string(pstr);
		}

		Param(const char* pstr)
			: tag(PARAM_STR)
		{
			u.str = new std::string(pstr);
		}

		Param(double num)
		:tag(PARAM_NUM) {
			u.num = num;
		}

		Param(int i)
			: tag(PARAM_INT) {
			u.i = i;
		}

		Param(int64 i)
			: tag(PARAM_NUM) {
			u.num = static_cast<double>(i);
		}

		Param(bool b) 
		: tag(PARAM_BOOL) {
			u.b = b;
		}

		~Param() {
			if (tag == PARAM_VEC) {
				delete u.params;
			}
			else if (tag == PARAM_STR) {
				delete u.str;
			}
		}

		Param(const Param& other) 
			: tag(other.tag)	{
			if (tag == PARAM_VEC) {
				u.params = new params_map(*other.u.params);
			}
			else if (tag == PARAM_STR) {
				u.str = new std::string(*other.u.str);
			}
			else if (tag == PARAM_NUM) {
				u.num = other.u.num;
			}
			else if (tag == PARAM_BOOL) {
				u.b = other.u.b;
			}
			else if (tag == PARAM_INT) {
				u.i = other.u.i;
			}
		}

		void swap(Param& first, Param& second) {
			std::swap(first.tag, second.tag);
			std::swap(first.u, second.u);
		}

		Param& operator=(Param other)
		{
			swap(*this, other);
			return *this;
		}

		bool operator<(const Param& other) const {
			if (tag != other.tag) {
				return false;
			}
			if (tag == PARAM_VEC) {
				return *u.params < *other.u.params;
			}
			else if (tag == PARAM_STR) {
				return *u.str < *other.u.str;
			}
			else if (tag == PARAM_NUM) {
				return u.num < other.u.num;
			}
			else if (tag == PARAM_BOOL) {
				return u.b < other.u.b;
			}
			else if (tag == PARAM_INT) {
				return u.i < other.u.i;
			}
			return false;
		}

		union
		{
			params_map* params;
			std::string* str;
			double num;
			bool b;
			int i;
		} u;
		enum 
		{
			PARAM_VEC = 1, 
			PARAM_STR,
			PARAM_NUM,
			PARAM_BOOL,
			PARAM_INT
		} tag;
	};


	virtual std::string compileScript(const std::string& script) = 0;
	virtual int64 runScript(const std::string& script, const Param& params, int64& ret2, std::string& state, std::string& global_data, const SInterpreterFunctions& funcs) = 0;
};