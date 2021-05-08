#pragma once
#include <atomic>

template<typename T>
struct relaxed_atomic : std::atomic<T>
{
    relaxed_atomic()
        : std::atomic<T>() {}

	relaxed_atomic(const relaxed_atomic&) = delete;
	relaxed_atomic& operator=(const relaxed_atomic&) = delete;

    relaxed_atomic(const T val)
        : std::atomic<T>(val)
    {

    }

    T operator=(const T val) volatile noexcept {
        this->store(val, std::memory_order_relaxed);
        return val;
    }

    T operator=(const T val) noexcept {
        this->store(val, std::memory_order_relaxed);
        return val;
    }

    T operator++(int) noexcept {
        return this->fetch_add(1, std::memory_order_relaxed);
    }

    T operator++() noexcept {
        T tmp = this->fetch_add(1, std::memory_order_relaxed);
        ++tmp;
        return tmp;
    }

    T operator--(int) noexcept {
        return this->fetch_sub(1, std::memory_order_relaxed);
    }

    T operator--() noexcept {
        T tmp = this->fetch_sub(1, std::memory_order_relaxed);
        --tmp;
        return tmp;
    }

    T operator+=(const T val) noexcept {
        return this->fetch_add(val, std::memory_order_relaxed);
    }

    T operator+=(const T val) volatile noexcept {
        return this->fetch_add(val, std::memory_order_relaxed);
    }

    T operator-=(const T val) noexcept {
        return this->fetch_sub(val, std::memory_order_relaxed);
    }

    T operator-=(const T val) volatile noexcept {
        return this->fetch_sub(val, std::memory_order_relaxed);
    }

    operator T() const volatile noexcept {
        return this->load(std::memory_order_relaxed);
    }

    operator T() const noexcept {
        return this->load(std::memory_order_relaxed);
    }
};
