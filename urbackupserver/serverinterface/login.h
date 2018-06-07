#pragma once

#include "helper.h"

enum LoginMethod
{
	LoginMethod_Webinterface = 0,
	LoginMethod_RestoreCD = 1
};

std::string loginMethodToString(LoginMethod lm);

void logSuccessfulLogin(Helper& helper, str_map& PARAMS, const std::string& username, LoginMethod method);

void logFailedLogin(Helper& helper, str_map& PARAMS, const std::string& username, LoginMethod method);

void logFailedAuth(const std::string& username, const std::string& remote_ip);