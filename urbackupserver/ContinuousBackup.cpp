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

#include "ContinuousBackup.h"
#include "ClientMain.h"
#include "server_continuous.h"
#include "IncrFileBackup.h"

ContinuousBackup::ContinuousBackup( ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname, LogAction log_action,
	int group, bool use_tmpfiles, std::string tmpfile_path, bool use_reflink, bool use_snapshots, std::string details)
	: FileBackup(client_main, clientid, clientname, clientsubname, log_action,
	true, group, use_tmpfiles, tmpfile_path, use_reflink, use_snapshots, server_token, details)
{
	cdp_path=true;
}

ContinuousBackup::~ContinuousBackup()
{
}

bool ContinuousBackup::doFileBackup()
{
	if(!client_main->sendClientMessageRetry("CONTINUOUS WATCH START", "OK", "Error sending command to continuously watch the backups from now on", 10000, 3))
	{
		return false;
	}

	ServerLogger::Log(logid, "Starting continuous data protection synchronization...", LL_INFO);

	bool intra_file_diffs;
	if(client_main->isOnInternetConnection())
	{
		intra_file_diffs=(server_settings->getSettings()->internet_incr_file_transfer_mode=="blockhash");
	}
	else
	{
		intra_file_diffs=(server_settings->getSettings()->local_incr_file_transfer_mode=="blockhash");
	}

	{
		continuous_update = new BackupServerContinuous(client_main,
			backuppath, backuppath_hashes, backuppath, tmpfile_path, use_tmpfiles,
			clientid, clientname, backupid, use_snapshots, use_reflink, hashpipe_prepare);
		continuous_thread_ticket = Server->getThreadPool()->execute(continuous_update, "backup continuous main");
		client_main->setContinuousBackup(continuous_update);
	}

	IncrFileBackup incr_backup(client_main, clientid, clientname, clientsubname, LogAction_NoLogging,
		group, use_tmpfiles, tmpfile_path, use_reflink, use_snapshots, server_token, details);
	incr_backup.setStopBackupRunning(false);
	incr_backup();

	if(incr_backup.getResult())
	{
		continuous_sequences=incr_backup.getContinuousSequences();

		std::vector<BackupServerContinuous::SSequence> new_sequences;
		for(std::map<std::string, SContinuousSequence>::iterator it=continuous_sequences.begin();
			it!=continuous_sequences.end();++it)
		{
			BackupServerContinuous::SSequence seq = {};
			seq.id = it->second.id;
			seq.next = it->second.next;
			new_sequences.push_back(seq);
		}

		Server->wait(1000);

		continuous_update->setSequences(new_sequences);

		continuous_update->startExecuting();
	}
	else
	{
		continuous_update->doStop();
	}

	return incr_backup.getResult();
}
