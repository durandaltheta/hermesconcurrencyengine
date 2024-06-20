//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_CONDITION_VARIABLE__
#define __HERMES_COROUTINE_ENGINE_CONDITION_VARIABLE__ 

// c++
#include <mutex>
#include <deque>
#include <chrono>
#include <optional>

// local
#include "utility.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "mutex.hpp"

namespace hce {

/**
 @brief a united coroutine and non-coroutine condition_variable
 */
struct condition_variable_any : public printable {
    condition_variable_any() = default;
    condition_variable_any(const condition_variable_any&) = delete;
    condition_variable_any(condition_variable_any&&) = delete;
    condition_variable_any& operator=(const condition_variable_any&) = delete;
    condition_variable_any& operator=(condition_variable_any&&) = delete;

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "condition_variable_any"; }

    template <typename Lock>
    hce::awt<bool> wait(hce::unique_lock<Lock>& lk) {
        // a simple awaitable to block this coroutine
        struct ai : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    std::unique_lock<hce::spinlock>>>
        {

            // inherit the std::unique_lock from the coroutine
            ai(std::unique_lock<spinlock>&& lk) : 
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<bool>::interface,
                        std::unique_lock<hce::spinlock>>>(std::move(lk), true)
            { }

            static inline hce::co<void> op(
                    hce::condition_variable& cv,
                    hce::unique_lock<Lock>& user_lk) {
                // acquire the condition_variable lock and interleave locks
                std::unique_lock<hce::spinlock> lk(cv.lk_);
                user_lk.unlock();

                auto a = new ai(std::move(lk));
                cv.blocked_queue_.push_back({ a, [a]{ a->resume(nullptr); }});

                // block until resume() is called by notify operation
                co_await hce::awt<void>::make(a);

                // reacquire the user lock
                co_await user_lk.lock();
            }

            inline bool on_ready() { return false; }
            inline void on_resume(void* m) { }
        };

        return hce::join(awt::op(this, &user_lk));
    }

    template <typename Lock, class Pred>
    hce::awt<bool> wait(hce::unique_lock<Lock>& lk, Pred p) {
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
    hce::awt<std::optional<std::cv_status>> wait_for(
            hce::unique_lock<Lock>& lk, 
            const std::chrono::steady_clock::duration d) {
        return wait_until(lk, timer::now() + d);
    }
    
    template <typename Lock, class Pred>
    hce::awt<bool> wait_for(
            hce::unique_lock<Lock>& lk, 
            const std::chrono::steady_clock::duration d,
            Pred p) {
        struct ai : 
            public hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    hce::spinlock>>
        {
            ai(hce::condition_variable& cv,
                hce::unique_lock<Lock>& user_lk,
                const std::chrono::steady_clock::duration d,
                Pred&& p) :
                hce::scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::awt<bool>::interface,
                        hce::spinlock>>(lk_,false),
                co_(awt::op(cv, user_lk, d, std::move(p), cv, this))
            { }

            static inline co<void> op(
                    hce::condition_variable& cv,
                    hce::unique_lock<Lock>& lk,
                    const std::chrono::steady_clock::duration& d,
                    Pred p,
                    awt* a) {
                bool res = false;

                while(!(res = p())) {
                    std::optional<std::cv_status> sts = co_await cv.wait_for(lk, dur);
                    if(sts) {
                        if(*sts == std::cv_status::timeout) { break; }
                    } else { break; }
                }

                a->resume((void*)res);
            }

            inline bool on_ready() { 
                schedule(std::move(co_));
                return false; 
            }

            inline void on_resume(void* m) { res_ = (bool)m; }
            inline bool get_result() { return res_; }

        private:
            hce::spinlock lk_;
            hce::co<void> co_;
            bool res_ = false;
        };

        return awt<bool>::make(new ai(*this, lk, dur, std::move(p)));
    }

    template <typename Lock>
    hce::awt<std::optional<std::cv_status>> wait_until(
        hce::unique_lock<Lock>& lk, 
        const std::chrono::steady_clock::time_point tp) {

        struct awt : protected hce::awt<std::cv_status>::impl {
            awt(std::unique_lock<hce::spinlock>&& lk) : lk_(std::move(lk)) { }

            static inline hce::co<std::cv_status> op(
                    hce::condition_variable* cv,
                    hce::unique_lock<Lock>& user_lk,
                    std::chrono::steady_clock::time_point tp) {
                // object called by notify() methods
                struct canceller : public resumable {
                    inline void resume(void* m) { 
                        if(!m) {
                            // attempt to cancel the timer as we were called by 
                            // a notify. This call may fail.
                            auto s = sch_.lock();
                            if(s) { s->cancel(std::move(id)); }
                        }
                    }

                    std::weak_ptr<scheduler> sch;
                    timer::id id;
                };

                // interleave locks
                std::unique_lock<hce::spinlock> lk(cv->lk_);
                user_lk.unlock();

                // acquire some scheduler
                auto& sch = scheduler::get();

                // Create the object that condition_variable notify operations 
                // will call to attempt to cancel the timer.
                //
                // This stays in memory on the coroutine stack frame until 
                // the unblocked by the timer calling `resume()`.
                canceller c((std::weak_ptr<hce::scheduler>)sch, id);

                // start the timer which will ultimately resume the call to 
                // wait_until()
                auto id = sch.start(hce::timer::make(
                        tp,
                        [&]{
                            {
                                std::unique_lock<hce::spinlock> lk(cv->lk_);
                                auto it = cv->blocked_queue_.begin();
                                auto end = cv->blocked_queue_.end();

                                // remove the canceller if it exists
                                while(it!=end) {
                                    if(*it == c) {
                                        cv->blocked_queue_.erase(it);
                                        break;
                                    } else { ++it; }
                                }
                            }

                            // resume the awaitable and inform of timeout
                            a->resume((void*)1); 
                        },
                        [&]{ 
                            // resume the awaitable and inform of cancellation
                            a->resume((void*)0); 
                        });

                cv->blocked_queue_.push_back({ &c, [cp=&c]{ cp->resume(nullptr); }});

                // move the lock to the awaitable
                auto a = new awt(std::move(lk));

                // block until resume() is called by a timer operation
                auto status = co_await hce::awt<std::cv_status>::make(a);

                // reacquire user lock after we are resumed
                co_await user_lk.lock();

                // return the final result to the coroutine promise which will 
                // be forwarded by the coroutine's cleanup handler to the 
                // awaitable returned by join()
                co_return status;
            }

            inline bool on_ready() { return false; }
            inline void on_resume(void* m) { res_ = *((bool*)m); }

            inline std::cv_status get_result() { 
                return res_ 
                    ? std::cv_status::timeout 
                    : std::cv_status::no_timeout;
            }

            bool res_ = false;
            std::unique_lock<hce::spinlock> lk_;
        };

        // await a coroutine which executes the operation logic
        return hce::join(awt::op(this, user_lk, timer::now() + dur, a));
    }
    
    inline void notify_one() {
        std::unique_lock<spinlock> lk(lk_);

        if(blocked_queue_.size()) {
            auto r = blocked_queue_.front();
            blocked_queue_.pop_front();

            lk.unlock();
            r.callback();
        }
    }
    
    inline void notify_all() {
        std::deque<resumable*> bq;

        {
            std::unique_lock<spinlock> lk(lk_);

            // swap the queues
            bq = std::move(blocked_queue_);
        }

        for(auto r : bq) { r.callback(); }
    }

private:
    struct resumable {
        void* key;
        std::function<void()> callback;
    };

    spinlock slk_;
    std::deque<resumable> blocked_queue_;
};

/**
 @brief condition_variable variant which only accepts `hce::unique_lock<hce::mutex>`
 */
struct condition_variable : public printable {
    condition_variable() = default;
    condition_variable(const condition_variable&) = delete;
    condition_variable(condition_variable&&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;
    condition_variable& operator=(condition_variable&&) = delete;
    
    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "condition_variable"; }
    
    inline hce::awt<bool> wait(hce::unique_lock<hce::mutex>& lk) {
        return cv_.wait(lk);
    }

    template <class Pred>
    hce::awt<bool> wait(hce::unique_lock<hce::mutex>& lk, Pred&& p) {
        return cv_.wait(lk, p);
    }

    inline hce::awt<std::optional<std::cv_status>> wait_for(
            hce::unique_lock<hce::mutex>& lk, 
            const std::chrono::steady_clock::duration& d) {
        return cv_.wait_for(lk, d);
    }
    
    template <class Pred>
    hce::awt<bool> wait_for(
            hce::unique_lock<hce::mutex>& lk, 
            const std::chrono::steady_clock::duration& d,
            Pred p) {
        return cv_.wait_for(lk, d, p);
    }

    inline hce::awt<std::optional<std::cv_status>> wait_until(
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
