#include "backup_url_parser.h"
#include "../stringtools.h"

bool parse_backup_url(const std::string& url, const std::string& url_params, 
	const str_map& secret_params, IClouddriveFactory::CloudSettings& settings)
{
	if (next(url, 0, "s3://") ||
		next(url, 0, "ss3://"))
	{
		bool ssl = next(url, 0, "ss3://");
		settings.endpoint = IClouddriveFactory::CloudEndpoint::S3;

		std::string authorization = getbetween(ssl ? "ss3://" : "s3://", "@", url);
		std::string server = getafter("@", url);
		if (server.empty())
		{
			server = url.substr(5);
		}

		settings.s3_settings.access_key = getuntil(":", authorization);
		settings.s3_settings.secret_access_key = getafter(":", authorization);

		settings.s3_settings.endpoint = (ssl ? "https://" : "http://") + getuntil("/", server);
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
		settings.cache_img_path = "urbackup/" + cacheid + ".vhdx";
		settings.s3_settings.cache_db_path = "urbackup/" + cacheid + ".db";

		return true;
	}

	return false;
}
