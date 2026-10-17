#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <atomic>

#include "logging.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace hce {

namespace detail {
inline void cpu_pause() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#else
    // fallback: nothing, or a compiler barrier
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}
} // namespace detail

/**
 @brief core mechanism for atomic synchronization.

 Implements atomic lock API without operating system blocking.
 Uses a short pause while spinning to reduce power and cache traffic.
 */
struct spinlock : public printable {
    spinlock() {
        HCE_MIN_CONSTRUCTOR();
        lock_.clear();
    }

    virtual ~spinlock() { HCE_MIN_DESTRUCTOR(); }

    static inline std::string info_name() { return "hce::spinlock"; }
    inline std::string name() const { return spinlock::info_name(); }

    inline void lock() {
        HCE_MIN_METHOD_ENTER("lock");

        // Fast path: try once
        if (!lock_.test_and_set(std::memory_order_acquire)) [[likely]] {
            return;
        }

        // Contended: spin with pause until it looks clear, then retry TAS
        while(true) {
            while(lock_.test(std::memory_order_relaxed)) {
                detail::cpu_pause();
            }

            if(!lock_.test_and_set(std::memory_order_acquire)) [[likely]] {
                return;
            }
        }
    }

    inline bool try_lock() {
        HCE_MIN_METHOD_ENTER("try_lock");
        return !lock_.test_and_set(std::memory_order_acquire);
    }

    inline void unlock() {
        HCE_MIN_METHOD_ENTER("unlock");
        lock_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag lock_;
};

/**
 @brief no-op lockable for use only when all access is already serialized
 on the same thread (e.g. coroutines scheduled on one scheduler thread).

 Not safe for concurrent access from multiple threads.
 */
struct lockfree : public printable {
    lockfree() { HCE_MIN_CONSTRUCTOR(); }
    ~lockfree() { HCE_MIN_DESTRUCTOR(); }

    static inline std::string info_name() { return "hce::lockfree"; }
    inline std::string name() const { return lockfree::info_name(); }

    inline void lock() { HCE_MIN_METHOD_ENTER("lock"); }

    inline bool try_lock() { 
        HCE_MIN_METHOD_ENTER("try_lock");
        return true; 
    }

    inline void unlock() { HCE_MIN_METHOD_ENTER("unlock"); }
};

}
