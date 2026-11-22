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
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

/**
 @brief a mutex capable of synchronizing any combination of coroutines and non-coroutines 

 It should be noted that other high level mechanisms (`hce::join()`, 
 `hce::scope()`, `hce::channel<T>`, etc.) may be more useful (and more 
 efficient) than implementing custom mechanisms with `hce::mutex`. `hce::mutex` 
 is most useful when integrating this library into existing user code.
 */
struct mutex : public printable {
    struct already_unlocked_exception : public std::exception {
        already_unlocked_exception(mutex* m) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "cannot unlock already unlocked"
                   << m->to_string()
                   << "]";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { 
            HCE_ERROR_LOG(estr.c_str());
            return estr.c_str(); 
        }

    private:
        const std::string estr;
    };

    mutex(){ HCE_MIN_CONSTRUCTOR(); }
    mutex(const mutex&) = delete;
    mutex(mutex&&) = delete;

    virtual ~mutex(){ HCE_MIN_DESTRUCTOR(); }

    static inline std::string info_name() { return "hce::mutex"; }
    inline std::string name() const { return mutex::info_name(); }
    
    mutex& operator=(const mutex&) = delete;
    mutex& operator=(mutex&&) = delete;

    /// awaitably lock the mutex 
    inline awt<void> lock() {
        HCE_MIN_METHOD_ENTER("lock");
        return awt<void>::make(new hce::mutex::acquire(this));
    }
   
    /// return true if the mutex is successfully locked, else false
    inline bool try_lock() {
        HCE_MIN_METHOD_ENTER("try_lock");

        std::unique_lock<mce::spinlock> lk(lk_);

        if(acquired_) [[unlikely]] {
            return false;
        } else [[likely]] { 
            acquired_ = true; 
            return true;
        }
    }
   
    /// unlock the mutex
    inline void unlock() {
        HCE_MIN_METHOD_ENTER("unlock");

        std::unique_lock<spinlock> lk(lk_);

        if(acquired_) [[likely]] {
            acquired_ = false;

            if(blocked_queue_.size()) {
                blocked_queue_.front()->resume(nullptr);
                blocked_queue_.pop_front();
            }
        } else [[unlikely]] { throw already_unlocked_exception(this); }
    }

private:
    struct acquire : 
        public hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<void>::interface>>
    {
        acquire(hce::mutex* parent) : 
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<void>::interface>(
                    parent->slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::no_lock),
            parent_(parent) 
        { }

        // returns true if acquired, else we need to suspend
        inline bool on_ready() { return parent_->lock_(this); }

        // only returns when acquired
        inline void on_resume(void* m) { }

    private:
        hce::mutex* parent_;
    };

    inline bool lock_(acquire* lw) {
        if(acquired_) [[unlikely]] {
            blocked_queue_.push_back(lw);
            return false;
        } else [[likely]] { 
            acquired_ = true; 
            return true;
        }
    }

    hce::spinlock slk_;
    bool acquired_;
    std:deque<acquire*> blocked_queue_;
    friend struct acquire;
};

namespace detail {
namespace unique_lock {

/// innefficiently, but safely, lock any lockable type
template <typename Lock>
static inline hce::awt<void> lock_impl(void* lk) {
    return hce::block([=]() mutable { ((Lock*)lk)->lock(); }
}

/// more efficiently lock hce::mutex
template <>
static inline hce::awt<void> lock_impl<hce::mutex>(void* mtx) {
    return ((hce::mutex*)mtx)->lock();
}

}
}

/**
 @brief a united coroutine and non-coroutine unique_lock 

 hc::unique_lock is more limited than std::unique_lock due to requirements 
 around awaitable blocking. As such, default `std::unique_lock<Lock>(Lock&)` 
 construction (which attempts to acquire its argument Lock) must be emulated by 
 calling (and, if in a coroutine, `co_await`ing) 
 `hce::unique_lock<Lock>::make(Lock&)`.

 This object is most efficient when templated with `hce::mutex` as the Lock type.
 */
template <typename Lock>
struct unique_lock : public printable {
    unique_lock() = delete;
    unique_lock(const unique_lock<Lock>& rhs) = delete;

    unique_lock(unique_lock<Lock>&& rhs) {
        HCE_MIN_CONSTRUCTOR(rhs);
        swap(rhs);
    }

    virtual ~unique_lock() {
        HCE_MIN_DESTRUCTOR();
        if(acquired_) { unlock(); } 
    }

    static inline std::string info_name() { 
        return type::templatize<Lock>("hce::mutex"); 
    }

    inline std::string name() const { return unique_lock<Lock>::info_name(); }

    unique_lock<Lock>& operator=(const unique_lock<Lock>& rhs) = delete;

    unique_lock<Lock>& operator=(unique_lock<Lock>&& rhs) {
        HCE_MIN_METHOD_ENTER(rhs);
        swap(rhs);
        return *this;
    }

    /// construct a unique_lock which assumes it has not acquired the mutex
    unique_lock(Lock& mtx, std::defer_lock_t t) noexcept : 
        lk_(&mtx),
        acquired_(false)
    { 
        HCE_MIN_CONSTRUCTOR();
    }

    /// construct a unique_lock which assumes it has acquired the mutex
    unique_lock(Lock& mtx, std::adopt_lock_t t) : 
        lk_(&mtx),
        acquired_(true) 
    {
        HCE_MIN_CONSTRUCTOR();
    }

    /**
     @brief construct a unique_lock which has acquired the mutex

     This static operation replaces constructor `unique_lock(Lock&)`
     */
    static inline awt<hce::unique_lock<Lock>> make(Lock& mtx) {
        return hce::join(acquire::op(&mtx)); 
    }

    /// return our stringified mutex address
    inline std::string content() const { 
        return std::string("lock@") + 
               std::to_string((void*)lk_) +
               ", acquired:" +
               std::to_string(acquired_);
    }

    inline awt<void> lock() { 
        HCE_MIN_METHOD_ENTER("lock");
        acquired_ = true;
        return detail::mutex::lock_impl<Lock>(lk_); 
    }

    inline bool try_lock() { 
        HCE_MIN_METHOD_ENTER("try_lock");
        return lk_->try_lock(); 
    }

    inline void unlock() { 
        HCE_MIN_METHOD_ENTER("unlock");
        lk_->unlock(); 
    }

    inline void swap(unique_lock<Lock>& rhs) {
        HCE_TRACE_METHOD_ENTER("swap",rhs);
        std::swap(lk_, rhs.lk_);
        std::swap(acquired_, rhs.acquired_);
    }

    inline Lock* release() {
        HCE_MIN_METHOD_ENTER("release");
        auto mtx = lk_;
        lk_ = nullptr;
        acquired_ = false;
        return mtx;
    }

    inline Lock* mutex() { 
        HCE_MIN_METHOD_ENTER("mutex");
        return lk_; 
    }

    inline bool owns_lock() { 
        HCE_MIN_METHOD_ENTER("owns_lock");
        return acquired_; 
    }

    inline operator bool() { 
        HCE_MIN_METHOD_ENTER("operator bool");
        return owns_lock(); 
    }

private:
    struct acquire {
        static inline hce::co<hce::unique_lock<Lock>> op(Lock* lk) {
            // block until locked
            co_await detail::mutex::lock_impl<Lock>(lk);
            co_return hce::unique_lock<Lock>(*lk, std::adopt_lock_t());
        }
    };

    Lock* lk_;
    bool acquired_;
};

}

#endif
