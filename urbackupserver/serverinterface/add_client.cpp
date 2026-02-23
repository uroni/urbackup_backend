#include "action_header.h"
#include "../ClientMain.h"
#include "rights.h"
#include "settings.h"

ACTION_IMPL(add_client)
{
	Helper helper(tid, &POST, &PARAMS);
	JSON::Object ret;

	SUser *session = helper.getSession();
	if (session != NULL && session->id == SESSION_ID_INVALID) return;
	if (session != NULL && helper.getRights("add_client")=="all")
	{
		if (POST["clientname"].empty())
		{
			return;
		}

		bool empty_internet_server_url = false;
		str_map::iterator it_internet_server = POST.find("internet_server");
		str_map::iterator it_internet_server_port = POST.find("internet_server_port");
		if (it_internet_server != POST.end() &&
			it_internet_server_port != POST.end())
		{
			if (it_internet_server->second.empty())
			{
				empty_internet_server_url = true;
			}
			else if(helper.getRights("general_settings") == RIGHT_ALL)
			{
				str_map change_settings;
				change_settings["internet_server"] = it_internet_server->second;
				change_settings["internet_server_port"] = it_internet_server_port->second;
				bool changed_backupfolder;
				saveGeneralSettingsExternal(change_settings, helper.getDatabase(), changed_backupfolder);
				ServerSettings::updateAll();
			}
		}

		int p_group_id = -1;
		str_map::iterator group_id = POST.find("group_id");
		if (group_id != POST.end())
		{
			p_group_id = watoi(group_id->second);
		}
		else
		{
			str_map::iterator group_name = POST.find("group_name");
			if (group_name != POST.end())
			{
				IQuery* q = helper.getDatabase()->Prepare("SELECT id FROM settings_db.si_client_groups WHERE name=?");
				q->Bind(group_name->second);
				db_results res = q->Read();
				q->Reset();

				if (!res.empty())
				{
					p_group_id = watoi(res[0]["id"]);
				}
			}
		}

		bool new_client = false;
		std::string new_authkey;
		int id = ClientMain::getClientID(helper.getDatabase(), POST["clientname"], NULL,
			&new_client, &new_authkey, p_group_id>0 ? &p_group_id : NULL);
		if (new_client)
		{
			ServerSettings settings(helper.getDatabase());

			SSettings* s = settings.getSettings();

			std::vector<std::string> internet_servers;
			Tokenize(s->internet_server, internet_servers, ";");

			std::vector<std::string> internet_server_ports;
			Tokenize(s->internet_server_port, internet_server_ports, ";");

			std::string server_url;

			for (size_t i = 0; i < internet_servers.size(); ++i)
			{
				std::string& internet_server = internet_servers[i];

				std::string port = "55415";
				if (i < internet_server_ports.size())
					port = internet_server_ports[i];
				else if (!internet_server_ports.empty())
					port = internet_server_ports[internet_server_ports.size()-1];

				if (i > 0)
					server_url += ";";

				if (internet_server.find("urbackup://") != 0 &&
					internet_server.find("ws://") != 0 &&
					internet_server.find("wss://") != 0)
				{
					if(port!="55415")
						server_url += "urbackup://" + internet_server + ":" + port;
					else
						server_url += "urbackup://" + internet_server;
				}
				else
				{
					server_url += internet_server;
				}
			}

			ret.set("new_clientid", id);
			ret.set("new_clientname", POST["clientname"]);
			ret.set("new_authkey", new_authkey);
			ret.set("server_url", server_url);
			ret.set("internet_server", s->internet_server);
			ret.set("internet_server_port", s->internet_server_port);
			ret.set("empty_internet_server_url", empty_internet_server_url);
			if (!s->internet_server_proxy.empty())
			{
				ret.set("internet_server_proxy", s->internet_server_proxy);
			}
			ret.set("added_new_client", true);
		}
		else
		{
			ret.set("already_exists", true);
		}
		ret.set("ok", true);
	}
	else
	{
		ret.set("error", 1);
	}
	helper.Write(ret.stringify(false));
}