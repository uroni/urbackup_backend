#pragma once
#include "../Interface/Database.h"
#include <string>

void sqlgen(IDatabase* db, std::string &cppfile, std::string &headerfile);