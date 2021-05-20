#include "FilesystemManager.h"
#include "../btrfs/btrfsplugin/IBtrfsFactory.h"
#include "DocanyMount.h"
#include "../stringtools.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../clouddrive/IClouddriveFactory.h"
#include "../common/miniz.h"

extern IBtrfsFactory* btrfs_fak;
extern IFSImageFactory* image_fak;
extern IClouddriveFactory* clouddrive_fak;

namespace
{
#include "cache_init_vhdx.h"

	std::string get_cache_init_vhdx()
	{
		size_t out_len;
		void* cdata = tinfl_decompress_mem_to_heap(btrfs2_vhdx_z, btrfs2_vhdx_z_len, &out_len, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32);
		if (cdata == NULL)
		{
			return std::string();
		}

		std::string ret(reinterpret_cast<char*>(cdata), reinterpret_cast<char*>(cdata) + out_len);
		mz_free(cdata);
		return ret;
	}

	bool copy_vhdx(IVHDFile* src_vhdx, IFile* dst)
	{
		std::vector<char> buf(512 * 1024);
		unsigned int blocksize = src_vhdx->getBlocksize();
		for (int64 pos = 0; pos < src_vhdx->Size();)
		{
			if (!src_vhdx->this_has_sector())
			{
				pos += blocksize;
				src_vhdx->Seek(pos);
				continue;
			}

			int64 spos = pos;
			while (pos < spos + blocksize)
			{
				size_t toread = (std::min)(static_cast<size_t>((spos + blocksize) - pos), buf.size());
				size_t read;
				if (!src_vhdx->Read(buf.data(), toread, read))
					return false;

				if (dst->Write(pos, buf.data(), static_cast<_u32>(read)) != read)
					return false;

				pos += read;
			}
		}

		return true;
	}

	bool copy_vhdx(const std::string& src_fn, IFile* dst)
	{
		std::unique_ptr<IVHDFile> src_vhdx(image_fak->createVHDFile(src_fn, true, 0,
			2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX));

		if (!src_vhdx ||
			!src_vhdx->isOpen())
			return false;

		return copy_vhdx(src_vhdx.get(), dst);
	}

	bool copy_vhdx(const std::string& src_fn, const std::string& dst_fn, int64 dst_size)
	{
		std::unique_ptr<IVHDFile> dst_vhdx(image_fak->createVHDFile(dst_fn + ".new", false, dst_size,
			2 * 1024 * 1024, true, IFSImageFactory::ImageFormat_VHDX));

		if (!dst_vhdx ||
			!dst_vhdx->isOpen())
			return false;

		if (!copy_vhdx(src_fn, dst_vhdx.get()))
			return false;

		dst_vhdx.reset();

		return os_rename_file(dst_fn + ".new", dst_fn);
	}

	bool create_cache_init_vhdx()
	{
		if (!FileExists("urbackup/cache_init.vhdx"))
		{
			std::unique_ptr<IFile> f(Server->openFile("urbackup/cache_init.vhdx.new", MODE_WRITE));
			if (!f)
				return false;

			std::string data = get_cache_init_vhdx();

			if (f->Write(data) != data.size())
				return false;

			if (!f->Sync())
				return false;

			f.reset();

			if (!os_rename_file("urbackup/cache_init.vhdx.new", "urbackup/cache_init.vhdx"))
				return false;
		}

		return true;
	}
}

std::mutex FilesystemManager::mutex;
std::map<std::string, IBackupFileSystem*> FilesystemManager::filesystems;

bool FilesystemManager::openFilesystemImage(const std::string& url, const std::string& url_params,
	const str_map& secret_params)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(url) != filesystems.end())
		return true;

	IFile* img = nullptr;

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
	else if (next(url, 0, "s3://") ||
		next(url, 0, "ss3://") )
	{
		bool ssl = next(url, 0, "ss3://");
		IClouddriveFactory::CloudSettings settings;
		settings.endpoint = IClouddriveFactory::CloudEndpoint::S3;

		std::string authorization = getbetween(ssl ? "ss3://" : "s3://", "@", url);
		std::string server = getafter("@", url);
		if (server.empty())
		{
			server = url.substr(5);
		}

		settings.s3_settings.access_key = getuntil(":", authorization);
		settings.s3_settings.secret_access_key = getafter(":", authorization);

		settings.s3_settings.endpoint = (ssl ? "https://" : "http://" ) + getuntil("/", server);
		settings.s3_settings.bucket_name = getafter("/", server);

		auto encryption_key_it = secret_params.find("encryption_key");
		if (encryption_key_it != secret_params.end())
			settings.encryption_key = encryption_key_it->second;

		if (settings.s3_settings.access_key.empty())
		{
			auto access_key_it = secret_params.find("access_key");
			if (access_key_it != secret_params.end())
				settings.s3_settings.access_key = access_key_it->second;
		}

		if (settings.s3_settings.secret_access_key.empty())
		{
			auto secret_access_key_it = secret_params.find("secret_access_key");
			if (secret_access_key_it != secret_params.end())
				settings.s3_settings.secret_access_key = secret_access_key_it->second;
		}

		std::string nurl = (ssl ? "ss3://" : "s3://") + server;
		std::string cacheid = Server->GenerateHexMD5(nurl);
		std::string cache_img_path = "urbackup/" + cacheid + ".vhdx";
		settings.s3_settings.cache_db_path = "urbackup/" + cacheid + ".db";

		if (!FileExists(cache_img_path))
		{
			if (!create_cache_init_vhdx())
				return false;

			if (!copy_vhdx("urbackup/cache_init.vhdx", cache_img_path, 1LL * 1024 * 1024 * 1024))
			{
				Server->Log("Could not copy urbackup/cache_init.vhdx to " + cache_img_path + ". " + os_last_error_str(),
					LL_ERROR);
				return false;
			}
		}

		IVHDFile* cachevhdx = image_fak->createVHDFile(cache_img_path, false, 0,
			2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX);

		if (cachevhdx == nullptr || !cachevhdx->isOpen())
		{
			Server->Log("Could not open cache vhdx at " + cache_img_path, LL_ERROR);
			return false;
		}

		IBackupFileSystem* cachefs = btrfs_fak->openBtrfsImage(cachevhdx);
		if (cachefs == nullptr)
		{
			Server->Log("Could not open cache btrfs at " + cache_img_path, LL_ERROR);
			return false;
		}
		
		settings.size = 100LL * 1024 * 1024 * 1024 * 1024; //100TB

		img = clouddrive_fak->createCloudFile(cachefs, settings);
	}

	if (img == nullptr)
		return false;

	if (img->RealSize() == 0)
	{
		if (!create_cache_init_vhdx())
			return false;

		if (!copy_vhdx("urbackup/cache_init.vhdx", img))
			return false;
	}

	IBackupFileSystem* fs = btrfs_fak->openBtrfsImage(img);
	if (fs == nullptr)
		return false;

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

	return dokany_mount(fs, mount_path);
}

IBackupFileSystem* FilesystemManager::getFileSystem(const std::string& path)
{
	std::lock_guard<std::mutex> lock(mutex);

	auto it = filesystems.find(path);
	if (it == filesystems.end())
		return nullptr;

	return it->second;
}
