#include "FilesystemManager.h"
#include "../btrfs/btrfsplugin/IBtrfsFactory.h"
#include "DocanyMount.h"
#include "../stringtools.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../clouddrive/IClouddriveFactory.h"
#include "../common/miniz.h"
#include "../urbackupcommon/backup_url_parser.h"
#include "../urbackupcommon/os_functions.h"
#include "ClientService.h"
#include <chrono>
#include <atomic>
#include <memory>
#include <thread>
#include <condition_variable>
using namespace std::chrono_literals;

extern IBtrfsFactory* btrfs_fak;
extern IFSImageFactory* image_fak;
extern IClouddriveFactory* clouddrive_fak;

std::mutex FilesystemManager::mutex;
std::map<std::string, IBackupFileSystem*> FilesystemManager::filesystems;

namespace
{
	class TmpFileHandlingFileSystem : public IBackupFileSystem
	{
		std::unique_ptr<IBackupFileSystem> fs;
		bool do_stop = false;
		std::mutex flush_mutex;
		std::condition_variable flush_cond;
		std::thread flush_thread;
		std::atomic<bool> dirty = false;
	public:

		TmpFileHandlingFileSystem(IBackupFileSystem* fs)
			: fs(fs)
		{
			flush_thread = std::thread([this]() {
				this->regular_sync();
				});
		}

		TmpFileHandlingFileSystem(const TmpFileHandlingFileSystem&) = delete;
		void operator=(const TmpFileHandlingFileSystem&) = delete;
		TmpFileHandlingFileSystem(TmpFileHandlingFileSystem&&) = delete;
		void operator=(TmpFileHandlingFileSystem&&) = delete;		

		~TmpFileHandlingFileSystem()
		{
			{
				std::unique_lock<std::mutex> lock(flush_mutex);
				do_stop = true;
				flush_cond.notify_all();
			}

			flush_thread.join();
		}

		virtual bool hasError() override
		{
			return fs->hasError();
		}

		bool isTmpFile(const std::string& path)
		{
			return next(path, 0, "tmp://");
		}

		std::string tmpFileName(const std::string& path)
		{
			return path.substr(6);
		}

		virtual IFsFile* openFile(const std::string& path, int mode) override
		{
			if (isTmpFile(path))
				return Server->openFile(tmpFileName(path), mode);

			if(mode!=MODE_READ && mode!=MODE_READ_DEVICE &&
				mode!=MODE_READ_SEQUENTIAL)
				dirty = true;

			return fs->openFile(path, mode);
		}

		virtual bool reflinkFile(const std::string& source, const std::string& dest) override
		{
			dirty = true;
			return fs->reflinkFile(source, dest);
		}

		virtual bool createDir(const std::string& path) override
		{
			dirty = true;
			return fs->createDir(path);
		}

		virtual bool deleteFile(const std::string& path) override
		{
			if (isTmpFile(path))
				return Server->deleteFile(tmpFileName(path));

			dirty = true;

			return fs->deleteFile(path);
		}

		virtual int getFileType(const std::string& path) override
		{
			if (isTmpFile(path))
				return os_get_file_type(tmpFileName(path));

			return fs->getFileType(path);
		}

		virtual bool sync(const std::string& path) override
		{
			bool ret = fs->sync(path);
			if (ret)
				dirty = false;
			return ret;
		}

		virtual std::vector<SFile> listFiles(const std::string& path) override
		{
			return fs->listFiles(path);
		}

		virtual bool createSubvol(const std::string& path) override
		{
			dirty = true;
			return fs->createSubvol(path);
		}

		virtual bool createSnapshot(const std::string& src_path, const std::string& dest_path) override
		{
			dirty = true;
			return fs->createSnapshot(src_path, dest_path);
		}

		virtual bool deleteSubvol(const std::string& path) override
		{
			dirty = true;
			return fs->deleteSubvol(path);
		}

		virtual bool rename(const std::string& src_name, const std::string& dest_name) override
		{
			dirty = true;
			return fs->rename(src_name, dest_name);
		}

		virtual bool removeDirRecursive(const std::string& path) override
		{
			dirty = true;
			return fs->removeDirRecursive(path);
		}

		virtual bool directoryExists(const std::string& path) override
		{
			return fs->directoryExists(path);
		}

		virtual bool linkSymbolic(const std::string& target, const std::string& lname) override
		{
			return fs->linkSymbolic(target, lname);
		}

		virtual bool copyFile(const std::string& src, const std::string& dst, bool flush = false, std::string* error_str = nullptr) override
		{
			return fs->copyFile(src, dst, flush, error_str);
		}

		virtual int64 totalSpace() override
		{
			return fs->totalSpace();
		}

		virtual int64 freeSpace() override
		{
			return fs->freeSpace();
		}
		virtual int64 freeMetadataSpace() override
		{
			return fs->freeMetadataSpace();
		}
		virtual int64 unallocatedSpace() override
		{
			return fs->unallocatedSpace();
		}
		virtual bool forceAllocMetadata() override
		{
			return fs->forceAllocMetadata();
		}
		virtual bool balance(int usage, size_t limit, bool metadata, bool& enospc, size_t& relocated) override
		{
			return fs->balance(usage, limit, metadata, enospc, relocated);
		}

		virtual std::string fileSep() override
		{
			return fs->fileSep();
		}

		virtual std::string filePath(IFile* f) override
		{
			return fs->filePath(f);
		}

		virtual bool getXAttr(const std::string& path, const std::string& key, std::string& value) override
		{
			return fs->getXAttr(path, key, value);
		}
		virtual bool setXAttr(const std::string& path, const std::string& key, const std::string& val) override
		{
			dirty = true;
			return fs->setXAttr(path, key, val);
		}
		virtual std::string getName() override
		{
			return fs->getName();
		}
		virtual IFile* getBackingFile() override
		{
			return fs->getBackingFile();
		}
		virtual std::string lastError() override
		{
			return fs->lastError();
		}
		virtual std::vector<SChunk> getChunks() override
		{
			return fs->getChunks();
		}

		void regular_sync()
		{
			std::unique_lock<std::mutex> lock(flush_mutex);
			while (!do_stop)
			{
				flush_cond.wait_for(lock, 10s);

				lock.unlock();

				if(dirty.exchange(false, std::memory_order_release))
				{
					if (!fs->sync(std::string()))
						break;
				}

				lock.lock();
			}
		}
};
}

bool FilesystemManager::openFilesystemImage(const std::string& url, const std::string& url_params,
	const str_map& secret_params)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(url) != filesystems.end())
		return true;

	IFile* img = nullptr;

	IClouddriveFactory::CloudSettings settings;
	std::unique_ptr<TmpFileHandlingFileSystem> tmpcachefs;

	if (next(url, 0, "file://"))
	{
		std::string fpath = url.substr(7);
		std::string ext = findextension(fpath);
		if (ext == "img" || ext == "raw")
		{
			img = Server->openFile(fpath, MODE_RW);
		}
		else if (ext == "vhdx")
		{
			IVHDFile* vhdfile = image_fak->createVHDFile(fpath, false, 0,
				2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX);

			if (vhdfile == nullptr || !vhdfile->isOpen())
				return false;

			img = vhdfile;
		}
	}
	else if (parse_backup_url(url, url_params, secret_params, settings) )
	{
		if (!FileExists(settings.cache_img_path))
		{
			Server->Log("Creating new cache file system", LL_INFO);

			int64 cache_size = 2LL * 1024 * 1024 * 1024;
			std::unique_ptr<IVHDFile> dst_vhdx(image_fak->createVHDFile(settings.cache_img_path + ".new", false, cache_size,
				2 * 1024 * 1024, true, IFSImageFactory::ImageFormat_VHDX));

			if (!dst_vhdx || !dst_vhdx->isOpen())
			{
				Server->Log("Could not open cache vhdx to crate at " + settings.cache_img_path, LL_ERROR);
				return false;
			}

			if (!btrfs_fak->formatVolume(dst_vhdx.get()))
			{
				Server->Log("Could not btrfs format cache volume", LL_ERROR);
				return false;
			}

			dst_vhdx.reset();

			if (!os_rename_file(settings.cache_img_path + ".new",
				settings.cache_img_path))
			{
				Server->Log("Error renaming " + settings.cache_img_path + ". " + os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		IVHDFile* cachevhdx = image_fak->createVHDFile(settings.cache_img_path, false, 0,
			2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX);

		if (cachevhdx == nullptr || !cachevhdx->isOpen())
		{
			Server->Log("Could not open cache vhdx at " + settings.cache_img_path, LL_ERROR);
			return false;
		}

		IBackupFileSystem* cachefs = btrfs_fak->openBtrfsImage(cachevhdx);
		if (cachefs == nullptr)
		{
			Server->Log("Could not open cache btrfs at " + settings.cache_img_path, LL_ERROR);
			return false;
		}
		
		settings.size = 20LL * 1024 * 1024 * 1024; //20GB

		tmpcachefs = std::make_unique<TmpFileHandlingFileSystem>(cachefs);

		img = clouddrive_fak->createCloudFile(tmpcachefs.get(), settings);
	}

	if (img == nullptr)
		return false;

	if (img->RealSize() == 0)
	{
		if (!btrfs_fak->formatVolume(img))
		{
			Server->Log("Could not btrfs format volume", LL_ERROR);
			return false;
		}
	}

	IBackupFileSystem* fs = btrfs_fak->openBtrfsImage(img);
	if (fs == nullptr)
		return false;

	clouddrive_fak->setTopFs(img, fs);

	tmpcachefs.release();

	filesystems[url] = fs;

	return true;
}

bool FilesystemManager::mountFileSystem(const std::string& url, const std::string& url_params,
	const str_map& secret_params, const std::string& mount_path)
{
	if (!openFilesystemImage(url, url_params, secret_params))
		return false;

	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(url) == filesystems.end())
	{
		return false;
	}
	
	IBackupFileSystem* fs = filesystems[url];

	std::thread dm([url, fs, mount_path]() {
		#ifndef _WIN32
		//TODO: Implement fuse
		#else
		if (!dokany_mount(fs, mount_path))
		{
			Server->Log("Mounting fs " + url + " at \"" + mount_path + "\" failed", LL_ERROR);
		}
		#endif
		});

	dm.detach();

	return true;
}

IBackupFileSystem* FilesystemManager::getFileSystem(const std::string& path)
{
	std::lock_guard<std::mutex> lock(mutex);

	auto it = filesystems.find(path);
	if (it == filesystems.end())
		return nullptr;

	return it->second;
}

void FilesystemManager::startupMountFileSystems()
{
	std::vector<SFile> facet_dirs = getFiles("urbackup");

	for (SFile& fdir : facet_dirs)
	{
		if (!fdir.isdir || !next(fdir.name, 0, "data_"))
			continue;

		int facet_id = watoi(getafter("data_", fdir.name));

		std::vector<SFile> settings_files = getFiles("urbackup/" + fdir.name);

		for (SFile& sfn : settings_files)
		{
			if (sfn.isdir ||
				(sfn.name != "settings.cfg" &&
					!next(sfn.name, 0, "settings_")))
				continue;

			std::string clientsubname;
			if (sfn.name != "settings.cfg")
			{
				clientsubname = getbetween("settings_", ".cfg", sfn.name);

				if (clientsubname.empty())
					continue;
			}

			std::string dest, dest_params, computername;
			str_map dest_secret_params;
			size_t max_backups = 1;
			std::string perm_uid;
			if (!ClientConnector::getBackupDest(clientsubname, facet_id, dest,
				dest_params, dest_secret_params, computername, max_backups, perm_uid))
				continue;

			std::string backups_mpath;

			if (facet_id != 1)
				backups_mpath += std::to_string(facet_id);

			if (!clientsubname.empty())
			{
				if (!backups_mpath.empty())
					backups_mpath += "_";

				backups_mpath += clientsubname;
			}

			backups_mpath = "backups" + backups_mpath;


			if (!os_directory_exists(backups_mpath)
				&& !os_create_dir(backups_mpath))
				continue;

			std::string fmpath = os_get_final_path(backups_mpath);

			mountFileSystem(dest, dest_params, dest_secret_params,
				fmpath);
		}
	}
}
