#pragma once

typedef void* HANDLE;
typedef unsigned long DWORD;

HANDLE create_event(bool state);

bool event_wait(HANDLE hEvent, DWORD ms);

bool event_set(HANDLE hEvent);

bool event_reset(HANDLE hEvent);

void event_destroy(HANDLE hEvent);

bool event_pulse(HANDLE hEvent);