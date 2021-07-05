
#include <wctype.h>
#include "os.h"
#include <unistd.h>
#include <assert.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <random>

wchar_t os_toupper(wchar_t ch)
{
	return towupper(ch);
}

long os_interlocked_increment(long volatile* val)
{
	return __sync_add_and_fetch(val, 1);
}

long long os_interlocked_increment64(long long volatile* val)
{
    return __sync_add_and_fetch(val, 1);
}

long os_interlocked_decrement(long volatile* val)
{
    return __sync_sub_and_fetch(val, 1);
}

long os_interlocked_add(long volatile* val, long add)
{
    return __sync_add_and_fetch(val, add);
}

void os_sleep(unsigned int ms)
{
    if (ms > 1000)
	{
		sleep(ms / 1000);
		if (ms % 1000 != 0)
		{
			usleep((ms % 1000) * 1000);
		}
	}
	else
	{
		usleep(ms * 1000);
	}
}

long os_interlocked_compare_exchange(long volatile* dest, long change, long comp)
{
    return __sync_val_compare_and_swap(dest, comp, change);
}

#define INFINITE 0xFFFFFFFF

bool os_wait_on_address(volatile void* address, void* compare_address, size_t address_size, unsigned int waitms)
{
    assert(address_size==sizeof(unsigned int));
    struct timespec timeout;
    timeout.tv_sec = waitms/1000;
    waitms-=timeout.tv_sec*1000;
    timeout.tv_nsec = waitms*1000*1000;
    struct timespec* timeout_p = &timeout;
    if(waitms==INFINITE)
        timeout_p=nullptr;

    unsigned int* cmp_ptr = reinterpret_cast<unsigned int*>(compare_address);
    int rc = syscall(SYS_futex, address, FUTEX_WAIT_PRIVATE, *cmp_ptr, timeout_p);
    return rc == 0;
}

void os_wake_by_address_single(void* address)
{
    syscall(SYS_futex, address, FUTEX_WAKE_PRIVATE, 1);
}

long long os_perf_counter(long long* freq)
{
    if (freq != nullptr)
	{
		*freq=0;
	}
    return 0;
}

unsigned long os_rand_next(unsigned long curr)
{
    std::mt19937 mr(curr);
	return mr();
}