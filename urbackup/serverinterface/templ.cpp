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

#ifdef _DEBUG
#define GEN_TEMPL
#endif

#ifndef _WIN32
#define GEN_TEMPL
#endif

#include "action_header.h"
#include "../../Interface/File.h"

ACTION_IMPL(generate_templ)
{
#ifdef GEN_TEMPL
	std::vector<std::string> templates;
	templates.push_back("progress_row");
	templates.push_back("progress_table");
	templates.push_back("progress_table_none");
	templates.push_back("main_nav");
	templates.push_back("main_nav_sel");
	templates.push_back("lastacts_table");
	templates.push_back("lastacts_row");
	templates.push_back("stat_general");
	templates.push_back("stat_general_row");
	templates.push_back("stat_nav_pos");
	templates.push_back("stat_user");
	templates.push_back("status");
	templates.push_back("status_row");
	templates.push_back("backups_clients");
	templates.push_back("backups_clients_row");
	templates.push_back("backups_backups");
	templates.push_back("backups_backups_row");
	templates.push_back("backups_files");
	templates.push_back("backups_files_row");
	templates.push_back("settings_save_ok");
	templates.push_back("settings_general");
	templates.push_back("settings_inv_row");
	templates.push_back("settings_user");
	templates.push_back("settings_users_start");
	templates.push_back("settings_users_start_row");
	templates.push_back("settings_users_start_row_empty");
	templates.push_back("settings_user_create");
	templates.push_back("settings_user_add_done");
	templates.push_back("login");
	templates.push_back("settings_user_create_admin");
	templates.push_back("settings_user_rights_change");
	templates.push_back("settings_user_rights_change_row");
	templates.push_back("settings_user_pw_change");
	templates.push_back("logs_table");
	templates.push_back("logs_row");
	templates.push_back("log_single");
	templates.push_back("log_single_row");
	templates.push_back("log_single_none");
	templates.push_back("log_single_filter");
	templates.push_back("logs_filter");
	templates.push_back("logs_none");

	IFile *out=Server->openFile("urbackup/www/templates.js", MODE_WRITE);
	if(out==NULL)
		return;

	out->Write("if(!window.tmpls) tmpls=new Object();\n");

	for(size_t i=0;i<templates.size();++i)
	{
		IFile *f=Server->openFile("urbackup/www/templates/"+templates[i]+".htm", MODE_READ);
		if(f==NULL)
		{
			Server->Log("Template not found: "+templates[i], LL_ERROR);
			continue;
		}
		std::string buf;
		out->Write("tmpls."+templates[i]+"=new Template(\"");
		while( (buf=f->Read(4096)).size()>0 )
		{
			std::string tw;
			for(size_t i=0;i<buf.size();++i)
			{
				if(buf[i]=='\n') tw+="\\n";
				else if(buf[i]=='\t') tw+="\\t";
				else if(buf[i]=='\r') tw+="\\r";
				else if(buf[i]=='"') tw+="\\\"";
				else tw+=buf[i];
			}
			out->Write(tw);
		}
		out->Write("\");\n");
		Server->destroy(f);
	}

	Server->destroy(out);
	
#ifndef _WIN32
	system("chmod 777 urbackup/www/templates.js");
#endif

	Server->Write(tid, "Writing templates done");
#endif
}

#endif //CLIENT_ONLY