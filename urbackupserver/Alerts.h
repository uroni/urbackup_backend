#pragma once

#include "../Interface/Thread.h"
#include <string>

class IDatabase;
std::string get_alert_script(IDatabase* db, int script_id);

class Alerts : public IThread
{
public:
	void operator()();
};
