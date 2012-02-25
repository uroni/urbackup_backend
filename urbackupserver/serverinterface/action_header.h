#include "../../Interface/Action.h"
#include "../../Interface/Server.h"
#include "../../Interface/File.h"
#include "actions.h"
#include "helper.h"
#include "json.h"
#include "../../stringtools.h"
#include "../../urbackupcommon/settings.h"
#include "../../pychart/IPychartFactory.h"
#include <stdlib.h>

extern IPychartFactory *pychart_fak;
extern SStartupStatus startup_status;