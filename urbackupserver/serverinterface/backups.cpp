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

#include "action_header.h"
#include "../../urbackupcommon/os_functions.h"

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

bool create_zip_to_output(const std::wstring& foldername, const std::wstring& filter);

namespace
{
	bool sendFile(Helper& helper, const std::wstring& filename)
	{
		THREAD_ID tid = Server->getThreadID();
		Server->setContentType(tid, "application/octet-stream");
		Server->addHeader(tid, "Content-Disposition: attachment; filename=\""+Server->ConvertToUTF8(ExtractFileName(filename))+"\"");
		IFile *in=Server->openFile(os_file_prefix(filename), MODE_READ);
		if(in!=NULL)
		{
			helper.releaseAll();

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
			return true;
		}
		else
		{
			Server->Log(L"Error opening file \""+filename+L"\"", LL_ERROR);
			return false;
		}
	}

	bool sendZip(Helper& helper, const std::wstring& foldername)
	{
		std::wstring zipname=ExtractFileName(foldername)+L".zip";

		THREAD_ID tid = Server->getThreadID();
		Server->setContentType(tid, "application/octet-stream");
		Server->addHeader(tid, "Content-Disposition: attachment; filename=\""+Server->ConvertToUTF8(zipname)+"\"");
		helper.releaseAll();

		return create_zip_to_output(foldername, L"");
	}
}

ACTION_IMPL(backups)
{
	Helper helper(tid, &GET, &PARAMS);
	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	std::wstring sa=GET[L"sa"];
	std::string rights=helper.getRights("browse_backups");
	std::string archive_rights=helper.getRights("manual_archive");
	std::vector<int> clientid=helper.getRightIDs(rights);
	std::vector<int> clientid_archive=helper.getRightIDs(archive_rights);
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
			std::string qstr="SELECT id, name, strftime('"+helper.getTimeFormatString()+"', lastbackup, 'localtime') AS lastbackup FROM clients ORDER BY name";
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
			bool r_ok=helper.hasRights(t_clientid, rights, clientid);
			bool archive_ok=helper.hasRights(t_clientid, archive_rights, clientid_archive);

			if(r_ok)
			{
				if(archive_ok)
				{
					if(GET.find(L"archive")!=GET.end())
					{
						IQuery *q=db->Prepare("UPDATE backups SET archived=1, archive_timeout=0 WHERE id=?");
						q->Bind(watoi(GET[L"archive"]));
						q->Write();
					}
					else if(GET.find(L"unarchive")!=GET.end())
					{
						IQuery *q=db->Prepare("UPDATE backups SET archived=0 WHERE id=?");
						q->Bind(watoi(GET[L"unarchive"]));
						q->Write();
					}
				}

				IQuery *q=db->Prepare("SELECT id, strftime('"+helper.getTimeFormatString()+"', backuptime, 'localtime') AS t_backuptime, incremental, size_bytes, archived, archive_timeout FROM backups WHERE complete=1 AND done=1 AND clientid=? ORDER BY backuptime DESC");
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
					obj.set("archived", res[i][L"archived"]);
					_i64 archive_timeout=0;
					if(!res[i][L"archive_timeout"].empty())
					{
						archive_timeout=watoi64(res[i][L"archive_timeout"]);
						if(archive_timeout!=0)
						{
							archive_timeout-=Server->getTimeSeconds();
						}
					}
					obj.set("archive_timeout", archive_timeout);
					backups.add(obj);
				}
				ret.set("backups", backups);
				ret.set("can_archive", archive_ok);

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
		else if(sa==L"files" || sa==L"filesdl" || sa==L"zipdl" )
		{
			int t_clientid=watoi(GET[L"clientid"]);
			bool r_ok=helper.hasRights(t_clientid, rights, clientid);

			if(r_ok)
			{
				int backupid=watoi(GET[L"backupid"]);
				std::wstring u_path=UnescapeHTML(GET[L"path"]);
				std::wstring path;
				std::vector<std::wstring> t_path;
				Tokenize(u_path, t_path, L"/");
				for(size_t i=0;i<t_path.size();++i)
				{
					if(!t_path[i].empty() && t_path[i]!=L" " && t_path[i]!=L"." && t_path[i]!=L".." && t_path[i].find(L"/")==std::string::npos && t_path[i].find(L"\\")==std::string::npos )
					{
						path+=UnescapeSQLString(t_path[i])+os_file_sep();
					}
				}

				if( (sa==L"filesdl" || sa==L"zipdl")
					&& !path.empty())
				{
					path.erase(path.size()-1, 1);
				}

				IQuery *q=db->Prepare("SELECT name FROM clients WHERE id=?");
				q->Bind(t_clientid);
				db_results res=q->Read();
				q->Reset();
				q=db->Prepare("SELECT value FROM settings_db.settings WHERE key='backupfolder'");
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

					q=db->Prepare("SELECT path,strftime('"+helper.getTimeFormatString()+"', backuptime, 'localtime') AS backuptime FROM backups WHERE id=?");
					q->Bind(backupid);
					res=q->Read();
					q->Reset();

					if(!res.empty())
					{
						std::wstring backuppath=res[0][L"path"];

						ret.set("backuptime", res[0][L"backuptime"]);

						std::wstring currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath+(path.empty()?L"":(os_file_sep()+path));


						if(sa==L"filesdl")
						{
							sendFile(helper, currdir);
							return;
						}
						else if(sa==L"zipdl")
						{
							sendZip(helper, currdir);
							return;
						}

						std::vector<SFile> tfiles=getFiles(os_file_prefix(currdir));

						JSON::Array files;
						for(size_t i=0;i<tfiles.size();++i)
						{
							if(tfiles[i].isdir)
							{
								if(path.empty() && tfiles[i].name==L".hashes")
									continue;

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