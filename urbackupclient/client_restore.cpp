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

#include <string>
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../common/data.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "client_restore.h"
#include "client_restore_http.h"
#include <iostream>
#include <stdlib.h>
#include <memory.h>
#include <algorithm>
#include <thread>
#include "../urbackupcommon/fileclient/socket_header.h"
#ifndef _WIN32
#include <net/if.h>
#include <sys/ioctl.h>
#else
#include <ws2tcpip.h>
#endif
#include "../urbackupcommon/mbrdata.h"
#include "../fileservplugin/settings.h"
#include "../fileservplugin/packet_ids.h"
#include <memory>
#include "../cryptoplugin/ICryptoFactory.h"
#include "../fsimageplugin/IFSImageFactory.h"

#ifndef _WIN32
#include "../config.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif

#ifndef BLKRRPART
#define BLKRRPART  _IO(0x12,95)
#endif

#endif

using namespace restore;

#ifdef _WIN32
const std::string pw_file="pw.txt";
#else
const std::string pw_file="urbackup/pw.txt";
#endif

const std::string configure_wlan="wicd-curses";
const std::string configure_networkcard=configure_wlan;

#define UDP_SOURCE_PORT 35623

extern ICryptoFactory *crypto_fak;
extern IFSImageFactory* image_fak;

namespace
{
	bool rereadPartitionLayout(const std::string& devfn)
	{
		bool ret = true;
#ifndef _WIN32
		int fd = open(devfn.c_str(), O_NONBLOCK | O_RDWR);
		if (fd == -1)
			return false;

		int rc = ioctl(fd, BLKRRPART, NULL);

		if (rc == -1)
		{
			ret = false;
			Server->Log("Error re-reading partition layout. " + os_last_error_str(), LL_WARNING);
		}

		close(fd);
#endif
		return ret;
	}
}

namespace restore
{
	std::string trim2(const std::string& str)
	{
		size_t startpos = str.find_first_not_of(" \t\n");
		size_t endpos = str.find_last_not_of(" \t\n");
		if (std::string::npos == startpos || std::string::npos == endpos)
		{
			return "";
		}
		else
		{
			return str.substr(startpos, endpos - startpos + 1);
		}
	}

	std::string getResponse(IPipe* c)
	{
		CTCPStack tcpstack;
		char* resp = nullptr;
		char buffer[1024];
		size_t packetsize;
		while (resp == nullptr)
		{
			size_t rc = c->Read(buffer, 1024, 60000);
			if (rc == 0)
			{
				return "";
			}
			tcpstack.AddData(buffer, rc);

			resp = tcpstack.getPacket(&packetsize);
			if (resp != nullptr && packetsize == 0)
			{
				delete[]resp;
				return "";
			}
		}

		std::string ret;
		ret.resize(packetsize);
		memcpy(&ret[0], resp, packetsize);
		delete[]resp;
		return ret;
	}

	std::string restorePw()
	{
		return getFile(pw_file);
	}

	std::vector<std::string> getBackupclients(int& ec)
	{
		std::string pw = getFile(pw_file);
		CTCPStack tcpstack;
		std::vector<std::string> ret;
		ec = 0;

		IPipe* c = Server->ConnectStream("localhost", 35623, 60000);
		if (c == nullptr)
		{
			Server->Log("Error connecting to client service -1", LL_ERROR);
			ec = 10;
			return ret;
		}

		tcpstack.Send(c, "GET BACKUPCLIENTS#pw=" + pw);
		std::string r = getResponse(c);
		if (r.empty())
		{
			Server->Log("No response from ClientConnector", LL_ERROR);
			ec = 1;
		}
		else
		{
			if (r[0] == '0')
			{
				Server->Log("No backupserver found", LL_ERROR);
				ec = 2;
			}
			else
			{
				std::vector<std::string> toks;
				std::string t = r.substr(1);
				Tokenize(t, toks, "\n");
				for (size_t i = 0; i < toks.size(); ++i)
				{
					std::string nam = trim2(getafter("|", toks[i]));
					if (!nam.empty())
					{
						if (std::find(ret.begin(), ret.end(), nam) == ret.end())
						{
							ret.push_back(nam);
						}
					}
				}
			}
		}
		Server->destroy(c);
		return ret;
	}

	IPipe* connectToService(int& ec)
	{
		IPipe* c = Server->ConnectStream("localhost", 35623, 60000);
		if (c == nullptr)
		{
			Server->Log("Error connecting to client service -1", LL_ERROR);
			ec = 10;
			return nullptr;
		}

		return c;
	}

	IPipe* connectToService()
	{
		int ec = 0;
		return connectToService(ec);
	}
}



struct SPasswordSalt
{
	std::string salt;
	std::string rnd;
	int pbkdf2_rounds;
};

std::vector<SPasswordSalt> getSalt(const std::string& username, int tries, int *ec)
{
	std::string pw=getFile(pw_file);
	CTCPStack tcpstack;
	*ec=0;
	std::vector<SPasswordSalt> ret;

	IPipe *c=connectToService(*ec);
	if(c==nullptr)
	{
		return ret;
	}

	bool has_ok = false;
	tcpstack.Send(c, "GET SALT#pw="+pw+"&username="+username);
	std::string r=getResponse(c);
	if(r.empty() )
	{
		Server->Log("No response from ClientConnector", LL_ERROR);
		*ec=1;
	}
	else
	{
		Server->Log("Salt Response: "+r, LL_INFO);

		std::vector<std::string> toks;
		Tokenize(r, toks, "/");

		for(size_t i=0;i<toks.size();++i)
		{
			if(toks[i].find("ok;")==0)
			{
				has_ok = true;
				std::vector<std::string> salt_toks;
				Tokenize(toks[i], salt_toks, ";");

				SPasswordSalt new_salts;
				new_salts.pbkdf2_rounds=0;

				if(salt_toks.size()==3)
				{
					new_salts.salt = salt_toks[1];
					new_salts.rnd = salt_toks[2];
				}
				else if(salt_toks.size()==4)
				{
					new_salts.salt = salt_toks[1];
					new_salts.rnd = salt_toks[2];
					new_salts.pbkdf2_rounds = atoi(salt_toks[3].c_str());
				}

				ret.push_back(new_salts);
			}
			else
			{
				Server->Log("SALT error: "+toks[i], LL_ERROR);
				Server->Log("Username not found on server", LL_ERROR);
				SPasswordSalt new_salts = {};
				ret.push_back(new_salts);
				tries = 0;
			}
		}
	}
	Server->destroy(c);

	if (!has_ok && tries>0)
	{
		Server->wait(10000);
		return getSalt(username, tries - 1, ec);
	}

	return ret;
}

bool tryLogin(const std::string& username, const std::string& password, std::vector<SPasswordSalt> salts, int tries, int *ec)
{
	std::string pw=getFile(pw_file);
	CTCPStack tcpstack;
	*ec=0;

	IPipe *c=connectToService(*ec);
	if(c==nullptr)
	{
		return false;
	}

	std::string auth_str;
	if(!username.empty())
	{
		auth_str="&username="+username;

		for(size_t i=0;i<salts.size();++i)
		{
			if(!salts[i].salt.empty()
				&& !salts[i].rnd.empty())
			{

				std::string pw_md5 = Server->GenerateHexMD5(salts[i].salt+password);

				if(salts[i].pbkdf2_rounds>0)
				{
					pw_md5 = strlower(crypto_fak->generatePasswordHash(hexToBytes(pw_md5), salts[i].salt, salts[i].pbkdf2_rounds));
				}

				auth_str+="&password"+convert(i)+"="+
					Server->GenerateHexMD5(salts[i].rnd+pw_md5);
			}
		}
	}
	
	bool do_retry = false;
	bool ret=false;
	tcpstack.Send(c, "LOGIN FOR DOWNLOAD#pw="+pw+auth_str);
	std::string r=getResponse(c);
	if(r.empty() )
	{
		Server->Log("No response from ClientConnector", LL_ERROR);
		*ec=1;
	}
	else
	{
		Server->Log("Login Response: "+r, LL_INFO);

		if(r=="ok")
		{
			ret=true;
		}
		else if (r == "no channels available")
		{
			ret = true;
			do_retry = true;
		}
		else
		{
			if (r == "err")
			{
				tries = 0;
			}
			Server->Log("Error during login: "+r, LL_ERROR);
		}
	}
	Server->destroy(c);

	if ((!ret || do_retry) && tries>0)
	{
		Server->wait(10000);
		return tryLogin(username, password, salts, tries - 1, ec);
	}
	return ret;
}



std::vector<SImage> getBackupimages(std::string clientname, int *ec)
{
	std::string pw=getFile(pw_file);
	CTCPStack tcpstack;
	std::vector<SImage> ret;
	*ec=0;

	IPipe *c=Server->ConnectStream("localhost", 35623, 60000);
	if(c==nullptr)
	{
		Server->Log("Error connecting to client service -1", LL_ERROR);
		*ec=10;
		return ret;
	}

	tcpstack.Send(c, "GET BACKUPIMAGES "+clientname+"#pw="+pw);
	std::string r=getResponse(c);
	if(r.empty() )
	{
		Server->Log("No response from ClientConnector", LL_ERROR);
		*ec=1;
	}
	else
	{
		if(r[0]=='0')
		{
			Server->Log("No backupserver found", LL_ERROR);
			*ec=1;
		}
		else
		{
			ret = parse_backup_images_output(r.substr(1));
		}
	}
	Server->destroy(c);
	return ret;
}

struct SFileBackup
{
	bool operator<(const SFileBackup &other) const
	{
		return other.time_s<time_s;
	}
	std::string time_str;
	_i64 time_s;
	int id;
	int tgroup;
};

std::vector<SFileBackup> getFileBackups(std::string clientname, int *ec)
{
	std::string pw=getFile(pw_file);
	CTCPStack tcpstack;
	std::vector<SFileBackup> ret;
	*ec=0;

	IPipe *c=Server->ConnectStream("localhost", 35623, 60000);
	if(c==nullptr)
	{
		Server->Log("Error connecting to client service -1", LL_ERROR);
		*ec=10;
		return ret;
	}

	tcpstack.Send(c, "GET FILE BACKUPS "+clientname+"#pw="+pw);
	std::string r=getResponse(c);
	if(r.empty() )
	{
		Server->Log("No response from ClientConnector", LL_ERROR);
		*ec=1;
	}
	else
	{
		if(r[0]=='0')
		{
			Server->Log("No backupserver found", LL_ERROR);
			*ec=1;
		}
		else
		{
			std::vector<std::string> toks;
			std::string t=r.substr(1);
			Tokenize(t, toks, "\n");
			for(size_t i=0;i<toks.size();++i)
			{
				std::vector<std::string> t2;
				Tokenize(toks[i], t2, "|");
				if(t2.size()==4  )
				{
					SFileBackup si;
					si.id=atoi(t2[0].c_str());
					si.time_s=os_atoi64(t2[1]);
					si.time_str=t2[2];
					si.tgroup=atoi(t2[3].c_str());
					ret.push_back(si);
				}
			}
		}
	}
	Server->destroy(c);
	return ret;
}

volatile bool restore_retry_ok=false;

namespace restore
{
	EDownloadResult retryDownload(EDownloadResult errrc, int img_id, std::string img_time, std::string outfile,
		bool mbr, LoginData login_data, DownloadStatus& dl_status, int recur_depth, int64* o_imgsize, int64* o_output_file_size)
	{
		if (recur_depth == 0)
		{
			Server->Log("Read Timeout: Retrying -1", LL_WARNING);

			int total_tries = 20;
			int tries = total_tries;
			EDownloadResult rc;
			do
			{
				Server->wait(30000);
				int64 starttime = Server->getTimeMS();
				rc = downloadImage(img_id, img_time, outfile, mbr, login_data, dl_status, recur_depth + 1, o_imgsize, o_output_file_size);
				if (rc == EDownloadResult_Ok)
				{
					return rc;
				}
				--tries;
				if (Server->getTimeMS() - starttime > 180000)
				{
					tries = total_tries;
				}

				if (rc == EDownloadResult_SizeReadError)
				{
					do_login(login_data);
					Server->wait(10000);
				}
			} while (tries > 0);
		}
		Server->Log("Read Timeout.", LL_ERROR);
		return errrc;
	}

	EDownloadResult downloadImage(int img_id, std::string img_time, std::string outfile, bool mbr, LoginData login_data,
		DownloadStatus& dl_status, int recur_depth, int64* o_imgsize, int64* o_output_file_size)
	{
		std::string pw = getFile(pw_file);
		CTCPStack tcpstack;
		std::vector<SImage> ret;

		std::unique_ptr<IPipe> client_pipe(Server->ConnectStream("localhost", 35623, 60000));
		if (client_pipe.get() == nullptr)
		{
			Server->Log("Error connecting to client service -1", LL_ERROR);
			return EDownloadResult_ConnectError;
		}

		std::string s_offset;
		if (dl_status.offset != -1)
		{
			s_offset = "&offset=" + convert(dl_status.offset) + "&received_bytes=" + convert(dl_status.received);
		}

		std::string dl_args;
		if (img_id != 0 || !img_time.empty())
		{
			dl_args = "&img_id=" + convert(img_id) + "&time=" + img_time;
		}
		else if (login_data.has_login_data
			&& !login_data.token.empty())
		{
			dl_args = "&token=" + EscapeParamString(login_data.token);
		}

		tcpstack.Send(client_pipe.get(), "DOWNLOAD IMAGE#pw=" + pw + dl_args + "&mbr=" + convert(mbr) + s_offset);

		std::string restore_out = outfile;
		Server->Log("Restoring to " + restore_out);
		std::unique_ptr<IFile> out_file(Server->openFile(restore_out, MODE_RW_READNONE));
		if (out_file.get() == nullptr)
		{
			Server->Log("Could not open \"" + restore_out + "\" for writing. "+os_last_error_str(), LL_ERROR);
			return EDownloadResult_OpenError;
		}
		else if (o_output_file_size != nullptr)
		{
			*o_output_file_size = out_file->Size();
		}

		_i64 imgsize = -1;
		client_pipe->Read((char*)&imgsize, sizeof(_i64), 60000);

		if (o_imgsize != nullptr)
		{
			*o_imgsize = imgsize;
		}

		if (imgsize == -1)
		{
			Server->Log("Error reading size", LL_ERROR);
			return EDownloadResult_SizeReadError;
		}
		if (imgsize == -2)
		{
			Server->Log("Connection timeout", LL_ERROR);
			EDownloadResult rc = retryDownload(EDownloadResult_TimeoutError1, img_id, img_time,
				outfile, mbr, login_data, dl_status, recur_depth, o_imgsize, o_output_file_size);
			return rc;
		}

		if (!mbr && imgsize > out_file->Size())
		{
			Server->Log("Output device too small. File size = " + convert(out_file->Size()) + " needed = " + convert(imgsize), LL_ERROR);
			return EDownloadResult_DeviceTooSmall;
		}

		const size_t c_buffer_size = 32768;
		const unsigned int c_block_size = 4096;

		char buf[c_buffer_size];
		if (mbr == true)
		{
			_i64 read = 0;
			while (read < imgsize)
			{
				size_t c_read = client_pipe->Read(buf, c_buffer_size, 180000);
				if (c_read == 0)
				{
					EDownloadResult rc = retryDownload(EDownloadResult_TimeoutError2, img_id, img_time, outfile,
						mbr, login_data, dl_status, recur_depth, o_imgsize, o_output_file_size);
					return rc;
				}
				out_file->Write(buf, (_u32)c_read);
				read += c_read;
			}
			return EDownloadResult_Ok;
		}
		else
		{
			_i64 read = 0;
			unsigned int blockleft = 0;
			unsigned int off = 0;
			char blockdata[c_block_size];
			bool first = true;
			bool has_data = false;
			_i64 pos = 0;
			while (pos < imgsize)
			{
				size_t r = client_pipe->Read(&buf[off], c_buffer_size - off, 180000);
				if (r != 0)
					r += off;
				off = 0;
				if (r == 0)
				{
					Server->Log("Read Timeout: Retrying", LL_WARNING);
					client_pipe.reset(nullptr);
					out_file.reset(nullptr);
					if (has_data)
					{
						return retryDownload(EDownloadResult_TimeoutError2, img_id, img_time,
							outfile, mbr, login_data, dl_status, recur_depth, o_imgsize, o_output_file_size);
					}
					else
					{
						Server->Log("Read Timeout: No data", LL_ERROR);
						return EDownloadResult_TimeoutError2;
					}
				}
				while (true)
				{
					if (blockleft == 0)
					{
						if (!first)
						{
							//Only write one block
							_u32 tw = c_block_size;
							//Don't write over blockdev boundary
							if (imgsize >= pos && imgsize - pos < c_block_size)
								tw = (_u32)(imgsize - pos);

							//Write to blockdev
							_u32 woff = 0;
							do
							{
								bool has_write_error = false;
								_u32 w = out_file->Write(&blockdata[woff], tw - woff, &has_write_error);
								if (w == 0)
								{
									Server->Log("Writing to output file failed", LL_ERROR);
									return EDownloadResult_WriteFailed;
								}
								if (has_write_error)
								{
									Server->Log("Writing to output file failed -2", LL_ERROR);
									return EDownloadResult_WriteFailed;
								}
								woff += w;
							} while (tw - woff > 0);

							if (pos > dl_status.offset)
							{
								dl_status.offset = pos;
							}

							has_data = true;
						}
						else
						{
							first = false;

							if (pos != -1 && !restore_retry_ok)
							{
								restore_retry_ok = true;
							}
						}

						if (r - off >= sizeof(_i64))
						{
							blockleft = c_block_size;
							_i64* s = reinterpret_cast<_i64*>(&buf[off]);
							if (*s == 0x7fffffffffffffffLL)
							{
								Server->Log("Restore finished", LL_INFO);
								pos = *s;
								break;
							}
							if (*s > imgsize)
							{
								Server->Log("Invalid seek value: " + convert(*s), LL_ERROR);
								//TODO return error code here after deprecation period
								pos = *s;
								break;
							}
							else if (*s < pos)
							{
								Server->Log("Position out of order!", LL_ERROR);
							}
							else
							{
								if (!out_file->Seek(*s))
								{
									Server->Log("Seeking in output file failed (to position " + convert(*s) + ")", LL_ERROR);
									return EDownloadResult_WriteFailed;
								}
								pos = *s;
							}
							off += sizeof(_i64);
						}
						else if (r - off > 0)
						{
							memmove(buf, &buf[off], r - off);
							off = (_u32)r - off;
							break;
						}
						else
						{
							off = 0;
							break;
						}
					}
					else
					{
						unsigned int available = (std::min)((unsigned int)r - off, blockleft);
						if (available > 0)
						{
							memcpy(&blockdata[c_block_size - blockleft], &buf[off], (_u32)available);
						}
						read += available;
						blockleft -= available;
						off += available;
						dl_status.received += available;
						if (off >= r)
						{
							off = 0;
							break;
						}
					}
				}
			}

			return EDownloadResult_Ok;
		}
		return EDownloadResult_Ok;
	}

	int downloadFiles(int backupid, std::string backup_time)
	{
		std::string pw=getFile(pw_file);
		CTCPStack tcpstack;
		std::vector<SImage> ret;

		std::unique_ptr<IPipe> client_pipe(Server->ConnectStream("localhost", 35623, 60000));
		if(client_pipe.get()==nullptr)
		{
			Server->Log("Error connecting to client service -1", LL_ERROR);
			return 10;
		}

		tcpstack.Send(client_pipe.get(), "DOWNLOAD FILES#pw="+pw+"&backupid="+convert(backupid)+"&time="+backup_time);
		int64 starttime = Server->getTimeMS();

		while(Server->getTimeMS()-starttime<60000)
		{
			std::string msg;
			if(client_pipe->Read(&msg, 60000)>0)
			{
				tcpstack.AddData(&msg[0], msg.size());

				if(tcpstack.getPacket(msg) )
				{
					if(msg=="ok")
					{
						return 0;
					}
					else
					{
						Server->Log("Error: "+msg, LL_ERROR);
						return 1;
					}

					break;
				}
			}
			else
			{
				break;
			}
		}

		Server->Log("Timeout", LL_ERROR);
		return 2;
	}

} //namespace restore

namespace restore
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
			addrinfo hints;
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_protocol = IPPROTO_TCP;
			hints.ai_socktype = SOCK_STREAM;

			addrinfo* hp;
			if(getaddrinfo(host, nullptr, &hints, &hp)==0 && hp!=nullptr)
			{
				if(hp->ai_addrlen<=sizeof(sockaddr_in))
				{
					memcpy(dest, &reinterpret_cast<sockaddr_in*>(hp->ai_addr)->sin_addr, sizeof(in_addr));
					freeaddrinfo(hp);
					return true;
				}
				else
				{
					freeaddrinfo(hp);
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	bool ping_server(void)
	{
		int type = SOCK_DGRAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
		type |= SOCK_CLOEXEC;
#endif
		SOCKET udpsock=socket(AF_INET, type,0);

#if !defined(_WIN32) && !defined(SOCK_CLOEXEC)
		fcntl(udpsock, F_SETFD, fcntl(udpsock, F_GETFD, 0) | FD_CLOEXEC);
#endif

		std::string server=Server->getServerParameter("ping_server");

		if(server.empty())
			return false;
		sockaddr_in server_addr;
		if(!LookupBlocking(server, &server_addr.sin_addr))
			return false;

		server_addr.sin_family=AF_INET;
        server_addr.sin_port=htons(UDP_SOURCE_PORT);

		std::string ping_clientname=getFile("clientname.txt");

		std::vector<char> buffer;
		buffer.resize(ping_clientname.size()+2);
		buffer[0]=ID_PONG;
		buffer[1]=FILESERV_VERSION;
		memcpy(&buffer[2], ping_clientname.c_str(), ping_clientname.size());
		
		while(true)
		{
			int rc = sendto(udpsock, &buffer[0], static_cast<int>(buffer.size()), 0, (sockaddr*)&server_addr, sizeof(server_addr));
			if(rc == -1)
			{
				Server->Log("Error sending to UrBackup server on UDP socket", LL_ERROR);
				return false;
			}
			Server->wait(10000);
		}

		return true;
	}
	
	std::vector<SLsblk> lsblk(const std::string& dev)
	{
		int rc = system(("lsblk -x MAJ:MIN -o MAJ:MIN,MODEL,SIZE,TYPE,PATH -P "+dev+" 1> out").c_str());
		
		std::vector<SLsblk> ret;
		
		if(rc!=0)
		{
			Server->Log("Error while running 'lsblk'", LL_ERROR);
			return ret;
		}
		
		std::vector<std::string> lines;
		Tokenize(getFile("out"), lines, "\n");
		
		for(size_t i=0;i<lines.size();++i)
		{
			SLsblk c;
			c.maj_min = getbetween("MAJ:MIN=\"", "\"", lines[i]);
			c.model = getbetween("MODEL=\"", "\"", lines[i]);
			c.size = getbetween("SIZE=\"", "\"", lines[i]);
			c.type = getbetween("TYPE=\"", "\"", lines[i]);
			c.path = getbetween("PATH=\"", "\"", lines[i]);
			
			if(next(c.path, 0, "/dev/fd"))
				continue;
			
			ret.push_back(c);
		}
		
		return ret;
	}
	
	std::string getPartitionPath(const std::string& dev, int partnum)
	{
		std::vector<SLsblk> parts = lsblk(dev);

		std::map<size_t, SLsblk> parts_maj_min;

		for(auto& it: parts)
		{
			if(it.type=="part") 
			{
				int maj = watoi(getuntil(":", it.maj_min));
				int min = watoi(getafter(":", it.maj_min));
				parts_maj_min[maj*100000ULL + min] = it;
			}
		}

		int idx=1;
		for(auto it: parts_maj_min)
		{
			if(idx==partnum)
			{
				return it.second.path;
			}
			++idx;
		}
		
		return std::string();
	}
}

namespace restore
{
	std::vector<SImage> parse_backup_images_output(const std::string& data)
	{
		std::vector<SImage> ret;
		std::vector<std::string> toks;
		Tokenize(data, toks, "\n");
		for (size_t i = 0; i < toks.size(); ++i)
		{
			std::vector<std::string> t2;
			Tokenize(toks[i], t2, "|");
			if (t2.size() == 3 || (t2.size() == 4 && t2[0] != "#"))
			{
				SImage si;
				si.id = atoi(t2[0].c_str());
				si.time_s = os_atoi64(t2[1]);
				si.time_str = t2[2];
				if (t2.size() == 4)
				{
					si.letter = t2[3];
				}
				ret.push_back(si);
			}
			else if ( (t2.size() == 4 || t2.size() == 5) &&
				t2[0] == "#" && !ret.empty())
			{
				SImage si;
				si.id = atoi(t2[1].c_str());
				si.time_s = os_atoi64(t2[2]);
				si.time_str = t2[3];
				if (t2.size() == 5)
				{
					si.letter = t2[4];
				}
				ret[ret.size() - 1].assoc.push_back(si);
			}
		}
		return ret;
	}

	JSON::Array backup_images_output_to_json(const std::string& data)
	{
		std::vector<SImage> images = parse_backup_images_output(data);

		JSON::Array ret;
		for (size_t i = 0; i < images.size(); ++i)
		{
			JSON::Object image;
			image.set("id", images[i].id);
			image.set("time_s", images[i].time_s);
			image.set("time_str", images[i].time_str);
			image.set("letter", images[i].letter);

			JSON::Array assoc;
			for (size_t j = 0; j < images[i].assoc.size(); ++j)
			{
				JSON::Object image_assoc;
				image_assoc.set("id", images[i].assoc[j].id);
				image_assoc.set("time_s", images[i].assoc[j].time_s);
				image_assoc.set("time_str", images[i].assoc[j].time_str);
				image_assoc.set("letter", images[i].assoc[j].letter);
				assoc.add(image_assoc);
			}
			image.set("assoc", assoc);

			ret.add(image);
		}

		return ret;
	}

	std::string backup_images_output_to_json_str(const std::string& data)
	{
		return backup_images_output_to_json(data).stringify(false);
	}

	struct SgdiskPart
	{
		size_t number;
		int64 start;
		int64 end;
		std::string size;
		std::string code;
		std::string type_name;
		std::string part_guid;
		std::string unique_guid;
		std::string name;
		std::string flags;
	};

	std::vector<SgdiskPart> getSgdiskParts(const std::string& dev)
	{
		std::vector<SgdiskPart> ret;
		std::string out;
		int rc = os_popen("sgdisk -p \""+dev+"\" 2>&1", out);
		if(rc!=0)
			return ret;

		if(out.find("Number")==std::string::npos)
			return ret;

		std::string table = getafter("\n", getafter("Number", out));

		std::vector<std::string> lines;
		Tokenize(table, lines, "\n");

		for(auto& line: lines)
		{
			std::vector<std::string> cols;
			Tokenize(line, cols, " ");

			SgdiskPart new_part;
			new_part.number = 0;
			new_part.start = -1;
			new_part.end = -1;
			size_t idx=0;
			for(auto& col: cols)
			{
				std::string cd = trim(col);
				if(!cd.empty())
				{
					if(idx==0)					
						new_part.number = watoi(cd);
					else if(idx==1)
						new_part.start = watoi64(cd);
					else if(idx==2)
						new_part.end = watoi64(cd);
					else if(idx==3)
						new_part.size = cd;
					else if(idx==4)
						new_part.size += " "+ cd;
					else if(idx==5)
						new_part.code = cd;
					else 
						new_part.type_name += cd;
					++idx;
				}
			}

			if(new_part.start!=-1 && 
				new_part.number!=0 &&
				new_part.end!=-1)
			{
				std::string iout;
				rc = os_popen("sgdisk -i "+std::to_string(new_part.number)+" \""+dev+"\" 2>&1", iout);
				if(rc==0)
				{
					new_part.part_guid=trim(getbetween("Partition GUID code: ", " ", iout));
					if(new_part.part_guid.empty())
						new_part.part_guid=trim(getbetween("Partition GUID code: ", "\n", iout));

					new_part.unique_guid = trim(getbetween("Partition unique GUID: ", "\n", iout));
					new_part.flags = trim(getbetween("Attribute flags: ", "\n", iout));
					new_part.name = trim(getbetween("Partition name: '", "\n", iout));
					if(!new_part.name.empty() && new_part.name[new_part.name.size()-1]=='\'')
						new_part.name = new_part.name.substr(0, new_part.name.size()-1);
				}
				else
				{
					Server->Log("Getting detailed info for partition "+std::to_string(new_part.number)+" failed: "+iout, LL_ERROR);
				}

				ret.push_back(new_part);
			}
		}

		return ret;
	}

	std::string createSgdiskPart(const SgdiskPart& lp, const std::string& out_device)
	{
		std::string out;
		int rc = os_popen("sgdisk -n "+std::to_string(lp.number)+":"+std::to_string(lp.start)+":0 "
							"-A "+std::to_string(lp.number)+":=:"+lp.flags+" "
							"-c "+std::to_string(lp.number)+":\""+lp.name+"\" "
							"-t "+std::to_string(lp.number)+":"+lp.part_guid+" "
							"-u "+std::to_string(lp.number)+":"+lp.unique_guid+" "
							+out_device, out);

		if(rc!=0)
		{
			return "Error creating partition "+std::to_string(lp.number)+": "+trim(out);
		}

		return std::string();
	}

	int64 getSgdiskDiskSize(const std::string& dev)
	{
		std::string out;
		int rc = os_popen("sgdisk -p \""+dev+"\" 2>&1", out);
		if(rc!=0)
			return -1;

		std::string s = trim(getbetween("Disk "+dev+": ", " sectors", out));
		if(s.empty())
			return -1;

		return watoi64(s);
	}

	bool do_restore_write_mbr(const std::string& mbr_filename, const std::string& out_device,
		bool fix_gpt, std::string& errmsg)
	{
		IFile *f=Server->openFile(mbr_filename, MODE_READ);
		if(f==nullptr)
		{
			errmsg = "Could not open MBR file";
			return false;
		}
		size_t fsize=(size_t)f->Size();
		char *buf=new char[fsize];
		f->Read(buf, (_u32)fsize);
		Server->destroy(f);

		CRData mbr(buf, fsize);
		SMBRData mbrdata(mbr);
		if(mbrdata.hasError())
		{
			errmsg = "Error while parsing MBR data";
			delete []buf;
			return false;
		}

		IFile *dev=Server->openFile(out_device, MODE_RW);
		if(dev==nullptr)
		{
			errmsg = "Could not open device file for writing";
			delete []buf;
			return false;
		}
		dev->Seek(0);
		Server->Log("Writing MBR data...", LL_INFO);
		if (dev->Write(mbrdata.mbr_data) != mbrdata.mbr_data.size())
		{
			errmsg = "Writing MBR data failed. " + os_last_error_str();
			Server->Log(errmsg, LL_ERROR);
		}
		else
		{
			Server->Log("done.", LL_INFO);
		}

		bool curr_fix_gpt=false;
		if (mbrdata.gpt_style)
		{
			Server->Log("Writing GPT header...");
			if (dev->Write(mbrdata.gpt_header_pos, mbrdata.gpt_header) != mbrdata.gpt_header.size() )
			{
				errmsg = "Writing GPT header failed. " + os_last_error_str();
				Server->Log(errmsg, LL_ERROR);
			}

			Server->Log("Writing GPT table...");
			if (dev->Write(mbrdata.gpt_table_pos, mbrdata.gpt_table) != mbrdata.gpt_table.size())
			{
				errmsg = "Writing GPT table failed. " + os_last_error_str();
				Server->Log(errmsg, LL_ERROR);
			}

			if (mbrdata.backup_gpt_header_pos != -1)
			{
				Server->Log("Writing GPT backup header...");
				if (dev->Write(mbrdata.backup_gpt_header_pos, mbrdata.backup_gpt_header) != mbrdata.backup_gpt_header.size())
				{
					errmsg = "Writing GPT backup header failed. " + os_last_error_str();
					Server->Log(errmsg, LL_ERROR);
					curr_fix_gpt = true;
				}
			}

			if (mbrdata.backup_gpt_table_pos != -1)
			{
				Server->Log("Writing GPT backup table...");
				if (dev->Write(mbrdata.backup_gpt_table_pos, mbrdata.backup_gpt_table) != mbrdata.backup_gpt_table.size())
				{
					errmsg = "Writing backup GPT table failed. " + os_last_error_str();
					Server->Log(errmsg, LL_ERROR);
					curr_fix_gpt = true;
				}
			}
		}

		if (mbrdata.extra_data_pos != -1)
		{
			Server->Log("Writing extra data at position "+convert(mbrdata.extra_data_pos)+" size "+convert(mbrdata.extra_data.size())+" ...");
			if (dev->Write(mbrdata.extra_data_pos, mbrdata.extra_data) != mbrdata.extra_data.size())
			{
				errmsg = "Writing extra data failed. " + os_last_error_str();
				Server->Log(errmsg, LL_ERROR);
			}
		}

		if(curr_fix_gpt && fix_gpt)
		{
			std::string fix_output;
			int rc = os_popen("sgdisk -e " + out_device + " 2>&1", fix_output);
			errmsg="Fixed GPT backup table: "+trim(fix_output);
			if(fix_output.find("too big for the disk")!=std::string::npos)
			{
				std::vector<SgdiskPart> partitions = getSgdiskParts(out_device);

				int64 sgdisk_size = getSgdiskDiskSize(out_device);
				while(!partitions.empty())
				{
					if(sgdisk_size<0)
						break;

					SgdiskPart lp = partitions[partitions.size()-1];

					if(lp.start>=sgdisk_size)
					{
						Server->Log("Partition "+std::to_string(lp.number)+" not on disk. Removing...");
						std::string out;
						rc = os_popen("sgdisk -d "+std::to_string(lp.number)+" "+out_device+" 2>&1", out);

						if(rc!=0)
						{
							errmsg="Error deleting last partition (1): "+trim(out);
							Server->Log(errmsg, LL_ERROR);
						}
					}
					else
					{
						break;
					}
				}

				if(!partitions.empty())
				{
					Server->Log("Partition too big. Resizing last partition", LL_WARNING);
					SgdiskPart lp = partitions[partitions.size()-1];

					Server->Log("Last partition num="+std::to_string(lp.number)+" start="+std::to_string(lp.start)+" end="+std::to_string(lp.end)
						+" size="+lp.size, LL_INFO);

					std::string out;
					rc = os_popen("sgdisk -d "+std::to_string(lp.number)+" "+out_device+" 2>&1", out);

					if(rc!=0)
					{
						errmsg="Error deleting last partition: "+trim(out);
						Server->Log(errmsg, LL_ERROR);
					}

					std::string err = createSgdiskPart(lp, out_device);

					if(!err.empty())
					{
						errmsg="Error re-creating last partition: "+trim(err);
						Server->Log(errmsg, LL_ERROR);
					}
				}
				else
				{
					Server->Log("No partitions found", LL_ERROR);
				}
			}
		}

		Server->destroy(dev);

		if (out_device.find("/dev/") == 0)
		{
			rereadPartitionLayout(out_device);
		}

		delete []buf;

		return true;
	}
}

void do_restore(void)
{
	std::string cmd=Server->getServerParameter("restore_cmd");

	if(cmd=="write_mbr")
	{
		std::string mbr_filename=Server->getServerParameter("mbr_filename");
		std::string out_device=Server->getServerParameter("out_device");
		if(mbr_filename.empty())
		{
			Server->Log("MBR filename not specified (mbr_filename parameter)", LL_ERROR);
			exit(1);
		}

		if(out_device.empty())
		{
			Server->Log("Output device not specified (out_device paramter)", LL_ERROR);
			exit(1);
		}
		
		std::string errmsg;
		if(!do_restore_write_mbr(mbr_filename, out_device, false, errmsg))
		{
			Server->Log(errmsg, LL_ERROR);
			exit(2);
		}
		
		exit(0);
	}
	else if(cmd=="mbrinfo")
	{
		std::string mbr_filename=Server->getServerParameter("mbr_filename");
		if(mbr_filename.empty())
		{
			Server->Log("MBR filename not specified (mbr_filename parameter)", LL_ERROR);
			exit(1);
		}
		IFile *f=Server->openFile(mbr_filename, MODE_READ);
		if(f==nullptr)
		{
			Server->Log("Could not open MBR file", LL_ERROR);
			exit(1);
		}
		size_t fsize=(size_t)f->Size();
		char *buf=new char[fsize];
		f->Read(buf, (_u32)fsize);
		Server->destroy(f);

		CRData mbr(buf, fsize);
		SMBRData mbrdata(mbr);
		if(mbrdata.hasError())
		{
			Server->Log("Error while parsing MBR data", LL_ERROR);
			delete []buf; exit(1);
		}

		std::cout << mbrdata.infoString();
		delete []buf;
		exit(0);
	}
	else if (cmd == "login")
	{
		LoginData login_data;
		login_data.has_login_data = true;
		login_data.username = Server->getServerParameter("username");
		login_data.password = Server->getServerParameter("password");

		char* pw = getenv("LOGIN_PASSWORD");
		if (pw != nullptr)
		{
			login_data.password = pw;
		}
		if (do_login(login_data))
		{
			exit(0);
		}
		else
		{
			exit(1);
		}
	}
	else if (cmd == "read_mbr")
	{
		std::string dev_fn = Server->getServerParameter("device_fn");
		std::unique_ptr<IFile> dev(Server->openFile(dev_fn, MODE_READ_DEVICE));

		if (dev.get() == nullptr)
		{
			Server->Log("Error opening device at " + dev_fn+". "+os_last_error_str(), LL_ERROR);
			exit(5);
		}

		bool gpt_style;
		std::vector<IFSImageFactory::SPartition> partitions = image_fak->readPartitions(dev.get(), gpt_style);

		if (partitions.empty())
		{
			Server->Log("No partitions found", LL_ERROR);
			exit(4);
		}

		for (size_t i = 0; i < partitions.size(); ++i)
		{
			std::cout << "partition " << (i+1) << " off " << partitions[i].offset << " size " << partitions[i].length << std::endl;
		}

		exit(0);
	}
	else if(cmd=="resize_ntfs")
	{
		std::string dev_fn = Server->getServerParameter("device_fn");
		std::unique_ptr<IFile> dev(Server->openFile(dev_fn, MODE_READ_DEVICE));

		if (dev.get() == nullptr)
		{
			Server->Log("Error opening device at " + dev_fn+". "+os_last_error_str(), LL_ERROR);
			exit(5);
		}

		dev.reset();

		std::string s_new_size = Server->getServerParameter("new_size");
		if(s_new_size.empty())
		{
			Server->Log("new_size paramater missing", LL_ERROR);
			exit(6);
		}

		int64 new_size = watoi64(s_new_size);

		std::string err;
		std::atomic<int> complete_pc;
		std::atomic<bool> done(false);
		std::thread t( [&err, dev_fn, new_size, &complete_pc, &done]()
		{
			err = restore::resize_ntfs(new_size, dev_fn, complete_pc);
			done=true;
		});

		t.detach();

		int last_pc = complete_pc;
		while(!done)
		{
			Server->wait(1000);
			if(complete_pc!=last_pc)
			{
				last_pc = complete_pc;
				Server->Log("Resize "+std::to_string(last_pc)+"% complete", LL_INFO);
			}
		}

		if(!err.empty())
		{
			Server->Log("Resize error: "+err, LL_ERROR);
			exit(1);
		}

		exit(0);
	}
	else if(cmd=="get_sgparts")
	{
		std::string dev_fn = Server->getServerParameter("device_fn");

		Server->Log("Device size: "+PrettyPrintBytes(getSgdiskDiskSize(dev_fn)*512), LL_INFO);

		std::vector<SgdiskPart> parts = getSgdiskParts(dev_fn);

		for(const SgdiskPart& part: parts)
		{
			Server->Log("part number="+std::to_string(part.number)
				+" start="+std::to_string(part.start)+" end="+std::to_string(part.end)
				+" size="+part.size+" part_uuid="+part.part_guid+" unique_uuid="+part.unique_guid
				+" name=\""+part.name+"\" flags="+part.flags, LL_INFO);
		}

		exit(0);
	}
	else if(cmd=="help")
	{
		Server->Log("restore_cmd commands are...", LL_INFO);
		Server->Log("write_mbr(mbr_filename,out_device)", LL_INFO);
		Server->Log("get_clientnames", LL_INFO);
		Server->Log("get_backupimages(restore_name)", LL_INFO);
		Server->Log("get_backupimages_json(restore_name)", LL_INFO);
		Server->Log("get_file_backups(restore_name)", LL_INFO);
		Server->Log("download_mbr(restore_img_id,restore_time,restore_out)", LL_INFO);
		Server->Log("download_image(restore_img_id,restore_time,restore_out)", LL_INFO);
		Server->Log("download_files(restore_backupid,restore_time,restore_out)", LL_INFO);
		Server->Log("download_progress(mbr_filename,out_device)", LL_INFO);
		Server->Log("login(username,password)", LL_INFO);
		Server->Log("read_mbr(device_fn)", LL_INFO);
		Server->Log("resize_ntfs(device_fn,new_size)", LL_INFO);
		exit(0);
	}
	else if(cmd=="ping_server")
	{
		bool b=ping_server();
		exit(b?0:1);
	}

	IPipe *c=Server->ConnectStream("localhost", 35623, 60000);
	if(c==nullptr)
	{
		Server->Log("Error connecting to client service -1", LL_ERROR);
		exit(1);return;
	}

	std::string pw=getFile(pw_file);

	CTCPStack tcpstack;	
	if(cmd=="get_clientnames")
	{
		tcpstack.Send(c, "GET BACKUPCLIENTS#pw="+pw);
		std::string r=getResponse(c);
		if(r.empty() )
		{
			Server->Log("No response from ClientConnector", LL_ERROR);
			Server->destroy(c);exit(2);return;
		}
		else
		{
			if(r[0]=='0')
			{
				Server->Log("No backupserver found", LL_ERROR);
				Server->destroy(c);exit(3);return;
			}
			else
			{
				std::cout << r.substr(1) ;
				Server->destroy(c);exit(0);return;
			}
		}
	}
	else if(cmd=="get_backupimages"
		|| cmd == "get_backupimages_json")
	{
		tcpstack.Send(c, "GET BACKUPIMAGES "+Server->getServerParameter("restore_name")+"#pw="+pw);
		std::string r=getResponse(c);
		if(r.empty() )
		{
			Server->Log("No response from ClientConnector", LL_ERROR);
			Server->destroy(c);exit(2);return;
		}
		else
		{
			if(r[0]=='0')
			{
				Server->Log("No backupserver found", LL_ERROR);
				Server->destroy(c);exit(3);return;
			}
			else
			{
				if (cmd == "get_backupimages_json")
				{
					std::cout << backup_images_output_to_json_str(r.substr(1));
				}
				else
				{
					std::cout << r.substr(1);
				}
				Server->destroy(c);exit(0);return;
			}
		}
	}
	else if(cmd=="get_file_backups" )
	{
		tcpstack.Send(c, "GET FILE BACKUPS "+Server->getServerParameter("restore_name")+"#pw="+pw);
		std::string r=getResponse(c);
		if(r.empty() )
		{
			Server->Log("No response from ClientConnector", LL_ERROR);
			Server->destroy(c);exit(2);return;
		}
		else
		{
			if(r[0]=='0')
			{
				Server->Log("No backupserver found", LL_ERROR);
				Server->destroy(c);exit(3);return;
			}
			else
			{
				std::cout << r.substr(1) ;
				Server->destroy(c);exit(0);return;
			}
		}
	}
	else if(cmd=="download_mbr" || cmd=="download_image" )
	{
		bool mbr=false;
		if(cmd=="download_mbr")
			mbr=true;

		LoginData login_data;
		login_data.has_login_data = false;

		std::string restore_token = Server->getServerParameter("restore_token");
		if (!restore_token.empty())
		{
			login_data.has_login_data = true;
			login_data.token = restore_token;
		}

		DownloadStatus dl_status;
		int ec=downloadImage(atoi(Server->getServerParameter("restore_img_id").c_str()), Server->getServerParameter("restore_time"), Server->getServerParameter("restore_out"), mbr, login_data, dl_status);
		exit(ec);
	}
	else if(cmd=="download_files")
	{
		int ec=downloadFiles(atoi(Server->getServerParameter("restore_backupid").c_str()), Server->getServerParameter("restore_time"));
		exit(ec);
	}
	else if(cmd=="download_progress")
	{
		std::string decorate = Server->getServerParameter("decorate");

		tcpstack.Send(c, "GET DOWNLOADPROGRESS#pw="+pw);
		int lpc=0;
		while(true)
		{
			std::string curr;
			size_t r=c->Read(&curr, 10000);
			for(int i=0;i<linecount(curr);++i)
			{
				std::string l=getline(i, curr);
				if(!trim2(l).empty())
				{
					int npc=atoi(trim2(l).c_str());
					if(npc!=lpc)
					{
						if(decorate.empty())
							std::cout << npc << std::endl;
						else
							std::cout << "Image restore complete: "<< npc << "%" << std::endl;
					    lpc=npc;
					}
				}
			}
			if(r==0)
				break;
		}
		if(lpc!=100)
		{
			if (decorate.empty())
			    std::cout << "100" << std::endl;
			else
				std::cout << "Image restore complete: 100%" << std::endl;
		}

		exit(0);
	}
}

class RestoreThread : public IThread
{
public:
	RestoreThread(int pImg_id, std::string pImg_time, std::string pOutfile, LoginData login_data) 
		: img_id(pImg_id), img_time(pImg_time), outfile(pOutfile), imgsize(0), output_file_size(0), login_data(login_data)
	{
		done=false;
	}

	void operator()(void)
	{
		DownloadStatus dl_status;
		rc=downloadImage(img_id, img_time, outfile, false, login_data, dl_status, 0, &imgsize, &output_file_size);
		done=true;
	}

	bool isDone(void)
	{
		return done;
	}

	EDownloadResult getRC(void)
	{
		return rc;
	}

	int64 getImgsize()
	{
		return imgsize;
	}

	int64 getOutputFileSize()
	{
		return output_file_size;
	}

private:
	EDownloadResult rc;
	volatile bool done;
	int img_id;
	std::string img_time;
	std::string outfile;
	LoginData login_data;

	int64 imgsize;
	int64 output_file_size;
};

namespace restore
{
	bool has_network_device(void)
	{
#ifdef _WIN32
		return true;
#else
#ifdef sun
		return true;
#else
		char          buf[1024];
		struct ifconf ifc;
		struct ifreq* ifr;
		int           sck;
		int           nInterfaces;
		int           i;

		int type = SOCK_DGRAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
		type |= SOCK_CLOEXEC;
#endif
		/* Get a socket handle. */
		sck = socket(AF_INET, type, 0);
		if (sck < 0)
		{
			return true;
		}
#if !defined(_WIN32) && !defined(SOCK_CLOEXEC)
		fcntl(sck, F_SETFD, fcntl(sck, F_GETFD, 0) | FD_CLOEXEC);
#endif

		/* Query available interfaces. */
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = buf;
		if (ioctl(sck, SIOCGIFCONF, &ifc) < 0)
		{
			close(sck);
			return true;
		}

		/* Iterate through the list of interfaces. */
		ifr = ifc.ifc_req;
		nInterfaces = ifc.ifc_len / sizeof(struct ifreq);
		for (i = 0; i < nInterfaces; i++)
		{
			struct ifreq* item = &ifr[i];

			if (htonl(INADDR_LOOPBACK) != ((struct sockaddr_in*)&item->ifr_addr)->sin_addr.s_addr)
			{
				close(sck);
				return true;
			}
		}
		close(sck);
		return false;
#endif //sun
#endif //_WIN32
	}
}

void ping_named_server(void)
{
	std::string out;
	while(out.empty())
	{
		int rc=system("dialog --inputbox \"`cat urbackup/restore/enter_server_ip_input`\" 8 30 2> out");
		out=getFile("out");

		if(rc!=0)
		{
			return;
		}
	}

	if(!out.empty())
	{
		system(("./urbackuprestoreclient --ping-server \""+out+"\" &").c_str());
	}
}

namespace restore
{
	bool has_internet_connection(int& ec, std::string& errstatus)
	{
		ec = 0;

		IPipe* c = connectToService(ec);

		if (c == nullptr)
		{
			return false;
		}

		std::string pw = getFile(pw_file);
		CTCPStack tcpstack;
		tcpstack.Send(c, "STATUS DETAIL#pw=" + pw);
		std::string r = getResponse(c);
		if (r.empty())
		{
			Server->Log("No response from ClientConnector", LL_ERROR);
			ec = 1;
			return false;
		}

		std::string internet_connected = getbetween("\"internet_connected\": ", ",", r);
		std::string internet_status = getbetween("\"internet_status\": \"", "\",", r);

		if (internet_connected == "true"
			&& r.find("\"servers\": []") == std::string::npos)
		{
			return true;
		}

		errstatus = internet_status;
		ec = 2;
		return false;
	}
}

namespace
{
	bool internet_server_configured = false;
}

namespace restore
{
	void configure_internet_server()
	{
		std::string internet_server;
		while (internet_server.empty())
		{
			int rc = system("dialog --inputbox \"`cat urbackup/restore/enter_internet_server_name`\" 8 30 2> out");
			internet_server = trim(getFile("out"));

			if (rc != 0)
			{
				system("dialog --msgbox \"`cat urbackup/restore/no_internet_server_configured`\" 10 70");
				return;
			}
		}

		if (internet_server.empty())
		{
			system("dialog --msgbox \"`cat urbackup/restore/no_internet_server_configured`\" 10 70");
			return;
		}

		std::string internet_authkey;
		while (internet_authkey.empty())
		{
			int rc = system("dialog --inputbox \"`cat urbackup/restore/enter_internet_server_authkey`\" 8 30 2> out");
			internet_authkey = getFile("out");

			if (rc != 0)
			{
				system("dialog --msgbox \"`cat urbackup/restore/no_internet_server_configured`\" 10 70");
				return;
			}
		}

		if (internet_authkey.empty())
		{
			system("dialog --msgbox \"`cat urbackup/restore/no_internet_server_configured`\" 10 70");
			return;
		}

		system("clear");
		std::cout << "Configuring UrBackup restore client as Internet client..." << std::endl;

		configure_internet_server(internet_server, internet_authkey, "", true);
	}

	void configure_internet_server(std::string server_url, const std::string& server_authkey, const std::string& server_proxy,
		bool with_cli)
	{
		system("systemctl stop restore-client");

		int internet_server_port = 55415;

		if (next(server_url, 0, "urbackup://"))
		{
			server_url.erase(0, 11);
			if (server_url.find(":") != std::string::npos)
			{
				internet_server_port = watoi(getafter(":", server_url));
				server_url = getuntil(":", server_url);
			}
		}

		std::string rnd;
		rnd.resize(16);
		Server->secureRandomFill(&rnd[0], 16);

		std::string clientname = "##restore##" + bytesToHex(rnd);

		os_create_dir_recursive("urbackup/data_1");

		writestring("internet_mode_enabled=true\n"
			"internet_server=" + server_url + "\n"
			"internet_server_port=" + std::to_string(internet_server_port) + "\n"
			"internet_authkey=" + server_authkey + "\n"
			"computername=" + clientname + "\n", "urbackup/data_1/settings.cfg");

		if (with_cli)
			std::cout << "Starting UrBackup Internet restore client..." << std::endl;

		if (system("systemctl restart restore-client-internet") != 0)
		{
			std::cout << "Error starting Internet restore client." << std::endl;
			char ch;
			std::cin >> ch;
			return;
		}

		if (!with_cli)
			return;

		std::cout << "Waiting for Internet restore client to connect..." << std::endl;

		Server->wait(1000);

		int64 starttime = Server->getTimeMS();
		int ec;
		std::string errstatus;
		while (Server->getTimeMS() - starttime < 60000)
		{
			if (has_internet_connection(ec, errstatus))
			{
				internet_server_configured = true;
				return;
			}
			if (ec == 2 && next(errstatus, 0, "error:"))
				break;
			Server->wait(1000);
		}

		std::cout << "Connecting to Internet server failed: " << errstatus << std::endl;
		std::cout << "Restarting local UrBackup client... " << errstatus << std::endl;
		system("systemctl stop restore-client-internet");
		Server->deleteFile("urbackup/data_1/settings.cfg");
		system("systemctl start restore-client");

		std::string errmsg;
		switch (ec)
		{
		case 10:
		case 1:
			errmsg = "`cat urbackup/restore/internal_error`";
			break;
		case 2:
			errmsg = getFile("urbackup/restore/internet_authentification_failed");
			errmsg = greplace("_STATUS_", errstatus, errmsg);
			break;
		}

		int r = system(("dialog --menu \"`cat urbackup/restore/error_happend` " + errmsg + ". `cat urbackup/restore/how_to_continue`\" 15 50 10 "
			"\"r\" \"`cat urbackup/restore/search_again`\" "
			"\"i\" \"`cat urbackup/restore/connect_to_internet_server`\" "
			"\"s\" \"`cat urbackup/restore/start_shell`\" \"s\" "
			"\"`cat urbackup/restore/stop_restore`\" 2> out").c_str());

		if (r != 0)
		{
			return;
		}
		std::string out = getFile("out");
		if (out == "s")
		{
			system("bash");
		}
		else if (out == "i")
		{
			configure_internet_server();
		}
	}

	void configure_local_server()
	{
		system("systemctl stop restore-client-internet");
		Server->deleteFile("urbackup/data_1/settings.cfg");
		system("systemctl restart restore-client");
	}
}

namespace restore
{

bool do_login(LoginData& login_data, bool with_cli_output)
{
	if (login_data.has_login_data)
	{
		if (login_data.username.empty())
		{
			int ec;
			return tryLogin("", "", std::vector<SPasswordSalt>(), 10, &ec);
		}

		int ec;
		std::vector<SPasswordSalt> salts = getSalt(login_data.username, 10, &ec);

		bool found_salt = false;
		for (size_t i = 0; i < salts.size(); ++i)
		{
			if (!salts[i].salt.empty() &&
				!salts[i].rnd.empty())
			{
				found_salt = true;
				break;
			}
		}

		if (!found_salt)
		{
			return false;
		}

		return tryLogin(login_data.username, login_data.password, salts, 10, &ec);
	}

	if (with_cli_output)
	{
		system("clear");
		system("cat urbackup/restore/trying_to_login");
	}

	int ec;
	if (!tryLogin("", "", std::vector<SPasswordSalt>(), 0, &ec))
	{
		if (ec == 1)
		{
			//Server probably does not support logging in
			std::vector<std::string> clients = getBackupclients(ec);
			if (ec == 0 && !clients.empty())
			{
				return true;
			}
		}

		if (!with_cli_output)
		{
			return false;
		}

		while (true)
		{			
			int rc = system("dialog --inputbox \"`cat urbackup/restore/enter_username`\" 8 30 2> out");
			std::string username = getFile("out");

			if (rc != 0)
			{
				return false;
			}

			std::vector<SPasswordSalt> salts = getSalt(username, 5, &ec);

			bool found_salt = false;
			for (size_t i = 0; i < salts.size(); ++i)
			{
				if (!salts[i].salt.empty() &&
					!salts[i].rnd.empty())
				{
					found_salt = true;
					break;
				}
			}

			if (!found_salt)
			{
				rc = system("dialog --yesno \"`cat urbackup/restore/user_not_found`\" 7 50");
				if (rc != 0)
				{
					return false;
				}
			}
			else
			{
				rc = system("dialog --insecure --passwordbox \"`cat urbackup/restore/enter_password`\" 8 30 2> out");
				std::string password = getFile("out");

				if (rc != 0)
				{
					return false;
				}

				if (!tryLogin(username, password, salts, 5, &ec))
				{
					rc = system("dialog --yesno \"`cat urbackup/restore/login_failed`\" 7 50");
					if (rc != 0)
					{
						return false;
					}
				}
				else
				{
					login_data.has_login_data = true;
					login_data.username = username;
					login_data.password = password;
					return true;
				}
			}
		}
	}
	else
	{
		login_data.has_login_data = true;
		return true;
	}
}

}

const int start_state=-2;

void restore_wizard(void)
{
	int state=start_state;
	std::vector<std::string> clients;
	std::string clientname;
	std::vector<SImage> images;
	SImage selimage;
	SImage r_selimage;
	std::string seldrive;
	std::string windows_partition;
	std::string selpart;
	std::string err;
	bool res_sysvol=false;
	LoginData login_data;
	while(true)
	{

		switch(state)
		{
		case -2:
			{
				system("dialog --msgbox \"`cat urbackup/restore/welcome`\" 10 70");
				++state;
			}break;
		case -1:
			{
				if(!has_network_device())
				{
					system("clear");
					system("cat urbackup/restore/search_network");
					int tries=20;
					bool has_interface=false;
					do
					{
						bool b=has_network_device();
						if(b)
						{
							has_interface=true;
							break;
						}
						--tries;
						Server->wait(1000);
					} while(tries>=0);

					if(has_interface==false)
					{
						int r=system("dialog --menu \"`cat urbackup/restore/no_network_device`\" 15 50 10 \"n\" \"`cat urbackup/restore/configure_networkcard`\" \"w\" \"`cat urbackup/restore/configure_wlan`\" \"e\" \"`cat urbackup/restore/start_shell`\" \"c\" \"`cat urbackup/restore/continue_restore`\" 2> out");
						if(r!=0)
						{
							state=start_state;
							break;
						}

						std::string out=getFile("out");
						if(out=="n")
						{
							system(configure_networkcard.c_str());
							state=0;
						}
						else if(out=="w")
						{
							system(configure_wlan.c_str());
							state=0;
						}
						else if(out=="e")
						{
							system("bash");
							state=0;
						}
						else if(out=="c")
							state=0;
						else
							state=99;
					}
					else
					{
						state=0;
					}
				}
				else
				{
					state=0;
				}
			}break;
		case 0:
			{
				if (!internet_server_configured)
				{
					system("urbackup/restore/progress-start.sh | dialog --backtitle \"`cat urbackup/restore/search`\" --gauge \"`cat urbackup/restore/t_progress`\" 6 60 0");
				}
				++state;
			}break;
		case 1:
			{
				std::string errmsg;
				int ec;
				login_data = LoginData();
				if(do_login(login_data))
				{					
					clients=getBackupclients(ec);
					
					switch(ec)
					{
					case 10:
					case 1:
						errmsg="`cat urbackup/restore/internal_error`";
						break;
					case 2:
						errmsg="`cat urbackup/restore/no_server_found`";
						break; 
					}

					if(errmsg.empty() && clients.empty())
					{
						ec=3;
						errmsg="`cat urbackup/restore/no_clients_found`";
					}
				}
				else
				{
					errmsg="login failed";
					ec=1;
				}

				if(ec!=0)
				{
					int r=system(("dialog --menu \"`cat urbackup/restore/error_happend` "+errmsg+". `cat urbackup/restore/how_to_continue`\" 15 50 10 "
						"\"r\" \"`cat urbackup/restore/search_again`\" "
						"\"n\" \"`cat urbackup/restore/configure_networkcard`\" "
						"\"w\" \"`cat urbackup/restore/configure_wlan`\" "
						"\"e\" \"`cat urbackup/restore/enter_server_ip`\" "
						"\"i\" \"`cat urbackup/restore/connect_to_internet_server`\" "
						"\"s\" \"`cat urbackup/restore/start_shell`\" \"s\" "
						"\"`cat urbackup/restore/stop_restore`\" 2> out").c_str());
					if(r!=0)
					{
						state=start_state;
						break;
					}

					std::string out=getFile("out");
					if(out=="r")
						state=0;
					else if(out=="n")
					{
						system(configure_networkcard.c_str());
						state=0;
					}
					else if(out=="w")
					{
						system(configure_wlan.c_str());
						state=0;
					}
					else if(out=="e")
					{
						ping_named_server();
						state=0;
					}
					else if (out == "i")
					{
						configure_internet_server();
						state = 0;
					}
					else if(out=="s")
					{
						system("bash");
						state=0;
					}
					else
						state=99;
				}
				else
				{
					std::string mi;
					for(size_t i=0;i<clients.size();++i)
					{
						mi+="\""+convert((int)i+1)+"\" \""+clients[i]+"\" ";
					}
					int r=system(("dialog --menu \"`cat urbackup/restore/select`\" 15 50 10 "+mi+"2> out").c_str());
					if(r!=0)
					{
						state=start_state;
						break;
					}
					

					std::string out=getFile("out");
					clientname=clients[atoi(out.c_str())-1];
					++state;
				}
			}break;
		case 2:
			{
				int ec;
				images=getBackupimages(clientname, &ec);
				std::string errmsg;
				switch(ec)
				{
				case 10:
				case 1:
					errmsg="`cat urbackup/restore/internal_error`";
					break;
				case 2:
					errmsg="`cat urbackup/restore/no_server_found`";
					break; 
				}

				if(images.empty())
				{
					ec=3;
					errmsg="`cat urbackup/restore/no_images_for_client1` '"+clientname+"' `cat urbackup/restore/no_images_for_client2`";
				}

				std::sort(images.begin(), images.end());

				if(ec!=0)
				{
					int r=system(("dialog --menu \"`cat urbackup/restore/error_happend` "+errmsg+". `cat urbackup/restore/how_to_continue`\" 15 50 10 \"a\" \"`cat urbackup/restore/error_j_select`\" \"r\" \"`cat urbackup/restore/search_again`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out").c_str());
					if(r!=0)
					{
						state=start_state;
						break;
					}

					std::string out=getFile("out");
					if(out=="r")
						state=0;
					else if(out=="a")
						state=1;
					else
						state=99;
				}
				else
				{
					std::string mi;
					for(size_t i=0;i<images.size();++i)
					{
						if(!images[i].letter.empty())
							images[i].letter=" "+images[i].letter;

						mi+="\""+convert((int)i+1)+"\" \""+images[i].time_str+images[i].letter+"\" ";
					}
					int r=system(("dialog --menu \"`cat urbackup/restore/select_date`\" 15 50 10 "+mi+"2> out").c_str());
					if(r!=0)
					{
						state=start_state;
						break;
					}

					std::string out=getFile("out");
					selimage=images[atoi(out.c_str())-1];
					r_selimage=selimage;
					++state;
				}
			}break;
		case 3:
			{
				std::vector<SLsblk> drives = lsblk("");
				
				bool has_disk=false;
				
				for(size_t i=0;i<drives.size();++i)
				{
					if(drives[i].type=="disk" && !drives[i].path.empty())
					{
						has_disk=true;
						break;
					}
				}
				
				
				if(!has_disk)
				{
					int r=system("dialog --menu \"`cat urbackup/restore/no_disks_found`. `cat urbackup/restore/how_to_continue`\" 15 50 10 \"r\" \"`cat urbackup/restore/search_disk_again`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out");
					if(r!=0)
					{
						state=start_state;
						break;
					}

					std::string out=getFile("out");
					if(out=="r")
						state=3;
					else
						state=99;
					break;
				}

				std::string mi;
				for(size_t i=0;i<drives.size();++i)
				{
					if(drives[i].type=="disk")
					{
						mi+="\""+convert((int)i+1)+"\" \""+drives[i].model+" `cat urbackup/restore/size`: "+drives[i].size+"\" ";
					}
				}
				std::string scmd="dialog --menu \"`cat urbackup/restore/select_drive`\" 15 50 10 "+mi+"2> out";
				writestring(scmd, "scmd.sh");
				int r=system(scmd.c_str());
				if(r!=0)
				{
					state=start_state;
					break;
				}

				std::string out=getFile("out");
				int driveidx=atoi(out.c_str())-1;
				seldrive=drives[driveidx].path;
				r=system(("dialog --yesno \"`cat urbackup/restore/select_certain`\\n"+drives[driveidx].model+" `cat urbackup/restore/size`: "+drives[driveidx].size+"\" 10 50").c_str());
				if(r!=0)
				{
					break;
				}
				++state;
			}break;
		case 4:
			{
				system("clear");
				system("cat urbackup/restore/loading_mbr");
				system("echo");
				if(FileExists("mbr.dat"))
				{
					Server->deleteFile("mbr.dat");
				}
				system("touch mbr.dat");
				DownloadStatus dl_status;
				EDownloadResult rc = downloadImage(selimage.id, convert(selimage.time_s), "mbr.dat", true, login_data, dl_status);
				if (rc != EDownloadResult_Ok )
				{
					Server->Log("Error downloading MBR", LL_ERROR);
					err="cannot_read_mbr";
					state=101;
					break;
				}
				system("cat urbackup/restore/reading_mbr");
				system("echo");
				if (is_disk_mbr("mbr.dat"))
				{
					std::cout << "Restoring whole disk..." << std::endl;
					++state;
					selpart = seldrive;
					break;
				}
				IFile *f=Server->openFile("mbr.dat", MODE_READ);
				if(f==nullptr)
				{
					err="cannot_read_mbr";
					state=101;
					break;
				}
				size_t fsize=(size_t)f->Size();
				char *buf=new char[fsize];
				f->Read(buf, (_u32)fsize);
				Server->destroy(f);
				
				CRData mbr(buf, fsize);
				SMBRData mbrdata(mbr);
				if(mbrdata.hasError())
				{
					err="error_while_reading_mbr";
					exit(3);
					state=101;
					break;
				}

				system("cat urbackup/restore/writing_mbr");
				system("echo");
				IFile *dev=Server->openFile(seldrive, MODE_RW);
				if(dev==nullptr)
				{
					err="cannot_open_disk";
					state=101;
					break;
				}
				dev->Seek(0);
				if(dev->Write(mbrdata.mbr_data)!=mbrdata.mbr_data.size())
				{
					err="error_writing_mbr";
					state=101;
					break;
				}

				bool fix_gpt = false;
				std::vector<IFSImageFactory::SPartition> partitions;

				if(mbrdata.gpt_style)
				{
					std::string onlypart = ExtractFileName(seldrive);
					std::string logical_block_size_str = getFile("/sys/block/"+onlypart+"/queue/logical_block_size");

					if(!logical_block_size_str.empty())
					{
						unsigned int logical_block_size = atoi(logical_block_size_str.c_str());

						if(logical_block_size!=mbrdata.gpt_header_pos)
						{
							err="gpt_logical_blocksize_change";
							state=101;
							break;						
						}
						Server->Log("Logical block size: "+convert(logical_block_size));
					}
					else
					{
						Server->Log("Could not get logical block size");
					}

					system("cat urbackup/restore/writing_gpt_header");
					system("echo");

					if(!dev->Seek(mbrdata.gpt_header_pos) || dev->Write(mbrdata.gpt_header)!=mbrdata.gpt_header.size())
					{
						err="error_writing_gpt";
						state=101;
						break;
					}

					system("cat urbackup/restore/writing_gpt_table");
					system("echo");

					if(!dev->Seek(mbrdata.gpt_table_pos) || dev->Write(mbrdata.gpt_table)!=mbrdata.gpt_table.size())
					{
						err="error_writing_gpt";
						state=101;
						break;
					}

					system("cat urbackup/restore/writing_backup_gpt_header");
					system("echo");

					if(!dev->Seek(mbrdata.backup_gpt_header_pos) || dev->Write(mbrdata.backup_gpt_header)!=mbrdata.backup_gpt_header.size())
					{
						std::cout << "Error writing backup GPT header. " << os_last_error_str() << ". Fixing with GNU parted..." << std::endl;
						fix_gpt = true;
					}

					system("cat urbackup/restore/writing_backup_gpt_table");
					system("echo");

					if(!dev->Seek(mbrdata.backup_gpt_table_pos) || dev->Write(mbrdata.backup_gpt_table)!=mbrdata.backup_gpt_table.size())
					{
						std::cout << "Error writing backup GPT table. " << os_last_error_str() << ". Fixing with GNU parted..." << std::endl;
						fix_gpt = true;
					}

					if (fix_gpt)
					{
						system(("echo -e \"w\\nY\\nY\" | gdisk " + seldrive).c_str());

						bool t_gpt_style;
						partitions = image_fak->readPartitions(mbrdata.mbr_data, mbrdata.gpt_header, mbrdata.gpt_table, t_gpt_style);
					}
					else
					{
						system(("echo -e \"w\\nY\\nY\" | gdisk " + seldrive + " > /dev/null 2>&1").c_str());
					}
				}

				if (mbrdata.extra_data_pos != -1)
				{
					std::cout << "Writing extra data..." << std::endl;
					if (dev->Write(mbrdata.extra_data_pos,
						mbrdata.extra_data) != mbrdata.extra_data.size())
					{
						std::cout << "Error writing extra data. Ignoring...";
					}
				}

				Server->destroy(dev);

				system("cat urbackup/restore/reading_partition_table");
				system("echo");
				Server->Log("Selected device: "+seldrive+" Partition: "+convert(mbrdata.partition_number));
				system(("partprobe "+seldrive+" > /dev/null 2>&1").c_str());
				Server->wait(10000);
				system("cat urbackup/restore/testing_partition");
				system("echo");
				dev=nullptr;
				std::string partpath = getPartitionPath(seldrive, mbrdata.partition_number);
				if(!partpath.empty())
				{
					dev=Server->openFile(partpath, MODE_RW);
				}
				int try_c=0;
				int delete_parts = 0;
				while(dev==nullptr && try_c<10)
				{
					system(("partprobe "+seldrive+" > /dev/null 2>&1").c_str());
					Server->wait(10000);
					system("cat urbackup/restore/testing_partition");
					system("echo");
					partpath = getPartitionPath(seldrive, mbrdata.partition_number);
					if(!partpath.empty())
					{
						dev=Server->openFile(partpath, MODE_RW);
					}

					if(dev==nullptr)
					{
						if (!mbrdata.gpt_style)
						{
							Server->Log("Trying to fix LBA partitioning scheme via fdisk...");
							//Fix LBA partition signature
							system(("echo w | fdisk " + seldrive + " > /dev/null 2>&1").c_str());
							system(("echo -e \"w\\nY\" | fdisk " + seldrive + " > /dev/null 2>&1").c_str());
						}
						else
						{
							Server->Log("Trying to fix GPT partitioning scheme via gdisk...");
							system(("echo -e \"w\\nY\" | gdisk " + seldrive).c_str());
						}

						if (fix_gpt && try_c>5
							&& !partitions.empty())
						{
							//TODO: the last partition might not be the one with the highest offset
							Server->Log("Deleting last GPT partition...");
							++delete_parts;
							std::string cmd;
							for (int i = 0; i < delete_parts; ++i)
							{
								if (partitions.size() - i == mbrdata.partition_number)
								{
									Server->Log("Cannot delete last GPT partition. This is the partition to be restored. DISK IS PROBABLY TOO SMALL TO RESTORE TO.", LL_ERROR);
									system("read -n1 -r -p \"Press any key to continue...\" key");
									fix_gpt = false;
									try_c = 100;
								}

								cmd += "d\\n" + convert(partitions.size() - i);
							}
							cmd += "\\nw\\nY";

							if(fix_gpt)
								system(("echo -e \""+cmd+"\" | gdisk " + seldrive).c_str());
						}
					}

					++try_c;
				}
				if(dev==nullptr)
				{
					err="no_restore_partition";
					state=101;
					break;
				}
				selpart=partpath;
				Server->destroy(dev);
				delete []buf;
				++state;
			}break;
		case 5:
			{
				if(windows_partition.empty())
				{
					windows_partition=selpart;
				}
				RestoreThread rt(selimage.id, convert(selimage.time_s), selpart, login_data);
				THREADPOOL_TICKET rt_ticket=Server->getThreadPool()->execute(&rt, "image restore");
				while(true)
				{
					system(("./urbackuprestoreclient --image-download-progress | dialog --backtitle \"`cat urbackup/restore/restoration"+(std::string)(res_sysvol?"_sysvol":"")+"`\" --gauge \"`cat urbackup/restore/t_progress`\" 6 60 0").c_str());
					while(!rt.isDone() && !restore_retry_ok)
					{
						Server->wait(1000);
					}
					if(rt.isDone())
						break;
					if(restore_retry_ok)
						restore_retry_ok=false;
				}
				Server->getThreadPool()->waitFor(rt_ticket);
				EDownloadResult rc=rt.getRC();
				std::string errmsg;
				switch(rc)
				{
				case EDownloadResult_ConnectError: errmsg="`cat urbackup/restore/no_connection`"; break;
				case EDownloadResult_OpenError: errmsg="`cat urbackup/restore/cannot_write_on_partition`"; break;
				case EDownloadResult_SizeReadError: errmsg="`cat urbackup/restore/wrong_size`"; break;
				case EDownloadResult_TimeoutError2: errmsg="`cat urbackup/restore/server_doesnot_respond`"; break;
				case EDownloadResult_TimeoutError1: errmsg="`cat urbackup/restore/server_connection_timeout`"; break;
				case EDownloadResult_WriteFailed: errmsg="`cat urbackup/restore/writing_failed`"; break;
				case EDownloadResult_DeviceTooSmall: errmsg = greplace("_1_",
					PrettyPrintBytes(rt.getImgsize()), greplace("_2_", PrettyPrintBytes(rt.getOutputFileSize()), getFile("urbackup/restore/device_too_small"))); break;
				};

				if(rc!=0)
				{
					int r=system(("dialog --menu \"`cat urbackup/restore/error_happend` "+errmsg+". `cat urbackup/restore/how_to_continue`\" 15 50 10 \"r\" \"`cat urbackup/restore/restart_restore`\" \"s\" \"`cat urbackup/restore/start_shell`\" \"o\" \"`cat urbackup/restore/restore_other`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out").c_str());
					if(r!=0)
					{
						state=start_state;
						break;
					}

					std::string out=getFile("out");
					if(out=="r")
						state=5;
					else if(out=="o")
						state=2;
					else if(out=="s")
					{
						system("bash");
						state=0;
					}
					else
						state=99;
				}
				else
				{
					if(r_selimage.assoc.size()>0)
					{
						selimage=r_selimage.assoc[0];
						r_selimage.assoc.erase(r_selimage.assoc.begin());
						res_sysvol=true;
						state=4;
					}
					else
					{
						++state;
					}
				}
			}break;
		case 6:
			{
				int r=system("dialog --menu \"`cat urbackup/restore/restore_success`\" 15 70 10 \"r\" \"`cat urbackup/restore/restart_computer`\" \"b\" \"`cat urbackup/restore/bootable_on_different_hardware`\" \"o\" \"`cat urbackup/restore/restore_other`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out");
				if(r!=0)
				{
					state=start_state;
					break;
				}

				std::string out=getFile("out");
				if(out=="r")
					system("init 6");
				else if(out=="o")
					state=2;
				else if(out=="s")
					system("bash");
				else if(out=="b")
					system(("python3 driver_edit.py "+windows_partition).c_str());
				else
					system("init 6");

			}break;
		case 99:
			{
				system("dialog --msgbox \"`cat urbackup/restore/computer_halt`\" 7 50");
				system("init 0");
				exit(1);
			}break;
		case 101:
			{
				int r=system(("dialog --menu \"`cat urbackup/restore/error_happend` `cat urbackup/restore/"+err+"`. `cat urbackup/restore/how_to_continue`\" 15 50 10 \"r\" \"`cat urbackup/restore/restart_restore`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out").c_str());
				if(r!=0)
				{
					state=start_state;
					break;
				}

				std::string out=getFile("out");
				if(out=="r")
					state=0;
				else
					state=99;
			}break;
		default:
			{
				system("dialog --msgbox \"`cat urbackup/restore/internal_error`!!!!\" 7 50");
				exit(99);
			}break;
		}
	}
}