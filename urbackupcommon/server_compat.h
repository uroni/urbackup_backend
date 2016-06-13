#if !defined(OS_FUNC_NO_SERVER) && !defined(NO_SERVER)
#include "../Interface/Server.h"
#include "../Interface/File.h"
#else
#include <iostream>
#endif

#include "../utf8/utf8.h"

namespace {

#if defined(OS_FUNC_NO_SERVER) || defined(NO_SERVER)

#define LL_DEBUG -1
#define LL_INFO 0
#define LL_WARNING 1
#define LL_ERROR 2


    void Log(const std::string& pStr, int loglevel)
	{
		if( loglevel==LL_ERROR )
		{
			std::cerr << "ERROR: " << pStr << std::endl;
		}
		else if( loglevel==LL_WARNING )
		{
			std::cout << "WARNING: " << pStr << std::endl;
		}
		else
		{
			std::cout << pStr << std::endl;		
		}
	}


#else //OS_FUNC_NO_SERVER

	void Log(const std::string& pStr, int loglevel)
	{
		Server->Log(pStr, loglevel);
	}

#endif //OS_FUNC_NO_SERVER

	std::wstring ConvertToWchar(const std::string &input)
	{
		if(input.empty())
		{
			return std::wstring();
		}

		std::wstring ret;
		try
		{
			if(sizeof(wchar_t)==2)
			{
				utf8::utf8to16(&input[0], &input[input.size()-1]+1, back_inserter(ret));
			}
			else if(sizeof(wchar_t)==4)
			{
				utf8::utf8to32(&input[0], &input[input.size()-1]+1, back_inserter(ret));
			}

		}
		catch(...){}	
		return ret;
	}

	std::string ConvertFromWchar(const std::wstring &input)
	{
		if(input.empty())
		{
			return std::string();
		}

		std::string ret;
		try
		{
			if(sizeof(wchar_t)==2)
			{
				utf8::utf16to8(&input[0], &input[input.size()-1]+1, back_inserter(ret));
			}
			else if(sizeof(wchar_t)==4)
			{
				utf8::utf32to8(&input[0], &input[input.size()-1]+1, back_inserter(ret));
			}

		}
		catch(...){}	
		return ret;
	}

} //unnamed namespace
