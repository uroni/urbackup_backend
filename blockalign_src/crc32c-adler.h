#pragma once
#include <stdint.h>

uint32_t crc32c_sw(const char *buf, size_t len, uint32_t crci);