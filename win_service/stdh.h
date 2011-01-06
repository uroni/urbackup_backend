#ifndef _STD_HEADER_INCLUDED_
#define _STD_HEADER_INCLUDED_

// basic types
typedef unsigned char	byte_;
typedef unsigned short	word_;
typedef unsigned int	dword_;

#ifdef UNICODE
	typedef wchar_t char_;
#else
	typedef char char_;
#endif

#if defined(_WIN32)
	
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>

#endif

#if defined (linux)||(__linux)
#endif

#endif