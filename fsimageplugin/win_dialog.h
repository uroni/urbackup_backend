#include <Windows.h>
#include <string>
#include "../Interface/Server.h"

namespace
{
	std::vector<std::string> file_via_dialog(const std::string& title,
		const std::string& filter, bool multi_select, bool existing_file,
		const std::string& defExt)
	{
		OPENFILENAMEW ofn = {};

		std::vector<wchar_t> buf;
		buf.resize(524288);

		std::wstring wfilter = Server->ConvertToWchar(filter);
		std::wstring wtitle = Server->ConvertToWchar(title);
		std::wstring wdefExt = Server->ConvertToWchar(defExt);

		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = wfilter.c_str();
		ofn.nMaxCustFilter = static_cast<DWORD>(wfilter.size());
		ofn.nMaxFile = static_cast<DWORD>(buf.size());
		ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_LONGNAMES | OFN_NOTESTFILECREATE | OFN_PATHMUSTEXIST;

		if(existing_file)
		{
			ofn.Flags |= OFN_FILEMUSTEXIST;
		}
		else
		{
			ofn.Flags |= OFN_OVERWRITEPROMPT;
		}

		if(multi_select)
		{
			ofn.Flags |= OFN_ALLOWMULTISELECT;
		}

		ofn.lpstrTitle = wtitle.c_str();

		ofn.lpstrFile = buf.data();

		if(!wdefExt.empty())
		{
			ofn.lpstrDefExt = wdefExt.c_str();
		}

		if(GetOpenFileNameW(&ofn))
		{
			if(!multi_select)
			{
				std::vector<std::string> ret;
				ret.push_back(Server->ConvertFromWchar(buf.data()));
				return ret;
			}
			else
			{
				std::vector<std::string> ret;
				std::wstring cname;
				for(size_t i=0;i<buf.size();++i)
				{
					if(buf[i]==0)
					{
						if(cname.empty()) break;
						ret.push_back(Server->ConvertFromWchar(cname));
						cname.clear();
					}
					else
					{
						cname+=buf[i];
					}
				}

				if(ret.size()>1)
				{
					if(!ret[0].empty() && ret[0][ret[0].size()-1]!='\\' )
					{
						ret[0]+='\\';
					}

					for(size_t i=1;i<ret.size();++i)
					{
						ret[i] = ret[0] + ret[i];
					}

					ret.erase(ret.begin());
				}				

				return ret;
			}
		}
		else
		{
			return std::vector<std::string>();
		}
	}
}