//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_CONDITION_VARIABLE
#define HERMES_COROUTINE_ENGINE_CONDITION_VARIABLE

// c++
#include <mutex>
#include <deque>
#include <chrono>
#include <optional>

// local
#include "utility.hpp"
#include "logging.hpp"
#include "atomic.hpp"
#include "chrono.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "mutex.hpp"

namespace hce {

/**
 @brief a united coroutine and non-coroutine condition_variable accepting any templated Lock 

 It should be noted that `hce::join()`/`hce::scope()` and `hce::channel<T>` may 
 be more generally useful than direct usage of conditions (and potentially 
 more efficient). Condition variables are most useful when integrating this 
 library into existing user code.
 */
struct condition_variable_any : public printable {
    condition_variable_any() : blocked_queue_(new std::deque<resumable*>) {
        HCE_MIN_CONSTRUCTOR();
    }

    condition_variable_any(const condition_variable_any&) = delete;
    condition_variable_any(condition_variable_any&&) = delete;

    virtual ~condition_variable_any() {
        HCE_MIN_DESTRUCTOR();
    }

    condition_variable_any& operator=(const condition_variable_any&) = delete;
    condition_variable_any& operator=(condition_variable_any&&) = delete;

    static inline std::string info_name() { 
        return type::templatize<T>("hce::condition_variable_any"); 
    }

    inline std::string name() const { 
        return condition_variable_any::info_name(); 
    }

    template <typename Lock>
    hce::awt<void> wait(hce::unique_lock<Lock>& lk) {
        // a simple awaitable to block this coroutine
        struct ai : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<void>::interface,
                    std::unique_lock<hce::spinlock>>>
        {
            // inherit the std::unique_lock from the coroutine
            ai(spinlock& lk) : 
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<void>::interface,
                        hce::spinlock>>(
                            lk,
                            hce::awaitable::await::policy::defer,
                            hce::awaitable::await::policy::lock),
                ready_(false)
            { }

            static inline hce::co<void> op(
                    hce::condition_variable& cv,
                    hce::unique_lock<Lock>& user_lk) {
                auto& lk = cv.lk_;
                auto a = new ai(lk);

                // acquire the condition_variable lock and interleave locks
                lk.lock();
                user_lk.unlock();

                resumable r{ (void*)a, [a]{ a->resume(nullptr); }};
                cv.blocked_queue_.push_back(&r);

                lk.unlock();

                // block until resume() is called by notify operation
                co_await hce::awt<void>::make(a);

                // at this point lk will be unlocked again

                // reacquire the user lock
                co_await user_lk.lock();
            }

            inline bool on_ready() { return ready_; }
            inline void on_resume(void* m) { ready_ = true; }

        private:
            bool ready_;
        };

        return hce::join(ai::op(*this, lk));
    }

    template <typename Lock, class Pred>
    inline hce::awt<void> wait(hce::unique_lock<Lock>& lk, Pred p) {
        struct awt {
            static inline hce::co<void> op(
                    hce::condition_variable& cv,
                    hce::unique_lock<Lock>& lk,
                    const std::chrono::steady_clock::duration& d,
                    Pred p) {
                while(!p()) { co_await cv->wait(lk, dur); }
            }
        };

        return hce::join(awt::op(*this, lk, dur, std::move(p)));
    }

    template <typename Lock>
    inline hce::awt<std::cv_status> wait_for(
            hce::unique_lock<Lock>& lk, 
            const std::chrono::steady_clock::duration d) {
        return wait_until(lk, timer::now() + d);
    }
    
    template <typename Lock, class Pred>
    inline hce::awt<bool> wait_for(
            hce::unique_lock<Lock>& lk, 
            const std::chrono::steady_clock::duration d,
            Pred p) {
        struct awt {
            static inline hce::co<bool> op(
                    hce::condition_variable& cv,
                    hce::unique_lock<Lock>& lk,
                    const std::chrono::steady_clock::duration& d,
                    Pred p) {
                bool res = false;

                while(!(res = p())) {
                    std::optional<std::cv_status> sts = 
                        co_await cv.wait_for(lk, dur);

                    if(sts) {
                        if(*sts == std::cv_status::timeout) { break; }
                    } else { break; }
                }

                co_return res;
            }
        };

        return hce::join(awt::op(*this, lk, dur, std::move(p)));
    }

    template <typename Lock>
    inline hce::awt<std::cv_status> wait_until(
        hce::unique_lock<Lock>& lk, 
        const std::chrono::steady_clock::time_point tp) {

        struct ai {
            static inline hce::co<std::cv_status> op(
                    hce::condition_variable* cv,
                    hce::unique_lock<Lock>& user_lk,
                    std::chrono::steady_clock::time_point tp) {
                // acquire some scheduler
                auto& sch = scheduler::get();

                // timer id
                hce::id id;

                // start the timer 
                auto awt = sch.start(id, tp);

                // key is our id's unique memory
                void* key = id.get();

                // Create the object that condition_variable notify operations 
                // will call to attempt to cancel the timer.
                //
                // This stays in memory on the coroutine stack frame until 
                // the unblocked by the timer calling `resume()`.
                resumable r{
                    key,
                    [&]{ sch.cancel(id); }
                };

                // interleave locks
                std::unique_lock<hce::spinlock> lk(cv->lk_);
                user_lk.unlock();

                cv->blocked_queue_.push_back(&r);

                // release the condition lock while we co_await the timer
                lk.unlock();

                bool timed_out = co_await std::move(awt);
                bool found = false;

                // re-acquire the condition lock
                lk.lock();

                auto it = cv->blocked_queue_.begin();
                auto end = cv->blocked_queue_.end();

                // remove the canceller if it exists
                while(it!=end) {
                    if(it->key == key) {
                        cv->blocked_queue_.erase(it);
                        found = true;
                        break;
                    } else { ++it; }
                }
               
                // reverse interleave locks
                co_await user_lk.lock();
                lk.unlock();

                if(found && timed_out) {
                    // resume the awaitable and inform of timeout
                    co_return std::cv_status::timeout;
                } else {
                    // resume the awaitable and inform of cancellation or
                    // notification
                    co_return std::cv_status::no_timeout;
                }
            }
        };

        // await a coroutine which executes the operation logic
        return hce::join(ai::op(this, user_lk, timer::now() + dur));
    }
    
    inline void notify_one() {
        std::lock_guard<spinlock> lk(lk_);

        if(blocked_queue_.size()) {
            blocked_queue_->front()->callback();
            blocked_queue_->pop_front();
        }
    }
    
    inline void notify_all() {
        std::lock_guard<spinlock> lk(lk_);

        for(auto r : blocked_queue_) {
            r.callback();
        }
    }

private:
    struct resumable {
        void* key;
        std::function<void()> callback;
    };

    spinlock slk_;
    std::deque<resumable*> blocked_queue_;
};

/**
 @brief condition_variable variant which only accepts `hce::unique_lock<hce::mutex>`

 Unlike `std::` implementations, this is not necessarily superior to 
 `hce::condition_variable_any` (when used with `hce::mutex`), but is provided 
 for consistency with `std::`. However, `hce::mutex` will almost certainly be 
 faster than other templated `Lock` types.
 */
struct condition_variable : public printable {
    condition_variable() = default;
    condition_variable(const condition_variable&) = delete;
    condition_variable(condition_variable&&) = delete; 

    virtual ~condition_variable(){ }

    static inline std::string info_name() { 
        return type::templatize<T>("hce::condition_variable"); 
    }

    inline std::string name() const { return condition_variable::info_name(); }

    condition_variable& operator=(const condition_variable&) = delete;
    condition_variable& operator=(condition_variable&&) = delete;
    
    inline hce::awt<void> wait(hce::unique_lock<hce::mutex>& lk) {
        return cv_.wait(lk);
    }

    template <class Pred>
    inline hce::awt<void> wait(hce::unique_lock<hce::mutex>& lk, Pred&& p) {
        return cv_.wait(lk, p);
    }

    inline hce::awt<std::cv_status> wait_for(
            hce::unique_lock<hce::mutex>& lk, 
            const std::chrono::steady_clock::duration& d) {
        return cv_.wait_for(lk, d);
    }
    
    template <class Pred>
    inline hce::awt<bool> wait_for(
            hce::unique_lock<hce::mutex>& lk, 
            const std::chrono::steady_clock::duration& d,
            Pred p) {
        return cv_.wait_for(lk, d, p);
    }

    inline hce::awt<std::cv_status> wait_until(
            hce::unique_lock<hce::mutex>& lk, 
            const std::chrono::steady_clock::time_point& tp) {
        return cv_.wait_until(lk, tp);
    }
    
    inline void notify_one() { cv_.notify_one(); }
    inline void notify_all() { cv_.notify_all(); }

private:
    hce::condition_variable_any cv_;
};

}

#endif
