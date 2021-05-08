#pragma once
#include <windef.h>

struct _DISK_DEVICE;
typedef struct _DISK_DEVICE* PDISK_DEVICE;

struct _DEVICE_OBJECT;
typedef struct _DEVICE_OBJECT* PDEVICE_OBJECT;

NTSTATUS DiskDeviceCall(PDISK_DEVICE DiskDevice, PIRP Irp, PDEVICE_OBJECT DeviceObject);
PDISK_DEVICE InitDiskDevice(void* img, ULONGLONG Length, ULONG Number);
NTSTATUS FlushDiskDevice(PDISK_DEVICE DiskDevice);