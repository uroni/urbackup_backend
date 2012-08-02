#include "settings.h"

#ifdef LOG_OFF
#define Log(x, y) 
#else
void Log(const std::string &str, int loglevel=-1);
#endif

#define LL_DEBUG -1
#define LL_INFO 0
#define LL_WARNING 1
#define LL_ERROR 2

//Disable logging
//#define Log //
