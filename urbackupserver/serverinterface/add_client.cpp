#include "action_header.h"
#include "../ClientMain.h"

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

		bool new_client = false;
		std::string new_authkey;
		int id = ClientMain::getClientID(helper.getDatabase(), POST["clientname"], NULL, &new_client, &new_authkey);
		if (new_client)
		{
			ServerSettings settings(helper.getDatabase());

			SSettings* s = settings.getSettings();

			ret.set("new_clientid", id);
			ret.set("new_clientname", POST["clientname"]);
			ret.set("new_authkey", new_authkey);
			ret.set("internet_server", s->internet_server);
			ret.set("internet_sever_port", s->internet_server_port);
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