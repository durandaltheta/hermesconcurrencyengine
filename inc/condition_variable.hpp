//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_CONDITION_VARIABLE__
#define __HERMES_COROUTINE_ENGINE_CONDITION_VARIABLE__ 

// c++
#include <mutex>
#include <deque>
#include <chrono>

// local
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "mutex.hpp"

namespace hce {

/**
 @brief a united coroutine and non-coroutine condition_variable
 */
struct condition_variable {
    inline awaitable<void> wait(hce::unique_lock& lk) {
        // a simple awaitable to block this coroutine
        struct awt : protected hce::awaitable<void>::implementation,
                     public resumable {
            // inherit the std::unique_lock from the coroutine
            awt(std::unique_lock<spinlock>&& lk) : lk_(std::move(lk) { }

            static inline hce::coroutine op(
                    hce::condition_variable& cv,
                    hce::mutex& user_lk) {

                // acquire the condition_variable lock and interleave locks
                std::unique_lock<hce::spinlock> lk(cv.lk_);
                user_lk.unlock();

                auto a = new awt(std::move(lk));
                cv.blocked_queue_.push_back(a);

                // block until resume() is called by notify operation
                co_await awaitable<void>( 
                    std::unique_ptr<awaitable<void>::implementation>(a));

                // reacquire the user lock
                co_await user_lk.lock();
            }

        protected:
            inline std::unique_lock<hce::spinlock>& get_lock() { return lk_; }
            inline bool ready_impl() { return false; }
            inline void resume_impl(void* m) { }
        
            inline coroutine::destination acquire_destination() {
                return scheduler::reschedule{ this_scheduler() };
            }

        private:
            std::unique_lock<hce::spinlock> lk_(lf_);
        };

        return scheduler::get().await(awt::op(this, &user_lk));
    }

    template <class Pred>
    awaitable<void> wait(hce::unique_lock& lk, Pred p) {
        struct awt {
            static inline hce::coroutine op(
                    hce::condition_variable& cv,
                    hce::unique_lock& lk,
                    const std::chrono::steady_clock::duration& d,
                    Pred p) {
                while(!p()) { co_await cv->wait(lk, dur); }
            }
        };

        return scheduler::get().await(awt::op(*this, lk, dur, std::move(p)));
    }

    inline awaitable<std::cv_status> wait_for(
            hce::unique_lock& lk, 
            const std::chrono::steady_clock::duration d) {
        return wait_until(lk, timer::now() + d);
    }
    
    template <class Pred>
    awaitable<bool> wait_for(
            hce::unique_lock& lk, 
            const std::chrono::steady_clock::duration d,
            Pred p) {
        struct awt : protected awaitable<bool>::implementation,
                     public resumable {
            awt(hce::condition_variable& cv,
                hce::unique_lock& user_lk,
                const std::chrono::steady_clock::duration d,
                Pred&& p) :
                lk_(cv.lk_),
                co_(awt::op(cv, user_lk, d, std::move(p), cv, this))
            { }

            static inline coroutine op(
                    hce::condition_variable& cv,
                    hce::unique_lock& lk,
                    const std::chrono::steady_clock::duration& d,
                    Pred p,
                    awt* a) {
                bool res = false;

                while(!(res = p())) {
                    std::cv_status sts = co_await cv.wait_for(lk, dur);
                    if(sts == std::cv_status::timeout) { break; }
                }

                a->resume(&res);
            }

        protected:
            inline std::unique_lock<spinlock>& get_lock() { return lk_; }

            inline bool ready_impl() { 
                schedule(std::move(co_));
                return false; 
            }

            inline void resume_impl(void* m) { res_ = *((bool*)m); }
            inline bool result_impl() { return res_; }
        
            inline coroutine::destination acquire_destination() {
                return scheduler::reschedule{ this_scheduler() };
            }

        private:
            std::unique_lock<spinlock> lk_(lf_);
            hce::coroutine co_;
            bool res_ = false;
        };

        return awaitable<bool>::make<awt>(*this, lk, dur, std::move(p));
    }

    template <class Rep, class Period>
    awaitable<std::cv_status> wait_until(
        hce::unique_lock& lk, 
        const std::chrono::steady_clock::time_point tp) {

        struct awt : protected awaitable<std::cv_status>::implementation {
            awt(hce::unique_lock& user_lk,
                hce::condition_variable* cv, 
                const std::chrono::steady_clock::time_point& tp) :
                user_lk_(&user_lk),
                lk_(cv->lk_),
                tp_(tp)
             { }

        protected:
            inline std::unique_lock<lockfree>& get_lock() { return lk_; }

            inline bool ready_impl() { 
                schedule(awt::co, user_lk_, cv_, tp_, this);
                return false; 
            }

            inline void resume_impl(void* m) { res_ = *((bool*)m); }

            inline std::cv_status result_impl() { 
                return res_ 
                    ? std::cv_status::timeout 
                    : std::cv_status::no_timeout;
            }
        
            inline coroutine::destination acquire_destination() {
                return scheduler::reschedule{ this_scheduler() };
            }

        private:
            static inline coroutine op(
                    hce::unique_lock* user_lk,
                    hce::condition_variable* cv,
                    std::chrono::steady_clock::time_point tp,
                    awt* a) {
                struct resumer;

                struct wait_timer : protected hce::timer {
                    wait_timer(const std::chrono::steady_clock::time_point& tp,
                               hce::condition_variable* cv,
                               awt* a) :
                        hce::timer(tp)
                    { } 

                    virtual ~wait_timer(){}

                    inline void cancel() { 
                        // resume the root awaitable and inform of cancellation
                        a->resume(0); 
                    }

                    inline void timeout() { 
                        {
                            std::unique_lock<hce::spinlock> lk(cv->lk_);
                            auto it = cv->blocked_queue_.begin();
                            auto end = cv->blocked_queue_.end();

                            while(it!=end) {
                                if(*it == r_) {
                                    // resume the coroutine op, inform the 
                                    // resumer that was called on timeout
                                    (*it)->resume(1);
                                    cv->blocked_queue_.erase(it);
                                    break;
                                } else {
                                    ++it;
                                }
                            }
                        }

                        // resume the root awaitable and inform of timeout
                        a->resume(1); 
                    }

                    resumer* r_;
                };

                struct resumer : protected awaitable<void>::implementation,
                                 public resumable {
                    resumer(std::unique_lock<hce::spinlock>&& lk,
                            std::weak_ptr<scheduler> sch,
                            timer::id id) :
                        lk_(std::move(lk_)),
                        sch_(std::move(sch)),
                        id_(std::move(id))
                    { }

                protected:
                    inline std::unique_lock<hce::spinlock>& get_lock() { return lk_; }

                    inline bool ready_impl() { return false; }

                    inline void resume_impl(void* m) { 
                        if(!m) {
                            // attempt to cancel the timer as we were called by 
                            // a notify. This call may fail.
                            auto sch = sch_.lock();
                            if(sch) { sch->cancel(std::move(id_)); }
                        }
                    }
                
                    inline coroutine::destination acquire_destination() {
                        return scheduler::reschedule{ this_scheduler() };
                    }

                private:
                    std::unique_lock<hce::spinlock> lk_;
                    std::weak_ptr<scheduler> sch_;
                    timer::id id_;
                };

                // interleave locks
                std::unique_lock<hce::spinlock> lk(cv->lk_);
                user_lk->unlock();

                auto& sch = scheduler::get();

                // start the timer which will ultimately resume the call to 
                // wait_until()
                auto id = sch.start(hce::timer::make<wait_timer>(tp,a));

                // register the awaitable that condition_variable notify 
                // operations will call
                auto r = new resumer(
                        (std::weak_ptr<hce::scheduler>)sch,
                        std::move(lk),
                        id);
                cv->blocked_queue_.push_back(r);

                // block until resume() is called by notify operation, which 
                // will return the lock to us
                co_await awaitable<void>( 
                    std::unique_ptr<awaitable<void>::implementation>(r));

                // reacquire user lock after we are resumed
                co_await user_lk->lock();
            }

            bool res_ = false;
            hce::mutex* user_lk_;
            std::unique_lock<hce::spinlock> lk_;
            const std::chrono::steady_clock::time_point tp_;
        };

        return awaitable<std::cv_status>::make<awt>(this, dur);
    }
    
    inline void notify_one() {
        std::unique_lock<spinlock> lk(lk_);

        if(blocked_queue_.size()) {
            blocked_queue_.front()->resume(nullptr);
            blocked_queue_.pop_front();
        }
    }
    
    inline void notify_all() {
        std::unique_lock<spinlock> lk(lk_);

        for(auto r : blocked_queue_) {
            r->resume(nullptr);
        }

        blocked_queue_.clear();
    }

private:
    // a unified and limited interface for awaitables in the blocked queue
    struct resumable {
        virtual void resume(nullptr) = 0;
    };

    spinlock slk_;
    std::deque<resumable*> blocked_queue_;
};

}

#endif
