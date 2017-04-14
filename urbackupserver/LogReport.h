#pragma once
#include <string>

void init_log_report();

std::string load_report_script();

void reload_report_script();

bool run_report_script(int incremental, bool resumed, int image, int infos, 
	int warnings, int errors, bool success, const std::string& report_mail, const std::string& data,
	const std::string& clientname);