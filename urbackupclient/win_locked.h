#include <thread>
#include <atomic>
#include <Windows.h>
#include "../Interface/Server.h"
#include <WtsApi32.h>

std::atomic<int> locked_sessions;
std::atomic<int> active_sessions;
bool last_locked_state = true;

static bool is_win_locked()
{
	if (active_sessions == 0)
		return true;

	return active_sessions == locked_sessions;
}

static LRESULT win_locked_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_WTSSESSION_CHANGE)
	{
		if (wparam == WTS_SESSION_LOGON)
		{
			++active_sessions;
		}
		else if (wparam == WTS_SESSION_LOGOFF)
		{
			--active_sessions;
		}
		if (wparam == WTS_SESSION_UNLOCK)
		{
			--locked_sessions;
		}
		else if (wparam == WTS_SESSION_LOCK)
		{
			++locked_sessions;
		}

		bool locked_state = is_win_locked();
		if (last_locked_state != locked_state)
		{
			std::thread t([locked_state]() {
				ClientConnector::tochannelSendLocked(locked_state);

				if (IndexThread::pauseIfWindowsUnlocked())
				{
					IdleCheckerThread::setPause(!locked_state);
				}

				});
			t.detach();
		}
	}

	return NULL;
}

static void run_win_locked()
{
	HANDLE hEvt = OpenEventW(SYNCHRONIZE, FALSE, L"Global\\TermSrvReadyEvent");

	if (hEvt == NULL)
	{
		Server->Log("Error opening Global\\TermSrvReadyEvent", LL_ERROR);
		return;
	}

	DWORD rc = WaitForSingleObject(hEvt, INFINITE);

	if (rc != WAIT_OBJECT_0)
	{
		Server->Log("Waiting for Global\\TermSrvReadyEvent failed", LL_ERROR);
		return;
	}

	WNDCLASSEX wx = {};
	wx.cbSize = sizeof(WNDCLASSEX);
	wx.lpfnWndProc = win_locked_wndproc;
	wx.hInstance = GetModuleHandle(NULL); 
	wx.lpszClassName = L"trmwclass";
	if (RegisterClassEx(&wx) == 0)
	{
		Server->Log("Error registering class to receive trmsrv events", LL_ERROR);
		return;
	}
	HWND win = CreateWindowEx(0, wx.lpszClassName, L"trmwwin", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
	if(win==NULL)
	{
		Server->Log("Error creating window to receive trmsrv events", LL_ERROR);
		return;
	}

	if (!WTSRegisterSessionNotification(win, NOTIFY_FOR_ALL_SESSIONS))
	{
		Server->Log("Error registering window for session notificiations", LL_ERROR);
	}

	PWTS_SESSION_INFO sessionInfo;
	DWORD sessionCount;

	if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessionInfo, &sessionCount) != 0)
	{
		if (sessionCount != active_sessions)
		{
			active_sessions = sessionCount;
			last_locked_state = is_win_locked();
		}
		WTSFreeMemory(sessionInfo);
	}
}

static void init_win_locked()
{
	std::thread t(run_win_locked);
	t.detach();
}