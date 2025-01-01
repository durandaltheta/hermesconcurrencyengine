//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_TIMER__
#define __HERMES_COROUTINE_ENGINE_TIMER__

#include <list>
#include <exception>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "utility.hpp"
#include "memory.hpp"
#include "logging.hpp"
#include "thread.hpp"
#include "atomic.hpp"
#include "id.hpp"
#include "chrono.hpp"
#include "list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {
namespace config {
namespace timer {
namespace service {

/**
 If a timer would timeout at this threshold or lower, the timer will 
 busy-wait until the timeout occurs in order to increase timeout accuracy.

 @return the millisecond busy-wait threshold for timer timeouts
 */
extern unsigned int busy_wait_microsecond_threshold();

/**
 Get the platform specific thread priority to pass to hce::set_thread_priority() 
 for the timer processing thread.
 */
extern int thread_priority();

}
}
}

namespace timer {

// timer awaitable implementation
struct awaitable : public 
    hce::scheduler::reschedule<
       hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<bool>::interface>>
{
    awaitable() : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<bool>::interface>>(
                    slk_,
                    hce::awaitable::await::policy::defer,
                    hce::awaitable::resume::policy::lock),
        ready_(false),
        result_(false)
    { 
        HCE_MED_CONSTRUCTOR();
    }

    inline virtual ~awaitable(){
        HCE_MED_DESTRUCTOR();

        if(!ready_) {
            std::stringstream ss;
            ss << *this << "was not awaited nor resumed";
            HCE_FATAL_METHOD_BODY("~awaitable",ss.str());
            std::terminate();
        }
    }

    static inline std::string info_name() { 
        return "hce::timer::awaitable"; 
    }

    inline std::string name() const { return awaitable::info_name(); }

    inline bool on_ready() { 
        HCE_MED_METHOD_BODY("on_ready",ready_);
        return ready_; 
    }

    inline void on_resume(void* m) { 
        HCE_MED_METHOD_ENTER("on_resume",m);
        ready_ = true;
        result_ = (bool)m; 
    }

    inline bool get_result() { 
        HCE_MED_METHOD_BODY("get_result",result_);
        return result_; 
    }

private:
    bool ready_; 
    bool result_;
    hce::spinlock slk_;
};


/**
 @brief an object capable of starting, cancelling, and handling timer timeouts 

 This object is optimized to efficiently handle timeouts generally, including 
 some effort to increase precision.
 */
struct service : public hce::printable {
    struct bad_timer_timeout : public std::exception {
        bad_timer_timeout(service* s) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << s << " cannot start timer with null on_timeout callback";
                return ss.str();
            }())
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    static inline std::string info_name() { return "hce::timer::service"; }
    inline std::string name() const { return service::info_name(); }

    /// access the process-wide timer service
    static service& get();

    /**
     @brief start a timer 
     @param sid to overwrite with the started timer sid 
     @param timeout the timeout of the timer
     @param on_timeout the callback to execute when the timer times out 
     @param on_cancel the callback to execute if the timer is cancelled early 
     @return `true` on success, else `false`
     */
    void start(
        hce::sid& sid, 
        const hce::chrono::time_point& timeout,
        // std:: instead of hce::unique_ptr used to avoid stealing cache memory
        std::unique_ptr<hce::thunk> on_timeout,
        std::unique_ptr<hce::thunk> on_cancel = std::unique_ptr<hce::thunk>()) 
    {
        sid.make();
        HCE_MED_METHOD_ENTER("start",sid);

        if(on_timeout) {
            timer* t = hce::allocate<timer>(1);
            new(t) timer(sid, 
                         timeout,
                         std::move(on_timeout), 
                         std::move(on_cancel));

            std::lock_guard<hce::spinlock> lk(lk_);

            timers_.push_back(t);
            timers_.sort([](timer* lhs, timer* rhs) {
                return lhs->timeout < rhs->timeout;
            });

            notify_();
        } else {
            throw bad_timer_timeout(this);
        }
    }

    /**
     @brief start a timer 
     @param sid to overwrite with the started timer sid 
     @param dur the duration of the timer
     @param as... additional arguments passed to start()
     @return `true` on success, else `false`
     */
    template <typename... As>
    bool start(hce::sid& sid, 
               const hce::chrono::duration& dur,
               As&&... as) {
        return start(sid, hce::chrono::now() + dur, std::forward<As>(as)...);
    }

    /**
     @return `true` if timer is running, else `false`
     */
    bool running(const hce::sid& sid) {
        HCE_MED_METHOD_ENTER("running",sid);
        bool result = false;

        {
            std::lock_guard<hce::spinlock> lk(lk_);

            for(auto& t : timers_) {
                if(sid == t->sid) {
                    HCE_MED_METHOD_BODY("running","timer found");
                    result = true;
                    break;
                }
            }
        }

        return result;
    }

    /**
     @brief cancel a timer  
     @param sid the sid of the running timer
     @return `true` if the timer was found running and canceled, else `false`
     */
    bool cancel(const hce::sid& sid) {
        HCE_MED_METHOD_ENTER("cancel",sid);
        bool result = false;

        if(sid) {
            hce::unique_ptr<timer> t;

            std::unique_lock<hce::spinlock> lk(lk_);

            auto it = timers_.begin();
            auto end = timers_.end();

            while(it != end) [[likely]] {
                // search through the timers for a matching sid
                if((*it)->sid == sid) [[unlikely]] {
                    t.reset(*it);
                    timers_.erase(it);
                    notify_();
                    lk_.unlock();
                   
                    // do operations outside lock which don't require it
                    result = true;

                    // on_cancel is optional
                    if(t->on_cancel) [[likely]] {
                        (*(t->on_cancel))(); 
                    }

                    HCE_MED_METHOD_BODY("cancel","cancelled timer with ",sid);
                    break;
                }
            }
        } 

        return result;
    }

private:
    struct timer {
        hce::sid sid;
        hce::chrono::time_point timeout;
        std::unique_ptr<hce::thunk> on_timeout;
        std::unique_ptr<hce::thunk> on_cancel;
    };

    service() : 
        running_(true),
        waiting_(false),
        busy_wait_timer_(nullptr),
        thd_([](service* ts) { 
            HCE_HIGH_FUNCTION_ENTER("hce::timer::service::thread");
            ts->run(); 
            HCE_HIGH_FUNCTION_BODY("hce::timer::service::thread","exit");
        }, this) 
    {
        HCE_HIGH_CONSTRUCTOR();
        hce::set_thread_priority(
            thd_, 
            hce::config::timer::service::thread_priority());
    }

    ~service() {
        HCE_HIGH_DESTRUCTOR();

        {
            std::lock_guard<hce::spinlock> lk(lk_);
            busy_wait_timer_ = nullptr; // stop busy waiting
            running_ = false; 
            notify_();
        }

        thd_.join();

        // properly cancel and cleanup timers
        while(timers_.size()) {
            timer* t = timers_.front();
            timers_.pop_front();

            if(t->on_cancel) {
                (*(t->on_cancel))();
            }

            HCE_HIGH_METHOD_BODY("~service","cancelled timer with ", t->sid);

            t->~timer();
            hce::deallocate<timer>(t,1);
        }
    }

    inline void notify_() {
        // ensure busy wait stops for reprocessing
        busy_wait_timer_ = nullptr;

        if(waiting_) {
            waiting_ = false;
            cv_.notify_one();
        } 
    }

    inline void run() {
        HCE_HIGH_METHOD_ENTER("run");
        const hce::chrono::duration busy_wait_microsecond_threshold(
            std::chrono::milliseconds(
                hce::config::timer::service::busy_wait_microsecond_threshold()));
        hce::chrono::time_point now;
        hce::chrono::time_point timeout;
        hce::list<std::unique_ptr<hce::thunk>> timed_out;

        std::unique_lock<hce::spinlock> lk(lk_);

        // the high level service run loop, which continues till process exit
        while(running_) [[likely]] {
            // check for any ready timers 
            if(timers_.size()) [[unlikely]] {
                // update the current timepoint
                now = hce::chrono::now();
                auto it = timers_.begin();
                auto end = timers_.end();

                auto timeout_ready = [&] {
                    return it != end && (*it)->timeout <= now;
                };

                // check if a timer is ready to timeout
                if(timeout_ready()) [[unlikely]] {
                    do {
                        timer* t = *it;
                        // handle timeout callbacks outside lock
                        timed_out.push_back(std::move(t->on_timeout));
                        t->~timer();
                        hce::deallocate<timer>(t,1);
                        it = timers_.erase(it);

                        // Explicitly evaluate the condition and apply [[likely]]
                        if(timeout_ready()) [[likely]] {
                            continue;
                        } else {
                            break;
                        }
                    // need to continue processing at this point to check what 
                    // has changed during unlocked state
                    // a sane implementation should default `true` to [[likely]]
                    } while(true); 

                    // execute on_timeout callbacks outside the lock
                    lk.unlock();

                    do {
                        (*(timed_out.front()))();
                        timed_out.pop();
                    } while(timed_out.size()); [[likely]]

                    lk.lock();
                } else [[likely]] {
                    hce::chrono::duration time_left = now - (*it)->timeout;
                    bool busy_wait_not_ready = false;

                    // if the time till timeout is less than the configured 
                    // busy-wait threshold then we busy-wait for increased 
                    // timeout precision
                    if(time_left < busy_wait_microsecond_threshold) [[unlikely]] {
                        busy_wait_timer_ = *it;

                        do {
                            // The short unlocked window allows API calls to
                            // start()/cancel() during busy-wait. CPU usage
                            // doesn't matter because we're already trying 
                            // to waste CPU. 
                            lk.unlock();
                            now = hce::chrono::now();
                            lk.lock();

                            // we break out if busy_wait_timer_ is set to 
                            // nullptr or timeout occured
                            busy_wait_not_ready =
                                busy_wait_timer_ && 
                                now < busy_wait_timer_->timeout;

                        } while(busy_wait_not_ready);
                    } else [[likely]] {
                        waiting_ = true;

                        // take a copy of the timeout because the list may be 
                        // resorted while blocking and the timeout is passed in 
                        // as a reference
                        timeout = timers_.front()->timeout;

                        // wait till the first available timeout
                        cv_.wait_until(lk, timeout);
                    }
                } 
            } else {
                // wait for something to happen
                waiting_ = true;
                cv_.wait(lk);
            }
        }

        HCE_HIGH_METHOD_BODY("run","exit");
    }

    hce::spinlock lk_;
    bool running_;
    bool waiting_; // help guard against unnecessary system calls
    std::condition_variable_any cv_;
    timer* busy_wait_timer_;
    std::list<timer*,hce::allocator<timer*>> timers_;
    std::thread thd_;
};

/**
 @brief start a timer 

 The returned awaitable will result in `true` if the timer timeout was 
 reached, else `false` will be returned if it was cancelled early due to 
 scheduler being totally .

 @param id a reference to an hce::sid which will be set to the launched timer's id
 @param as the remaining arguments which will be passed to `hce::chrono::duration()`
 @return an awaitable to join with the timer timing out (returning true) or being cancelled (returning false)
 */
template <typename... As>
inline hce::awt<bool> start(hce::sid& sid, As&&... as) {
    // construct the sid with a guaranteed newly allocated address
    sid.make();

    // construct the timeout 
    hce::chrono::time_point timeout =
        hce::chrono::now() + hce::chrono::duration(std::forward<As>(as)...);

    // construct the timer awaitable
    auto a = new awaitable;

    // put in unique_ptr in case start() throws exception
    std::unique_ptr<awaitable> upa(a);

    // allocate callbacks
    auto* on_timeout = new hce::thunk;
    auto* on_cancel = new hce::thunk;

    // construct on_timeout callback
    hce::memory::construct_thunk_ptr(
        on_timeout, 
        [=]() mutable { a->resume((void*)1); });

    // construct on_cancel callback
    hce::memory::construct_thunk_ptr(
        on_cancel, 
        [=]() mutable { a->resume((void*)0); });

    // start the actual timer on the timer
    service::get().start(sid,
                         timeout,
                         std::unique_ptr<hce::thunk>(on_timeout), 
                         std::unique_ptr<hce::thunk>(on_cancel));

    // acquire the id of the constructed timer
    HCE_MED_FUNCTION_ENTER("hce::start", sid, timeout);
    return hce::awt<bool>(upa.release());
}

/**
 @brief determine if a timer with the given id is running
 @return true if the timer is running, else false
 */
inline bool running(const hce::sid& sid) {
    HCE_MED_FUNCTION_ENTER("hce::running",sid);
    bool result = service::get().running(sid);
    HCE_MED_FUNCTION_BODY("hce::running",result);
    return result;
}

/**
 @brief attempt to cancel a scheduled timer

 The `hce::sid` should be constructed from a call to the `hce::timer::start()` method.

 @param id the hce::sid associated with the timer to be cancelled
 @return true if cancelled timer successfully, false if timer already timed out or was never started or if the sid was never constructed
 */
inline bool cancel(const hce::sid& sid) {
    HCE_MED_FUNCTION_ENTER("hce::cancel",sid);
    bool result = service::get().cancel(sid);
    HCE_MED_FUNCTION_BODY("hce::cancel",result);
    return result;
}

}

/**
 @brief start a timer to sleep for a period

 Calls `hce::timer::start()` but abstracts away the timer's id. 

 @param as the arguments will be passed to `hce::chrono::duration()`
 @return an awaitable to join with the timer timing out or being cancelled
 */
template <typename... As>
inline hce::awt<void> sleep(As&&... as) {
    HCE_MED_FUNCTION_ENTER("hce::sleep");
    hce::sid sid;
    // start the timer and convert from awt<bool> to awt<void>
    return hce::awt<void>(timer::start(sid, std::forward<As>(as)...).release());
}

}

#endif
