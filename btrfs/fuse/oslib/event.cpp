#include "event.h"
#include <Windows.h>

HANDLE create_event(bool state)
{
	return CreateEventEx(NULL, NULL, state ? CREATE_EVENT_INITIAL_SET : 0, EVENT_ALL_ACCESS);
}

bool event_wait(HANDLE hEvent, DWORD ms)
{
	DWORD rc = WaitForSingleObjectEx(hEvent, ms, TRUE);
	return rc == WAIT_OBJECT_0 || rc==WAIT_IO_COMPLETION;
}

bool event_set(HANDLE hEvent)
{
	return SetEvent(hEvent) == TRUE;
}

bool event_reset(HANDLE hEvent)
{
	return ResetEvent(hEvent) == TRUE;
}

void event_destroy(HANDLE hEvent)
{
	CloseHandle(hEvent);
}

bool event_pulse(HANDLE hEvent)
{
	return PulseEvent(hEvent) == TRUE;
}
