/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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

#include "ClouddriveFactory.h"
#include "CloudFile.h"
#include "KvStoreFrontend.h"
#include "KvStoreBackendS3.h"

bool ClouddriveFactory::checkConnectivity(CloudSettings settings, int64 timeoutms)
{
	return false;
}

IFile* ClouddriveFactory::createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings)
{
	return createCloudFile(cachefs, settings, false);
}

IFile* ClouddriveFactory::createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings, bool check_only)
{
	IOnlineKvStore* online_kv_store;

	if (settings.endpoint == CloudEndpoint::S3)
	{
		IKvStoreBackend* s3_backend = new KvStoreBackendS3(settings.encryption_key,
			settings.s3_settings.access_key, settings.s3_settings.secret_access_key,
			settings.s3_settings.bucket_name,
			get_compress_encrypt_factory(),
			settings.s3_settings.endpoint,
			settings.s3_settings.region,
			settings.s3_settings.storage_class,
			static_cast<unsigned int>(settings.submit_compression), cachefs);

		online_kv_store = new KvStoreFrontend(settings.s3_settings.cache_db_path,
			s3_backend, !check_only, std::string(), std::string(), nullptr,
			std::string(), false, false, cachefs);
	}
	else
	{
		return nullptr;
	}

	return new CloudFile(std::string(), cachefs,
		settings.size, settings.size, online_kv_store, settings.encryption_key,
		get_compress_encrypt_factory(), settings.verify_cache, settings.cpu_multiplier,
		settings.background_compress, settings.no_compress_cpu_mult,
		settings.reserved_cache_device_space, settings.min_metadata_cache_free,
		settings.with_prev_link, settings.allow_evict, settings.with_submitted_files,
		settings.resubmit_compressed_ratio, settings.memcache_size, std::string(),
		settings.memory_usage_factor, settings.only_memfiles, std::string(),
		std::string(), static_cast<unsigned int>(settings.background_compression),
		std::string(), -1, false);
}
