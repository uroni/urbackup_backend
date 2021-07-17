#include "client_restore_http.h"
#include "client_restore.h"
#include <mutex>
#include <thread>
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/mbrdata.h"
#include "../fsimageplugin/IFSImageFactory.h"

extern IFSImageFactory* image_fak;

namespace
{
	std::mutex g_restore_data_mutex;
	restore::LoginData g_login_data;
	int64 res_id = 0;

	struct SRestoreRes
	{
		restore::DownloadStatus dl_status;
		int ec;
		bool finished;
	};

	std::map<int64, SRestoreRes> restore_results;

	void setGLoginData(restore::LoginData login_data)
	{
		std::lock_guard<std::mutex> lock(g_restore_data_mutex);
		g_login_data = login_data;
	}
}

ACTION_IMPL(status)
{
	std::unique_ptr<IPipe> c(restore::connectToService());

	JSON::Object ret;

	ret.set("ok", true);

	if (!c)
	{
		ret.set("err", "Error connecting to restore service");
		restore::writeJsonResponse(tid, ret);
		return;
	}

	std::string pw = restore::restorePw();
	CTCPStack tcpstack;
	tcpstack.Send(c.get(), "STATUS DETAIL#pw=" + pw);
	std::string r = restore::getResponse(c);
	if (r.empty())
	{
		ret.set("err", "No response from restore service");
		restore::writeJsonResponse(tid, ret);
		return;
	}
	
	Server->setContentType(tid, "application/json");
	Server->Write(tid, r);
}

ACTION_IMPL(login)
{
	restore::LoginData login_data;
	login_data.has_login_data = POST["has_login_data"]=="1";
	login_data.username = POST["username"];
	login_data.password = POST["password"];

	JSON::Object ret;

	ret.set("ok", true);

	if (restore::do_login(login_data, false))
	{
		ret.set("success", true);
		setGLoginData(login_data);
	}
	else
	{
		ret.set("success", false);
	}

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_clientnames)
{
	int rc = 0;
	std::vector<std::string> clients = restore::getBackupclients(rc);

	JSON::Object ret;

	ret.set("ok", true);
	ret.set("rc", rc);
	if (rc != 0)
		ret.set("err", "Error getting clients on server. Rc=" + std::to_string(rc));

	JSON::Array j_clients;
	for (auto clientname : clients)
	{
		j_clients.add(clientname);
	}

	ret.set("clients", j_clients);

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_backupimages)
{
	std::unique_ptr<IPipe> c(restore::connectToService());

	JSON::Object ret;

	ret.set("ok", true);

	if (!c)
	{
		ret.set("err", "Error connecting to restore service");
		restore::writeJsonResponse(tid, ret);
		return;
	}

	std::string pw = restore::restorePw();
	CTCPStack tcpstack;
	tcpstack.Send(c.get(), "GET BACKUPIMAGES " + POST["restore_name"]+ "#pw=" + pw);
	std::string r = restore::getResponse(c);
	if (r.empty())
	{
		ret.set("err", "No response from restore service");
	}
	else
	{
		if (r[0] == '0')
		{
			ret.set("err", "No backup server found");
		}
		else
		{
			ret.set("images", restore::backup_images_output_to_json(r.substr(1)));
		}
	}

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(start_download)
{
	int img_id = watoi(POST["img_id"]);
	std::string img_time = POST["img_time"];
	std::string out = POST["out"];
	bool mbr = POST["mbr"] == "1";

	SRestoreRes* restore_res;
	int64 curr_res_id;
	{
		std::lock_guard<std::mutex> lock(g_restore_data_mutex);
		curr_res_id = res_id++;
		restore_res = &restore_results[curr_res_id];
	}

	std::thread dl_thread([img_id, img_time, mbr, out, restore_res]() {
		restore_res->ec = restore::downloadImage(img_id, img_time, out, mbr, g_login_data, restore_res->dl_status);
		std::lock_guard<std::mutex> lock(g_restore_data_mutex);
		restore_res->finished = true;
		});

	dl_thread.detach();

	JSON::Object ret;
	ret.set("ok", true);
	ret.set("res_id", curr_res_id);

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(download_progress)
{
	int64 res_id = watoi64(POST["res_id"]);

	SRestoreRes* restore_res = nullptr;

	{
		std::lock_guard<std::mutex> lock(g_restore_data_mutex);
		auto it = restore_results.find(res_id);
		if (it != restore_results.end())
			restore_res = &it->second;
	}

	JSON::Object ret;

	ret.set("ok", true);

	if (restore_res == nullptr)
	{
		ret.set("err", "Restore not found");
		restore::writeJsonResponse(tid, ret);
		return;
	}

	if (restore_res->finished)
	{
		ret.set("finished", true);
		restore::writeJsonResponse(tid, ret);
		return;
	}

	std::unique_ptr<IPipe> c(restore::connectToService());

	if (!c)
	{
		ret.set("err", "Error connecting to restore service");
		restore::writeJsonResponse(tid, ret);
		return;
	}

	CTCPStack tcpstack;
	std::string pw = restore::restorePw();
	tcpstack.Send(c.get(), "GET DOWNLOADPROGRESS#pw=" + pw);
	int lpc = 0;
	bool got_pc = false;

	std::string curr;
	size_t r = c->Read(&curr, 10000);
	for (int i = 0; i < linecount(curr); ++i)
	{
		std::string l = getline(i, curr);
		if (!restore::trim2(l).empty())
		{
			int npc = atoi(restore::trim2(l).c_str());
			got_pc = true;
			if (npc != lpc)
			{
				lpc = npc;
			}
		}
	}

	if (got_pc)
	{
		ret.set("pc", lpc);
	}

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(has_network_device)
{
	JSON::Object ret;

	ret.set("ok", true);
	ret.set("ret", restore::has_network_device());

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(ping_server)
{
	std::string servername = POST["servername"];
	std::thread t([servername]() {
		system(("./urbackuprestoreclient --ping-server \"" + servername + "\"").c_str());
		});

	t.detach();

	JSON::Object ret;
	ret.set("ok", true);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(has_internet_connection)
{
	JSON::Object ret;

	ret.set("ok", true);
	int rc;
	std::string errstatus;
	ret.set("ret", restore::has_internet_connection(rc, errstatus));
	ret.set("rc", rc);
	ret.set("errstatus", errstatus);

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(configure_server)
{
	bool active_client = POST["active"] == "1";

	if (active_client)
	{
		restore::configure_internet_server(POST["url"], POST["authkey"], POST["proxy"], false);
	}
	else
	{
		restore::configure_local_server();
	}

	JSON::Object ret;
	ret.set("ok", true);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_disks)
{
	std::vector<restore::SLsblk> drives = restore::lsblk("");

	JSON::Array j_disks;

	for (restore::SLsblk& blk : drives)
	{
		if (blk.type == "disk")
		{
			JSON::Object disk;
			disk.set("model", blk.model);
			disk.set("maj_min", blk.maj_min);
			disk.set("path", blk.path);
			disk.set("size", blk.size);
			disk.set("type", blk.type);
			j_disks.add(disk);
		}
	}

	JSON::Object ret;
	ret.set("ok", true);
	ret.set("disks", j_disks);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_is_disk_mbr)
{
	JSON::Object ret;
	ret.set("ok", true);
	ret.set("res", is_disk_mbr(POST["mbrfn"]));
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(write_mbr)
{
	std::string errmsg;
	bool b = restore::do_restore_write_mbr(POST["mbrfn"], POST["out_device"], errmsg);

	JSON::Object ret;
	ret.set("ok", true);
	ret.set("success", b);
	ret.set("errmsg", errmsg);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_partition)
{
	std::unique_ptr<IFsFile> f(Server->openFile(POST["mbrfn"], MODE_READ));
	if(f==nullptr)
	{
		JSON::Object ret;
		ret.set("ok", true);
		ret.set("success", false);
		ret.set("errmsg", "Cannot open mbr file. "+os_last_error_str());
		restore::writeJsonResponse(tid, ret);
		return;
	}
	size_t fsize=(size_t)f->Size();
	std::vector<char> buf(fsize);
	f->Read(buf.data(), static_cast<_u32>(fsize));

	CRData mbr(buf.data(), fsize);
	SMBRData mbrdata(mbr);
	if(mbrdata.hasError())
	{
		JSON::Object ret;
		ret.set("ok", true);
		ret.set("success", false);
		ret.set("errmsg", "Error while reading MBR file");
		restore::writeJsonResponse(tid, ret);
		return;
	}

	bool t_gpt_style;
	std::vector<IFSImageFactory::SPartition> partitions 
		= image_fak->readPartitions(mbrdata.mbr_data, mbrdata.gpt_header, mbrdata.gpt_table, t_gpt_style);

	std::string seldrive = POST["out_device"];

	Server->Log("Selected device: "+seldrive+" Partition: "+convert(mbrdata.partition_number));
	system(("partprobe "+seldrive+" > /dev/null 2>&1").c_str());
	std::string partpath = restore::getPartitionPath(seldrive, mbrdata.partition_number);
	std::unique_ptr<IFsFile> dev;
	if(!partpath.empty())
	{
		dev.reset(Server->openFile(partpath, MODE_RW));
	}
	int try_c=0;
	int delete_parts = 0;
	bool fix_gpt=true;

	while(!dev && try_c<10)
	{
		system(("partprobe "+seldrive+" > /dev/null 2>&1").c_str());
		Server->wait(10000);
		partpath = restore::getPartitionPath(seldrive, mbrdata.partition_number);
		if(!partpath.empty())
		{
			dev.reset(Server->openFile(partpath, MODE_RW));
		}

		if(!dev)
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
		JSON::Object ret;
		ret.set("ok", true);
		ret.set("success", false);
		ret.set("errmsg", "Could not open restore partition");
		restore::writeJsonResponse(tid, ret);
		return;
	}

	JSON::Object ret;
	ret.set("ok", true);
	ret.set("success", false);
	ret.set("partpath", partpath);
	restore::writeJsonResponse(tid, ret);
	return;
}

ACTION_IMPL(restart)
{
	system("init 6");
	JSON::Object ret;
	ret.set("ok", true);
	restore::writeJsonResponse(tid, ret);
}