#pragma once

#include "../Interface/Thread.h"
#include <string>

class ImageMount : public IThread
{
public:
	void operator()();

	static bool mount_image(int backupid);
	static std::string get_mount_path(int backupid);

private:

};
