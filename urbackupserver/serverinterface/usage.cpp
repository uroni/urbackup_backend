/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifndef CLIENT_ONLY

#include "action_header.h"
#include "../server_cleanup.h"
#include "../../Interface/ThreadPool.h"

namespace 
{
	class RecalculateStatistics : public IThread
	{
	public:
		RecalculateStatistics(IDatabase *db)
			: db(db)
		{
		}

		void operator()(void)
		{
			db->BeginTransaction();
			db->Write("UPDATE files SET did_count=0");
			db->Write("UPDATE clients SET bytes_used_files=0");
			db->Write("UPDATE backups SET size_bytes=0");
			db->EndTransaction();
			ServerCleanupThread::updateStats(false);
			delete this;
		}
	private:
		IDatabase *db;
	};
}

ACTION_IMPL(usage)
{
	Helper helper(tid, &GET, &PARAMS);

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	if(session!=NULL )
	{
		IDatabase *db=helper.getDatabase();
		if(helper.getRights("piegraph")=="all")
		{
			IQuery *q=db->Prepare("SELECT (bytes_used_files+bytes_used_images) AS used, bytes_used_files, bytes_used_images, name FROM clients ORDER BY (bytes_used_files+bytes_used_images) DESC");
			db_results res=q->Read();
		
			JSON::Array usage;
			for(size_t i=0;i<res.size();++i)
			{
				JSON::Object obj;
				obj.set("used",atof(wnarrow(res[i][L"used"]).c_str()));
				obj.set("files",atof(wnarrow(res[i][L"bytes_used_files"]).c_str()));
				obj.set("images",atof(wnarrow(res[i][L"bytes_used_images"]).c_str()));
				obj.set("name",res[i][L"name"]);
				usage.add(obj);
			}
			ret.set("usage", usage);
		}
		if(helper.getRights("reset_statistics")=="all")
		{
			ret.set("reset_statistics", "true");

			if(GET[L"recalculate"]==L"true")
			{
				Server->getThreadPool()->execute(new RecalculateStatistics(db));
			}
		}
	}
	else
	{
		ret.set("error", 1);
	}
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY