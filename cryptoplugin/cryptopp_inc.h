#ifdef _WIN32
#include <aes.h>
#include <sha.h>
#include <modes.h>
#include <zlib.h>
#include <dsa.h>
#include <osrng.h>
#include <files.h>
#include <pwdbased.h>
#include <hex.h>
#else
#include "config.h"
#include <@CRYPTOPP_INCLUDE_PREFIX@/aes.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/sha.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/modes.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/zlib.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/dsa.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/osrng.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/files.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/pwdbased.h>
#include <@CRYPTOPP_INCLUDE_PREFIX@/hex.h>
#endif