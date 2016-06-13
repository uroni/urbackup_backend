#pragma once

#include <string>


class IPermissionCallback
{
public:
	virtual std::string getPermissions(const std::string& path) = 0;
};