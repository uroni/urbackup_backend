import regedit_linux
import sys
import os
from subprocess import call

def padNum(x, i):
    x=str(x)
    while(len(x)<i):
        x="0"+x
    return x

def pressEnter():
    input("Press Enter to continue...")


if len(sys.argv)<2:
    print("Not enough arguments")
   
call("clear", shell=True)
    
print("Mounting Windows partition...");

mountpoint = "/media/tmp_driver_edit";
os.mkdir("/media/tmp_driver_edit");
ret = call("mount -o ignore_case -t lowntfs-3g "+sys.argv[1]+" "+mountpoint, shell=True)

if ret!=0:
    
    print("Mounting failed. Trying to fix NTFS...")
    
    call("ntfsfix "+sys.argv[1], shell=True)
    
    print("Trying to mount again...")
    
    call("mount -o ignore_case -t lowntfs-3g "+sys.argv[1]+" "+mountpoint, shell=True)

windows_path = mountpoint+"/WINDOWS"

if not os.path.isdir(windows_path):
    call("umount "+mountpoint, shell=True)
    os.rmdir(mountpoint)
    print("Error: Windows not found on mountpoint")
    pressEnter()
    exit(1)
    

hive_path= windows_path+"/system32/config/system"

if not os.path.exists(hive_path):
    call("umount "+mountpoint, shell=True)
    print("Error: System registry hive not found on mountpoint")
    pressEnter()
    exit(1)


# Disk controller drivers that have to start at boot. msahci is the AHCI driver of Windows 7,
# storahci the one since Windows 8, stornvme the NVMe driver (since Windows 8.1)
enableDrivers = ["atapi", "intelide", "pciide", "msahci", "iastorv", "storahci", "stornvme"]


def enableBootDrivers(hive_path):
    print("Opening registry...")
    regedit = regedit_linux.regedit_linux(hive_path)

    print("Getting registry path...")
    selectedControlSet = regedit.list("Select");

    v = selectedControlSet["Current"].value;

    currentControlSet = "ControlSet"+padNum(v, 3);

    print("Enumerating drivers...")
    servicesList = regedit.list(currentControlSet+"\\Services")

    print("Found "+str(len(servicesList))+" drivers and services.")

    driverEnabled = False

    for service in servicesList:

        if service.lower() not in enableDrivers:
            continue

        servicePath = currentControlSet+"\\Services\\"+service
        serviceVals = regedit.list(servicePath)

        if "Type" not in serviceVals or "Start" not in serviceVals:
            continue

        if serviceVals["Type"].value!=1:
            continue

        if serviceVals["Start"].value!=0:
            print("Enabling driver "+service+"...")
            regedit.edit(servicePath, "Start", "0")
            driverEnabled=True

        # Since Windows 8 a driver that was not needed to boot on the old hardware has a
        # StartOverride value that keeps it from starting at boot, whatever "Start" says
        if "StartOverride" in serviceVals and serviceVals["StartOverride"].key:
            overridePath = servicePath+"\\StartOverride"
            overrideVals = regedit.list(overridePath)

            for name in overrideVals:
                override = overrideVals[name]
                if not override.key and override.value!=0:
                    print("Removing the start override of driver "+service+"...")
                    regedit.edit(overridePath, name, "0")
                    driverEnabled=True

    return driverEnabled


if not enableBootDrivers(hive_path):
    print("No driver needed to be enabled. Changed nothing.")

print("Syncing...")
call("sync", shell=True)

print("Unmounting Windows partition")
call("umount "+mountpoint, shell=True)

if os.path.isdir(windows_path):
    print("Still there. Trying again.")
    call("umount -l "+mountpoint, shell=True)

os.rmdir(mountpoint)

print("Done.")

pressEnter()

exit(0)