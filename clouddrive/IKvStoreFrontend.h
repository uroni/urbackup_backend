#pragma once
#include <string>

class IKvStoreFrontend
{
public:
	virtual void incr_total_del_ops() = 0;

	virtual std::string prefixKey(const std::string& key) = 0;

	virtual std::string encodeKey(int64 cd_id, const std::string& key, int64 transid) = 0;

	virtual bool log_del_mirror(const std::string& fn) = 0;
};