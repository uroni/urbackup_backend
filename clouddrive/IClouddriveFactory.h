#pragma once

#include "../Interface/Plugin.h"
#include "../Interface/File.h"
#include "../Interface/BackupFileSystem.h"

class IKvStoreBackend;

class IClouddriveFactory : public IPlugin
{
public:
	enum class CloudEndpoint
	{
		S3
	};

	enum class CompressionMethod
	{
		lzma_5 = 0,
		zlib_5 = 1,
		zstd_3 = 2,
		zstd_19 = 3,
		zstd_9 = 4,
		none = 5,
		zstd7 = 6,
		lzo = 7
	};

	struct CloudSettingsS3
	{
		std::string access_key;
		std::string secret_access_key;
		std::string bucket_name;
		std::string endpoint;
		std::string region;
		std::string storage_class;
		std::string cache_db_path;
	};

	struct CloudSettings
	{
		int64 size = -1;
		int64 memcache_size = 0;
		bool background_compress = false;
		bool with_prev_link = true;
		bool allow_evict = true;
		std::string encryption_key;
		float resubmit_compressed_ratio = 0.8f;
		bool verify_cache = false;
		CompressionMethod submit_compression = CompressionMethod::none;
		CompressionMethod metadata_submit_compression = CompressionMethod::zstd_3;
		CompressionMethod background_compression = CompressionMethod::lzma_5;
		CompressionMethod cache_object_compression = CompressionMethod::none;
		CompressionMethod metadata_cache_object_compression = CompressionMethod::lzo;
		bool multi_trans_delete = true;
		int64 reserved_cache_device_space = 100 * 1024 * 1024;
		float memory_usage_factor = 0.1f;
		float cpu_multiplier = 1.0f;
		size_t no_compress_cpu_mult = 1;
		int64 min_metadata_cache_free = 100 * 1024 * 1024;
		bool with_submitted_files = true;
		bool only_memfiles = false;
		bool background_worker_manual_run = true;
		std::string cache_img_path;
		
		CloudEndpoint endpoint;
		CloudSettingsS3 s3_settings;
	};

	virtual bool checkConnectivity(CloudSettings settings,
		int64 timeoutms) = 0;

	virtual IKvStoreBackend* createBackend(IBackupFileSystem* cachefs, CloudSettings settings) = 0;

	virtual IFile* createCloudFile(IBackupFileSystem* cachefs,
		CloudSettings settings) = 0;

	virtual bool setTopFs(IFile* cloudFile, IBackupFileSystem* fs) = 0;

	virtual bool isCloudFile(IFile* cloudfile) = 0;

	virtual bool runBackgroundWorker(IFile* cloudfile, const std::string& output_fn) = 0;

	virtual int64 getCfTransid(IFile* cloudfile) = 0;

	virtual bool flush(IFile* cloudfile, bool do_submit) = 0;

	virtual std::string getCfNumDirtyItems(IFile* cloudfile) = 0;
};