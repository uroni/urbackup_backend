#include "../config.h"
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

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
}
