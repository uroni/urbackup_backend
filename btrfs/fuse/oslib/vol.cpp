#include "vol.h"
#include "vol.h"
#include <Windows.h>
#include <thread>

struct vol::vol_handle
{
	HANDLE hvol;
};

namespace
{
	struct overlapped_wrapper
	{
		OVERLAPPED ovl;
		vol::completion_args completion;
	};

	void VolCompletion(
		DWORD dwErrorCode,
		DWORD dwNumberOfBytesTransfered,
		LPOVERLAPPED lpOverlapped
	)
	{
		overlapped_wrapper* ovl = static_cast<overlapped_wrapper*>(lpOverlapped->hEvent);
		ovl->completion.CompletionRoutine(ovl->completion.DeviceObject,
			ovl->completion.Irp, ovl->completion.Context, dwErrorCode, dwNumberOfBytesTransfered);
		delete ovl;
	}
}

static void completion_port_thread(HANDLE completion_port)
{
	while (true)
	{
		DWORD bytes_transferred;
		ULONG_PTR key;
		LPOVERLAPPED ovl;
		BOOL b = GetQueuedCompletionStatus(completion_port,
			&bytes_transferred, &key, &ovl, INFINITE);

		if (b && ovl!=nullptr)
		{
			HANDLE hvol = reinterpret_cast<HANDLE>(key);
			DWORD dwErrorCode = 0;
			if (!GetOverlappedResult(hvol, ovl, &bytes_transferred, FALSE))
			{
				dwErrorCode = GetLastError();
			}
			overlapped_wrapper* ovl_w = reinterpret_cast<overlapped_wrapper*>(ovl);
			ovl_w->completion.CompletionRoutine(ovl_w->completion.DeviceObject,
				ovl_w->completion.Irp, ovl_w->completion.Context, dwErrorCode, bytes_transferred);
			delete ovl_w;
		}
	}
}
	

vol::vol(const std::string& path)
	: path(path),
	hvol(std::make_unique<vol_handle>())
{
	hvol->hvol =
		CreateFileA(path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL| FILE_FLAG_OVERLAPPED,
			NULL);

	if (hvol->hvol == INVALID_HANDLE_VALUE)
	{
		//TODO:HANDLE
		abort();
	}


	completion_port = CreateIoCompletionPort(hvol->hvol,
		NULL, reinterpret_cast<ULONG_PTR>(hvol->hvol), 0);

	std::thread compl_thread([&]() {
		completion_port_thread(completion_port);
		});

	compl_thread.detach();
	Sleep(1000);
}

vol::~vol()
{
	if (hvol->hvol != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hvol->hvol);
	}
}

bool vol::read(unsigned long long offset, char* buf, size_t buf_size, completion_args completion)
{
	overlapped_wrapper* ovl = new overlapped_wrapper;
	LARGE_INTEGER loff;
	loff.QuadPart = offset;
	ovl->ovl = {};
	ovl->ovl.Offset = loff.LowPart;
	ovl->ovl.OffsetHigh = loff.HighPart;
	ovl->completion = completion;

	BOOL b = ReadFile(hvol->hvol, buf, buf_size,
		NULL, &ovl->ovl);

	if (!b)
		return false;
	else
		return true;
}

bool vol::write(unsigned long long offset, char* buf, size_t buf_size, completion_args completion)
{
	overlapped_wrapper* ovl = new overlapped_wrapper;
	LARGE_INTEGER loff;
	loff.QuadPart = offset;
	ovl->ovl = {};
	ovl->ovl.Offset = loff.LowPart;
	ovl->ovl.OffsetHigh = loff.HighPart;
	ovl->completion = completion;

	BOOL b = WriteFile(hvol->hvol, buf, buf_size,
		NULL, &ovl->ovl);

	if (!b)
		return false;
	else
		return true;
}

bool vol::flush()
{
	return FlushFileBuffers(hvol->hvol) == TRUE;
}

