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

#include "server_ping.h"
#include "ClientMain.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "server_status.h"

const int64 ping_intervall=10000;

namespace
{
	bool isFileBackup(const SProcess& proc)
	{
		return proc.action == sa_incr_file
			|| proc.action == sa_full_file
			|| proc.action == sa_resume_incr_file
			|| proc.action == sa_resume_full_file;
	}

	//Text of file backup activities (phase, current file), shown in the tray of the client
	std::string getPingDetails(const SProcess& proc)
	{
		const size_t max_len = 400;
		std::string ret;
		for (size_t i = 0; i < proc.details.size() && ret.size() < max_len; ++i)
		{
			unsigned char ch = static_cast<unsigned char>(proc.details[i]);
			if (ch >= 32 && ch != 127)
			{
				ret += proc.details[i];
			}
		}

		//Do not cut in the middle of a UTF-8 sequence
		if (ret.size() >= max_len)
		{
			while (!ret.empty()
				&& (static_cast<unsigned char>(ret[ret.size() - 1]) & 0xC0) == 0x80)
			{
				ret.erase(ret.size() - 1);
			}
			if (!ret.empty()
				&& (static_cast<unsigned char>(ret[ret.size() - 1]) & 0xC0) == 0xC0)
			{
				ret.erase(ret.size() - 1);
			}
		}

		return ret;
	}
}

ServerPingThread::ServerPingThread(ClientMain *client_main, const std::string& clientname,
	size_t status_id, bool with_eta, std::string server_token, bool with_xfer_stats)
	: client_main(client_main), clientname(clientname), status_id(status_id),
	with_eta(with_eta), server_token(server_token), with_xfer_stats(with_xfer_stats),
	n_xfer_samples(0)
{
	stop=false;
	is_timeout=false;
}

void ServerPingThread::operator()(void)
{
	ClientMain::SConnection connection;
	int64 last_ping_ok=Server->getTimeMS();
	while(!stop)
	{
		//Server->Log("Sending ping running...", LL_DEBUG);
		std::string pcdone;
		SProcess proc = ServerStatus::getProcess(clientname, status_id);

		if(proc.action==sa_none)
		{
			break;
		}

		int i_pcdone = proc.pcdone;
		if(i_pcdone>=0)
		{
			pcdone=convert(i_pcdone);
		}

		int64 add_time = Server->getTimeMS() - proc.eta_set_time;
		int64 etams = proc.eta_ms - add_time;

		if(!with_eta)
		{
			if(client_main->sendClientMessage("PING RUNNING -"+pcdone+"-#token="+server_token,
				"OK", "Error sending 'running' ping to client", 30000, false, LL_DEBUG,
				NULL, NULL, &connection))
			{
				last_ping_ok=Server->getTimeMS();
			}
		}
		else
		{
			if(etams>0 && etams<60*1000)
			{
				etams=61*1000;
			}
			std::string ret = client_main->sendClientMessage("2PING RUNNING pc_done="+pcdone+(etams>0?"&eta_ms="+convert(etams):"")
							+"&status_id="+convert(status_id)
							+"&speed_bpms="+convert(proc.speed_bpms)
							+"&total_bytes="+convert(proc.total_bytes)
							+"&done_bytes="+convert(proc.done_bytes)
							+(isFileBackup(proc) ? "&details=" + EscapeParamString(getPingDetails(proc)) : std::string())
							+"&paused_fb=1"
							+(with_xfer_stats ? "&xfer_fb=1" : "")
							+"#token="+server_token, "Error sending 'running' (2) ping to client",
									30000, false, LL_DEBUG, &connection);
			//With xfer_fb=1 a newer client answers "OK#rd=<ms>&snd=<ms>" (or PAUSED#...)
			std::string xfer_params;
			size_t xfer_pos = ret.find('#');
			if (with_xfer_stats
				&& xfer_pos != std::string::npos)
			{
				xfer_params = ret.substr(xfer_pos + 1);
				ret.resize(xfer_pos);
			}
			if(ret=="OK"
				|| ret=="PAUSED")
			{
				if (!xfer_params.empty())
				{
					addXferSample(xfer_params);
				}

				if (ret == "PAUSED")
				{
					setPaused(&proc, true);
				}
				else
				{
					setPaused(&proc, false);
				}
				last_ping_ok=Server->getTimeMS();
				client_main->refreshSessionIdentity();
			}
			else
			{
				setPaused(&proc, false);

				if (!ret.empty())
				{
					ret = ". Received: \"" + ret+"\"";
				}
				Server->Log("Error sending 'running' (3) ping to client" + ret, LL_DEBUG);
			}
		}
		
		//Server->Log("Done sending ping running.", LL_DEBUG);

		if(Server->getTimeMS()-last_ping_ok>ping_intervall*6)
		{
			is_timeout=true;
		}
		else
		{
			is_timeout=false;
		}

		Server->wait(ping_intervall);
	}
	while(!stop)
	{
		Server->wait(1000);
	}
	setPaused(NULL, false);
	delete this;
}

void ServerPingThread::setStop(bool b)
{
	stop=b;
}

bool ServerPingThread::isTimeout(void)
{
	return is_timeout;
}

void ServerPingThread::addXferSample(const std::string& params_str)
{
	str_map params;
	ParseParamStrHttp(params_str, &params);

	std::lock_guard<std::mutex> lock(xfer_mutex);
	xfer_time[0] = xfer_time[1];
	xfer_read[0] = xfer_read[1];
	xfer_send[0] = xfer_send[1];
	xfer_time[1] = Server->getTimeMS();
	xfer_read[1] = watoi64(params["rd"]);
	xfer_send[1] = watoi64(params["snd"]);
	++n_xfer_samples;
}

bool ServerPingThread::getClientWaitShares(double& read_share, double& send_share)
{
	std::lock_guard<std::mutex> lock(xfer_mutex);
	if (n_xfer_samples < 2
		|| Server->getTimeMS() - xfer_time[1] > 3 * ping_intervall)
	{
		return false;
	}

	int64 window = xfer_time[1] - xfer_time[0];
	int64 read_ms = xfer_read[1] - xfer_read[0];
	int64 send_ms = xfer_send[1] - xfer_send[0];
	if (window < ping_intervall / 2
		|| read_ms < 0 || send_ms < 0)
	{
		return false;
	}

	read_share = static_cast<double>(read_ms) / window;
	send_share = static_cast<double>(send_ms) / window;
	return true;
}

void ServerPingThread::setPaused(SProcess* proc, bool b)
{
	if (proc==NULL || b != proc->paused)
	{
		ServerStatus::setProcessPaused(clientname, status_id, b);
	}
}

#endif //CLIENT_ONLY