#include <Windows.h>
#include "os.h"
#include <random>

size_t os_get_num_cpus()
{
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	return system_info.dwNumberOfProcessors;
}

wchar_t os_toupper(wchar_t ch)
{
	return towupper(ch);
}

LONG os_interlocked_increment(long volatile* val)
{
	return InterlockedIncrement(val);
}

long long os_interlocked_increment64(long long volatile* val)
{
	return InterlockedIncrement64(val);
}

LONG os_interlocked_decrement(long volatile* val)
{
	return InterlockedDecrement(val);
}

long os_interlocked_add(long volatile* val, long add)
{
	return InterlockedAdd(val, add);
}

void os_sleep(unsigned int sleepms)
{
	SleepEx(sleepms, TRUE);
}

long os_interlocked_compare_exchange(long volatile* dest, long change, long comp)
{
	return InterlockedCompareExchange(dest, change, comp);
}

bool os_wait_on_address(volatile void* address, void* compare_address, size_t address_size, unsigned int waitms)
{
	return WaitOnAddress(address, compare_address, address_size, waitms) ? true : false;
}

void os_wake_by_address_single(void* address)
{
	WakeByAddressSingle(address);
}

long long os_perf_counter(long long* p_freq)
{
	LARGE_INTEGER ret;
	QueryPerformanceCounter(&ret);

	if (p_freq != NULL)
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		*p_freq = freq.QuadPart;
	}

	return ret.QuadPart;
}

unsigned long os_rand_next(unsigned long curr)
{
	std::mt19937 mr(curr);
	return mr();
}
