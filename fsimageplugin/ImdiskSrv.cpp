#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"
#include "ImdiskSrv.h"
#include "vhdfile.h"
#include <Windows.h>
#include <Subauth.h>
#include <imdproxy.h>
#include <imdisk.h>
#include <string>

namespace
{
	HRESULT ModifyPrivilege(
		IN LPCTSTR szPrivilege,
		IN BOOL fEnable)
	{
		HRESULT hr = S_OK;
		TOKEN_PRIVILEGES NewState;
		LUID             luid;
		HANDLE hToken = NULL;

		// Open the process token for this process.
		if (!OpenProcessToken(GetCurrentProcess(),
			TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
			&hToken))
		{
			Server->Log("Failed OpenProcessToken", LL_ERROR);
			return ERROR_FUNCTION_FAILED;
		}

		// Get the local unique ID for the privilege.
		if (!LookupPrivilegeValue(NULL,
			szPrivilege,
			&luid))
		{
			CloseHandle(hToken);
			Server->Log("Failed LookupPrivilegeValue", LL_ERROR);
			return ERROR_FUNCTION_FAILED;
		}

		// Assign values to the TOKEN_PRIVILEGE structure.
		NewState.PrivilegeCount = 1;
		NewState.Privileges[0].Luid = luid;
		NewState.Privileges[0].Attributes =
			(fEnable ? SE_PRIVILEGE_ENABLED : 0);

		// Adjust the token privilege.
		if (!AdjustTokenPrivileges(hToken,
			FALSE,
			&NewState,
			0,
			NULL,
			NULL))
		{
			Server->Log("Failed AdjustTokenPrivileges", LL_ERROR);
			hr = ERROR_FUNCTION_FAILED;
		}

		// Close the handle.
		CloseHandle(hToken);

		return hr;
	}

	bool stopImdiskSvc()
	{
		SC_HANDLE sc = OpenSCManager(NULL, NULL,
			SC_MANAGER_ALL_ACCESS);

		if (sc == NULL)
		{
			return false;
		}

		SC_HANDLE service = OpenService(
			sc, L"ImDskSvc",
			SERVICE_STOP |	SERVICE_QUERY_STATUS |
			SERVICE_CHANGE_CONFIG);

		if (service == NULL)
		{
			CloseServiceHandle(sc);
			return false;
		}

		bool ret = true;
		do
		{
			SERVICE_STATUS ssp;
			if (!ControlService(service, SERVICE_CONTROL_STOP, &ssp))
			{
				ret = false;
			}

			if (!ChangeServiceConfig(service, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
			{
				ret = false;
			}

		} while (0);

		CloseServiceHandle(sc);
		CloseServiceHandle(service);

		return ret;
	}

	class ScopedCloseHandle
	{
	public:
		ScopedCloseHandle(HANDLE h)
			: h(h) {}

		~ScopedCloseHandle()
		{
			if (h != INVALID_HANDLE_VALUE)
			{
				CloseHandle(h);
			}
		}

		void reset(HANDLE hNew = INVALID_HANDLE_VALUE)
		{
			if (h != INVALID_HANDLE_VALUE)
			{
				CloseHandle(h);
			}
			h = hNew;
		}

		void release()
		{
			h = INVALID_HANDLE_VALUE;
		}

	private:
		HANDLE h;
	};

	class ImdiskConnection : public IThread
	{
	public:
		ImdiskConnection(HANDLE hPipe)
			: hPipe(hPipe)
		{}

		~ImdiskConnection()
		{
			if (hPipe != INVALID_HANDLE_VALUE)
			{
				CloseHandle(hPipe);
			}
		}

		bool send_error(DWORD code)
		{
			IMDPROXY_CONNECT_RESP connect_resp = {};
			connect_resp.error_code = code;
			DWORD written;
			BOOL b = WriteFile(hPipe, &connect_resp, sizeof(connect_resp), &written, NULL);
			return b && written == sizeof(connect_resp);
		}

		void connect()
		{
			IMDPROXY_CONNECT_REQ connect_req;

			DWORD read;
			BOOL b = ReadFile(hPipe, &connect_req.request_code, sizeof(connect_req.request_code), &read, NULL);
			if (!b || read != sizeof(connect_req.request_code))
			{
				return;
			}
			if (connect_req.request_code != IMDPROXY_REQ_CONNECT)
			{
				return;
			}

			b = ReadFile(hPipe, reinterpret_cast<char*>(&connect_req) + sizeof(connect_req.request_code), 
				sizeof(connect_req) - sizeof(connect_req.request_code), &read, NULL);
			if (!b || read != sizeof(connect_req) - sizeof(connect_req.request_code))
			{
				return;
			}
			if (connect_req.length == 0 || connect_req.length > 520)
			{
				return;
			}
			std::wstring connect_string;
			connect_string.resize(connect_req.length);
			b = ReadFile(hPipe, &connect_string[0], static_cast<DWORD>(connect_req.length), &read, NULL);
			if (!b || read != connect_req.length)
			{
				return;
			}		

			if (IMDISK_PROXY_TYPE(connect_req.flags) != IMDISK_PROXY_TYPE_TCP)
			{
				send_error(ERROR_NOT_SUPPORTED);
				return;
			}

			size_t l_col = connect_string.find_last_of(':');

			int64 offset = 0;
			int64 length = -1;
			if (l_col > 3)
			{
				std::string slice = Server->ConvertFromWchar(connect_string.substr(l_col + 1));
				offset = watoi64(getuntil("-", slice));
				length = watoi64(getafter("-", slice));
				connect_string.erase(l_col, connect_string.size() - l_col);
			}
			else
			{
				offset = 512 * 1024;
			}

			VHDFile vhdfile(Server->ConvertFromWchar(connect_string), true, 0);
			if(!vhdfile.isOpen())
			{
				send_error(ERROR_FILE_NOT_FOUND);
				return;
			}

			vhdfile.addVolumeOffset(offset);

			if (length == -1)
			{
				length = vhdfile.Size();
			}
			else
			{
				length = (std::min)(length, vhdfile.Size());
			}

			std::string uid;
			uid.resize(16);
			Server->randomFill(&uid[0], uid.size());

			std::wstring pipe_name = L"\\\\.\\PIPE\\" + Server->ConvertToWchar(bytesToHex(uid));

			HANDLE hConnectPipe1 = CreateNamedPipe(pipe_name.c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED, 0, PIPE_UNLIMITED_INSTANCES,
				0, 0, 0, NULL);

			if (hConnectPipe1 == INVALID_HANDLE_VALUE)
			{
				send_error(GetLastError());
				return;
			}

			ScopedCloseHandle closeConnectPipe1(hConnectPipe1);

			OVERLAPPED ovl = {};
			ovl.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

			ScopedCloseHandle closeEvent(ovl.hEvent);

			if (!ConnectNamedPipe(hConnectPipe1, &ovl)
				&& GetLastError() != ERROR_IO_PENDING)
			{
				send_error(GetLastError());
				return;
			}

			HANDLE hConnectPipe2 = CreateFile(pipe_name.c_str(),
				GENERIC_READ | GENERIC_WRITE, 0, NULL,
				OPEN_EXISTING, 0, NULL);

			if (hConnectPipe2 == INVALID_HANDLE_VALUE)
			{
				send_error(GetLastError());
				return;
			}

			ScopedCloseHandle closeConnectPipe2(hConnectPipe2);

			if (WaitForSingleObject(ovl.hEvent, INFINITE) != WAIT_OBJECT_0)
			{
				send_error(ERROR_CONNECTION_ABORTED);
				return;
			}

			closeEvent.reset();

			IMDPROXY_CONNECT_RESP connect_resp = {};

			{
				HANDLE hDriver = CreateFile(IMDISK_CTL_DOSDEV_NAME,
					GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					0,
					NULL);

				if (hDriver == INVALID_HANDLE_VALUE)
				{
					send_error(GetLastError());
					return;
				}

				ScopedCloseHandle closeDriver(hDriver);

				DWORD bytes_returned;
				if (!DeviceIoControl(hDriver, IOCTL_IMDISK_REFERENCE_HANDLE,
					&hConnectPipe1, sizeof(hConnectPipe1), &connect_resp.object_ptr,
					sizeof(connect_resp.object_ptr), &bytes_returned, NULL))
				{
					send_error(GetLastError());
					return;
				}
			}

			DWORD written;
			b = WriteFile(hPipe, &connect_resp, sizeof(connect_resp), &written, NULL);
			if (!b || written != sizeof(connect_resp))
			{
				return;
			}

			std::vector<char> buf;

			while (true)
			{
				ULONGLONG request_code;
				b = ReadFile(hConnectPipe2, &request_code, sizeof(request_code), &read, NULL);
				if (!b || read != sizeof(request_code))
				{
					return;
				}

				if (request_code == IMDPROXY_REQ_INFO)
				{
					IMDPROXY_INFO_RESP info_resp;
					info_resp.file_size = length;
					info_resp.flags = IMDPROXY_FLAG_RO;
					info_resp.req_alignment = 512;

					b = WriteFile(hConnectPipe2, &info_resp, sizeof(info_resp), &written, NULL);

					if (!b || written != sizeof(info_resp))
					{
						Server->Log("Error sending info response. Err: " + convert((int64)GetLastError()), LL_ERROR);
						break;
					}
					continue;
				}
				else if (request_code != IMDPROXY_REQ_READ)
				{
					Server->Log("Unsupported ImDisk request code "+ convert(request_code), LL_ERROR);

					ULONGLONG resp = ENODEV;
					WriteFile(hConnectPipe2, &resp, sizeof(resp), &written, NULL);
					return;
				}
				
				IMDPROXY_READ_REQ read_req = {};

				b = ReadFile(hConnectPipe2, &read_req.offset, sizeof(read_req) - sizeof(request_code), &read, NULL);
				if (!b || read!=sizeof(read_req)-sizeof(request_code))
				{
					Server->Log("Reading read request failed. Err: " + convert((int64)GetLastError()), LL_ERROR);
					break;
				}

				if (buf.size() < read_req.length + sizeof(IMDPROXY_READ_RESP))
				{
					buf.resize(read_req.length + sizeof(IMDPROXY_READ_RESP));
				}

				//Server->Log("Read req offset=" + convert(read_req.offset) + " length=" + convert(read_req.length), LL_INFO);

				IMDPROXY_READ_RESP* read_resp = reinterpret_cast<IMDPROXY_READ_RESP*>(buf.data());
				read_resp->length = 0;
				read_resp->errorno = 0;

				if (read_req.length > 0)
				{
					bool has_read_error = false;
					_u32 rc = vhdfile.Read(read_req.offset, buf.data() + sizeof(IMDPROXY_READ_RESP)+ read_resp->length,
						static_cast<_u32>(read_req.length), &has_read_error);
					if (has_read_error)
					{
						read_resp->errorno = EIO;
						read_resp->length = 0;
						rc = 0;
					}
					else
					{
						read_resp->length += rc;
					}
				}

				//Server->Log("Read resp length=" + convert(read_resp->length), LL_INFO);
				b = WriteFile(hConnectPipe2, read_resp, static_cast<DWORD>(sizeof(IMDPROXY_READ_RESP) + read_resp->length), &written, NULL);
				if (!b || written != sizeof(IMDPROXY_READ_RESP) + read_resp->length)
				{
					Server->Log("Sending read response failed. Err: " + convert((int64)GetLastError()), LL_ERROR);
					break;
				}
			}
		}

		void operator()()
		{
			connect();			
			delete this;
		}

	private:
		HANDLE hPipe;
	};
}


ImdiskSrv::ImdiskSrv()
{
	
}

bool ImdiskSrv::installed()
{
	HANDLE hDriver = CreateFile(IMDISK_CTL_DOSDEV_NAME,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		0,
		NULL);

	if (hDriver != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hDriver);
	}

	return hDriver != INVALID_HANDLE_VALUE;
}

void ImdiskSrv::operator()()
{
	HRESULT hr = ModifyPrivilege(SE_TCB_NAME, TRUE);
	if (!SUCCEEDED(hr))
	{
		Server->Log("Failed to modify SE_TCB privilege. VHD mounting won't work.", LL_ERROR);
		return;
	}

	stopImdiskSvc();

	while (true)
	{
		HANDLE hNamedPipe = CreateNamedPipe(IMDPROXY_SVC_PIPE_DOSDEV_NAME,
			PIPE_ACCESS_DUPLEX | FILE_FLAG_WRITE_THROUGH,
			0,
			PIPE_UNLIMITED_INSTANCES,
			0, 0, 0, NULL);

		if (hNamedPipe == INVALID_HANDLE_VALUE)
		{
			Server->Log("Error creating named pipe to ImProxy. Err: " + convert((int64)GetLastError()), LL_ERROR);
			return;
		}

		if (!ConnectNamedPipe(hNamedPipe, NULL))
		{
			Server->Log("Error connecting named pipe to ImProxy. Err: " + convert((int64)GetLastError()), LL_ERROR);
			CloseHandle(hNamedPipe);
			return;
		}

		Server->createThread(new ImdiskConnection(hNamedPipe), "imdisk conn");
	}
}
