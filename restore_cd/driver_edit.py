import regedit_linux
import sys
import os
from subprocess import call

def padNum(x, i):
    x=str(x)
    while(len(x)<i):
        x="0"+x
    return x


if len(sys.argv)<2:
    print("Not enough arguments")
   
call("clear", shell=True)
    
print("Mounting Windows partition...");

mountpoint = "/media/tmp_driver_edit";
os.mkdir("/media/tmp_driver_edit");
ret = call("mount -t ntfs-3g "+sys.argv[1]+" "+mountpoint, shell=True)

if ret!=0:
    
    print("Mounting failed. Trying to fix NTFS...")
    
    call("ntfsfix "+sys.argv[1], shell=True)
    
    print("Trying to mount again...")
    
    call("mount -t ntfs-3g "+sys.argv[1]+" "+mountpoint, shell=True)

windows_path = mountpoint+"/WINDOWS"


if not os.path.isdir(windows_path):
    call("umount "+mountpoint)
    os.rmdir(mountpoint)
    print("Error: Windows not found on mountpoint")
    exit(1)
    

hive_path= windows_path+"/system32/config/system"

if not os.path.exists(hive_path):
    call("umount "+mountpoint)
    print("Error: System registry hive not found on mountpoint")
    exit(1)


enableDrivers = ["atapi", "intelide", "pciide", "msahci", "iastorv"]
    

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
    
    servicePath = currentControlSet+"\\Services\\"+service
    serviceVals = regedit.list(servicePath)
    
    if "Type" in serviceVals and "Start" in serviceVals:
        if serviceVals["Type"].value==1 and serviceVals["Start"].value!=0:        
            
            if service.lower() in enableDrivers:
                
                print("Enabling driver "+service+"...")
                regedit.edit(servicePath, "Start", "0")
                driverEnabled=True
                
                
if not driverEnabled:
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

input("Press Enter to continue...")

exit(0)