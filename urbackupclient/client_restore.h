#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../Interface/Pipe.h"
#include "../urbackupcommon/json.h"

namespace restore
{
	struct LoginData
	{
		LoginData() : has_login_data(false) {}

		bool has_login_data;
		std::string username;
		std::string password;
		std::string token;
	};

	bool do_login(LoginData& login_data, bool with_cli_output=true);

	std::vector<std::string> getBackupclients(int& ec);

	std::string restorePw();

	IPipe* connectToService(int& ec);

	IPipe* connectToService();

	std::string getResponse(IPipe* c);

	static std::string getResponse(std::unique_ptr<IPipe>& c) {
		return getResponse(c.get());
	}

	JSON::Array backup_images_output_to_json(const std::string& data);

	struct SImage
	{
		bool operator<(const SImage& other) const
		{
			return other.time_s < time_s;
		}
		std::string time_str;
		_i64 time_s;
		int id;
		std::vector<SImage> assoc;
		std::string letter;
	};

	std::vector<SImage> parse_backup_images_output(const std::string& data);

	enum EDownloadResult
	{
		EDownloadResult_Ok = 0,
		EDownloadResult_ConnectError = 10,
		EDownloadResult_OpenError = 2,
		EDownloadResult_SizeReadError = 3,
		EDownloadResult_TimeoutError2 = 4,
		EDownloadResult_TimeoutError1 = 5,
		EDownloadResult_WriteFailed = 6,
		EDownloadResult_DeviceTooSmall = 11
	};

	struct DownloadStatus
	{
		DownloadStatus()
			: offset(-1),
			received(0) {}

		int64 offset;
		int64 received;
	};

	EDownloadResult downloadImage(int img_id, std::string img_time, std::string outfile, bool mbr, LoginData login_data,
		DownloadStatus& dl_status, int recur_depth = 0, int64* o_imgsize = nullptr, int64* o_output_file_size = nullptr);

	std::string trim2(const std::string& str);

	bool has_network_device(void);

	bool has_internet_connection(int& ec, std::string& errstatus);

	void configure_internet_server(std::string server_url, const std::string& server_authkey, const std::string& server_proxy,
		bool with_cli);

	void configure_local_server();

	struct SLsblk
	{
		std::string maj_min;
		std::string model;
		std::string size;
		std::string type;
		std::string path;
	};

	std::vector<SLsblk> lsblk(const std::string& dev);

	std::string getPartitionPath(const std::string& dev, int partnum);

	bool do_restore_write_mbr(const std::string& mbr_filename, const std::string& out_device,
		bool fix_gpt, std::string& errmsg);
}