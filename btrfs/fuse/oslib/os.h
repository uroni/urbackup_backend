#pragma once
#include <cstddef>

size_t os_get_num_cpus();

wchar_t os_toupper(wchar_t ch);

long os_interlocked_increment(long volatile* val);

long long os_interlocked_increment64(long long volatile* val);

long os_interlocked_decrement(long volatile* val);

long os_interlocked_add(long volatile* val, long add);

void os_sleep(unsigned int sleepms);

long os_interlocked_compare_exchange(long volatile* dest, long change, long comp);

bool os_wait_on_address(volatile void* address, void* compare_address, size_t address_size, unsigned int waitms);

void os_wake_by_address_single(void* address);

long long os_perf_counter(long long* freq);

unsigned long os_rand_next(unsigned long curr);