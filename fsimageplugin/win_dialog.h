#include <Windows.h>
#include <string>

namespace
{
	std::wstring file_via_dialog(const std::wstring& title, const std::wstring& filter)
	{
		OPENFILENAMEW ofn = {};

		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = filter.c_str();
		ofn.nMaxCustFilter = static_cast<DWORD>(filter.size());
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_LONGNAMES | OFN_NOTESTFILECREATE | OFN_PATHMUSTEXIST;
		ofn.lpstrTitle = title.c_str();

		wchar_t fileName[MAX_PATH] = {};
		ofn.lpstrFile = fileName;

		if(GetOpenFileNameW(&ofn))
		{
			return fileName;
		}
		else
		{
			return std::wstring();
		}
	}
}