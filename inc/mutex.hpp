//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_MUTEX__
#define __HERMES_COROUTINE_ENGINE_MUTEX__

// c++
#include <mutex>

// local
#include "atomic.hpp"
#include "coroutine.hpp"
#include "block.hpp"

namespace hce {

/**
 @brief a united coroutine and non-coroutine mutex 
 */
struct mutex {
    struct already_unlocked_exception : public std::exception {
        already_unlocked_exception(mutex* m) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "cannot unlock already unlocked mutex[0x" 
                   << (void*)m
                   << "]";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { 
            LOG_F(ERROR, estr.c_str());
            return estr.c_str(); 
        }

    private:
        const std::string estr;
    };

    /// lock the mutex
    inline awaitable<void> lock() {
        return awaitable<void>(new lock_awt>(this));
    }
   
    /// return true if the mutex is successfully locked, else false
    inline bool try_lock() {
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
        std::unique_lock<spinlock> lk(lk_);
        if(acquired_) {
            acquired_ = false;
            if(blocked_queue_.size()) {
                blocked_queue_.front()->resume(nullptr);
                blocked_queue_.pop_front();
            }
        }
        else{ throw already_unlocked_exception(this); }
    }

private:
    struct lock_awt : protected awaitable<void>::implementation {
        lock_awt(hce::mutex* parent) : parent_(parent) { }

    protected:
        inline std::unique_lock<spinlock>& get_lock() { return lk_; }
        inline bool ready_impl() { return parent_->lock_(this); }
        inline void resume_impl(void* m) { }
    
        inline base_coroutine::destination acquire_destination() {
            return scheduler::reschedule{ scheduler::local() };
        }

        hce::mutex* parent_;
        std::unique_lock<spinlock> lk_(parent_->slk_);
    };

    inline bool lock_(lock_awt* lw) {
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
    std::deque<lock_awt*> blocked_queue_;
    friend struct lock_awt;
};

/**
 @brief a united coroutine and non-coroutine unique_lock 

 hc::unique_lock is more limited than std::unique_lock due to requirements 
 around awaitable blocking. As such, default `std::unique_lock<Lock>()` 
 construction (which attempts to acquire its argument Lock) must be emulated by 
 calling `hce::unique_lock<Lock>::make()`.

 This object is most efficient when templated with `hce::mutex` as the Lock type.
 */
template <typename Lock>
struct unique_lock {
    unique_lock() = delete;
    unique_lock(const unique_lock<Lock>& rhs) = delete;
    unique_lock(unique_lock<Lock>&& rhs) = default;

    unique_lock<Lock>& operator=(const unique_lock<Lock>& rhs) = delete
    unique_lock<Lock>& operator=(unique_lock<Lock>&& rhs) = default;

    /// construct a unique_lock which assumes it has not acquired the mutex
    unique_lock(Lock& mtx, std::defer_lock_t t) noexcept : 
        acquired_(false) 
    { }

    /// construct a unique_lock which assumes it has acquired the mutex
    unique_lock(Lock& mtx, std::adopt_lock_t t) : acquired_(true) { }

    /// construct a unique_lock which has acquired the mutex
    static inline awaitable<unique_lock<Lock>> make(Lock& mtx) {
        return awaitable<unique_lock<Lock>>(new acquire(mtx));
    }

    ~unique_lock() { if(acquired_) { unlock(); } }

    /// return an awaitable to lock the Lock
    inline awaitable<void> lock() { 
        acquired_ = true;
        return lock_op_(mtx_); 
    }

    inline bool try_lock() { return mtx_->try_lock(); }

    inline void unlock() { mtx_->unlock(); }

    inline void swap(unique_lock<Lock>& rhs) {
        std::swap(mtx_, rhs.mtx_);
        std::swap(acquired_, rhs.acquired_);
    }

    inline Lock* release() {
        acquired_ = false;
        return mtx_;
    }

    inline Lock* mutex() { return mtx_; }
    inline bool owns_lock() { return acquired_; }
    inline operator bool() { return owns_lock(); }

private:
    struct acquire : public hce::awaitable<unique_lock<Lock>>::implementation {
        acquire(Lock& mtx) : mtx_(&mtx) { }

    protected:
        inline std::unique_lock<hce::spinlock>& get_lock() { return lk_; }

        inline bool ready_impl() {
            schedule(acquire::op(lock_op_, mtx_, this));
            return false;
        }

        inline void resume_impl(void* m) { 
            res_lk_ = unique_lock<Lock>(*mtx_, std::adopt_lock_t);
        }

        inline unique_lock result_impl() { return std::move(res_lk_); }

    private:
        static inline hce::coroutine<void> op(
                hce::awaitable<void> (*lock_op)(Lock*), 
                Lock* lk, 
                acquire* a) {
            co_await lock_op(lk);
            a->resume(nullptr);
        }

        hce::spinlock slk_;
        std::unique_lock<spinlock> lk_(slk_);
        Lock* mtx_;
        hce::unique_lock<Lock> res_lk_;
    };

    /// safely lock any lockable type
    template <typename Lock2>
    static inline awaitable<void> lock_op_impl(void* lk) {
        return blocking::call([&]{ ((Lock2*)lk)->lock(); });
    }

    /// more efficiently lock hce::mutex
    static inline awaitable<void> lock_op_impl<hce::mutex>(void* mtx) {
        return ((hce::mutex*)mtx)->lock();
    }

    Lock* mtx_;
    bool acquired_;
    awaitable<void> (*lock_op_)(Lock*) = lock_op_impl<Lock>;
};

}

#endif
