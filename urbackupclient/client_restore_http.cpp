#include "client_restore_http.h"
#include "client_restore.h"
#include <mutex>
#include <thread>
#include <set>
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/mbrdata.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../urbackupcommon/os_functions.h"
#include "../Interface/Thread.h"

extern IFSImageFactory* image_fak;

namespace
{
	std::mutex g_restore_data_mutex;
	restore::LoginData g_login_data;
	int64 res_id = 0;

	std::atomic<bool> timezone_download_started(false);

	struct SRestoreRes
	{
		restore::DownloadStatus dl_status;
		restore::EDownloadResult ec;
		bool finished;
		std::string err;
		std::atomic<int> pc_complete;
	};

	std::map<int64, SRestoreRes> restore_results;

	void setGLoginData(restore::LoginData login_data)
	{
		std::lock_guard<std::mutex> lock(g_restore_data_mutex);
		g_login_data = login_data;
	}

	const std::string timezone_data_file = "/tmp/timezone_data.json";

	class DownloadTimezoneData : public IThread
	{
	public:
		void operator()()
		{
			int rc = system(("wget -q \"https://app.urbackup.com/api/online\" -O "+timezone_data_file).c_str());

			if (rc != 0)
			{
				Server->deleteFile(timezone_data_file);
			}

			delete this;
		}
	};
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

	if(mbr && !FileExists(out))
	{
		writestring("", out);
	}

	Server->Log("Start_download: id = \""+std::to_string(img_id)+"\" time = \""+img_time+ "\"", LL_INFO);

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
		ret.set("ec", restore_res->ec);
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

	/*std::unique_ptr<IPipe> c(restore::connectToService());

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

	restore::writeJsonResponse(tid, ret); */
}

ACTION_IMPL(has_network_device)
{
	JSON::Object ret;

	ret.set("ok", true);
	bool has_device = restore::has_network_device();
	ret.set("ret", has_device);

	bool did_not_start = false;
	if (has_device && timezone_download_started.compare_exchange_strong(did_not_start, true))
	{
		Server->createThread(new DownloadTimezoneData(), "dl tz data");
	}

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

	bool get_parts = POST["partitions"] == "1";

	JSON::Array j_disks;

	std::string last_model;

	for (restore::SLsblk& blk : drives)
	{
		if(blk.path.find("/dev/sr")==0)
			continue;
		if(blk.path.find("/dev/cdrom")==0)
			continue;
		if(blk.path.find("/dev/loop")==0)
			continue;
		if(blk.path.find("/dev/mapper")==0)
			continue;

		if(blk.type=="disk")
			last_model = blk.model;

		if (!get_parts && blk.type == "disk" ||
			get_parts && blk.type=="part")
		{
			JSON::Object disk;
			disk.set("model", blk.model);
			if(blk.type=="part" && blk.model.empty() &&
				!last_model.empty())
				disk.set("model", last_model);

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
	bool b = restore::do_restore_write_mbr(POST["mbrfn"], 
		POST["out_device"], true, errmsg);

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
	std::string partpath = restore::getPartitionPath(seldrive, mbrdata.partition_number);
	Server->Log("Partition path: "+partpath);
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
	ret.set("success", true);
	ret.set("partpath", partpath);
	ret.set("partnum", mbrdata.partition_number);
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

ACTION_IMPL(get_keyboard_layouts)
{
	std::string llist;
	JSON::Array j_layouts;
	if(os_popen("localectl list-x11-keymap-layouts", llist)==0)
	{
		std::vector<std::string> toks;
		Tokenize(llist, toks, "\n");

		for(auto tok: toks)
		{
			std::string ctok = trim(tok);
			if(!ctok.empty())
				j_layouts.add(ctok);
		}
	}

	JSON::Object ret;
	ret.set("layouts", j_layouts);
	ret.set("ok", true);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(set_keyboard_layout)
{
	writestring(POST["layout"]+"\n", "/home/urbackup/setxkbmap");
	JSON::Object ret;
	ret.set("ok", true);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_connection_settings)
{
	std::string settingsfn = "/run/live/medium/connection_settings.json";
	if(!FileExists(settingsfn))
	{
		JSON::Object ret;
		ret.set("ok", true);
		ret.set("no_config", true);
		restore::writeJsonResponse(tid, ret);
		return;
	}

	Server->setContentType(tid, "application/json");
	Server->Write(tid, getFile(settingsfn));
}

bool test_spill_space(const std::string& path)
{
	IFile* f = Server->openFile(path+"/32d59234-64cc-42dd-bc56-48e59dd4db6a.test", MODE_WRITE);
	ScopedDeleteFile del_f(f);

	if(f==nullptr)
		return false;

	return f->Write("test")==4 && f->Sync();
}

std::string get_fs(const std::string& dev)
{
	std::string out;
	int rc = os_popen("file -s -L -b \""+dev+"\"", out);

	if(rc!=0)
		return std::string();

	if(out.find("NTFS, ")!=std::string::npos)
		return "ntfs";

	if(out.find("FAT ")!=std::string::npos)
		return "fat";

	if(out.find("ext2 ")!=std::string::npos ||
		out.find("ext3 ")!=std::string::npos ||
		out.find("ext4 ")!=std::string::npos)
		return "ext";

	if(out.find("XFS ")!=std::string::npos)
		return "xfs";

	return "unknown";
}

ACTION_IMPL(get_spill_disks)
{
	std::vector<restore::SLsblk> drives = restore::lsblk("-b");

	std::string exclude = POST["exclude"];

	JSON::Object ret;

	if(test_spill_space("/run/live/medium"))
	{
		ret.set("live_medium", true);
		ret.set("live_medium_space", os_free_space("/run/live/medium"));
	}

	ret.set("ok", true);

	std::string last_model;
	std::string disk_path;
	JSON::Array j_disks;
	for (restore::SLsblk& blk : drives)
	{
		if(blk.path.find("/dev/sr")==0)
			continue;
		if(blk.path.find("/dev/cdrom")==0)
			continue;
		if(blk.path.find("/dev/loop")==0)
			continue;
		if(blk.path.find("/dev/mapper")==0)
			continue;

		if(blk.type=="disk")
		{
			last_model = blk.model;
			disk_path = blk.path;
		}

		if(disk_path==exclude ||
			blk.path==exclude)
			continue;

		JSON::Object disk;
		disk.set("model", blk.model);
		if(blk.type=="part" && blk.model.empty() &&
			!last_model.empty())
			disk.set("model", last_model);

		disk.set("maj_min", blk.maj_min);
		disk.set("path", blk.path);
		disk.set("devpath", disk_path);
		disk.set("size", watoi64(blk.size));
		disk.set("type", blk.type);
		disk.set("fstype", get_fs(blk.path));
		j_disks.add(disk);
	}

	ret.set("disks", j_disks);
	restore::writeJsonResponse(tid, ret);
}

namespace
{
	struct SSpillFile
	{
		std::string fn;
		int64 size;
		std::string lodev;
		int64 sizesz;
	};

	void cleanup_spill(const std::vector<SSpillFile>& spill_files)
	{
		for(auto sf: spill_files) 
		{
			if(!sf.lodev.empty())
			{
				system(("losetup -d \""+sf.lodev+"\"").c_str());
			}
		}

		std::string out;
		int rc = os_popen("losetup -l -n", out);
		if(rc==0)
		{
			std::vector<std::string> toks;
			Tokenize(out, toks, "\n");
			for(auto& tok: toks) 
			{
				if(tok.find("32d59234-64cc-42dd-bc56-48e59dd4db6a")!=std::string::npos)
				{
					system(("losetup -d \""+getuntil(" ", tok)+"\"").c_str());
				}
			}
		}

		std::vector<SFile> mnts = getFiles("/mnt");

		for(const SFile& m: mnts)
		{
			if(m.isdir && m.name.find("spilldisk")==0)
			{
				std::vector<SFile> spillfiles = getFiles("/mnt/"+m.name);

				for(const SFile& f: spillfiles)
				{
					if(f.name.find("32d59234-64cc-42dd-bc56-48e59dd4db6a")!=std::string::npos)
					{
						Server->deleteFile("/mnt/"+m.name+"/"+f.name);
					}
				}

				system(("umount \"/mnt/"+m.name+"\"").c_str());
				os_remove_dir("/mnt/"+m.name);
			}
		}

		system("dmsetup remove spill_disk");
	}
}

ACTION_IMPL(test_spill_disks)
{
	system("dmsetup remove spill_disk");
	
	std::vector<SSpillFile> spill_files;
	cleanup_spill(spill_files);

	std::vector<std::string> disks;
	for(size_t idx=0;POST.find("disk"+std::to_string(idx))!=POST.end();++idx)
	{
		disks.push_back(POST["disk"+std::to_string(idx)]);
	}

	os_create_dir_recursive("/mnt/spilltest");
	system("umount /mnt/spilltest");

	JSON::Array j_disks;

	for(std::string& disk: disks) {

		std::string out;
		int rc = os_popen("mount \""+disk+"\" /mnt/spilltest 2>&1", out);

		if(rc==0) {
			JSON::Object obj;
			obj.set("path", disk);
			obj.set("space", os_free_space("/mnt/spilltest"));
			j_disks.add(obj);
		} else {
			Server->Log("Mounting "+disk+" failed: "+out);
		}

		system("umount /mnt/spilltest");
	}

	JSON::Object ret;
	ret.set("ok", true);
	ret.set("disks", j_disks);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(setup_spill_disks)
{
	system("dmsetup remove spill_disk");
	
	std::vector<SSpillFile> spill_files;
	cleanup_spill(spill_files);

	struct SSpillDisk
	{
		std::string path;
		int destructive;
		size_t idx;
		std::string mountpath;
	};
	std::vector<SSpillDisk> disks;
	for(size_t idx=0;POST.find("disk"+std::to_string(idx))!=POST.end();++idx)
	{
		SSpillDisk sd;
		sd.path = POST["disk"+std::to_string(idx)];
		sd.destructive = watoi(POST["destructive"+std::to_string(idx)]);
		sd.idx = idx;
		disks.push_back(sd);
	}	

	std::string orig_dev = POST["orig_dev"];

	int64 orig_dev_sz;
	{
		std::string out;
		int rc = os_popen("blockdev --getsz \""+orig_dev+"\" 2>&1", out);

		if(rc!=0)
		{
			JSON::Object ret;
			ret.set("ok", true);
			ret.set("err", "Error getting orig dev size "+orig_dev+": "+out);
			restore::writeJsonResponse(tid, ret);
			return;
		}

		orig_dev_sz = watoi(trim(out));
	}

	std::string dm_table = "0 "+std::to_string(orig_dev_sz)+" linear "+orig_dev+" 0";

	int64 dm_offs = orig_dev_sz;

	for(auto& sd: disks)
	{
		if(sd.path=="live_medium")
		{
			sd.mountpath="/run/live/medium";
		}
		else
		{
			sd.mountpath = "/mnt/spilldisk"+std::to_string(sd.idx);
			os_create_dir_recursive(sd.mountpath);

			system(("umount "+sd.mountpath).c_str());

			if(sd.destructive!=0) 
			{
				std::string out;
				int rc = os_popen("mkfs.ext4 -F \""+sd.path+"\" 2>&1", out);

				if(rc!=0)
				{
					cleanup_spill(spill_files);

					JSON::Object ret;
					ret.set("ok", true);
					ret.set("err", "Error creating filesystem at "+sd.path+": "+out);
					restore::writeJsonResponse(tid, ret);
					return;
				}

				system(("tune2fs -m 0 \""+sd.path+"\"").c_str());
			}

			std::string out;
			int rc = os_popen("mount \""+sd.path+"\" "+sd.mountpath+" 2>&1", out);

			if(rc!=0)
			{
				cleanup_spill(spill_files);

				JSON::Object ret;
				ret.set("ok", true);
				ret.set("err", "Error mounting "+sd.path+" at "+sd.mountpath+": "+out);
				restore::writeJsonResponse(tid, ret);
				return;
			}
		}

		const int64 freespace_leave = 250LL*1024*1024;

		int64 freespace = os_free_space(sd.mountpath);

		if(freespace<freespace_leave)
			freespace = 0;
		else
			freespace-=freespace_leave;

		size_t idx=0;

		const int64 spill_file_size_max = 1LL*1024*1024*1024*1024;
		const int64 spill_file_size_min = 1LL*1024*1024*1024;

		while(freespace>freespace_leave)
		{
			SSpillFile sf;

			sf.size = std::min(spill_file_size_max, freespace);
			sf.fn = sd.mountpath+"/spill-32d59234-64cc-42dd-bc56-48e59dd4db6a-"+std::to_string(idx)+".img";

			++idx;

			std::unique_ptr<IFsFile> f(Server->openFile(sf.fn , MODE_WRITE));

			bool resize_ok=false;
			if(f)
			{
				if(!f->Resize(sf.size, true))
				{
					if(sf.size>spill_file_size_min)
					{
						sf.size = spill_file_size_min;
						resize_ok = f->Resize(sf.size, true);
					}
				}
				else
				{
					resize_ok=true;
				}
			}

			if(resize_ok)
			{
				freespace -= sf.size;
				std::string out;
				int rc = os_popen("losetup -f --show \""+sf.fn+"\" 2>&1", out);

				if(rc!=0)
				{
					cleanup_spill(spill_files);

					JSON::Object ret;
					ret.set("ok", true);
					ret.set("err", "Error setting up loop device for "+sf.fn+ ": "+out);
					restore::writeJsonResponse(tid, ret);
					return;
				}

				sf.lodev = trim(out);

				out.clear();
				rc = os_popen("blockdev --getsz \""+sf.lodev+"\" 2>&1", out);

				if(rc!=0)
				{
					cleanup_spill(spill_files);

					JSON::Object ret;
					ret.set("ok", true);
					ret.set("err", "Error loop device size of "+sf.lodev+ ": "+out);
					restore::writeJsonResponse(tid, ret);
					return;
				}

				sf.sizesz = watoi64(trim(out));

				dm_table+="\n"+std::to_string(dm_offs)+" "+std::to_string(sf.sizesz)+" linear "+sf.lodev+" 0";
				dm_offs+=sf.sizesz;

				spill_files.push_back(sf);
			}
			else
			{
				f.reset();
				Server->deleteFile(sf.fn);				

				cleanup_spill(spill_files);

				JSON::Object ret;
				ret.set("ok", true);
				ret.set("err", "Error creating spill file at \""+sf.fn+"\"");
				restore::writeJsonResponse(tid, ret);
				return;
			}
		}
	}

	writestring(dm_table, "spill_disk_dm_table");

	std::string out;
	int rc = os_popen("cat spill_disk_dm_table | dmsetup create spill_disk 2>&1", out);

	if(rc!=0)
	{
		cleanup_spill(spill_files);

		JSON::Object ret;
		ret.set("ok", true);
		ret.set("err", "Error setting up spill disk: "+trim(out));
		restore::writeJsonResponse(tid, ret);
		return;
	}

	for(auto sf: spill_files) 
	{
		if(!sf.lodev.empty())
		{
			system(("losetup -d \""+sf.lodev+"\"").c_str());
		}
	}

	JSON::Object ret;
	ret.set("ok", true);
	ret.set("path", "/dev/mapper/spill_disk");
	ret.set("orig_size", std::to_string(orig_dev_sz));
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(cleanup_spill_disks)
{
	std::vector<SSpillFile> spill_files;
	cleanup_spill(spill_files);

	JSON::Object ret;
	ret.set("ok", true);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(resize_disk)
{
	std::string disk_fn = POST["disk_fn"];

	int64 new_size = watoi64(POST["new_size"]);

	SRestoreRes* restore_res;
	int64 curr_res_id;
	{
		std::lock_guard<std::mutex> lock(g_restore_data_mutex);
		curr_res_id = res_id++;
		restore_res = &restore_results[curr_res_id];
	}

	std::thread t([disk_fn, new_size, restore_res]()
	{
		if(get_fs(disk_fn)!="ntfs")
		{
			restore_res->err = "Only resizing NTFS is currently supported";
			restore_res->ec=restore::EDownloadResult_Ok;
			restore_res->finished=true;
			return;	
		}

		std::string err = restore::resize_ntfs(new_size*512, disk_fn, restore_res->pc_complete);

		restore_res->err = err;
		restore_res->ec=restore::EDownloadResult_Ok;
		restore_res->finished=true;
	});

	t.detach();

	JSON::Object ret;
	ret.set("ok", true);
	ret.set("res_id", curr_res_id);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(restore_finished)
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
		ret.set("ec", restore_res->ec);
		if(!restore_res->err.empty())
		{
			ret.set("err", restore_res->err);
		}
	}
	else
	{
		ret.set("pcdone", restore_res->pc_complete.load());
	}

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(resize_part)
{
	std::string disk_fn = POST["disk_fn"];
	int partnum = watoi(POST["partnum"]);

	int64 new_size = watoi64(POST["new_size"]);

	JSON::Object ret;
	ret.set("ok", true);

	std::string out;
	int rc = os_popen("parted \""+disk_fn+"\" -sm unit B print", out);
	if(rc!=0)
	{
		ret.set("err", out);
		restore::writeJsonResponse(tid, ret);
		return;
	}

	int64 start = -1;

	std::vector<std::string> lines;
	Tokenize(out, lines, "\n");
	for(auto line: lines)
	{
		std::vector<std::string> toks;
		Tokenize(line, toks, ":");

		if(toks.size()>2)
		{
			int cpartnum = watoi(toks[0]);

			if(cpartnum==partnum)
			{
				start = watoi64(toks[1]);
				break;
			}
		}
	}

	if(start==-1)
	{
		ret.set("err", "Error finding partition start. Out: "+out);
		restore::writeJsonResponse(tid, ret);
		return;
	}

	out.clear();
	rc = os_popen("parted \""+disk_fn+"\" -sm resizepart "+std::to_string(partnum)+" "+std::to_string(start + new_size*512)+"B 2>&1", out);
	
	if(rc!=0)
	{
		rc = os_popen("echo -e \"resizepart "+std::to_string(partnum)+" "+std::to_string(start + new_size*512)+"B\\nYes\\n\" | parted \""+disk_fn+"\" ---pretend-input-tty", out);
		if(rc!=0)
		{
			ret.set("err", out);
		}
	}

	restore::writeJsonResponse(tid, ret);
}

namespace restore
{
	std::string resize_ntfs(int64 new_size, const std::string& disk_fn, std::atomic<int>& pc_complete)
	{
#ifndef _WIN32
		FILE* in = NULL;
		in = popen(("stdbuf -o0 ntfsresize -f -f -s "+std::to_string(new_size)+" \""+disk_fn+"\" 2>&1").c_str(), "re");
		if(in==NULL)
		{
			return "Error opening command: "+ os_last_error_str();
		}

		char buf[4096];
		std::string out;
		std::string line;
		while(true)
		{
			int ich = fgetc(in);

			if(ich==EOF)
				break;

			char ch = static_cast<char>(ich);

			if(ch!='\n' && ch!='\r')
			{
				line += ch;
			}
			else
			{
				if(line.find("percent completed")!=std::string::npos)
				{
					std::string str_pc = trim(getuntil("percent", line));
					if(!str_pc.empty())
						pc_complete = watoi(str_pc);
				}
				else
				{
					Server->Log("ntrfsresize: "+line, LL_INFO);

					out += line + "\n";
				}

				line.clear();
			}
		}

		int rc = pclose(in);

		if(rc!=0)
		{
			return "Error runnning resize command. Rc: "+std::to_string(rc)+". Output: "+out;
		}

#endif

		return std::string();
	}
}

ACTION_IMPL(capabilities)
{
	JSON::Object ret;
	ret.set("ok", true);

#ifndef _WIN32
	ret.set("keyboard_config", true);
	ret.set("restore_spill", true);
#else
	ret.set("keyboard_config", false);
	ret.set("restore_spill", false);
#endif
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_tmpfn)
{
	JSON::Object ret;
	ret.set("ok", true);

	std::unique_ptr<IFsFile> tmpf(Server->openTemporaryFile());

	if(!tmpf)
	{
		ret.set("err", "Cannot open tmpfile: "+os_last_error_str());
	}
	else
	{
		std::string tmpfn = tmpf->getFilename();
		ret.set("fn", tmpfn);
		tmpf.reset();
		Server->deleteFile(tmpfn);
	}

	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_timezone_data)
{
	std::string data;
	int64 starttime = Server->getTimeMS();

	while (data.empty() && Server->getTimeMS() - starttime < 1000)
	{
		data = getFile(timezone_data_file);

		if (data.empty())
		{
			Server->wait(50);
		}
	}

	if (data.empty())
	{
		JSON::Object ret;
		ret.set("ok", false);
		restore::writeJsonResponse(tid, ret);
	}
	else
	{
		Server->setContentType(tid, "application/json");
		Server->Write(tid, data);
	}
}

static std::vector<std::pair<std::string, std::string> > getTimezones()
{
	std::vector<std::pair<std::string, std::string> > ret;
	std::string tzdata;
	int rc = os_popen("timedatectl list-timezones", tzdata);
	if (rc != 0)
	{
		return ret;
	}

	std::vector<std::string> lines;
	Tokenize(tzdata, lines, "\n");

	for (auto& line : lines)
	{
		if (line.find("/") != std::string::npos)
		{
			ret.push_back(std::make_pair(getuntil("/", line), getafter("/", line)));
		}
		else
		{
			ret.push_back(std::make_pair(trim(line), ""));
		}
	}

	return ret;
}

ACTION_IMPL(get_timezone_areas)
{
	auto tzs = getTimezones();

	std::set<std::string> tzAreas;
	for (auto tz : tzs)
	{
		if (!tz.first.empty())
			tzAreas.insert(tz.first);
	}

	JSON::Object ret;
	ret.set("ok", true);
	JSON::Array areas;

	for (auto tz : tzAreas)
		areas.add(tz);

	ret.set("areas", areas);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(get_timezone_cities)
{
	std::string area = POST["area"];
	auto tzs = getTimezones();

	JSON::Object ret;
	ret.set("ok", true);

	JSON::Array cities;
	for (auto tz : tzs)
	{
		if(tz.first == area && !tz.second.empty())
			cities.add(tz.second);
	}

	ret.set("cities", cities);
	restore::writeJsonResponse(tid, ret);
}

ACTION_IMPL(set_timezone)
{
	std::string tz = POST["tz"];

	JSON::Object ret;
	ret.set("ok", true);

	if (tz.empty() || tz.find('"')!=std::string::npos)
	{
		ret.set("ok", false);
		restore::writeJsonResponse(tid, ret);
		return;
	}

	int rc = system(("timedatectl set-timezone \"" + tz + "\"").c_str());

	if (rc != 0)
		ret.set("ok", false);

	restore::writeJsonResponse(tid, ret);
}

