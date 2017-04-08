/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#ifndef CLIENT_ONLY

#include "action_header.h"
#include "../server_status.h"
#include "../database.h"

const size_t max_display=20;

void cleanupLastActs()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	db_results res=db->Read("SELECT id FROM clients");

	for(size_t i=0;i<res.size();++i)
	{
	  int64 clientid = watoi(res[i]["id"]);
	  db->Write("DELETE FROM del_stats WHERE clientid="+convert(clientid)+" AND backupid NOT IN (SELECT backupid FROM del_stats WHERE clientid="+convert(clientid)+" ORDER BY created DESC, backupid DESC LIMIT "+convert(max_display)+")");
	}
}

void getLastActs(Helper &helper, JSON::Object &ret, std::vector<int> clientids)
{
	std::string filter;
	if(!clientids.empty()) 
	{
		filter=" AND ( ";
		for(size_t i=0;i<clientids.size();++i)
		{
			filter+="clientid=?";
			if(i+1<clientids.size())
				filter+=" OR ";
		}
		filter+=" ) ";
	}

	IDatabase *db=helper.getDatabase();
	IQuery *q=db->Prepare("SELECT * FROM ("
	"SELECT a.id AS backupid, clientid, name, strftime('"+helper.getTimeFormatString()+"', a.backuptime) AS backuptime, backuptime AS bt,"
	 "incremental, (strftime('%s',running)-strftime('%s',a.backuptime)) AS duration, size_bytes, 0 AS image, 0 AS del, size_calculated, resumed, 0 AS restore, '' AS details "
	"FROM backups a INNER JOIN clients b ON a.clientid=b.id "
	 "WHERE complete=1 "+filter+
	"UNION ALL "
	"SELECT c.id AS backupid, clientid, name, strftime('"+helper.getTimeFormatString()+"', c.backuptime) AS backuptime, backuptime AS bt,"
	"incremental, (strftime('%s',running)-strftime('%s',c.backuptime)) AS duration, (size_bytes+IFNULL(0,("
	"SELECT SUM(size_bytes) FROM backup_images INNER JOIN (SELECT * FROM assoc_images WHERE img_id=c.id) ON assoc_id=id"
	")) ) AS size_bytes, 1 AS image, 0 AS del, 1 as size_calculated, 0 AS resumed, 0 AS restore, letter AS details "
	"FROM backup_images c INNER JOIN clients d ON c.clientid=d.id "
	"WHERE complete=1 AND length(letter)<=2 "+filter+
	"UNION ALL "
	"SELECT e.backupid AS backupid, clientid, name, strftime('"+helper.getTimeFormatString()+"', e.created) AS backuptime, created AS bt,"
	"incremental, (strftime('%s',stoptime)-strftime('%s',e.created)) AS duration, delsize AS size_bytes, image, 1 AS del, 1 AS size_calculated, 0 AS resumed, 0 AS restore, '' AS details "
	"FROM del_stats e INNER JOIN clients f ON e.clientid=f.id "
	"WHERE 1=1 "+filter+
	"UNION ALL " 
	"SELECT g.id AS backupid, clientid, name, strftime('"+helper.getTimeFormatString()+"', g.created) AS backuptime, g.created as bt,"
	"0 AS incremental, (strftime('%s',g.finished)-strftime('%s',g.created)) AS duration, -1 AS size_bytes, image, 0 AS del, 0 AS size_calculated, 0 AS resumed, 1 AS restore,"
		" (CASE WHEN image=1 THEN letter ELSE path END) AS details "
	"FROM restores g INNER JOIN clients h ON g.clientid=h.id "
	"WHERE done=1 "+filter+
	") ORDER BY bt DESC LIMIT "+convert(max_display));

	for(size_t i=0;i<clientids.size();++i)
	{
		q->Bind(clientids[i]);
	}
	for(size_t i=0;i<clientids.size();++i)
	{
		q->Bind(clientids[i]);
	}

	db_results res=q->Read();
	q->Reset();

	JSON::Array lastacts;
	for(size_t i=0;i<res.size();++i)
	{
		JSON::Object obj;
		obj.set("id", watoi(res[i]["backupid"]));
		obj.set("clientid", watoi(res[i]["clientid"]));
		obj.set("name", res[i]["name"]);
		obj.set("backuptime", watoi64(res[i]["backuptime"]));
		obj.set("incremental", watoi(res[i]["incremental"]));
		obj.set("duration", watoi64(res[i]["duration"]));
		obj.set("resumed", watoi(res[i]["resumed"]));
		obj.set("restore", watoi(res[i]["restore"]));
		if(watoi(res[i]["size_calculated"])==1)
		{
			obj.set("size_bytes", watoi64(res[i]["size_bytes"]));
		}
		else
		{
			obj.set("size_bytes", -1);
		}
		obj.set("image", watoi(res[i]["image"]));
		obj.set("del", (res[i]["del"]=="1") );
		obj.set("details", res[i]["details"]);
		lastacts.add(obj);
	}
	ret.set("lastacts", lastacts);
}

ACTION_IMPL(lastacts)
{
	Helper helper(tid, &POST, &PARAMS);
	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	std::vector<int> clientids;
	std::string rights=helper.getRights("lastacts");
	if(rights!="none" && rights!="all")
	{
		std::vector<std::string> s_cid;
		Tokenize(rights, s_cid, ",");
		for(size_t i=0;i<s_cid.size();++i)
		{
			clientids.push_back(atoi(s_cid[i].c_str()));
		}
	}

	if(session!=NULL && (rights=="all" || clientids.empty()) )
	{
		getLastActs(helper, ret, clientids);
	}
	else
	{
		ret.set("error", 1);
	}
    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
