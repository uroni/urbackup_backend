/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2017 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include <sys/mman.h>
#include "../tclap/CmdLine.h"
#include <libdevmapper.h>
#include "../stringtools.h"
#include <sys/time.h>
#include <unistd.h>

std::string cmdline_version = "1.0";

int64_t getTimeMS()
{

	timespec tp;
	if (clock_gettime(CLOCK_MONOTONIC, &tp) != 0)
	{
		timeval tv;
		gettimeofday(&tv, NULL);
		static long start_t = tv.tv_sec;
		tv.tv_sec -= start_t;
		return tv.tv_sec * 1000 + tv.tv_usec / 1000;
	}
	return static_cast<int64_t>(tp.tv_sec) * 1000 + tp.tv_nsec / 1000000;
}

std::string strip_mapper(std::string dev)
{
    if (dev.find("/dev/mapper/") == 0)
    {
        dev.erase(0, 12);
    }
    return dev;
}

bool do_task_simple(int task, const std::string& dev)
{
    struct dm_task* dmt;
    if (!(dmt = dm_task_create(task)))
        return false;
    
    if (!dm_task_set_name(dmt, strip_mapper(dev).c_str()))
        return false;

    if (!dm_task_run(dmt))
        return false;

    dm_task_destroy(dmt);
    return true;
}

bool do_create_dm_dev(uint64_t start, uint64_t size, const std::string& ttype, const std::string& params, bool readonly, const std::string& dev, uint32_t cookie)
{
    struct dm_task* dmt;
    if (!(dmt = dm_task_create(DM_DEVICE_CREATE)))
        return false;

    if (!dm_task_set_name(dmt, strip_mapper(dev).c_str()))
        return false;


    if (!dm_task_set_cookie(dmt, &cookie, 0))
        return false;

    if (readonly && !dm_task_set_ro(dmt))
        return false;

    if (!dm_task_add_target(dmt, start, size, ttype.c_str(), params.c_str()))
        return false;

    if (!dm_task_run(dmt))
    {
        std::cerr << "Error creating device" << std::endl;
        return false;
    }

    dm_task_destroy(dmt);

    return true;
}

bool do_reload_dm_dev(uint64_t start, uint64_t size, const std::string& ttype, const std::string& params, bool readonly, const std::string& dev)
{
    struct dm_task* dmt;
    if (!(dmt = dm_task_create(DM_DEVICE_RELOAD)))
        return false;

    if (!dm_task_set_name(dmt, strip_mapper(dev).c_str()))
        return false;

    if (readonly && !dm_task_set_ro(dmt))
        return false;

    if (!dm_task_add_target(dmt, start, size, ttype.c_str(), params.c_str()))
        return false;

    if (!dm_task_run(dmt))
    {
        std::cerr << "Error creating device" << std::endl;
        return false;
    }

    dm_task_destroy(dmt);

    return true;
}

bool do_message(const std::string& dev, const std::string& msg)
{
    struct dm_task* dmt;
    if (!(dmt = dm_task_create(DM_DEVICE_RELOAD)))
        return false;

    if (!dm_task_set_name(dmt, strip_mapper(dev).c_str()))
        return false;

    if (!dm_task_set_sector(dmt, 0))
        return false;

    if (!dm_task_set_message(dmt, msg.c_str()))
        return false;

    if (!dm_task_run(dmt))
    {
        std::cerr << "Error sending message" << std::endl;
        return false;
    }
    dm_task_destroy(dmt);

    return true;
}

bool has_snapshot_origin(const std::string& dev, bool& ret, uint64_t& start, uint64_t& length, std::string& params)
{
    struct dm_task* dmt;
    if (!(dmt = dm_task_create(DM_DEVICE_TABLE)))
        return false;

    if (!dm_task_set_name(dmt, strip_mapper(dev).c_str()))
        return false;

    if (!dm_task_run(dmt))
    {
        std::cerr << "Error getting device table" << std::endl;
        return false;
    }

    struct dm_info info;
    if (!dm_task_get_info(dmt, &info) || !info.exists)
    {
        std::cerr << "Error getting device info" << std::endl;
        return false;
    }

    ret = false;

    void* next = NULL;
    do {
        uint64_t start, length;
        char* target_type, * ptr_params;
        next = dm_get_next_target(dmt, next, &start, &length,
            &target_type, &ptr_params);
        
        ret = std::string(target_type) == "snapshot-origin";
        if (ptr_params != NULL)
            params = ptr_params;

    } while (next);

    dm_task_destroy(dmt);

    return true;
}

bool get_era(const std::string& dev, int64_t& era)
{
    struct dm_task* dmt;
    if (!(dmt = dm_task_create(DM_DEVICE_STATUS)))
        return false;

    if (!dm_task_set_name(dmt, strip_mapper(dev).c_str()))
        return false;

    if (!dm_task_run(dmt))
    {
        std::cerr << "Error getting device table" << std::endl;
        return false;
    }

    struct dm_info info;
    if (!dm_task_get_info(dmt, &info) || !info.exists)
    {
        std::cerr << "Error getting device info for getting era" << std::endl;
        return false;
    }

    bool ret = false;
    void* next = NULL;
    do {
        uint64_t start, length;
        char* target_type, * ptr_params;
        next = dm_get_next_target(dmt, next, &start, &length,
            &target_type, &ptr_params);

        if (std::string(target_type) == "era")
        {
            std::vector<std::string> toks;
            Tokenize(ptr_params, toks, " ");

            if (toks.size() < 4)
            {
                ret = false;
                std::cerr << "Not enough status" << std::endl;
            }
            else
            {
                era = watoi64(toks[2]);
                if (convert(static_cast<long long int>(era)) != toks[2])
                {
                    std::cerr << "Unable to convert era to number" << std::endl;
                }
            }
        }

    } while (next);

    dm_task_destroy(dmt);

    return ret;
}

struct DMResumeDev
{
    DMResumeDev(std::string dev, uint32_t cookie)
        : dev(dev), cookie(cookie)
    {}

    ~DMResumeDev()
    {
        if (!do_task_simple(DM_DEVICE_RESUME, dev))
        {
            std::cerr << "Error resuming dev " << dev << std::endl;
        }
        
        if (!dm_udev_wait(cookie))
        {
            std::cerr << "Error waiting for udev cookie release" << std::endl;
        }
    }

    std::string dev;
    uint32_t cookie;
};

bool do_create_dm_snapshot(const std::string& dev, const std::string& dev_clone, const std::string& dev_snap, const std::string& cow_dev, const std::string& dev_origin, const std::string& era_access_dev, uint64_t dev_size)
{
    int rc = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (rc != 0)
    {
        perror("mlockall");
        return false;
    }

    uint32_t cookie = 0;

    if (!dm_udev_create_cookie(&cookie))
    {
        std::cerr << "Error creating udev cookie" << std::endl;
        return false;
    }

    if (!do_task_simple(DM_DEVICE_SUSPEND, dev))
    {
        if (!dm_udev_wait(cookie))
        {
            std::cerr << "Error waiting for udev cookie release (1)" << std::endl;
        }
        return false;
    }

    int64_t init_era;
    if (get_era(dev_clone, init_era))
    {
        std::cout << "Checkpointing device for CBT..." << std::endl;

        if (!do_message(dev_clone, "checkpoint"))
        {
            if (!dm_udev_wait(cookie))
        {
            std::cerr << "Error waiting for udev cookie release (2)" << std::endl;
        }
            return false;
        }

        int64_t starttime = getTimeMS();

        bool era_update = false;
        while (getTimeMS() - starttime < 60000)
        {
            int64_t curr_era;
            if (get_era(dev_clone, curr_era)
                && curr_era > init_era)
            {
                era_update = true;
                break;
            }

            usleep(100000);
        }

        if (era_update)
        {
            std::cout << "CBT=type=era" << std::endl;
            std::cout << "CBT_FILE=" << era_access_dev << std::endl;
        }
    }

    DMResumeDev resume_dev(dev, cookie);


    std::cout << "Creating snapshot device..." << std::endl;

    if (!do_create_dm_dev(0, dev_size, "snapshot", dev_clone + " " + cow_dev + " N 32", true, dev_snap, cookie))
        return false;

    struct stat statbuf;
    if (stat(dev_origin.c_str(), &statbuf)!=0 && errno==ENOENT)
    {
        std::cout << "Creating snapshot origin device..." << std::endl;
        if (!do_create_dm_dev(0, dev_size, "snapshot-origin", dev_clone, false, dev_origin, cookie))
            return false;
    }

    bool b_origin;
    uint64_t start, length;
    std::string origin_params;
    if (!has_snapshot_origin(dev_origin, b_origin, start, length, origin_params))
        return false;

    if(start!=0 && length!=dev_size)
    {
        std::cerr << "Start (" << start << ") or length (" << length << ") of origin dev wrong. Expected "
            << "0, " << dev_size << std::endl;
        return false;
    }

    if(!b_origin)
    {
        std::cerr << "Origin dev " << dev_origin << " is not snapshot-origin" << std::endl;
        return false;
    }

    bool b_snapshot_origin;
    uint64_t tmp_start, tmp_length;
    std::string tmp_params;
    if (!has_snapshot_origin(dev, b_snapshot_origin, tmp_start, tmp_length, tmp_params))
        return false;

    if (!b_snapshot_origin)
    {
        if (!do_reload_dm_dev(0, dev_size, "snapshot-origin", origin_params, false, dev))
            return false;
    }

    return true;
}

int main(int argc, char* argv[])
{
	if(argc==0)
	{
		std::cout << "Not enough arguments (zero arguments) -- no program name" << std::endl;
		return 1;
	}
    try
    {
        TCLAP::CmdLine cmd("Create snapshot of dm-snapshot root volume", ' ', cmdline_version);

        //bool do_create_dm_snapshot(const std::string& dev, const std::string& dev_clone, const std::string& dev_snap, const std::string& cow_dev, const std::string& dev_origin, const std::string& era_access_dev, uint64_t dev_size)

        TCLAP::ValueArg<std::string> dev_arg("","dev","Device to snapshot",true,"","string");
        TCLAP::ValueArg<std::string> clone_dev_arg("","clone-dev","Clone of device to snapshot",true,"","string");
        TCLAP::ValueArg<std::string> snap_dev_arg("","snap-dev","Name of snapshot device",true,"","string");
        TCLAP::ValueArg<std::string> cow_dev_arg("","cow-dev","COW snapshot storage device",true,"","string");
        TCLAP::ValueArg<std::string> origin_dev_arg("","origin-dev","Snapshot origin device",true,"","string");
        TCLAP::ValueArg<std::string> era_access_dev_arg("","era-access-dev","Device for ERA access",true,"","string");
        TCLAP::ValueArg<int64_t> dev_size_arg("","dev-size","Device for ERA access",true,0,"int");

        cmd.add(dev_arg);
        cmd.add(clone_dev_arg);
        cmd.add(snap_dev_arg);
        cmd.add(cow_dev_arg);
        cmd.add(origin_dev_arg);
        cmd.add(era_access_dev_arg);
        cmd.add(dev_size_arg);

        cmd.parse(argc, argv);

        bool r = do_create_dm_snapshot(dev_arg.getValue(), clone_dev_arg.getValue(), snap_dev_arg.getValue(), cow_dev_arg.getValue(), origin_dev_arg.getValue(), era_access_dev_arg.getValue(), dev_size_arg.getValue());

        if(!r)
        {
            std::cerr << "Creating dm-snapshot failed" << std::endl;
            exit(1);
        }

        exit(0);
    }
    catch (TCLAP::ArgException &e)
	{
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    }
}
