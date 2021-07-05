extern "C"
{
#include "device.h"
}
#include "../../../Interface/File.h"
#include <string>

namespace
{
	class DiskDevice
	{
	public:
		DiskDevice() {}

		void Init(IFile* p_img, ULONGLONG DiskLength, ULONG DiskNumber)
		{
			img = p_img;
			disk_length = DiskLength;
		}

		NTSTATUS InternalDeviceControl(PIRP Irp)
		{
			switch (Irp->StackLocation.Parameters.DeviceIoControl.IoControlCode)
			{
			case IOCTL_DISK_UPDATE_PROPERTIES:
				return STATUS_SUCCESS;
			case IOCTL_DISK_GET_DRIVE_LAYOUT_EX:
			{
				PDRIVE_LAYOUT_INFORMATION_EX layout_info = reinterpret_cast<PDRIVE_LAYOUT_INFORMATION_EX>(Irp->AssociatedIrp.SystemBuffer);
				layout_info->PartitionCount = 0;
				Irp->IoStatus.Information = sizeof(DRIVE_LAYOUT_INFORMATION_EX);
				return STATUS_SUCCESS;
			}
			case IOCTL_DISK_GET_LENGTH_INFO:
			{
				PGET_LENGTH_INFORMATION length_info = reinterpret_cast<PGET_LENGTH_INFORMATION>(Irp->AssociatedIrp.SystemBuffer);
				length_info->Length.QuadPart = disk_length;
				Irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);
				return STATUS_SUCCESS;
			}
			case IOCTL_STORAGE_GET_DEVICE_NUMBER:
			{
				PSTORAGE_DEVICE_NUMBER device_number = reinterpret_cast<PSTORAGE_DEVICE_NUMBER>(Irp->AssociatedIrp.SystemBuffer);
				device_number->DeviceNumber = disk_number;
				device_number->PartitionNumber = 0;
				device_number->DeviceType = 0;
				Irp->IoStatus.Information = sizeof(STORAGE_DEVICE_NUMBER);
				return STATUS_SUCCESS;
			}
			case IOCTL_DISK_GET_DRIVE_GEOMETRY:
			{
				PDISK_GEOMETRY disk_geom = reinterpret_cast<PDISK_GEOMETRY>(Irp->AssociatedIrp.SystemBuffer);
				memset(disk_geom, 0, sizeof(DISK_GEOMETRY));
				disk_geom->BytesPerSector = 4096;
				Irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
				return STATUS_SUCCESS;
			}
			case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
			{
				PMOUNTDEV_NAME mountdev_name = reinterpret_cast<PMOUNTDEV_NAME>(Irp->AssociatedIrp.SystemBuffer);
				std::wstring name = L"\\Device\\Btrfs{foobar}";
				mountdev_name->NameLength = static_cast<ULONG>((name.size() + 1)*sizeof(wchar_t));

				if (Irp->StackLocation.Parameters.DeviceIoControl.OutputBufferLength >= sizeof(ULONG) + (name.size() + 1) * sizeof(wchar_t))
				{
					memcpy(mountdev_name->Name, name.data(), sizeof(wchar_t) * (name.size() + 1));
					return STATUS_SUCCESS;
				}
				else
				{
					return STATUS_BUFFER_OVERFLOW;
				}
			}
			}

			return STATUS_NOT_IMPLEMENTED;
		}

		NTSTATUS Read(PIRP Irp, PDEVICE_OBJECT DeviceObject)
		{
			bool has_read_err = false;
			_u32 rc = img->Read(static_cast<int64>(Irp->StackLocation.Parameters.Read.ByteOffset.QuadPart),
				reinterpret_cast<char*>(Irp->UserBuffer), Irp->StackLocation.Parameters.Read.Length,
				&has_read_err);

			if (has_read_err)
			{
				return STATUS_CRC_ERROR;
			}

			Irp->IoStatus.Status = STATUS_SUCCESS;
			Irp->IoStatus.Information = rc;

			Irp->CompletionRoutine(DeviceObject, Irp, Irp->CompletionRoutineContext);

			return STATUS_SUCCESS;
		}

		NTSTATUS Write(PIRP Irp, PDEVICE_OBJECT DeviceObject)
		{
			bool has_read_err = false;
			_u32 rc = img->Write(static_cast<int64>(Irp->StackLocation.Parameters.Write.ByteOffset.QuadPart),
				reinterpret_cast<char*>(Irp->UserBuffer), Irp->StackLocation.Parameters.Write.Length,
				&has_read_err);

			if (has_read_err)
			{
				return STATUS_CRC_ERROR;
			}

			Irp->IoStatus.Status = STATUS_SUCCESS;
			Irp->IoStatus.Information = rc;

			Irp->CompletionRoutine(DeviceObject, Irp, Irp->CompletionRoutineContext);

			return STATUS_SUCCESS;
		}

		NTSTATUS DeviceControl(PIRP Irp, PDEVICE_OBJECT DeviceObject)
		{
			if (Irp->StackLocation.Parameters.DeviceIoControl.IoControlCode != IOCTL_ATA_PASS_THROUGH)
			{
				return STATUS_NOT_IMPLEMENTED;
			}

			if (Irp->StackLocation.Parameters.DeviceIoControl.InputBufferLength != sizeof(ATA_PASS_THROUGH_EX))
				return STATUS_INVALID_PARAMETER;

			ATA_PASS_THROUGH_EX* ata = static_cast<ATA_PASS_THROUGH_EX*>(Irp->UserBuffer);

			if (ata->CurrentTaskFile[6] == IDE_COMMAND_FLUSH_CACHE)
			{
				bool b = img->Sync();
				Irp->IoStatus.Status = b ? STATUS_SUCCESS : STATUS_UNEXPECTED_IO_ERROR;
				Irp->CompletionRoutine(DeviceObject, Irp, Irp->CompletionRoutineContext);
				return STATUS_SUCCESS;
			}

			return STATUS_NOT_IMPLEMENTED;
		}

		NTSTATUS Call(PIRP Irp, PDEVICE_OBJECT DeviceObject)
		{
			switch (Irp->StackLocation.MajorFunction)
			{
			case IRP_MJ_INTERNAL_DEVICE_CONTROL:
				return InternalDeviceControl(Irp);
			case IRP_MJ_READ:
				return Read(Irp, DeviceObject);
			case IRP_MJ_WRITE:
				return Write(Irp, DeviceObject);
			case IRP_MJ_DEVICE_CONTROL:
				return DeviceControl(Irp, DeviceObject);
			}
			return STATUS_NOT_IMPLEMENTED;
		}

		bool Flush()
		{
			return img->Sync();
		}

	private:
		ULONGLONG disk_length;
		ULONG disk_number;

		IFile* img;
	};
}

struct _DISK_DEVICE
{
	DiskDevice disk_device;
};

NTSTATUS DiskDeviceCall(PDISK_DEVICE DiskDevice, PIRP Irp, PDEVICE_OBJECT DeviceObject)
{
	return DiskDevice->disk_device.Call(Irp, DeviceObject);
}

PDISK_DEVICE InitDiskDevice(void* img, ULONGLONG Length, ULONG Number)
{
	PDISK_DEVICE ret = new _DISK_DEVICE;
	ret->disk_device.Init(static_cast<IFile*>(img), Length, Number);
	return ret;
}

NTSTATUS FlushDiskDevice(PDISK_DEVICE DiskDevice)
{
	if (DiskDevice->disk_device.Flush())
	{
		return STATUS_SUCCESS;
	}
	return STATUS_UNEXPECTED_IO_ERROR;
}
