//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_ATOMIC
#define HERMES_COROUTINE_ENGINE_ATOMIC

#include <atomic>

#include "logging.hpp"

namespace hce {

/**
@brief core mechanism for atomic synchronization. 

Implements atomic lock API without operating system blocking.
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
        while(lock_.test_and_set(std::memory_order_acquire)){ } 
    }

    inline bool try_lock() {
        HCE_MIN_METHOD_ENTER("try_lock");
        return !(lock_.test_and_set(std::memory_order_acquire)); 
    }

    inline void unlock() {
        HCE_MIN_METHOD_ENTER("unlock");
        lock_.clear(std::memory_order_release); 
    }

private:
    std::atomic_flag lock_;
};

/**
 @brief lockless lockable implementation 

 For use when an object implement lock API is required but no atomic 
 locking is actually desired.
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

#endif
