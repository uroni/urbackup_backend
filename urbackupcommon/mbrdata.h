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
		if(version!=0 && version!=1)
		{
			Server->Log("MBR data version not supported: "+convert(version), LL_ERROR);
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
		volume_name=tmp;
		if(!data.getStr(&tmp))
		{
			Server->Log("Cannot get fsn name", LL_ERROR);
			has_error=true;return;
		}
		fsn=tmp;
		if(!data.getStr(&mbr_data))
		{
			Server->Log("Cannot get mbr data", LL_ERROR);
			has_error=true;return;
		}
		if(version==1)
		{
			gpt_style=true;

			if(!data.getInt64(&gpt_header_pos))
			{
				Server->Log("Cannot get GPT header pos", LL_ERROR);
				has_error=true;return;
			}
			if(!data.getStr(&gpt_header))
			{
				Server->Log("Cannot get GPT header", LL_ERROR);
				has_error=true;return;
			}
			if(!data.getInt64(&gpt_table_pos))
			{
				Server->Log("Cannot get GPT table pos", LL_ERROR);
				has_error=true;return;
			}
			if(!data.getStr(&gpt_table))
			{
				Server->Log("Cannot get GPT table", LL_ERROR);
				has_error=true;return;
			}

			if(!data.getInt64(&backup_gpt_header_pos))
			{
				Server->Log("Cannot get backup GPT header pos", LL_ERROR);
				has_error=true;return;
			}
			if(!data.getStr(&backup_gpt_header))
			{
				Server->Log("Cannot get backup GPT header", LL_ERROR);
				has_error=true;return;
			}
			if(!data.getInt64(&backup_gpt_table_pos))
			{
				Server->Log("Cannot get backup GPT table pos", LL_ERROR);
				has_error=true;return;
			}
			if(!data.getStr(&backup_gpt_table))
			{
				Server->Log("Cannot get backup GPT table", LL_ERROR);
				has_error=true;return;
			}
		}
		else
		{
			gpt_style=false;
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
	#define ADD_INFO(x) ret+=std::string(#x "=")+convert(x)+"\n"
	#define ADD_INFO_STR(x) ret+=std::string(#x "=")+x+"\n"
		ADD_INFO(version);
		ADD_INFO(device_number);
		ADD_INFO(partition_number);
		ADD_INFO_STR(serial_number);
		ADD_INFO_STR(volume_name);
		ADD_INFO_STR(fsn);
		ret+=std::string("mbr_data (")+convert(mbr_data.size())+" bytes)\n";
		if (gpt_style)
		{
			ret += std::string("gpt_data (") + convert(gpt_header.size()+ gpt_table.size()) + " bytes)\n";
		}
		if (!errmsg.empty())
		{
			ADD_INFO_STR(errmsg);
		}
	#undef ADD_INFO
	#undef ADD_INFO_STR
		return ret;
	}

	char version;
	int device_number;
	int partition_number;
	std::string serial_number;
	std::string volume_name;
	std::string fsn;
	std::string mbr_data;
	std::string errmsg;

	bool gpt_style;

	int64 gpt_header_pos;
	std::string gpt_header;

	int64 gpt_table_pos;
	std::string gpt_table;

	int64 backup_gpt_header_pos;
	std::string backup_gpt_header;

	int64 backup_gpt_table_pos;
	std::string backup_gpt_table;

private:
	bool has_error;
};
