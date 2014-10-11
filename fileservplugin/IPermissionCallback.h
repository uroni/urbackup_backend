#pragma once

#include <string>


class IPermissionCallback
{
public:
	virtual std::string getPermissions(const std::wstring& path) = 0;
};