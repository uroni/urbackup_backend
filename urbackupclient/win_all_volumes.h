#pragma once

#include <string>

struct SVolumesCache;

std::string get_all_volumes_list(bool filter_usb, SVolumesCache*& cache);