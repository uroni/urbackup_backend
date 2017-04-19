/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) Martin Raiber
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
#include "action_header.h"
#include "../LogReport.h"
#include "../Alerts.h"

ACTION_IMPL(scripts)
{
	Helper helper(tid, &POST, &PARAMS);

	SUser *session = helper.getSession();
	if (session != NULL && session->id == SESSION_ID_INVALID) return;
	if (session == NULL)
	{
		JSON::Object ret;
		ret.set("error", 1);
		helper.Write(ret.stringify(false));
		return;
	}

	std::string sa = POST["sa"];
	IDatabase* db = helper.getDatabase();

	if (sa == "get_alert"
		|| sa=="set_alert"
		|| sa=="rm_alert")
	{
		if (helper.getRights(RIGHT_ALERT_SCRIPTS) != RIGHT_ALL)
		{
			return;
		}

		JSON::Object ret;

		int id = watoi(POST["id"]);
		if (id == 0) id = 1;

		if (sa == "set_alert"
			&& id!=1)
		{
			IQuery* q = id > 1 ? db->Prepare("INSERT OR REPLACE INTO alert_scripts (id, script, name) VALUES (?,?,?)")
				: db->Prepare("INSERT INTO alert_scripts (script, name) VALUES (?, ?)");
			if (id > 1)
			{
				q->Bind(id);
			}
			q->Bind(POST["script"]);
			q->Bind(POST["name"]);
			q->Write();
			q->Reset();

			if (id < 1)
			{
				id = static_cast<int>(db->getLastInsertID());
			}

			db->Write("DELETE FROM alert_script_params WHERE script_id=" + convert(id));
			q = db->Prepare("INSERT INTO alert_script_params (script_id, idx, name, label, default_value) VALUES (?,?,?,?,?)");
			for (size_t idx=0;POST.find(convert(idx) + "_name") != POST.end();++idx)
			{
				q->Bind(id);
				q->Bind(idx);
				q->Bind(POST[convert(idx) + "_name"]);
				q->Bind(POST[convert(idx) + "_label"]);
				q->Bind(POST[convert(idx) + "_default"]);
				q->Write();
				q->Reset();
			}

			ret.set("saved_ok", true);
		}
		else if (sa == "rm_alert"
			&& id != 1)
		{
			db->Write("DELETE FROM alert_scripts WHERE id=" + convert(id));
			id = 1;
		}

		JSON::Array scripts;
		db_results res_scripts = db->Read("SELECT id, name FROM alert_scripts");
		for (size_t i = 0; i < res_scripts.size(); ++i)
		{
			JSON::Object s;
			s.set("id", watoi(res_scripts[i]["id"]));
			s.set("name", res_scripts[i]["name"]);
			scripts.add(s);
		}

		ret.set("scripts", scripts);
		ret.set("script", get_alert_script(db, id));
		ret.set("id", id);

		JSON::Array params;
		db_results res_params = db->Read("SELECT name, label, default_value, has_translation FROM alert_script_params WHERE script_id=" + convert(id) + " ORDER BY idx ASC");
		for (size_t i = 0; i < res_params.size(); ++i)
		{
			JSON::Object p;
			p.set("name", res_params[i]["name"]);
			p.set("label", res_params[i]["label"]);
			p.set("default_value", res_params[i]["default_value"]);
			p.set("has_translation", watoi(res_params[i]["has_translation"]));
			params.add(p);
		}

		ret.set("params", params);
		helper.Write(ret.stringify(false));
	}	
	else if (sa == "get_report"
		|| sa == "set_report")
	{
		if (helper.getRights(RIGHT_REPORT_SCRIPT) != RIGHT_ALL)
		{
			return;
		}

		JSON::Object ret;
		if (sa == "set_report")
		{
			db->Write("DELETE FROM misc WHERE tkey='report_script'");
			IQuery* q = db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES ('report_script', ?)");
			q->Bind(POST["script"]);
			q->Write();
			q->Reset();
			ret.set("saved_ok", true);
		}

		ret.set("script", load_report_script());
		helper.Write(ret.stringify(false));
	}
}
