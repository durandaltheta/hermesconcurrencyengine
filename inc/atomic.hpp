//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_ATOMIC__
#define __HERMES_COROUTINE_ENGINE_ATOMIC__

#include <atomic>

#include "utility.hpp"

namespace hce {

/**
@brief core mechanism for atomic synchronization. 
*/
struct spinlock : public printable {
    spinlock() { 
        HCE_MIN_CONSTRUCTOR();
        lock_.clear(); 
    }

    virtual ~spinlock() { HCE_MIN_DESTRUCTOR(); }

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

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "spinlock"; }

private:
    std::atomic_flag lock_;
};

/**
 @brief lockless lockable implementation
 */
struct lockfree : public printable {
    lockfree() { HCE_MIN_CONSTRUCTOR(); }
    virtual ~lockfree() { HCE_MIN_DESTRUCTOR(); }

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "lockfree"; }

    inline void lock() { HCE_MIN_METHOD_ENTER("lock"); }

    inline bool try_lock() { 
        HCE_MIN_METHOD_ENTER("try_lock");
        return true; 
    }

    inline void unlock() { HCE_MIN_METHOD_ENTER("unlock"); }
};

}

#endif
