#pragma once

#include "helper.h"

enum LoginMethod
{
	LoginMethod_Webinterface = 0,
	LoginMethod_RestoreCD = 1
};

void logLogin(Helper& helper, str_nmap& PARAMS, const std::wstring& username, LoginMethod method);