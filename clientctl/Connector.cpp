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

#include "Connector.h"
#include "tcpstack.h"
#include "../stringtools.h"
#include "../urbackupcommon/escape.h"
#include "../urbackupcommon/os_functions.h"
#include "../socket_header.h"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <iostream>
#include <memory.h>
#include <stdlib.h>

std::string Connector::pw;
bool Connector::error=false;
const size_t conn_retries=4;
bool Connector::busy=false;
std::string Connector::pwfile="pw.txt";
std::string Connector::pwfile_change="pw_change.txt";
std::string Connector::client="127.0.0.1";
std::string Connector::tokens;

namespace
{
	bool LookupBlocking(std::string pServer, in_addr *dest)
	{
		const char* host=pServer.c_str();
		unsigned int addr = inet_addr(host);
		if (addr != INADDR_NONE)
		{
			dest->s_addr = addr;
		}
		else
		{
			hostent* hp = gethostbyname(host);
			if (hp != 0)
			{
				memcpy(dest, hp->h_addr, hp->h_length);
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	void read_tokens(std::string token_path, std::string& tokens)
	{
            if(os_directory_exists(os_file_prefix(token_path)))
            {
		std::vector<SFile> token_files = getFiles(token_path);

		for(size_t i=0;i<token_files.size();++i)
		{
			if(token_files[i].isdir)
			{
				continue;
			}

			std::string nt = getFile(token_path + os_file_sep() + token_files[i].name);
			if(!nt.empty())
			{
				if(!tokens.empty())
				{
					tokens+=";";
				}
				tokens+=nt;
			}
		}
            }
	}
}


std::string Connector::getResponse(const std::string &cmd, const std::string &args, bool change_command)
{
#ifdef _WIN32
    {
	 WSADATA wsaData = {0};
	int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (rc != 0) {
            wprintf(L"WSAStartup failed: %d\n", rc);
            return "";
        }
    }
#endif


	busy=true;
	error=false;
	std::string pw;
	if(!change_command)
	{
		pw=trim(getFile(pwfile));
	}
	else
	{
		pw=trim(getFile(pwfile_change));
	}

	SOCKET p=socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in addr;
	memset(&addr,0,sizeof(sockaddr_in));
	if(!LookupBlocking(client, &addr.sin_addr))
	{
		std::cout << "Error while getting hostname for '" << client << "'" << std::endl;
		return "";
	}
	addr.sin_family=AF_INET;
	addr.sin_port=htons(35623);

        int rc=connect(p, (sockaddr*)&addr, sizeof(sockaddr));

	if(rc==SOCKET_ERROR)
		return "";

	std::string t_args;

	if(!args.empty())
		t_args="&"+args;
	else
		t_args=args;

	CTCPStack tcpstack;
	tcpstack.Send(p, cmd+"#pw="+pw+t_args);

	char *resp=NULL;
	char buffer[1024];
	size_t packetsize;
	while(resp==NULL)
	{
                int rc=recv(p, buffer, 1024, MSG_NOSIGNAL);
		
                if(rc<=0)
		{
			closesocket(p);
			error=true;
			busy=false;
			return "";
		}
		tcpstack.AddData(buffer, rc );
		

		resp=tcpstack.getPacket(&packetsize);
		if(packetsize==0)
		{
			closesocket(p);
			busy=false;
			return "";
		}
	}

	std::string ret;
	ret.resize(packetsize);
	memcpy(&ret[0], resp, packetsize);
	delete resp;

	closesocket(p);

	busy=false;
	return ret;
}

bool Connector::hasError(void)
{
	return error;
}

std::vector<SBackupDir> Connector::getSharedPaths(void)
{
	std::vector<SBackupDir> ret;
	std::string d=getResponse("GET BACKUP DIRS","",false);
	int lc=linecount(d);
	for(int i=0;i<lc;i+=2)
	{
		SBackupDir bd;
		bd.id=atoi(getline(i, d).c_str() );
		bd.path=getline(i+1, d);
		ret.push_back( bd );
	}
	return ret;
}

bool Connector::saveSharedPaths(const std::vector<SBackupDir> &res)
{
	std::string args;
	for(size_t i=0;i<res.size();++i)
	{
		if(i!=0)
			args+="&";

		args+="dir_"+convert(i)+"="+(std::string)res[i].path;
	}

	std::string d=getResponse("SAVE BACKUP DIRS", args, false);

	if(d!="OK")
		return false;
	else
		return true;
}

SStatus Connector::getStatus(void)
{
	std::string d=getResponse("STATUS","",false);

	std::vector<std::string> toks;
	Tokenize(d, toks, "#");

	SStatus ret;
	ret.pause=false;
	if(toks.size()>0)
		ret.lastbackupdate=toks[0];
	if(toks.size()>1)
		ret.status=toks[1];
	if(toks.size()>2)
		ret.pcdone=toks[2];
	if(toks.size()>3)
	{
		if(toks[3]=="P")
			ret.pause=true;
		else if(toks[3]=="NP")
			ret.pause=false;
	}


	return ret;
}

int Connector::startBackup(bool full)
{
	std::string s;
	if(full)
		s="START BACKUP FULL";
	else
		s="START BACKUP INCR";

	std::string d=getResponse(s,"",false);

	if(d=="RUNNING")
		return 2;
	else if(d=="NO SERVER")
		return 3;
	else if(d!="OK")
		return 0;
	else
		return 1;
}

int Connector::startImage(bool full)
{
	std::string s;
	if(full)
		s="START IMAGE FULL";
	else
		s="START IMAGE INCR";

	std::string d=getResponse(s,"",false);

	if(d=="RUNNING")
		return 2;
	else if(d=="NO SERVER")
		return 3;
	else if(d!="OK")
		return 0;
	else
		return 1;
}

bool Connector::updateSettings(const std::string &ndata)
{
	std::string data=ndata;
	escapeClientMessage(data);
	std::string d=getResponse("UPDATE SETTINGS "+data,"", true);

	if(d!="OK")
		return false;
	else
		return true;
}

std::vector<SLogEntry> Connector::getLogEntries(void)
{
	std::string d=getResponse("GET LOGPOINTS","", true);
	int lc=linecount(d);
	std::vector<SLogEntry> ret;
	for(int i=0;i<lc;++i)
	{
		std::string l=getline(i, d);
		if(l.empty())continue;
		SLogEntry le;
		le.logid=atoi(getuntil("-", l).c_str() );
		std::string lt=getafter("-", l);
		le.logtime=lt;
		ret.push_back(le);
	}
	return ret;
}

std::vector<SLogLine>  Connector::getLogdata(int logid, int loglevel)
{
	std::string d=getResponse("GET LOGDATA","logid="+convert(logid)+"&loglevel="+convert(loglevel), true);
	std::vector<std::string> lines;
	TokenizeMail(d, lines, "\n");
	std::vector<SLogLine> ret;
	for(size_t i=0;i<lines.size();++i)
	{
		std::string l=lines[i];
		if(l.empty())continue;
		SLogLine ll;
		ll.loglevel=atoi(getuntil("-", l).c_str());
		ll.msg=getafter("-", l);
		ret.push_back(ll);
	}
	return ret;
}

bool Connector::setPause(bool b_pause)
{
	std::string data=b_pause?"true":"false";
	std::string d=getResponse("PAUSE "+data,"", false);

	if(d!="OK")
		return false;
	else
		return true;
}

bool Connector::isBusy(void)
{
	return busy;
}

void Connector::setPWFile(const std::string &pPWFile)
{
	pwfile=pPWFile;
}


void Connector::setPWFileChange( const std::string &pPWFile )
{
	pwfile_change=pPWFile;
}


void Connector::setClient(const std::string &pClient)
{
	client=pClient;
}

std::string Connector::getStatusDetail()
{
	return getResponse("STATUS DETAIL", "", false);
}

std::string Connector::getFileBackupsList(bool& no_server)
{
	no_server=false;

	if(!readTokens())
	{
		return std::string();
	}

	std::string list = getResponse("GET FILE BACKUPS TOKENS", "tokens="+tokens, false);

	if(!list.empty())
	{
		if(list[0]!='0')
		{
			if(list[0]=='1')
			{
				no_server=true;
			}

			return std::string();
		}
		else
		{
			return list.substr(1);
		}
	}
	else
	{
		return std::string();
	}
}

bool Connector::readTokens()
{
	if(!tokens.empty())
	{
		return true;
	}

	read_tokens("tokens", tokens);

#if !defined(_WIN32) && !defined(__APPLE__)
	read_tokens("/var/urbackup/tokens", tokens);
	read_tokens("/usr/local/var/urbackup/tokens", tokens);
#endif

#ifdef __APPLE__
	read_tokens("/usr/var/urbackup/tokens", tokens);
#endif

	return !tokens.empty();
}

std::string Connector::getFileList( const std::string& path, int* backupid, bool& no_server)
{
	no_server=false;

	if(!readTokens())
	{
		return std::string();
	}

	std::string params = "tokens="+tokens;

	if(!path.empty())
	{
		params+="&path="+EscapeParamString(path);
	}

	if(backupid!=NULL)
	{
		params+="&backupid="+convert(*backupid);
	}

	std::string list = getResponse("GET FILE LIST TOKENS",
		params, false);

	if(!list.empty())
	{
		if(list[0]!='0')
		{
			if(list[0]=='1')
			{
				no_server=true;
			}

			return std::string();
		}
		else
		{
			return list.substr(1);
		}
	}
	else
	{
		return std::string();
	}
}

std::string Connector::startRestore( const std::string& path, int backupid, const std::vector<SPathMap>& map_paths, bool& no_server )
{
	no_server=false;

	if(!readTokens())
	{
		return std::string();
	}

	std::string params = "tokens="+tokens;
	params+="&path="+EscapeParamString(path);
	params+="&backupid="+convert(backupid);

	for (size_t i = 0; i < map_paths.size(); ++i)
	{
		params += "&map_path_source"+convert(i)+"=" + EscapeParamString(map_paths[i].source);
		params += "&map_path_target" + convert(i) + "=" + EscapeParamString(map_paths[i].target);
	}

	std::string res = getResponse("DOWNLOAD FILES TOKENS",
		params, false);

	if(!res.empty())
	{
		if(res[0]!='0')
		{
			if(res[0]=='1')
			{
				no_server=true;
			}

			return std::string();
		}
		else
		{
			return res.substr(1);
		}
	}
	else
	{
		return std::string();
	}
}