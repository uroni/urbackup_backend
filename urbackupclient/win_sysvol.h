#pragma once
#include <string>

std::string getSysVolume(std::string &mpath);

std::string getEspVolume(std::string &mpath);

std::string getSysVolumeCached(std::string &mpath);
void cacheVolumes();

std::string getEspVolumeCached(std::string &mpath);