#pragma once

#include "../clouddrive/IClouddriveFactory.h"

bool parse_backup_url(const std::string& url, const std::string& url_params,
	const str_map& secret_params, IClouddriveFactory::CloudSettings& settings);