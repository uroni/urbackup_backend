#include "../common/data.h"
#include "../Interface/Server.h"

class SMBRData
{
public:
	SMBRData(CRData &data)
	{
		char ch;
		if(!data.getChar(&ch))
		{
			Server->Log("Cannot read first byte", LL_ERROR);
			has_error=true;return;
		}
		if(!data.getChar(&version))
		{
			Server->Log("Cannot read version", LL_ERROR);
			has_error=true;return;
		}
		if(version!=0)
		{
			Server->Log("Version is wrong", LL_ERROR);
			has_error=true;return;
		}
		if(!data.getInt(&device_number))
		{
			Server->Log("Cannot get device number", LL_ERROR);
			has_error=true;return;
		}
		if(!data.getInt(&partition_number))
		{
			Server->Log("Cannot get partition number", LL_ERROR);
			has_error=true;return;
		}
		if(!data.getStr(&serial_number))
		{
			Server->Log("Cannot get serial number", LL_ERROR);
			has_error=true;return;
		}
		std::string tmp;
		if(!data.getStr(&tmp))
		{
			Server->Log("Cannot get volume name", LL_ERROR);
			has_error=true;return;
		}
		volume_name=Server->ConvertToUnicode(tmp);
		if(!data.getStr(&tmp))
		{
			Server->Log("Cannot get fsn name", LL_ERROR);
			has_error=true;return;
		}
		fsn=Server->ConvertToUnicode(tmp);
		if(!data.getStr(&mbr_data))
		{
			Server->Log("Cannot get mbr data", LL_ERROR);
			has_error=true;return;
		}
		has_error=false;
		data.getStr(&errmsg);
	}

	bool hasError(void)
	{
		return has_error;
	}

	std::string infoString(void)
	{
		std::string ret;
	#define ADD_INFO(x) ret+=std::string(#x "=")+nconvert(x)+"\n"
	#define ADD_INFO_STR(x) ret+=std::string(#x "=")+x+"\n"
	#define ADD_INFO_WSTR(x) ret+=std::string(#x "=")+Server->ConvertToUTF8(x)+"\n"
		ADD_INFO(version);
		ADD_INFO(device_number);
		ADD_INFO(partition_number);
		ADD_INFO_STR(serial_number);
		ADD_INFO_WSTR(volume_name);
		ADD_INFO_WSTR(fsn);
		ret+=std::string("mbr_data (")+nconvert(mbr_data.size())+" bytes)\n";
		ADD_INFO_STR(errmsg);
	#undef ADD_INFO
	#undef ADD_INFO_STR
	#undef ADD_INFO_WSTR
		return ret;
	}

	char version;
	int device_number;
	int partition_number;
	std::string serial_number;
	std::wstring volume_name;
	std::wstring fsn;
	std::string mbr_data;
	std::string errmsg;

private:
	bool has_error;
};
