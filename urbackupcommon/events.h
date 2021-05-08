#pragma once

#include <string>
#include <map>

static std::string extractLastLogErrors(size_t nerrors = 1, const std::string& extra_filter = std::string(), bool with_warn = false) {
	return std::string();
}

static bool addSystemEvent(const std::string& evtid, const std::string& subj, const std::string& msg,
	int prio, std::map<std::string, std::string> extra = std::map<std::string, std::string>()) {
	return true;
}