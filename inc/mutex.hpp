//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_MUTEX
#define HERMES_COROUTINE_ENGINE_MUTEX

// c++
#include <mutex>
#include <deque>

// local
#include "logging.hpp"
#include "atomic.hpp"
#include "alloc.hpp"
#include "list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

/**
 @brief a mutex capable of synchronizing any combination of coroutines and non-coroutines 

 This object can be used normally by non-coroutines, replacing `std::mutex`.

 Calling `co_await` in a coroutine on `hce::mutex::lock()` is efficient and safe.

 It may be required to use `hce::block()` when used with 
 `std::condition_variable_any` (or `std::unique_lock`) is required. 

 It should be noted that other high level mechanisms (`hce::join()`, 
 `hce::channel<T>`, etc.) may be more useful (and more efficient) than 
 implementing custom mechanisms with `hce::mutex`. `hce::mutex` 
 is most useful when integrating this library into existing user code, requiring 
 less redesign.
 */
struct mutex : public printable {
    /// thrown when unlock() is called on an unlocked mutex
    struct already_unlocked_exception : public std::exception {
        already_unlocked_exception(mutex* m);
        const char* what() const noexcept;
    private:
        const std::string estr;
    };

    /// optionally configure the internal blocked queue pool_allocator 
    mutex(hce::pool_allocator<hce::awaitable::interface*> alloc = 
            hce::pool_allocator<hce::awaitable::interface*>());

    mutex(const mutex&) = delete;
    mutex(mutex&&) = delete;
    virtual ~mutex();
    static std::string info_name();
    std::string name() const;
    
    mutex& operator=(const mutex&) = delete;
    mutex& operator=(mutex&&) = delete;

    /// awaitably lock the mutex 
    awt<void> lock();
   
    /// return true if the mutex is successfully locked, else false
    bool try_lock();
   
    /// unlock the mutex
    void unlock();

private:
    struct acquire : 
        public hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<void>::interface>>
    {
        acquire(hce::mutex* parent);

        // returns true if acquired, else we need to suspend
        bool on_ready();

        // only returns when acquired
        void on_notify(void* m);

    private:
        hce::mutex* parent_;
        bool ready_;
    };

    bool lock_or_enqueue_blocked_(acquire* lw);

    hce::spinlock slk_;
    bool acquired_;
    hce::list<hce::awaitable::interface*,
              hce::pool_allocator<hce::awaitable::interface*>> blocked_queue_;
    friend struct acquire;
};

}

#endif
