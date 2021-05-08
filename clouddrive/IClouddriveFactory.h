#pragma once

#include "../Interface/Plugin.h"
#include "../Interface/File.h"
#include "../Interface/BackupFileSystem.h"

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
		zstd_9 = 4
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
		CompressionMethod submit_compression = CompressionMethod::zstd_9;
		CompressionMethod background_compression = CompressionMethod::lzma_5;
		bool multi_trans_delete = true;
		int64 reserved_cache_device_space = 100 * 1024 * 1024;
		float memory_usage_factor = 0.1f;
		float cpu_multiplier = 1.0f;
		size_t no_compress_cpu_mult = 1;
		int64 min_metadata_cache_free = 100 * 1024 * 1024;
		bool with_submitted_files = true;
		bool only_memfiles = false;
		
		CloudEndpoint endpoint;
		CloudSettingsS3 s3_settings;
	};

	virtual bool checkConnectivity(CloudSettings settings,
		int64 timeoutms) = 0;

	virtual IFile* createCloudFile(IBackupFileSystem* cachefs,
		CloudSettings settings) = 0;
};