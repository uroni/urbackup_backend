#ifndef OS_FUNC_NO_SERVER
#include "../Interface/Server.h"
#include "../Interface/File.h"
#else
#include "../utf8/utf8.h"
#include <iostream>
#endif

namespace {

#ifdef OS_FUNC_NO_SERVER

#define LL_DEBUG -1
#define LL_INFO 0
#define LL_WARNING 1
#define LL_ERROR 2

	std::string ConvertToUTF8(const std::wstring& input)
	{
		std::string ret;
		try
		{
			if(sizeof(wchar_t)==2 )
				utf8::utf16to8(input.begin(), input.end(), back_inserter(ret));
			else
				utf8::utf32to8(input.begin(), input.end(), back_inserter(ret));
		}
		catch(...){}
		return ret;
	}

        std::wstring ConvertToUnicode(const std::string& input)
        {
                std::wstring ret;
                try
                {
                            if(sizeof(wchar_t)==2)
                                    utf8::utf8to16(input.begin(), input.end(), back_inserter(ret));
                    else
                                    utf8::utf8to32(input.begin(), input.end(), back_inserter(ret));
                }
                catch(...){}

                return ret;
        }

        void Log(const std::wstring& pStr, int loglevel)
	{
		if( loglevel==LL_ERROR )
		{
			std::wcerr << L"ERROR: " << pStr << std::endl;
		}
		else if( loglevel==LL_WARNING )
		{
			std::wcout << L"WARNING: " << pStr << std::endl;
		}
		else
		{
			std::wcout << pStr << std::endl;		
		}
	}

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

	std::string ConvertToUTF8(const std::wstring& input)
	{
		return Server->ConvertToUTF8(input);
	}

        std::wstring ConvertToUnicode(const std::string& input)
        {
                return Server->ConvertToUnicode(input);
        }

	void Log(const std::wstring& pStr, int loglevel)
	{
		Server->Log(pStr, loglevel);
	}

	void Log(const std::string& pStr, int loglevel)
	{
		Server->Log(pStr, loglevel);
	}

#endif //OS_FUNC_NO_SERVER

} //unnamed namespace
