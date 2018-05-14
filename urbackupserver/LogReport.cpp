#include "LogReport.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/Database.h"
#include "../stringtools.h"
#include "database.h"
#include "../luaplugin/ILuaInterpreter.h"
#include "../urlplugin/IUrlFactory.h"
#include "Mailer.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.h"

extern ILuaInterpreter* lua_interpreter;
extern IUrlFactory *url_fak;

namespace
{
	IMutex* mutex;
	std::string script;

	class MailBridge : public ILuaInterpreter::IEMailFunction
	{
	public:
		// Inherited via IEMailFunction
		virtual bool mail(const std::string & send_to, const std::string & subject, const std::string & message)
		{
			return Mailer::sendMail(send_to, subject, message);
		}
	};

	class UrlBridge : public ILuaInterpreter::IUrlFunction
	{
	public:
		// Inherited via IUrlFunction
		virtual bool requestUrl(const std::string & url, str_map & params, std::string & ret, long & http_code, std::string * errmsg = NULL)
		{
			return  url_fak->requestUrl(url, params, ret, http_code, errmsg);
		}
	};

	ILuaInterpreter::SInterpreterFunctions funcs;

	IMutex* global_mutex;
	std::string global_data_mem;
	bool has_global_data = false;
	bool has_global_data_misc = false;
	std::string global_data;

#include "report_lua.h"

        std::string get_report_lua()
        {
                size_t out_len;
                void* cdata = tinfl_decompress_mem_to_heap(report_lua_z, report_lua_z_len, &out_len, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32);
                if (cdata == NULL)
                {
                        return std::string();
                }

                std::string ret(reinterpret_cast<char*>(cdata), reinterpret_cast<char*>(cdata) + out_len);
                mz_free(cdata);
                return ret;
        }
}

std::string load_report_script()
{
	if (FileExists("urbackupserver/report.lua"))
	{
		std::string ret = getFile("urbackupserver/report.lua");
		if (!ret.empty())
		{
			return ret;
		}
	}

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='report_script'");

	if (!res.empty())
	{
		has_global_data_misc = true;
		return res[0]["tvalue"];
	}

	return get_report_lua();
}

void load_global_data()
{
	if (has_global_data)
		return;

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='report_script_global_state'");

	has_global_data = true;

	if (!res.empty())
	{
		global_data = res[0]["tvalue"];
	}
}

void update_global_data(const std::string& new_global_data)
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if (has_global_data_misc)
	{
		IQuery* q_update = db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='report_script_global_state'", false);
		q_update->Bind(new_global_data.c_str(), new_global_data.size());
		q_update->Write();
		q_update->Reset();
		db->destroyQuery(q_update);
	}
	else
	{
		IQuery* q_insert = db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES ('report_script_global_state', ?)", false);
		q_insert->Bind(new_global_data.c_str(), new_global_data.size());
		q_insert->Write();
		q_insert->Reset();
		db->destroyQuery(q_insert);
	}

	global_data = new_global_data;
}

std::string get_report_script()
{
	IScopedLock lock(mutex);
	if (!script.empty())
	{
		return script;
	}

	script = load_report_script();
	if (script.empty())
		return script;

	std::string compiled_script = lua_interpreter->compileScript(script);

	if (!compiled_script.empty())
		return compiled_script;

	return script;
}

void init_log_report()
{
	mutex = Server->createMutex();
	funcs.mail_func = new MailBridge;
	funcs.url_func = new UrlBridge;
	global_mutex = Server->createMutex();
}

void reload_report_script()
{
	IScopedLock lock(mutex);
	script.clear();
}

bool run_report_script(int incremental, bool resumed, int image,
	int infos, int warnings, int errors, bool success, const std::string& report_mail, 
	const std::string & data, const std::string& clientname)
{
	if (lua_interpreter == NULL)
		return false;

	std::string script;
	{
		IScopedLock lock(mutex);
		script = get_report_script();
	}

	if (script.empty())
		return false;

	ILuaInterpreter::Param param_raw;
	ILuaInterpreter::Param::params_map& param = *param_raw.u.params;

	param["incremental"] = incremental;
	param["resumed"] = resumed;
	param["image"] = image;
	param["infos"] = infos;
	param["warnings"] = warnings;
	param["errors"] = errors;
	param["report_mail"] = report_mail;
	param["clientname"] = clientname;
	param["success"] = success;
	ILuaInterpreter::Param::params_map& pdata = *param["data"].u.params;
	
	std::vector<std::string> msgs;
	Tokenize(data, msgs, "\n");

	for (size_t j = 0; j<msgs.size(); ++j)
	{
		ILuaInterpreter::Param::params_map& obj = *pdata[static_cast<int>(j + 1)].u.params;
		std::string ll;
		if (!msgs[j].empty()) ll = msgs[j][0];
		int li = watoi(ll);
		msgs[j].erase(0, 2);
		std::string tt = getuntil("-", msgs[j]);
		std::string m = getafter("-", msgs[j]);
		obj["time"] = watoi64(tt);
		obj["msg"] = m;
		obj["ll"] = li;
	}

	int64 ret2;
	std::string state;
	std::string state_mem;
	int64 ret;
	{
		IScopedLock lock(global_mutex);
		load_global_data();
		std::string local_global_data = global_data;
		ret = lua_interpreter->runScript(script, param_raw, ret2, state, state_mem, local_global_data, global_data_mem, funcs);
		if (local_global_data != global_data)
		{
			update_global_data(local_global_data);
		}
	}

	if (ret<0)
	{
		return false;
	}

	return true;
}


