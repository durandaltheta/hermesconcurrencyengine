//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_MUTEX__
#define __HERMES_COROUTINE_ENGINE_MUTEX__

// c++
#include <mutex>

// local
#include "utility.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "block.hpp"

namespace hce {

/**
 @brief a mutex capable of synchronizing any combination of coroutines and non-coroutines
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

    ~mutex(){ HCE_MIN_DESTRUCTOR(); }
    
    mutex& operator=(const mutex&) = delete;
    mutex& operator=(mutex&&) = delete;

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "mutex"; }

    /// lock the mutex, returning true if the lock operation succeeded
    inline awt<void> lock() {
        HCE_MIN_METHOD_ENTER("lock");
        return awt<void>::make(new lock_awt>(this));
    }
   
    /// return true if the mutex is successfully locked, else false
    inline bool try_lock() {
        HCE_MIN_METHOD_ENTER("try_lock");

        std::unique_lock<mce::spinlock> lk(lk_);
        if(acquired_) {
            return false;
        } else { 
            acquired_ = true; 
            return true;
        }
    }
   
    /// unlock the mutex
    inline void unlock() {
        HCE_MIN_METHOD_ENTER("unlock");

        std::unique_lock<spinlock> lk(lk_);
        if(acquired_) {
            acquired_ = false;
            if(blocked_queue_.size()) {
                blocked_queue_.front()->resume((void*)1);
                blocked_queue_.pop_front();
            }
        }
        else{ throw already_unlocked_exception(this); }
    }

private:
    struct acquire : 
        public hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::awt<void>::interface,
                hce::spinlock>>
    {
        acquire(hce::mutex* parent) : 
            hce::awaitable::lockable<
                hce::awt<void>::interface,
                hce::spinlock>(parent->slk_,false),
            parent_(parent) 
        { }

        inline bool on_ready() { return parent_->lock_(this); }
        inline void on_resume(void* m) { }

        hce::mutex* parent_;
    };

    inline bool lock_(acquire* lw) {
        if(acquired_) {
            blocked_queue_.push_back(lw);
            return false;
        } else { 
            acquired_ = true; 
            return true;
        }
    }

    hce::spinlock slk_;
    bool acquired_;
    std::deque<acquire*> blocked_queue_;
    friend struct acquire;
};

namespace detail {
namespace unique_lock {

/// safely lock any lockable type
template <typename Lock>
static inline hce::awt<void> lock_impl(void* lk) {
    return blocking::call([=]() mutable { ((Lock*)lk)->lock(); }
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
        HCE_MIN_CONSTRUCTOR(,rhs);
        swap(rhs);
    }

    unique_lock<Lock>& operator=(const unique_lock<Lock>& rhs) = delete;

    unique_lock<Lock>& operator=(unique_lock<Lock>&& rhs) {
        HCE_MIN_METHOD_ENTER(rhs);
        swap(rhs);
        return *this;
    }

    /// construct a unique_lock which assumes it has not acquired the mutex
    unique_lock(Lock& mtx, std::defer_lock_t t) noexcept : 
        acquired_(false) 
    { 
        HCE_MIN_CONSTRUCTOR();
    }

    /// construct a unique_lock which assumes it has acquired the mutex
    unique_lock(Lock& mtx, std::adopt_lock_t t) : acquired_(true) { 
        HCE_MIN_CONSTRUCTOR();
    }

    /**
     @brief construct a unique_lock which has acquired the mutex

     This static operation replaces constructor `unique_lock(Lock&)`
     */
    static inline awt<hce::unique_lock<Lock>> make(Lock& mtx) {
        typedef hce::unique_lock<Lock> T;
        auto co = acquire::op(&mtx);
        return join(std::move(co)); 
    }

    ~unique_lock() { 
        HCE_MIN_DESTRUCTOR();
        if(acquired_) { unlock(); } 
    }
    
    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "unique_lock"; }

    /// return our stringified mutex address
    inline std::string content() const { 
        return std::string("mutex@") + 
               std::to_string(mtx_) +
               ", acquired:" +
               std::to_string(acquired_);
    }

    /// return an awaitable to lock the Lock
    inline awt<void> lock() { 
        HCE_MIN_METHOD_ENTER("lock");
        acquired_ = true;
        return detail::mutex::lock_impl<Lock>(mtx_); 
    }

    inline bool try_lock() { 
        HCE_MIN_METHOD_ENTER("try_lock");
        return mtx_->try_lock(); 
    }

    inline void unlock() { 
        HCE_MIN_METHOD_ENTER("unlock");
        mtx_->unlock(); 
    }

    inline void swap(unique_lock<Lock>& rhs) {
        std::swap(mtx_, rhs.mtx_);
        std::swap(acquired_, rhs.acquired_);
    }

    inline Lock* release() {
        HCE_MIN_METHOD_ENTER("release");
        auto mtx = mtx_;
        mtx_ = nullptr;
        acquired_ = false;
        return mtx;
    }

    inline Lock* mutex() { 
        HCE_MIN_METHOD_ENTER("mutex");
        return mtx_; 
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
        static inline hce::co<hce::unique_lock<Lock>> op(Lock* lk, acquire* a) {
            // block until locked
            co_await detail::mutex::lock_impl<Lock>(lk);
            a->resume(nullptr);
        }
    };

    Lock* mtx_;
    bool acquired_;
};

}

#endif
