/*
  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the author be held liable for any damages
  arising from the use of this software.
  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:
  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
  Mark Adler
  madler@alumni.caltech.edu
 */
 
#include <stdlib.h>
#include <stdint.h>
 
 /* CRC-32C (iSCSI) polynomial in reversed bit order. */
#define POLY 0x82f63b78

namespace
{
	/* Table for a quadword-at-a-time software crc. */
	static uint32_t crc32c_table[8][256];

	/* Construct table for software CRC-32C calculation. */
	static void crc32c_init_sw(void)
	{
		uint32_t n, crc, k;

		for (n = 0; n < 256; n++) {
			crc = n;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
			crc32c_table[0][n] = crc;
		}
		for (n = 0; n < 256; n++) {
			crc = crc32c_table[0][n];
			for (k = 1; k < 8; k++) {
				crc = crc32c_table[0][crc & 0xff] ^ (crc >> 8);
				crc32c_table[k][n] = crc;
			}
		}
	}

	class Crc32cInit
	{
	public:
		Crc32cInit()
		{
			crc32c_init_sw();
		}
	};

	static Crc32cInit crc32c_init;
}

/* Table-driven software version as a fall-back.  This is about 15 times slower
   than using the hardware instructions.  This assumes little-endian integers,
   as is the case on Intel processors that the assembler code here is for. */
uint32_t crc32c_sw(const char *buf, size_t len, uint32_t crci)
{
    const unsigned char *next = reinterpret_cast<const unsigned char*>(buf);
    uint64_t crc;

    crc = crci ^ 0xffffffff;
    while (len && ((uintptr_t)next & 7) != 0) {
        crc = crc32c_table[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }
    while (len >= 8) {
        crc ^= *(uint64_t *)next;
        crc = crc32c_table[7][crc & 0xff] ^
              crc32c_table[6][(crc >> 8) & 0xff] ^
              crc32c_table[5][(crc >> 16) & 0xff] ^
              crc32c_table[4][(crc >> 24) & 0xff] ^
              crc32c_table[3][(crc >> 32) & 0xff] ^
              crc32c_table[2][(crc >> 40) & 0xff] ^
              crc32c_table[1][(crc >> 48) & 0xff] ^
              crc32c_table[0][crc >> 56];
        next += 8;
        len -= 8;
    }
    while (len) {
        crc = crc32c_table[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }
    return (uint32_t)crc ^ 0xffffffff;
}