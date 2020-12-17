#pragma once
#include <memory>
#include <string>

struct _IRP;
typedef struct _IRP* PIRP;
struct _DEVICE_OBJECT;
typedef struct _DEVICE_OBJECT* PDEVICE_OBJECT;
typedef void* PVOID;
typedef long NTSTATUS;
typedef NTSTATUS(__stdcall VolIoCompletionRoutine)(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context, unsigned long errorCode, unsigned long bytesTransferred);
typedef VolIoCompletionRoutine VOL_IO_COMPLETION_ROUTINE;
typedef VolIoCompletionRoutine* PVOL_IO_COMPLETION_ROUTINE;
typedef void* HANDLE;

class vol
{
public:
	vol(const std::string& path);
	~vol();

	struct completion_args
	{
		PVOL_IO_COMPLETION_ROUTINE CompletionRoutine;
		PDEVICE_OBJECT DeviceObject;
		PIRP Irp;
		PVOID Context;
	};

	bool read(unsigned long long offset, char* buf, size_t buf_size, completion_args completion);

	bool write(unsigned long long offset, char* buf, size_t buf_size, completion_args completion);

	bool flush();

private:
	std::string path;
	struct vol_handle;
	std::unique_ptr<vol_handle> hvol;
	HANDLE completion_port;
};