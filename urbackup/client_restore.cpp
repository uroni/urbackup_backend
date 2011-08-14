#include <string>
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "fileclient/tcpstack.h"
#include "fileclient/data.h"
#include "../stringtools.h"
#include "os_functions.h"
#include <iostream>
#include <stdlib.h>
#include <memory.h>
#include <algorithm>


std::string trim2(const std::string &str)
{
    size_t startpos=str.find_first_not_of(" \t\n");
    size_t endpos=str.find_last_not_of(" \t\n");
    if( std::string::npos == startpos || std::string::npos==endpos)
    {
        return "";
    }
    else
    {
		return str.substr( startpos, endpos-startpos+1);
    }
}

std::string getResponse(IPipe *c)
{
	CTCPStack tcpstack;
	char *resp=NULL;
	char buffer[1024];
	size_t packetsize;
	while(resp==NULL)
	{
		size_t rc=c->Read(buffer, 1024, 60000);
		if(rc==0)
		{
			return "";
		}
		tcpstack.AddData(buffer, rc );

		resp=tcpstack.getPacket(&packetsize);
		if(packetsize==0)
		{
			return "";
		}
	}

	std::string ret;
	ret.resize(packetsize);
	memcpy(&ret[0], resp, packetsize);
	return ret;
}

std::vector<std::string> getBackupclients(int *ec)
{
	std::string pw=getFile("pw.txt");
	CTCPStack tcpstack;
	std::vector<std::string> ret;
	*ec=0;

	IPipe *c=Server->ConnectStream("localhost", 35623, 60000);
	if(c==NULL)
	{
		Server->Log("Error connecting to client service -1", LL_ERROR);
		*ec=10;
		return ret;
	}

	tcpstack.Send(c, "GET BACKUPCLIENTS#pw="+pw);
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
			*ec=2;
		}
		else
		{
			std::vector<std::string> toks;
			std::string t=r.substr(1);
			Tokenize(t, toks, "\n");
			for(size_t i=0;i<toks.size();++i)
			{
				std::string nam=trim2(getafter("|", toks[i]));
				if(!nam.empty())
				{
					bool found=false;
					for(size_t i=0;i<ret.size();++i)
					{
						if(ret[i]==nam)
						{
							found=true;
							break;
						}
					}
					if(!found)
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

struct SImage
{
	bool operator<(const SImage &other) const
	{
		return other.time_s<time_s;
	}
	std::string time_str;
	_i64 time_s;
	int id;
	std::vector<SImage> assoc;
};

std::vector<SImage> getBackupimages(std::string clientname, int *ec)
{
	std::string pw=getFile("pw.txt");
	CTCPStack tcpstack;
	std::vector<SImage> ret;
	*ec=0;

	IPipe *c=Server->ConnectStream("localhost", 35623, 60000);
	if(c==NULL)
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
			std::vector<std::string> toks;
			std::string t=r.substr(1);
			Tokenize(t, toks, "\n");
			for(size_t i=0;i<toks.size();++i)
			{
				std::vector<std::string> t2;
				Tokenize(toks[i], t2, "|");
				if(t2.size()==3)
				{
					SImage si;
					si.id=atoi(t2[0].c_str());
					si.time_s=os_atoi64(t2[1]);
					si.time_str=t2[2];
					ret.push_back(si);
				}
				else if(t2.size()==4 && t2[0]=="#" && !ret.empty())
				{
					SImage si;
					si.id=atoi(t2[1].c_str());
					si.time_s=os_atoi64(t2[2]);
					si.time_str=t2[3];
					ret[ret.size()-1].assoc.push_back(si);
				}
			}
		}
	}
	Server->destroy(c);
	return ret;
}

volatile bool restore_retry_ok=false;

int downloadImage(int img_id, std::string img_time, std::string outfile, bool mbr, _i64 offset=-1)
{
	std::string pw=getFile("pw.txt");
	CTCPStack tcpstack;
	std::vector<SImage> ret;

	IPipe *c=Server->ConnectStream("localhost", 35623, 60000);
	if(c==NULL)
	{
		Server->Log("Error connecting to client service -1", LL_ERROR);
		return 10;
	}

	std::string s_offset;
	if(offset!=-1)
	{
		s_offset="&offset="+nconvert(offset);
	}

	tcpstack.Send(c, "DOWNLOAD IMAGE#pw="+pw+"&img_id="+nconvert(img_id)+"&time="+img_time+"&mbr="+nconvert(mbr)+s_offset);

	std::string restore_out=outfile;
	IFile *out=Server->openFile(restore_out, MODE_RW);
	if(out==NULL)
	{
		Server->Log("Could not open \""+restore_out+"\" for writing", LL_ERROR);
		Server->destroy(c);return 2;
	}

	_i64 imgsize=-1;
	c->Read((char*)&imgsize, sizeof(_i64), 60000);
	if(imgsize==-1)
	{
		Server->Log("Error reading size", LL_ERROR);
		Server->destroy(c);Server->destroy(out);return 3;
	}

	char buf[4096];
	if(mbr==true)
	{
		_i64 read=0;
		while(read<imgsize)
		{
			size_t c_read=c->Read(buf, 4096, 180000);
			if(c_read==0)
			{
				Server->Log("Read Timeout", LL_ERROR);
				Server->destroy(c);Server->destroy(out);return 4;
			}
			out->Write(buf, (_u32)c_read);
			read+=c_read;
		}
		Server->destroy(c);Server->destroy(out);return 0;
	}
	else
	{
		_i64 read=0;
		/*while(read<512)
		{
			size_t c_read=c->Read(buf, 512-(size_t)read, 60000);
			if(c_read==0)
			{
				Server->Log("Read Timeout -1", LL_ERROR);
				Server->destroy(c);Server->destroy(out);return 4;
			}
			_u32 w=out->Write(buf,(_u32)c_read);
			if(w!=c_read)
			{
				Server->Log("Writing to output file failed", LL_ERROR);
				Server->destroy(c);Server->destroy(out);return 6;
			}
			read+=c_read;
		}*/

		unsigned int blockleft=0;
		unsigned int off=0;
		char blockdata[4096];
		bool first=true;
		bool has_data=false;
		_i64 pos=0;
		while(pos<imgsize)
		{
			size_t r=c->Read(&buf[off], 4096-off, 180000);
			if(r!=0)
				r+=off;
			off=0;
			if( r==0 )
			{
				Server->Log("Read Timeout: Retrying", LL_WARNING);
				Server->destroy(c);Server->destroy(out);
				if(has_data)
				{
					int tries=5;
					int rc;
					do
					{
						Server->wait(30000);
						rc=downloadImage(img_id, img_time, outfile, mbr, pos);
						if(rc==0)
						{
							return rc;
						}
						--tries;
					}
					while(tries>0);
					return rc;
				}
				else
				{
					Server->Log("Read Timeout: No data", LL_ERROR);
					return 4;
				}
			}
			while(true)
			{
				if( blockleft==0 )
				{
					if(!first)
					{
						_u32 tw=4096;
						if(imgsize>=pos && imgsize-pos<4096)
							tw=(_u32)(imgsize-pos);

						_u32 woff=0;
						do
						{
							_u32 w=out->Write(&blockdata[woff], tw-woff);
							if(w==0)
							{
								Server->Log("Writing to output file failed", LL_ERROR);
								Server->destroy(c);Server->destroy(out);return 6;
							}
							woff+=w;
						}
						while(tw-woff>0);

						has_data=true;
					}
					else
					{
						first=false;

						if(pos!=-1 && !restore_retry_ok)
						{
							restore_retry_ok=true;
						}
					}

					if(r-off>=sizeof(_i64) )
					{
						blockleft=4096;
						_i64 s;
						memcpy((char*)&s, &buf[off], sizeof(_i64) );
						if(s>imgsize)
						{
							Server->Log("invalid seek value: "+nconvert(s), LL_ERROR);
							pos=s;
							break;
						}
						else if(s<pos)
						{
							Server->Log("Position out of order!", LL_ERROR);
						}
						else
						{
							out->Seek(s);
							pos=s;
						}
						off+=sizeof(_i64);
					}
					else if(r-off>0)
					{
						char buf2[4096];
						memcpy(buf2, &buf[off], r-off);
						memcpy(buf, buf2, r-off);
						off=(_u32)r-off;
						break;
					}
					else
					{
						off=0;
						break;
					}
				}
				else
				{
					unsigned int available=(std::min)((unsigned int)r-off, blockleft);
					if(available>0)
					{
						memcpy(&blockdata[4096-blockleft], &buf[off], (_u32)available );
					}
					read+=available;
					blockleft-=available;
					off+=available;
					if(off>=r)
					{
						off=0;
						break;
					}
				}
			}
		}

		Server->destroy(c);Server->destroy(out);return 0;
	}
	return 0;
}

void do_restore(void)
{
	IPipe *c=Server->ConnectStream("localhost", 35623, 60000);
	if(c==NULL)
	{
		Server->Log("Error connecting to client service -1", LL_ERROR);
		exit(1);return;
	}

	std::string pw=getFile("pw.txt");

	CTCPStack tcpstack;

	std::string cmd=Server->getServerParameter("restore_cmd");
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
	else if(cmd=="get_backupimages" )
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

		int ec=downloadImage(atoi(Server->getServerParameter("restore_img_id").c_str()), Server->getServerParameter("restore_time"), Server->getServerParameter("restore_out"), mbr);
		exit(ec);
	}
	else if(cmd=="download_progress")
	{
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
					    std::cout << npc << std::endl;
					    lpc=npc;
					}
				}
			}
			if(r==0)
				break;
		}
		if(lpc!=100)
		{
		    std::cout << "100" << std::endl;
		}

		exit(0);
	}
}

class SMBRData
{
public:
	SMBRData(CRData &data)
	{
		char ch;
		if(!data.getChar(&ch))
		{
			Server->Log("Cannot read first byte");
			has_error=true;return;
		}
		if(!data.getChar(&version))
		{
			Server->Log("Cannot read version");
			has_error=true;return;
		}
		if(version!=0)
		{
			Server->Log("Version is wrong");
			has_error=true;return;
		}
		if(!data.getInt(&device_number))
		{
			Server->Log("Cannot get device number");
			has_error=true;return;
		}
		if(!data.getInt(&partition_number))
		{
			Server->Log("Cannot get partition number");
			has_error=true;return;
		}
		if(!data.getStr(&serial_number))
		{
			Server->Log("Cannot get serial number");
			has_error=true;return;
		}
		std::string tmp;
		if(!data.getStr(&tmp))
		{
			Server->Log("Cannot get volume name");
			has_error=true;return;
		}
		volume_name=Server->ConvertToUnicode(tmp);
		if(!data.getStr(&tmp))
		{
			Server->Log("Cannot get fsn name");
			has_error=true;return;
		}
		fsn=Server->ConvertToUnicode(tmp);
		if(!data.getStr(&mbr_data))
		{
			Server->Log("Cannot get mbr data");
			has_error=true;return;
		}
		has_error=false;
	}

	bool hasError(void)
	{
		return has_error;
	}

	char version;
	int device_number;
	int partition_number;
	std::string serial_number;
	std::wstring volume_name;
	std::wstring fsn;
	std::string mbr_data;

private:
	bool has_error;
};

class RestoreThread : public IThread
{
public:
	RestoreThread(int pImg_id, std::string pImg_time, std::string pOutfile) : img_id(pImg_id), img_time(pImg_time), outfile(pOutfile)
	{
		done=false;
	}

	void operator()(void)
	{
		rc=downloadImage(img_id, img_time, outfile, false);
		done=true;
	}

	bool isDone(void)
	{
		return done;
	}

	int getRC(void)
	{
		return rc;
	}
private:
	int rc;
	volatile bool done;
	int img_id;
	std::string img_time;
	std::string outfile;
};

void restore_wizard(void)
{
	int state=-1;
	std::vector<std::string> clients;
	std::string clientname;
	std::vector<SImage> images;
	SImage selimage;
	SImage r_selimage;
	std::string seldrive;
	int selpart;
	std::string err;
	bool res_sysvol=false;
	while(true)
	{

		switch(state)
		{
		case -1:
			{
				system("dialog --msgbox \"`cat urbackup/restore/welcome`\" 10 70");
				++state;
			}break;
		case 0:
			{
				system("urbackup/restore/progress-start.sh | dialog --backtitle \"`cat urbackup/restore/search`\" --gauge \"`cat urbackup/restore/t_progress`\" 6 60 0");
				++state;
			}break;
		case 1:
			{
				int ec;
				clients=getBackupclients(&ec);
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

				if(clients.empty())
				{
					ec=3;
					errmsg="`cat urbackup/restore/no_clients_found`";
				}

				if(ec!=0)
				{
					int r=system(("dialog --menu \"`cat urbackup/restore/error_happend` "+errmsg+". `cat urbackup/restore/how_to_continue`\" 15 50 10 \"r\" \"`cat urbackup/restore/search_again`\" \"n\" \"`cat urbackup/restore/configure_networkcard`\" \"w\" \"`cat urbackup/restore/configure_wlan`\" \"e\" \"`cat urbackup/restore/start_shell`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out").c_str());
					if(r!=0)
					{
						state=-1;
						break;
					}

					std::string out=getFile("out");
					if(out=="r")
						state=0;
					else if(out=="n")
					{
						system("netcardconfig");
						state=0;
					}
					else if(out=="w")
					{
						system("wlcardconfig");
						state=0;
					}
					else if(out=="e")
					{
						system("sh");
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
						mi+="\""+nconvert((int)i+1)+"\" \""+clients[i]+"\" ";
					}
					int r=system(("dialog --menu \"`cat urbackup/restore/select`\" 15 50 10 "+mi+"2> out").c_str());
					if(r!=0)
					{
						state=-1;
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
						state=-1;
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
						mi+="\""+nconvert((int)i+1)+"\" \""+images[i].time_str+"\" ";
					}
					int r=system(("dialog --menu \"`cat urbackup/restore/select_date`\" 15 50 10 "+mi+"2> out").c_str());
					if(r!=0)
					{
						state=-1;
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
				system("ls /dev | grep \"[h|s]d[a-z]\" > out");
				std::string drives_s=getFile("out");
				std::vector<std::string> drives;
				for(int i=0,lc=linecount(drives_s);i<lc;++i)
				{
					std::string l=trim2(getline(i, drives_s));
					if(l.size()==3)
					{
						drives.push_back(l);
					}
				}

				std::vector<std::string> vendors;
				std::vector<_i64> d_sizes;
				for(size_t i=0;i<drives.size();++i)
				{
					system(("hdparm -I /dev/"+drives[i]+" | grep Model > out").c_str());
					std::string out=getFile("out");
					std::string vendor=trim2(getafter("Model Number:", out));
					if(vendor.empty())
					{
						vendor="`cat urbackup/restore/unknown_disk`";
					}
					vendors.push_back(vendor);
					system(("hdparm -I /dev/"+drives[i]+" | grep \"M = 1000\" > out").c_str());
					out=getFile("out");
					std::string size_s=trim2(getbetween("1000:", "MBytes", out));
					d_sizes.push_back(os_atoi64(size_s));
				}

				if(drives.empty())
				{
					int r=system("dialog --menu \"`cat urbackup/restore/no_disks_found`. `cat urbackup/restore/how_to_continue`\" 15 50 10 \"r\" \"`cat urbackup/restore/search_disk_again`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out");
					if(r!=0)
					{
						state=-1;
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
					mi+="\""+nconvert((int)i+1)+"\" \""+vendors[i]+" `cat urbackup/restore/size`: "+nconvert((int)(d_sizes[i]/1000))+" GB\" ";
				}
				std::string scmd="dialog --menu \"`cat urbackup/restore/select_drive`\" 15 50 10 "+mi+"2> out";
				writestring(scmd, "scmd.sh");
				int r=system(scmd.c_str());
				if(r!=0)
				{
					state=-1;
					break;
				}

				std::string out=getFile("out");
				int driveidx=atoi(out.c_str())-1;
				seldrive=drives[driveidx];
				r=system(("dialog --yesno \"`cat urbackup/restore/select_certain`\\n"+vendors[driveidx]+" `cat urbackup/restore/size`: "+nconvert((int)(d_sizes[driveidx]/1000))+" GB\" 10 50").c_str());
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
				system("touch mbr.dat");
				downloadImage(selimage.id, nconvert(selimage.time_s), "mbr.dat", true);
				system("cat urbackup/restore/reading_mbr");
				system("echo");
				IFile *f=Server->openFile("mbr.dat", MODE_READ);
				if(f==NULL)
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
				IFile *dev=Server->openFile("/dev/"+seldrive, MODE_RW);
				if(dev==NULL)
				{
					err="cannot_open_disk";
					state=101;
					break;
				}
				dev->Seek(0);
				dev->Write(mbrdata.mbr_data);
				Server->destroy(dev);

				system("cat urbackup/restore/reading_partition_table");
				system("echo");
				system(("partprobe /dev/"+seldrive+" > /dev/null 2>&1").c_str());
				Server->wait(10000);
				system("cat urbackup/restore/testing_partition");
				system("echo");
				dev=Server->openFile("/dev/"+seldrive+nconvert(mbrdata.partition_number), MODE_RW);
				int try_c=0;
				while(dev==NULL && try_c<10)
				{
					system(("partprobe /dev/"+seldrive+" > /dev/null 2>&1").c_str());
					Server->wait(10000);
					system("cat urbackup/restore/testing_partition");
					system("echo");
					dev=Server->openFile("/dev/"+seldrive+nconvert(mbrdata.partition_number), MODE_RW);
					++try_c;
				}
				if(dev==NULL)
				{
					err="no_restore_partition";
					state=101;
					break;
				}
				selpart=mbrdata.partition_number;
				Server->destroy(dev);
				delete []buf;
				++state;
			}break;
		case 5:
			{
				RestoreThread rt(selimage.id, nconvert(selimage.time_s), "/dev/"+seldrive+nconvert( selpart ));
				THREADPOOL_TICKET rt_ticket=Server->getThreadPool()->execute(&rt);
				while(true)
				{
					system(("./cserver --plugin urbackup/.libs/liburbackup.so --no-server --restore true --restore_cmd download_progress | dialog --backtitle \"`cat urbackup/restore/restoration"+(std::string)(res_sysvol?"_sysvol":"")+"`\" --gauge \"`cat urbackup/restore/t_progress`\" 6 60 0").c_str());
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
				int rc=rt.getRC();
				std::string errmsg;
				switch(rc)
				{
				case 10: errmsg="`cat urbackup/restore/no_connection`"; break;
				case 2: errmsg="`cat urbackup/restore/cannot_write_on_partition`"; break;
				case 3: errmsg="`cat urbackup/restore/wrong_size`"; break;
				case 4: errmsg="`cat urbackup/restore/server_doesnot_respond`"; break;
				case 6: errmsg="`cat urbackup/restore/writing_failed`"; break;
				};

				if(rc!=0)
				{
					int r=system(("dialog --menu \"`cat urbackup/restore/error_happend`: "+errmsg+". `cat urbackup/restore/how_to_continue`\" 15 50 10 \"r\" \"`cat urbackup/restore/restart_restore`\" \"e\" \"`cat urbackup/restore/error_happend`\" \"o\" \"`cat urbackup/restore/start_shell`\" \"s\" \"`cat urbackup/restore/stop_restore`\" 2> out").c_str());
					if(r!=0)
					{
						state=-1;
						break;
					}

					std::string out=getFile("out");
					if(out=="r")
						state=5;
					else if(out=="o")
						state=3;
					else if(out=="e")
					{
						system("sh");
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
				int r=system("dialog --yesno \"`cat urbackup/restore/restore_success`\" 7 50");
				if(r==0)
				{
					system("init 6");
				}
				exit(0);
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
					state=-1;
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