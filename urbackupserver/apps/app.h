#pragma once

#include "../../Interface/Server.h"
#include "../../Interface/Database.h"
#include "../../Interface/Types.h"
#include "../database.h"
#include <stdlib.h>

void open_server_database(bool &use_berkeleydb, bool init_db);
void open_settings_database(bool use_berkeleydb);
void delete_file_index(void);