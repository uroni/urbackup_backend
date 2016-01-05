#pragma once

#include "../../Interface/Server.h"
#include "../../Interface/Database.h"
#include "../../Interface/Types.h"
#include "../database.h"
#include <stdlib.h>

void open_server_database(bool init_db);
void open_settings_database();
void delete_file_index(void);