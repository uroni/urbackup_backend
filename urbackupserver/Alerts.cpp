#include "Alerts.h"
#include "../Interface/Database.h"
#include "database.h"
#include "../Interface/Server.h"
#include "server_settings.h"
#include "../stringtools.h"
#include "../luaplugin/ILuaInterpreter.h"
#include "../urlplugin/IUrlFactory.h"
#include "Mailer.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.h"

extern ILuaInterpreter* lua_interpreter;
extern IUrlFactory *url_fak;

namespace
{
#include "alert_lua.h"
#include "alert_pulseway_lua.h"

        std::string get_alert_lua(unsigned char* data, unsigned int data_len)
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

std::string get_alert_script(IDatabase* db, int script_id)
{
	db_results res_script = db->Read("SELECT script FROM alert_scripts WHERE id="+convert(script_id));

        if (res_script.empty())
        {
        	Server->Log("Cannot find alert script with id " + convert(script_id), LL_ERROR);
                return std::string();
        }

        std::string ret = res_script[0]["script"];

        if (script_id == 1
              && FileExists("urbackupserver/alert.lua"))
        {
        	ret = getFile("urbackupserver/alert.lua");
        }

		if (script_id == 100000
			&& FileExists("urbackupserver/alert_pulseway.lua"))
		{
			ret = getFile("urbackupserver/alert_pulseway.lua");
		}

		if (ret.empty())
		{
			switch (script_id)
			{
			case 1: ret = get_alert_lua(alert_lua_z, alert_lua_z_len); break;
			case 100000: ret = get_alert_lua(alert_pulseway_lua_z, alert_pulseway_lua_z_len); break;
			}
		}

	return ret;
}


namespace
{
	struct SScriptParam
	{
		std::string name;
		std::string default_value;
		std::string type;
	};

	struct SScript
	{
		std::string code;
		std::vector<SScriptParam> params;
		std::string global;
		std::string global_mem;
		std::map<int, std::string> state_mem;
	};

	SScript prepareScript(IDatabase* db, ILuaInterpreter* lua_interpreter, int script_id)
	{
		SScript ret;
		ret.code = get_alert_script(db, script_id);

		if (!ret.code.empty())
		{
			std::string compiled_code = lua_interpreter->compileScript(ret.code);
			if (!compiled_code.empty())
			{
				ret.code = compiled_code;
			}
		}

		if (ret.code.empty())
		{
			return SScript();
		}

		db_results res_params = db->Read("SELECT name, default_value, type FROM alert_script_params WHERE script_id="+convert(script_id));

		for (size_t i = 0; i < res_params.size(); ++i)
		{
			SScriptParam param = { res_params[i]["name"], res_params[i]["default_value"], res_params[i]["type"] };
			ret.params.push_back(param);
		}

		return ret;
	}

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
			return url_fak->requestUrl(url, params, ret, http_code, errmsg);
		}
	};
}

void Alerts::operator()()
{
	if (lua_interpreter == NULL)
	{
		return;
	}

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	db->Write("UPDATE clients SET alerts_next_check=NULL");

	IQuery* q_get_alert_clients = db->Prepare("SELECT id, name, file_ok, image_ok, alerts_state, strftime('%s', lastbackup) AS lastbackup, "
		"strftime('%s', lastseen) AS lastseen, strftime('%s', lastbackup_image) AS lastbackup_image, created, os_simple "
		"FROM clients WHERE alerts_next_check IS NULL OR alerts_next_check<=?");
	IQuery* q_update_client = db->Prepare("UPDATE clients SET  file_ok=?, image_ok=?, alerts_next_check=?, alerts_state=? WHERE id=?");

	std::map<int, SScript> alert_scripts;

	ILuaInterpreter::SInterpreterFunctions funcs;
	funcs.mail_func = new MailBridge;
	funcs.url_func = new UrlBridge;


	bool first_run = true;
	while (true)
	{
#ifdef _DEBUG
		if(!first_run)
#endif
		Server->wait(60*1000);

#ifdef _DEBUG
		first_run = false;
#endif

		q_get_alert_clients->Bind(Server->getTimeMS());
		db_results res = q_get_alert_clients->Read();
		q_get_alert_clients->Reset();

		for (size_t i = 0; i < res.size(); ++i)
		{
			int clientid = watoi(res[i]["id"]);
			ServerSettings server_settings(db, clientid);
			int script_id = server_settings.getSettings()->alert_script;
			std::map<int, SScript>::iterator it = alert_scripts.find(script_id);
			if (it == alert_scripts.end())
			{
				alert_scripts[script_id] = prepareScript(db, lua_interpreter, script_id);
				it = alert_scripts.find(script_id);
			}			

			if (!it->second.code.empty())
			{
				ILuaInterpreter::Param params_raw;
				ILuaInterpreter::Param::params_map& params = *params_raw.u.params;
				params["clientid"] = clientid;
				params["clientname"] = res[i]["name"];
				int update_freq_file_incr = server_settings.getUpdateFreqFileIncr();
				int update_freq_file_full = server_settings.getUpdateFreqFileFull();
				params["incr_file_interval"] = update_freq_file_incr;
				params["full_file_interval"] = update_freq_file_full;
				int update_freq_image_incr = server_settings.getUpdateFreqImageIncr();
				int update_freq_image_full = server_settings.getUpdateFreqImageFull();
				params["incr_image_interval"] = update_freq_image_incr;
				params["full_image_interval"] = update_freq_image_full;
				params["no_images"] = server_settings.getSettings()->no_images;
				params["no_file_backups"] = server_settings.getSettings()->no_file_backups;
				params["os_simple"] = res[i]["os_simple"];

				int64 times = Server->getTimeSeconds();
				int64 created = watoi64(res[i]["created"]);
				int64 lastbackup_file = watoi64(res[i]["lastbackup"]);
				int64 lastbackup_image = watoi64(res[i]["lastbackup_image"]);

				params["passed_time_lastseen"] = times - watoi64(res[i]["lastseen"]);
				params["passed_time_lastbackup_file"] = (std::min)(times - lastbackup_file, times - created);
				params["passed_time_lastbackup_image"] = (std::min)(times - lastbackup_image, times - created);
				params["lastbackup_file"] = lastbackup_file;
				params["lastbackup_image"] = lastbackup_image;

				SSettings* settings = server_settings.getSettings();

				bool complex_file_interval = settings->update_freq_full.find(";") != std::string::npos
					|| settings->update_freq_incr.find(";") != std::string::npos;

				bool complex_image_interval = settings->update_freq_image_full.find(";") != std::string::npos
					|| settings->update_freq_image_incr.find(";") != std::string::npos;

				if ( complex_file_interval
					|| complex_image_interval )
				{
					params["complex_interval"] = true;
				}
				else
				{
					params["complex_interval"] = false;
				}

				bool file_ok = res[i]["file_ok"] == "1";
				params["file_ok"] = file_ok;
				bool image_ok = res[i]["image_ok"] == "1";
				params["image_ok"] = image_ok;

				str_map nondefault_params;
				ParseParamStrHttp(server_settings.getSettings()->alert_params, &nondefault_params);

				for (size_t j = 0; j < it->second.params.size(); ++j)
				{
					SScriptParam& param = it->second.params[j];
					str_map::iterator it_param = nondefault_params.find(param.name);
					std::string val;
					if (it_param != nondefault_params.end())
					{
						val = it_param->second;
					}
					else
					{
						val = param.default_value;
					}

					if (param.type == "int")
					{
						params[param.name] = watoi(val);
					}
					else if (param.type == "num")
					{
						params[param.name] = atof(val.c_str());
					}
					else if (param.type == "bool")
					{
						params[param.name] = val != "0";
					}
					else
					{
						params[param.name] = val;
					}
				}

				std::string state = res[i]["alerts_state"];
				int64 ret2;
				int64 ret = lua_interpreter->runScript(it->second.code, params_raw, ret2, state, 
					it->second.state_mem[clientid], it->second.global, it->second.global_mem, funcs);
				bool needs_update = false;
				
				if (ret>=0)
				{
					file_ok = !(ret & 1);
					image_ok = !(ret & 2);

					if (file_ok != params["file_ok"].u.b
						|| image_ok != params["image_ok"].u.b)
					{
						needs_update = true;
					}
				}
				else
				{
					Server->Log("Error executing alert script id " + convert(script_id) + ". Return value " + convert(ret) + ".", LL_ERROR);
				}

				int64 next_check;
				if (ret2 >= 0)
				{
					next_check = Server->getTimeMS() + ret2;
					needs_update = true;
				}
				else
				{
					if (!file_ok
						&& !image_ok)
					{
						next_check = Server->getTimeMS() + 1*60*60*1000;
						needs_update = true;
					}
					else
					{
						next_check = Server->getTimeMS();
					}
				}

				if (state != res[i]["alerts_state"])
				{
					needs_update = true;
				}

				int i_file_ok = file_ok ? 1 : 0;
				int i_image_ok = image_ok ? 1 : 0;

				if (settings->no_file_backups)
				{
					i_file_ok = -1;
					if (res[i]["file_ok"] != "-1")
					{
						needs_update = true;
					}
				}
				else if (!complex_file_interval
					&& update_freq_file_incr < 0
					&& update_freq_file_full < 0)
				{
					i_file_ok = -1;
					if (res[i]["file_ok"] != "-1")
					{
						needs_update = true;
					}
				}

				if (settings->no_images)
				{
					i_image_ok = -1;
					if (res[i]["image_ok"] != "-1")
					{
						needs_update = true;
					}
				}
				else if (!complex_image_interval
					&& update_freq_image_full < 0
					&& update_freq_image_incr < 0)
				{
					i_image_ok = -1;
					if (res[i]["image_ok"] != "-1")
					{
						needs_update = true;
					}
				}

				if (needs_update)
				{
					q_update_client->Bind(i_file_ok);
					q_update_client->Bind(i_image_ok);
					q_update_client->Bind(next_check);
					q_update_client->Bind(state);
					q_update_client->Bind(clientid);
					q_update_client->Write();
					q_update_client->Reset();
				}
			}
		}
	}
}
