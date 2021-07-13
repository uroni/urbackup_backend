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
#include "../cryptoplugin/cryptopp_inc.h"

using namespace CryptoPPCompat;

static std::string pbkdf2_sha256(const std::string& key)
{
	std::string ret;
	ret.resize(CryptoPP::SHA256::DIGESTSIZE);

	CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA256> pkcs;
	pkcs.DeriveKey(reinterpret_cast<byte*>(&ret[0]), CryptoPP::SHA256::DIGESTSIZE, 0,
		(byte*)key.c_str(), key.size(),
		nullptr, 0, 100000, 0);

	return ret;
}

bool ClouddriveFactory::checkConnectivity(CloudSettings settings, int64 timeoutms)
{
	return false;
}

IKvStoreBackend* ClouddriveFactory::createBackend(IBackupFileSystem* cachefs, CloudSettings settings)
{
	std::string aes_key = pbkdf2_sha256(settings.encryption_key);
	return createBackend(cachefs, aes_key, settings);
}

IFile* ClouddriveFactory::createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings)
{
	return createCloudFile(cachefs, settings, false);
}

IFile* ClouddriveFactory::createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings, bool check_only)
{
	IOnlineKvStore* online_kv_store;

	std::string aes_key = pbkdf2_sha256(settings.encryption_key);

	IKvStoreBackend* backend = createBackend(cachefs, aes_key, settings);

	if (settings.endpoint == CloudEndpoint::S3)
	{
		try
		{
			online_kv_store = new KvStoreFrontend(settings.s3_settings.cache_db_path,
				backend, !check_only, std::string(), std::string(), nullptr,
				std::string(), false, false, cachefs);
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
	}
	else
	{
		return nullptr;
	}

	return new CloudFile(std::string(), cachefs,
		settings.size, settings.size, online_kv_store, aes_key,
		get_compress_encrypt_factory(), settings.verify_cache, settings.cpu_multiplier,
		settings.background_compress, settings.no_compress_cpu_mult,
		settings.reserved_cache_device_space, settings.min_metadata_cache_free,
		settings.with_prev_link, settings.allow_evict, settings.with_submitted_files,
		settings.resubmit_compressed_ratio, settings.memcache_size, std::string(),
		settings.memory_usage_factor, settings.only_memfiles, std::string(),
		std::string(), static_cast<unsigned int>(settings.background_compression),
		std::string(), -1, false,
		static_cast<unsigned int>(settings.cache_object_compression),
		static_cast<unsigned int>(settings.metadata_cache_object_compression));
}

IKvStoreBackend* ClouddriveFactory::createBackend(IBackupFileSystem* cachefs, 
	const std::string& aes_key, CloudSettings settings)
{
	if (settings.endpoint == CloudEndpoint::S3)
	{
		IKvStoreBackend* s3_backend = new KvStoreBackendS3(aes_key,
			settings.s3_settings.access_key, settings.s3_settings.secret_access_key,
			settings.s3_settings.bucket_name,
			get_compress_encrypt_factory(),
			settings.s3_settings.endpoint,
			settings.s3_settings.region,
			settings.s3_settings.storage_class,
			static_cast<unsigned int>(settings.submit_compression),
			static_cast<unsigned int>(settings.metadata_submit_compression),
			cachefs);

		return s3_backend;
	}

	return nullptr;
}

int64 ClouddriveFactory::getCfTransid(IFile* cloudfile)
{
	CloudFile* cd = dynamic_cast<CloudFile*>(cloudfile);
	if (cd == nullptr)
		return -1;

	return cd->get_transid();
}

bool ClouddriveFactory::flush(IFile* cloudfile, bool do_submit)
{
	if (cloudfile == nullptr)
		return false;

	CloudFile* cd = dynamic_cast<CloudFile*>(cloudfile);
	if (cd == nullptr)
		return false;

	return cd->Flush(do_submit);
}

std::string ClouddriveFactory::getCfNumDirtyItems(IFile* cloudfile)
{
	if (cloudfile == nullptr)
		return std::string();

	CloudFile* cd = dynamic_cast<CloudFile*>(cloudfile);
	if (cd == nullptr)
		return std::string();


	return cd->getNumDirtyItems();
}

bool ClouddriveFactory::isCloudFile(IFile* cloudfile)
{
	return  dynamic_cast<CloudFile*>(cloudfile)!=nullptr;
}

bool ClouddriveFactory::runBackgroundWorker(IFile* cloudFile, const std::string& output_fn)
{
	CloudFile* cd = dynamic_cast<CloudFile*>(cloudFile);
	if (cd == nullptr)
		return false;

	if (!cd->has_background_task())
		return false;

	cd->set_background_worker_result_fn(output_fn + ".new");

	if (!cd->start_background_worker())
		return false;

	while (cd->is_background_worker_running())
	{
		Server->wait(100);
	}

	if (cd->has_background_task())
		return false;

	return os_rename_file(output_fn + ".new", output_fn);
}

bool ClouddriveFactory::setTopFs(IFile* cloudFile, IBackupFileSystem* fs)
{
	CloudFile* cd = dynamic_cast<CloudFile*>(cloudFile);
	if (cd == nullptr)
		return false;
	

	cd->set_is_mounted(std::string(), fs);

	return true;
}
