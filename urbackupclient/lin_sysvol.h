#include "../config.h"
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
	std::string getMountDevice(const std::string& path)
	{		
#ifndef HAVE_MNTENT_H
		return std::string();
#else
		FILE *aFile;

		aFile = setmntent("/proc/mounts", "r");
		if (aFile == NULL) {
			return std::string();
		}
		struct mntent *ent;
		while (NULL != (ent = getmntent(aFile)))
		{
			if(std::string(ent->mnt_dir)==path)
			{
				endmntent(aFile);
				return ent->mnt_fsname;
			}
		}
		endmntent(aFile);

		return std::string();
#endif //HAVE_MNTENT_H
	}

    std::string getSysVolumeCached(std::string& mpath)
    {
        mpath = getMountDevice("/boot");
        return mpath;
    }

    std::string getEspVolumeCached(std::string& mpath)
    {
        return "";
    }

    std::string getRootVol()
    {
        return getMountDevice("/");
    }

	bool isDevice(const std::string& path)
	{
		struct stat stbuf;
		if (stat(path.c_str(), &stbuf) == 0)
		{
			if (S_ISBLK(stbuf.st_mode))
			{
				return true;
			}
		}
		return false;
	}

	std::string mapLinuxDev(const std::string& path)
	{
		if (path == "C" || path == "C:")
			return getRootVol();

		if (isDevice(path))
			return path;

		return getMountDevice(path);
	}
}
