//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_MUTEX__
#define __HERMES_COROUTINE_ENGINE_MUTEX__

// c++
#include <mutex>

// local
#include "atomic.hpp"
#include "coroutine.hpp"

namespace hce {

/**
 @brief a united coroutine and non-coroutine mutex 
 */
struct mutex {
    struct already_unlocked_exception : public std::exception {
        virtual const char* what() const throw() {
            return "Cannot unlock an already unlocked hce::mutex";
        }
    };

    inline awaitable<void> lock() {
        return { awaitable<void>::make<lock_awt>(this) };
    }
    
    inline bool try_lock() {
        std::unique_lock<mce::spinlock> lk(lk_);
        if(acquired_) {
            return false;
        } else { 
            acquired_ = true; 
            return true;
        }
    }
    
    inline void unlock() {
        std::unique_lock<spinlock> lk(lk_);
        if(acquired_) {
            acquired_ = false;
            if(blocked_queue_.size()) {
                blocked_queue_.front()->resume();
                blocked_queue_.pop_front();
            }
        }
        else{ throw already_unlocked_exception(); }
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
 around awaitable blocking.
 ```
 */
struct unique_lock {
    unique_lock() = delete;
    unique_lock(const unique_lock& rhs) = delete;
    unique_lock(unique_lock&& rhs) = default;

    unique_lock& operator=(const unique_lock& rhs) = delete
    unique_lock& operator=(unique_lock&& rhs) = default;

    /// construct a unique_lock which assumes it has not acquired the mutex
    unique_lock(hce::mutex& mtx, std::defer_lock_t t) noexcept : 
        acquired_(false) 
    { }

    /// construct a unique_lock which assumes it has acquired the mutex
    unique_lock(hce::mutex& mtx, std::adopt_lock_t t) : acquired_(true) { }

    /// construct a unique_lock which has acquired the mutex
    static inline awaitable<unique_lock> make(hce::mutex& mtx) {
        return awaitable<unique_lock>::make<acquire>(mtx);
    }

    ~unique_lock() { if(acquired_) { unlock(); } }

    inline awaitable<void> lock() { 
        acquired_ = true;
        return mtx_->lock(); 
    }

    inline bool try_lock() { return mtx_->try_lock(); }

    inline void unlock() { mtx_->unlock(); }

    inline void swap(unique_lock& rhs) {
        std::swap(mtx_, rhs.mtx_);
        std::swap(acquired_, rhs.acquired_);
    }

    inline hce::mutex* release() {
        acquired_ = false;
        return mtx_;
    }

    inline hce::mutex* mutex() { return mtx_; }
    inline bool owns_lock() { return acquired_; }
    inline operator bool() { return owns_lock(); }

private:
    struct acquire : public hce::awaitable<unique_lock>::implementation {
        acquire(hce::mutex& mtx) : mtx_(&mtx) { }

    protected:
        inline std::unique_lock<hce::spinlock>& get_lock() { return lk_; }

        inline bool ready_impl() {
            schedule(acquire::co, mtx_, this);
            return false;
        }

        inline void resume_impl(void* m) { 
            res_lk_ = unique_lock(*mtx_, std::adopt_lock_t);
        }

        inline unique_lock result_impl() { return std::move(res_lk_); }

    private:
        static inline hce::coroutine co(hce::mutex* mtx, acquire* a) {
            co_await mtx->lock();
            a->resume(nullptr);
        }

        hce::spinlock slk_;
        std::unique_lock<spinlock> lk_(slk_);
        hce::mutex* mtx_;
        hce::unique_lock res_lk_;
    };

    hce::mutex* mtx_;
    bool acquired_;
};

}

#endif
