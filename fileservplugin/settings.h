#ifndef SETTINGS_H
#define SETTINGS_H


#include "types.h"

#include <string>

const _i32 BUFFERSIZE=1024;
const _i32 NBUFFERS=32;
const _i32 READSIZE=32768;
const _i32 SENDSIZE=16384;
const uchar VERSION=36;
const _i32 WINDOW_SIZE=512*1024; // 128 kbyte

#define DISABLE_NAGLE
//#define DISABLE_WINDOW_SIZE
//#define HIGH_PRIORITY
//#define LOG_FILE
//#define LOG_CONSOLE
//#define LOG_OFF
//#define CAMPUS
#define LOG_SERVER
#define DLL_EXPORT
#define CHECK_IDENT
#define BACKGROUND_PRIORITY

#ifdef _WIN32
const std::wstring testfilename=L"C:\\Windows\\Temp\\testfile.tmp";
#else
const std::wstring testfilename=L"/tmp/testfile.tmp";
#endif

#endif

