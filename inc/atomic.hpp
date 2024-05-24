//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_ATOMIC__
#define __HERMES_COROUTINE_ENGINE_ATOMIC__

#include <atomic>

namespace hce {

/**
@brief Core mechanism for atomic synchronization. 
*/
struct spinlock {
    spinlock() { lock_.clear(); }

    inline void lock() {
        while(lock_.test_and_set(std::memory_order_acquire)){ } 
    }

    inline bool try_lock() {
        return !(lock_.test_and_set(std::memory_order_acquire)); 
    }

    inline void unlock() {
        lock_.clear(std::memory_order_release); 
    }

private:
    std::atomic_flag lock_;
};

/// a lockless lockable implementation
struct lockfree {
    inline void lock() { }
    inline bool try_lock() { return true; }
    inline void unlock() { }
};

}

#endif
