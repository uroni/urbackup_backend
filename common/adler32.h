#pragma once

unsigned int urb_adler32(unsigned int adler, const char *pbuf, unsigned int len);

unsigned int urb_adler32_combine(unsigned int adler1, unsigned int adler2, unsigned int len2);