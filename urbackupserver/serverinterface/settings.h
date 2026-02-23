#pragma once
#include "../../Interface/Types.h"
#include "../../Interface/Database.h"

void saveGeneralSettingsExternal(str_map& POST, IDatabase* db, bool& changed_backupfolder);
