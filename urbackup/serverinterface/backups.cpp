/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
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
#include "../os_functions.h"

std::string constructFilter(const std::vector<int> &clientid, std::string key)
{
	std::string clientf="(";
	for(size_t i=0;i<clientid.size();++i)
	{
		clientf+=key+"="+nconvert(clientid[i]);
		if(i+1<clientid.size())
			clientf+=" OR ";
	}
	clientf+=")";
	return clientf;
}

ACTION_IMPL(backups)
{
	Helper helper(tid, &GET, &PARAMS);
	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	std::wstring sa=GET[L"sa"];
	std::string rights=helper.getRights("browse_backups");
	std::vector<int> clientid;
	if(rights!="all" && rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			clientid.push_back(atoi(s_clientid[i].c_str()));
		}
	}
	if(clientid.size()==1 && sa.empty() )
	{
		sa=L"backups";
		GET[L"clientid"]=convert(clientid[0]);
	}
	if(session!=NULL && rights!="none")
	{
		IDatabase *db=helper.getDatabase();
		if(sa.empty())
		{
			std::string qstr="SELECT id, name, strftime('"+helper.getTimeFormatString()+"', lastbackup) AS lastbackup FROM clients";
			if(!clientid.empty()) qstr+=" WHERE "+constructFilter(clientid, "id");
			IQuery *q=db->Prepare(qstr);
			db_results res=q->Read();
			q->Reset();
			JSON::Array clients;
			for(size_t i=0;i<res.size();++i)
			{
				JSON::Object obj;
				obj.set("name", res[i][L"name"]);
				obj.set("id", watoi(res[i][L"id"]));
				obj.set("lastbackup", res[i][L"lastbackup"]);
				clients.add(obj);
			}
			ret.set("clients", clients);
		}
		else if(sa==L"backups")
		{
			int t_clientid=watoi(GET[L"clientid"]);
			bool r_ok=false;
			if(rights!="all")
			{
				for(size_t i=0;i<clientid.size();++i)
				{
					if(clientid[i]==t_clientid)
					{
						r_ok=true;
						break;
					}
				}
			}
			else
			{
				r_ok=true;
			}

			if(r_ok)
			{
				IQuery *q=db->Prepare("SELECT id, strftime('"+helper.getTimeFormatString()+"', backuptime) AS t_backuptime, incremental, size_bytes FROM backups WHERE complete=1 AND done=1 AND clientid=? ORDER BY backuptime DESC");
				q->Bind(t_clientid);
				db_results res=q->Read();
				JSON::Array backups;
				for(size_t i=0;i<res.size();++i)
				{
					JSON::Object obj;
					obj.set("id", watoi(res[i][L"id"]));
					obj.set("backuptime", res[i][L"t_backuptime"]);
					obj.set("incremental", watoi(res[i][L"incremental"]));
					obj.set("size_bytes", res[i][L"size_bytes"]);
					backups.add(obj);
				}
				ret.set("backups", backups);

				q=db->Prepare("SELECT name FROM clients WHERE id=?");
				q->Bind(t_clientid);
				res=q->Read();
				q->Reset();
				if(!res.empty())
				{
					ret.set("clientname", res[0][L"name"]);
					ret.set("clientid", t_clientid);
				}
			}
			else
			{
				ret.set("error", 2);
			}
		}
		else if(sa==L"files" || sa==L"filesdl")
		{
			int t_clientid=watoi(GET[L"clientid"]);
			bool r_ok=false;
			if(rights!="all")
			{
				for(size_t i=0;i<clientid.size();++i)
				{
					if(clientid[i]==t_clientid)
					{
						r_ok=true;
						break;
					}
				}
			}
			else
			{
				r_ok=true;
			}

			if(r_ok)
			{
				int backupid=watoi(GET[L"backupid"]);
				std::wstring u_path=UnescapeSQLString(GET[L"path"]);
				std::wstring path;
				std::vector<std::wstring> t_path;
				Tokenize(u_path, t_path, L"/");
				for(size_t i=0;i<t_path.size();++i)
				{
					if(!t_path[i].empty() && t_path[i]!=L" " && t_path[i]!=L"." && t_path[i]!=L".." && t_path[i].find(L"/")==std::string::npos && t_path[i].find(L"\\")==std::string::npos )
					{
						path+=t_path[i]+os_file_sep();
					}
				}

				if(sa==L"filesdl" && !path.empty())
				{
					path.erase(path.size()-1, 1);
				}

				IQuery *q=db->Prepare("SELECT name FROM clients WHERE id=?");
				q->Bind(t_clientid);
				db_results res=q->Read();
				q->Reset();
				q=db->Prepare("SELECT value FROM settings WHERE key='backupfolder'");
				db_results res_bf=q->Read();
				q->Reset();
				if(!res.empty() && !res_bf.empty() )
				{
					std::wstring backupfolder=res_bf[0][L"value"];
					std::wstring clientname=res[0][L"name"];

					ret.set("clientname", clientname);
					ret.set("clientid", t_clientid);
					ret.set("backupid", backupid);
					ret.set("path", u_path);

					q=db->Prepare("SELECT path,strftime('"+helper.getTimeFormatString()+"', backuptime) AS backuptime FROM backups WHERE id=?");
					q->Bind(backupid);
					res=q->Read();
					q->Reset();

					if(!res.empty())
					{
						std::wstring backuppath=res[0][L"path"];

						ret.set("backuptime", res[0][L"backuptime"]);

						std::wstring currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath+os_file_sep()+path;


						if(sa==L"filesdl")
						{
							Server->setContentType(tid, "application/octet-stream");
							Server->addHeader(tid, "Content-Disposition: attachment; filename=\""+Server->ConvertToUTF8(ExtractFileName(path))+"\"");
							IFile *in=Server->openFile(os_file_prefix()+currdir, MODE_READ);
							if(in!=NULL)
							{
								Server->addHeader(tid, "Content-Length: "+nconvert(in->Size()) );
								char buf[4096];
								_u32 r;
								do
								{
									r=in->Read(buf, 4096);
									Server->WriteRaw(tid, buf, r, false);
								}
								while(r>0);
								Server->destroy(in);
								return;
							}
						}

						std::vector<SFile> tfiles=getFiles(os_file_prefix()+currdir);

						JSON::Array files;
						for(size_t i=0;i<tfiles.size();++i)
						{
							if(tfiles[i].isdir)
							{
								JSON::Object obj;
								obj.set("name", tfiles[i].name);
								obj.set("dir", tfiles[i].isdir);
								obj.set("size", tfiles[i].size);
								files.add(obj);
							}
						}
						for(size_t i=0;i<tfiles.size();++i)
						{
							if(!tfiles[i].isdir)
							{
								JSON::Object obj;
								obj.set("name", tfiles[i].name);
								obj.set("dir", tfiles[i].isdir);
								obj.set("size", tfiles[i].size);
								files.add(obj);
							}
						}
						ret.set("files", files);
					}
				}
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