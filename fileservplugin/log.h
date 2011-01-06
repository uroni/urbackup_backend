#include "settings.h"

#ifdef LOG_OFF
#define Log(x,...) 
#else
void Log(const char *pStr...);
#endif

//Disable logging
//#define Log //
