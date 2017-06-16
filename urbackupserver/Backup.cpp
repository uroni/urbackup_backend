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

#include "Backup.h"
#include "server_settings.h"
#include "../Interface/Server.h"
#include "database.h"
#include "../urbackupcommon/os_functions.h"
#include "server_log.h"
#include "../stringtools.h"
#include "ClientMain.h"
#include "../urlplugin/IUrlFactory.h"
#include "server_status.h"
#include "server_cleanup.h"
#include "LogReport.h"

extern IUrlFactory *url_fak;

namespace
{
	class DelayedWakeup : public IThread
	{
	public:
		DelayedWakeup(ClientMain* client_main)
			: client_main(client_main)
		{

		}

		void operator()()
		{
			Server->wait(1000);
			client_main->getInternalCommandPipe()->Write("WAKEUP");
			delete this;
		}

	private:
		ClientMain* client_main;
	};
}

Backup::Backup(ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname,
	LogAction log_action, bool is_file_backup, bool is_incremental, std::string server_token, std::string details, bool scheduled)
	: client_main(client_main), clientid(clientid), clientname(clientname), clientsubname(clientsubname), log_action(log_action),
	is_file_backup(is_file_backup), r_incremental(is_incremental), r_resumed(false), backup_result(false),
	log_backup(true), has_early_error(false), should_backoff(true), db(NULL), status_id(0), has_timeout_error(false),
	server_token(server_token), details(details), num_issues(0), stop_backup_running(true), scheduled(scheduled)
{
	
}

void Backup::operator()()
{
	logid = ServerLogger::getLogId(clientid);
	db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	server_settings.reset(new ServerSettings(db, clientid));
	ScopedFreeObjRef<ServerBackupDao*> backup_dao_free(backup_dao);
	backup_dao = new ServerBackupDao(db);

	ScopedActiveThread sat;
	active_thread=sat.get();

	if(is_file_backup)
	{
		if(r_incremental)
		{
			if(r_resumed)
			{
				status_id = ServerStatus::startProcess(clientname, sa_resume_incr_file, details, logid, true);
			}
			else
			{
				status_id = ServerStatus::startProcess(clientname, sa_incr_file, details, logid, true);
			}
		}
		else
		{
			if(r_resumed)
			{
				status_id = ServerStatus::startProcess(clientname, sa_resume_full_file, details, logid, true);
			}
			else
			{
				status_id = ServerStatus::startProcess(clientname, sa_full_file, details, logid, true);
			}
		}
	}
	else
	{
		if(r_incremental)
		{
			status_id = ServerStatus::startProcess(clientname, sa_incr_image, details, logid, true);
		}
		else
		{
			status_id = ServerStatus::startProcess(clientname, sa_full_image, details, logid, true);
		}
		ServerStatus::setProcessPcDone(clientname, status_id, 0);
	}

	createDirectoryForClient();

	int64 backup_starttime=Server->getTimeMS();

	bool do_log = false;
	backup_result = doBackup();

	if (stop_backup_running)
	{
		client_main->stopBackupRunning(is_file_backup);
	}

	if(!has_early_error && log_action!=LogAction_NoLogging)
	{
		ServerLogger::Log(logid, "Time taken for backing up client "+clientname+": "+PrettyPrintTime(Server->getTimeMS()-backup_starttime), LL_INFO);
		if(!backup_result)
		{
			ServerLogger::Log(logid, "Backup failed", LL_ERROR);
		}
		else if(num_issues==0)
		{
			ServerLogger::Log(logid, "Backup succeeded", LL_INFO);
		}
		else
		{
			ServerLogger::Log(logid, "Backup completed with issues", LL_INFO);
		}
	}

	if (!has_early_error)
	{
		ServerCleanupThread::updateStats(false);
	}

	if( (log_backup || log_action == LogAction_AlwaysLog) && log_action!=LogAction_NoLogging)
	{
		saveClientLogdata(is_file_backup?0:1, r_incremental?1:0, backup_result && !has_early_error, r_resumed); 
	}

	ServerLogger::reset(logid);	
	ServerStatus::stopProcess(clientname, status_id);

	server_settings.reset();
	db=NULL;

	Server->getThreadPool()->execute(new DelayedWakeup(client_main));
}

void Backup::setErrors(Backup& other)
{
	should_backoff = other.shouldBackoff();
	has_timeout_error = other.hasTimeoutError();
}

bool Backup::createDirectoryForClient(void)
{
	std::string backupfolder=server_settings->getSettings()->backupfolder;
	if(!os_create_dir(os_file_prefix(backupfolder+os_file_sep()+clientname)) && !os_directory_exists(os_file_prefix(backupfolder+os_file_sep()+clientname)) )
	{
		Server->Log("Could not create or read directory for client \""+clientname+"\"", LL_ERROR);
		return false;
	}
	return true;
}

void Backup::saveClientLogdata(int image, int incremental, bool r_success, bool resumed)
{
	int errors=0;
	int warnings=0;
	int infos=0;
	std::string logdata=ServerLogger::getLogdata(logid, errors, warnings, infos);

	backup_dao->saveBackupLog(clientid, errors, warnings, infos, is_file_backup?0:1,
		r_incremental?1:0, r_resumed?1:0, 0);

	backup_dao->saveBackupLogData(db->getLastInsertID(), logdata);

	sendLogdataMail(r_success, image, incremental, resumed, errors, warnings, infos, logdata);
}

void Backup::sendLogdataMail(bool r_success, int image, int incremental, bool resumed, int errors, int warnings, int infos, std::string &data)
{
	MailServer mail_server=ClientMain::getMailServerSettings();
	if(mail_server.servername.empty())
		return;

	if(url_fak==NULL)
		return;

	std::vector<int> mailable_user_ids = backup_dao->getMailableUserIds();
	for(size_t i=0;i<mailable_user_ids.size();++i)
	{
		std::string logr=getUserRights(mailable_user_ids[i], "logs");
		bool has_r=false;
		if(logr!="all")
		{
			std::vector<std::string> toks;
			Tokenize(logr, toks, ",");
			for(size_t j=0;j<toks.size();++j)
			{
				if(watoi(toks[j])==clientid)
				{
					has_r=true;
				}
			}
		}
		else
		{
			has_r=true;
		}

		if(has_r)
		{
			ServerBackupDao::SReportSettings report_settings =
				backup_dao->getUserReportSettings(mailable_user_ids[i]);

			if(report_settings.exists)
			{
				std::string report_mail=report_settings.report_mail;
				int report_loglevel=report_settings.report_loglevel;
				int report_sendonly=report_settings.report_sendonly;

				if( ( ( report_loglevel==0 && infos>0 )
					|| ( report_loglevel<=1 && warnings>0 )
					|| ( report_loglevel<=2 && errors>0 ) ) &&
					(report_sendonly==0 ||
					( report_sendonly==1 && !r_success ) ||
					( report_sendonly==2 && r_success) ||
					( report_sendonly==3 && !r_success && !has_timeout_error) ) )
				{
					if (run_report_script(incremental, resumed, image, infos, warnings,
						errors, r_success, report_mail, data, clientname))
					{
						return;
					}

					std::vector<std::string> to_addrs;
					Tokenize(report_mail, to_addrs, ",;");

					std::string subj="UrBackup: ";
					std::string msg="UrBackup just did ";
					if(incremental>0)
					{
						if(resumed)
						{
							msg+="a resumed incremental ";
							subj+="Resumed incremental ";
						}
						else
						{
							msg+="an incremental ";
							subj+="Incremental ";
						}
					}
					else
					{
						if(resumed)
						{
							msg+="a resumed full ";
							subj+="Resumed full ";
						}
						else
						{
							msg+="a full ";
							subj+="Full ";
						}
					}

					if(image>0)
					{
						msg+="image ";
						subj+="image ";
					}
					else
					{
						msg+="file ";
						subj+="file ";
					}
					subj+="backup of \""+clientname+"\"\n";
					msg+="backup of \""+clientname+"\".\n";
					msg+="\nReport:\n";
					msg+="( "+convert(infos);
					if(infos!=1) msg+=" infos, ";
					else msg+=" info, ";
					msg+=convert(warnings);
					if(warnings!=1) msg+=" warnings, ";
					else msg+=" warning, ";
					msg+=convert(errors);
					if(errors!=1) msg+=" errors";
					else msg+=" error";
					msg+=" )\n\n";
					std::vector<std::string> msgs;
					TokenizeMail(data, msgs, "\n");

					for(size_t j=0;j<msgs.size();++j)
					{
						std::string ll;
						if(!msgs[j].empty()) ll=msgs[j][0];
						int li=watoi(ll);
						msgs[j].erase(0, 2);
						std::string tt=getuntil("-", msgs[j]);
						std::string m=getafter("-", msgs[j]);
						tt=backup_dao->formatUnixtime(watoi64(tt)).value;
						std::string lls="info";
						if(li==1) lls="warning";
						else if(li==2) lls="error";
						msg+=(tt)+"("+lls+"): "+(m)+"\n";
					}
					if(!r_success)
						subj+=" - failed";
					else
						subj+=" - success";

					std::string errmsg;
					bool b=url_fak->sendMail(mail_server, to_addrs, subj, msg, &errmsg);
					if(!b)
					{
						Server->Log("Sending mail failed. "+errmsg, LL_WARNING);
					}
				}
			}
		}
	}
}

std::string Backup::getUserRights(int userid, std::string domain)
{
	if(domain!="all")
	{
		if(getUserRights(userid, "all")=="all")
			return "all";
	}

	ServerBackupDao::CondString t_right = backup_dao->getUserRight(userid, domain);
	if(t_right.exists)
	{
		return t_right.value;
	}
	else
	{
		return "none";
	}
}