#include <Windows.h>
#include <string>

namespace
{
	std::vector<std::wstring> file_via_dialog(const std::wstring& title,
		const std::wstring& filter, bool multi_select, bool existing_file,
		const std::wstring& defExt)
	{
		OPENFILENAMEW ofn = {};

		std::vector<wchar_t> buf;
		buf.resize(524288);

		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = filter.c_str();
		ofn.nMaxCustFilter = static_cast<DWORD>(filter.size());
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

		ofn.lpstrTitle = title.c_str();

		ofn.lpstrFile = buf.data();

		if(!defExt.empty())
		{
			ofn.lpstrDefExt = defExt.c_str();
		}

		if(GetOpenFileNameW(&ofn))
		{
			if(!multi_select)
			{
				std::vector<std::wstring> ret;
				ret.push_back(buf.data());
				return ret;
			}
			else
			{
				std::vector<std::wstring> ret;
				std::wstring cname;
				for(size_t i=0;i<buf.size();++i)
				{
					if(buf[i]==0)
					{
						if(cname.empty()) break;
						ret.push_back(cname);
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
			return std::vector<std::wstring>();
		}
	}
}