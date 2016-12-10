#pragma once

class ServerUpdate 
{
public:
	ServerUpdate(void);

	void update_client();

	void update_server_version_info();

	void update_dataplan_db();

private:
	void read_update_location();
};