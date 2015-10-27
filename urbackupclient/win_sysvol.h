#pragma once
#include <string>

std::wstring getSysVolume(std::wstring &mpath);
std::wstring getSysVolumeCached(std::wstring &mpath);
void cacheSysVolume();