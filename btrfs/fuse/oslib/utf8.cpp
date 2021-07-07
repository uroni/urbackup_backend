#include "utf8.h"
#include "../../../utf8/utf8.h"


std::wstring ConvertToWchar(const std::string& input)
{
	if (input.empty())
	{
		return std::wstring();
	}

	std::wstring ret;
	try
	{
		if (sizeof(wchar_t) == 2)
		{
			utf8::utf8to16(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
		}
		else if (sizeof(wchar_t) == 4)
		{
			utf8::utf8to32(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
		}

	}
	catch (...) {}
	return ret;
}

std::string ConvertFromWchar(const std::wstring& input)
{
	if (input.empty())
	{
		return std::string();
	}

	std::string ret;
	try
	{
		if (sizeof(wchar_t) == 2)
		{
			utf8::utf16to8(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
		}
		else if (sizeof(wchar_t) == 4)
		{
			utf8::utf32to8(&input[0], &input[input.size() - 1] + 1, back_inserter(ret));
		}

	}
	catch (...) {}
	return ret;
}
