//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_TIMER
#define HERMES_COROUTINE_ENGINE_TIMER

#include <list>
#include <exception>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "base.hpp"
#include "utility.hpp"
#include "alloc.hpp"
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

/**
 Get the platform specific thread priority to pass to hce::set_thread_priority() 
 for the timer processing thread. This priority is expected to be above normal 
 to increase timeout precision.
 */
int thread_priority();

/**
 Busy-waiting is not ideal, but is sometimes necessary to guarantee precision 
 during short timeouts. This value should be set low (IE, less than 10ms) in 
 order to encourage busy-waiting to occur only when timers are very close to 
 timeout. The larger this threshold, the more CPU will be wasted busy-waiting 
 (with potentially increased timeout precision).

 @return threshold in microseconds before timer service will busy wait for timeout
 */
hce::chrono::duration busy_wait_threshold();

/**
 The duration, in microseconds, that the timer service thread should 
 automatically wakeup *early* in order to increase precision of timeouts.

 How this value is used is determined by the timeout_algorithm().

 That is, longer sleeps can have imprecise wakeups due to OS and CPU power 
 saving behavior, so we set an "early" wakeup a short time before timeout so 
 that when the thread goes back to sleep, it's encouraged to wakeup with 
 increased precision the second time.

 @return microsecond early wakeup duration
 */
hce::chrono::duration early_wakeup_threshold();

/**
 An additional duration, in microseconds, that the timer service thread should 
 wakeup early with very long timeouts. 

 How this value is used is determined by the timeout_algorithm().

 This may become necessary because the OS or CPU can enter deeper power saving 
 modes during longer timeouts which can significantly impact timeout precision. 
 This allows the CPU to wakeup somewhat early, hopefully accounting for extra 
 delay and then wait again a much shorter timer before being woken up early at 
 the normal early wakeup threshold.

 The cost for this additional wakeup is quite low, as the longer the timeout
 the less this thread is consuming CPU anyway.

 @return microsecond early wakeup long duration
 */
hce::chrono::duration early_wakeup_long_threshold();

typedef hce::chrono::time_point (*algorithm_function_ptr)(
    const hce::chrono::time_point& now, 
    const hce::chrono::time_point& requested_timeout);

/**
  @brief the algorithm for determining how long the timer service should wait for until the next timeout

  If the returned time_point is greater than the requested_timeout, the 
  requested_timeout will be taken instead.

  An timer will not actually timeout until it's timeout is reached. This 
  operation is for putting the entire *timer service* thread to sleep.

  This operation allows for manipulation of timeouts to improve overall timeout 
  precision. Factors which influence timeout precision generally are often 
  non-trivial and non-deterministic, such as OS and power configurations.

  @return an algorithm to calculate service timeouts 
 */
algorithm_function_ptr timeout_algorithm();

}
}

namespace timer {

/**
 @brief an object capable of starting, cancelling, and handling timer timeouts 
 */
struct service : public hce::printable {
    static inline std::string info_name() { return "hce::"; }
    inline std::string name() const { return service::info_name(); }

    /// access the process-wide timer service
    static inline service& get() { return *(service::instance_); }

    /**
     @brief start a timer 
     @param sid to overwrite with the started timer sid 
     @param timeout the time_point of the timer timeout
     */
    hce::awt<bool> start(hce::sid& sid, const hce::chrono::time_point& timeout){
        sid.make(); 
        HCE_LOW_METHOD_ENTER("start", sid, timeout);
        return start_(sid, timeout);
    }

    /**
     @brief start a timer 
     @param sid to overwrite with the started timer sid 
     @param dur the duration of the timer timeout
     */
    hce::awt<bool> start(hce::sid& sid, const hce::chrono::duration& dur) {
        sid.make();
        HCE_LOW_METHOD_ENTER("start", sid, dur);
        return start_(sid, hce::chrono::now() + dur);
    }

    /**
     @return `true` if timer is running, else `false`
     */
    bool running(const hce::sid& sid) {
        HCE_LOW_METHOD_ENTER("running",sid);
        bool result = false;

        {
            std::lock_guard<hce::spinlock> lk(lk_);

            if(running_) [[likely]] {
                for(auto& t : timers_) {
                    if(sid == t->sid) {
                        HCE_LOW_METHOD_BODY("running","timer found");
                        result = true;
                        break;
                    }
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
        HCE_LOW_METHOD_ENTER("cancel",sid);
        bool result = false;

        if(sid) {
            // let unique_ptr call destructor
            std::unique_ptr<timer> t;

            std::unique_lock<hce::spinlock> lk(lk_);

            if(running_) [[likely]] {
                auto it = timers_.begin();
                auto end = timers_.end();

                while(it != end) [[likely]] {
                    // search through the timers for a matching sid
                    if((*it)->sid == sid) [[unlikely]] {
                        t.reset(*it);
                        timers_.erase(it);
                        notify_();
                        lk.unlock();
                       
                        // do operations outside lock which don't require it
                        result = true;
                        t->awt->resume((void*)0); // cancel awaitable

                        HCE_LOW_METHOD_BODY("cancel","cancelled timer with ",sid);
                        break;
                    }
                }
            }
        } 

        return result;
    }

    /**
     @brief microsecond ticks info struct
     */
    struct ticks {
        size_t runtime; // microsecond ticks spent running
        size_t busywait; // microsecond ticks spent busy-waiting 
    };
    
    /**
     @return timer service runtime ticks information
     */
    ticks get_ticks() const { 
        std::lock_guard<hce::spinlock> lk(lk_);
        return { micro_runtime_ticks_, micro_busywait_ticks_ };
    }
    
    /**
     @brief reset all timer service ticks for fresh calculation
     */
    void reset_ticks() {
        std::lock_guard<hce::spinlock> lk(lk_);
        micro_runtime_ticks_ = 0;
        micro_busywait_ticks_ = 0;
    }

private:
    // timer service awaitable implementation
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
            return "hce::timer::service::awaitable"; 
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

    // internal timer object
    struct timer {
        timer(const hce::sid& s, 
              const hce::chrono::time_point& t, 
              hce::timer::service::awaitable* a) :
            sid(s),
            timeout(t),
            awt(a)
        { }

        hce::sid sid;
        hce::chrono::time_point timeout;
        hce::timer::service::awaitable* awt;
    };

    /*
     The timer service thread doesn't start right away, because it's not a 
     thread that's guaranteed to be needed by user code. Instead, thread 
     launching is lazy. This is especially fine because the bottleneck in timer 
     code will never be a boolean check.
     */
    service() :
        running_(false),
        waiting_(false),
        micro_runtime_ticks_(0),
        micro_busywait_ticks_(0),
        busy_wait_threshold_(hce::config::timer::busy_wait_threshold()),
        timeout_algorithm_(hce::config::timer::timeout_algorithm())
    {
        service::instance_ = this;
        HCE_HIGH_CONSTRUCTOR();
    }

    virtual ~service() {
        HCE_HIGH_DESTRUCTOR();

        service::instance_ = nullptr;

        {
            std::unique_lock<hce::spinlock> lk(lk_);

            if(running_) {
                running_ = false; 
                notify_();
                lk.unlock();
                thd_.join();
            }
        }

        // properly cancel and cleanup timers
        while(timers_.size()) {
            // let unique_ptr call destructor
            std::unique_ptr<timer> t(timers_.front());
            timers_.pop_front();
            t->awt->resume((void*)0); // cancel awaitable

            HCE_HIGH_METHOD_BODY("~service","cancelled timer with ", t->sid);
        }
    }

    /*
      The default algorithm for determining how long the timer service should 
      wait for until the next timeout

      If the returned time_point is greater than the requested_timeout, the 
      requested_timeout will be taken instead.

      An timer will not actually timeout until it's timeout is reached. This 
      operation is for putting the entire *timer service* thread to sleep.

      This operation allows for manipulation of timeouts to improve overall timeout 
      precision. Factors which influence timeout precision generally are often 
      non-trivial and non-deterministic, such as OS and power configurations.
     */
    static hce::chrono::time_point default_timeout_algorithm(
        const hce::chrono::time_point& now, 
        const hce::chrono::time_point& requested_timeout);

   
    // sid must be set at this point
    inline hce::awt<bool> start_(
            hce::sid& sid, 
            const hce::chrono::time_point& timeout)
    {
        HCE_TRACE_METHOD_ENTER("start_",sid,timeout);

        // allocate and construct the timer service awaitable
        auto awt = new hce::timer::service::awaitable;

        // allocate and construct timer using default `new` (don't need to steal 
        // from calling thread's memory cache
        timer* t = new timer(sid, timeout, awt);

        {
            std::lock_guard<hce::spinlock> lk(lk_);

            if(!running_) [[unlikely]] {
                // launch the timer service thread if it was never started
                running_ = true;

                thd_ = std::thread([](service* ts) { 
                    HCE_HIGH_FUNCTION_ENTER("hce::timer::service::thread");
                    ts->run(); 
                    HCE_HIGH_FUNCTION_BODY("hce::timer::service::thread","exit");
                }, this);

                hce::thread::set_priority(
                    thd_, 
                    hce::config::timer::thread_priority());
            }

            timers_.push_back(t);
            timers_.sort([](timer* lhs, timer* rhs) {
                return lhs->timeout < rhs->timeout;
            });

            notify_();
        }

        // return the awaitable
        return hce::awt<bool>(awt);
    }

    inline void notify_() {
        if(waiting_) {
            waiting_ = false;
            cv_.notify_one();
        } 
    }

    inline void run() {
        HCE_HIGH_METHOD_ENTER("run");
        hce::chrono::time_point now = hce::chrono::now();
        hce::chrono::time_point prev = now;
        hce::chrono::time_point timeout;
        hce::list<hce::timer::service::awaitable*> timed_out;

        auto update_now = [&](bool busy){ 
            prev = now;
            now = hce::chrono::now();
            size_t ticks = hce::chrono::to<std::chrono::microseconds>(now - prev).count();

            // update the service total runtime
            micro_runtime_ticks_ += ticks;

            // runtime wrapped around somehow, reset calculation
            if(micro_runtime_ticks_ < micro_busywait_ticks_) [[unlikely]] {
                micro_busywait_ticks_ = 0;
            }

            // update busywait time
            if(busy) [[likely]] {
                micro_busywait_ticks_ += ticks;
            }
        };

        std::unique_lock<hce::spinlock> lk(lk_);

        // the high level service run loop, which continues till process exit
        while(running_) [[likely]] {
            // check for any ready timers 
            if(timers_.size()) [[unlikely]] {
                // update the current timepoint 
                update_now(false);
                auto it = timers_.begin();
                auto end = timers_.end();

                auto timeout_ready = [&] {
                    return it != end && (*it)->timeout <= now;
                };

                // check if a timer is ready to timeout
                if(timeout_ready()) [[unlikely]] {
                    do {
                        // let unique_ptr call destructor
                        std::unique_ptr<timer> t(*it);
                        // handle timeout callbacks outside lock
                        timed_out.push_back(t->awt);
                        it = timers_.erase(it);
                    } while(timeout_ready()); 

                    // resume awaitables outside the lock
                    lk.unlock();

                    do {
                        timed_out.front()->resume((void*)1); // resume awaitable
                        timed_out.pop();
                    } while(timed_out.size()); [[likely]]

                    // re-acquire the lock
                    lk.lock();
                } else [[likely]] {
                    auto below_busy_wait_threshold = [&]{
                        // only ever need to wait if we haven't reached timeout
                        if(now < timeout) {
                            // only need to busy-wait if the difference between 
                            // now and the timeout is less than the threshold
                            return (timeout - now) <= busy_wait_threshold_;
                        } else {
                            // break out of loop
                            return false;
                        }
                    };
                           
                    // update latest timeout to the latest timeout
                    timeout = timers_.front()->timeout;

                    if(below_busy_wait_threshold()) [[unlikely]] {
                        // spend as much time busy waiting as possible unlocked
                        lk_.unlock();

                        do {
                            // spend some time acquiring the time and keeping 
                            // the service unlocked
                            update_now(true);

                            lk_.lock();
                            // update latest timeout each check because the lock 
                            // is not held
                            timeout = timers_.front()->timeout;
                            lk_.unlock();

                            // don't actually need to lock during this check
                        } while(below_busy_wait_threshold());
                            
                        lk_.lock();
                    } else [[likely]] {
                        auto tmp_timeout = timeout_algorithm_(now, timeout);

                        // force a maximum of the user's timeout
                        if(tmp_timeout < timeout) [[likely]] {
                            timeout = tmp_timeout;
                        }

                        // wait till timeout
                        waiting_ = true;
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

    static service* instance_;

    mutable hce::spinlock lk_;
    bool running_;
    bool waiting_; // help guard against unnecessary system calls
    size_t micro_runtime_ticks_;
    size_t micro_busywait_ticks_;
    const hce::chrono::duration busy_wait_threshold_;
    std::condition_variable_any cv_;
    std::list<timer*,hce::allocator<timer*>> timers_;
    std::thread thd_;
    hce::config::timer::algorithm_function_ptr timeout_algorithm_;

    friend hce::lifecycle;
};

/**
 @brief start a timer  

 A simplification for calling hce::timer::service::get().start().

 The returned awaitable will result in `true` if the timer timeout was 
 reached, else `false` will be returned if it was cancelled early due to 
 scheduler being totally .

 @param id a reference to an hce::sid which will be set to the launched timer's id
 @param timeout an hce::chrono::time_point or hce::chrono::duration when the timer should time out
 @return an awaitable to join with the timer timing out (returning true) or being cancelled (returning false)
 */
template <typename TIMEOUT>
inline hce::awt<bool> start(hce::sid& sid, const TIMEOUT& timeout) {
    auto awt = service::get().start(sid, timeout);
    HCE_MED_FUNCTION_ENTER("hce::start", sid, timeout);
    return awt;
}

/**
 @brief determine if a timer is running

 A simplification for calling hce::timer::service::get().running().

 @param sid the sid associated with a launched timer
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

 A simplification for calling hce::timer::service::get().cancel().

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

 Calls `hce::timer::start()` but abstracts away the timer's sid and success 
 state (no need to track success when timer is uncancellable).

 @param timeout an hce::chrono::time_point or hce::chrono::duration when the sleep should time out
 @return an awaitable to join with the timer timing out or being cancelled
 */
template <typename TIMEOUT>
inline hce::awt<void> sleep(const TIMEOUT& timeout) {
    HCE_MED_FUNCTION_ENTER("hce::sleep", timeout);
    hce::sid sid;

    // start the timer and convert from awt<bool> to awt<void>
    return hce::awt<void>(timer::start(sid, timeout).release());
}

}

#endif
