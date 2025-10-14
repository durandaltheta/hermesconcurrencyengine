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

/**
 @brief an namespace object with static methods capable of starting, cancelling, and handling timer timeouts 
 */
struct timer : public hce::service<timer>, public hce::printable {
    static std::string info_name();
    std::string name() const;

    /**
     @brief microsecond ticks info struct 

     This provides runtime information about the timer service thread. It is 
     only useful during testing or when validating some algorithm configuration 
     in a `hce::lifecycle::config` passed to `hce::initialize()`.
     */
    struct ticks {
        // True if timer service thread is running. Requires timer::init() or 
        // timer::start() has been called
        bool running; 
        size_t runtime; // microsecond ticks spent running
        size_t busywait; // microsecond ticks spent busy-waiting  
    };
    
    /**
     @return timer service runtime ticks information
     */
    ticks get_ticks() const;
    
    /**
     @brief reset all timer service ticks for fresh calculation
     */
    void reset_ticks();

    /**
     @brief optionally ensure the timer service thread is running 

     If this function is not called, the timer service thread will not be 
     started until the first call to `hce::timer::start()`, potentially 
     introducing a slight delay. Call this early to prepare and launch the 
     thread.
     */
    static void init();

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
    static inline hce::awt<bool> start(hce::sid& sid, const TIMEOUT& timeout) {
        sid.make();
        auto awt = hce::service<timer>::get().start_(sid, timeout);
        HCE_MED_FUNCTION_ENTER("hce::start", sid, timeout);
        return awt;
    }

    /**
     @brief determine if a timer is running

     A simplification for calling hce::timer::service::get().running().

     @param sid the sid associated with a launched timer
     @return true if the timer is running, else false
     */
    static bool running(const hce::sid& sid);

    /**
     @brief attempt to cancel a scheduled timer

     A simplification for calling hce::timer::service::get().cancel().

     The `hce::sid` should be constructed from a call to the `hce::timer::start()` method.

     @param id the hce::sid associated with the timer to be cancelled
     @return true if cancelled timer successfully, false if timer already timed out or was never started or if the sid was never constructed
     */
    static bool cancel(const hce::sid& sid);

private:
    // timer service awaitable implementation
    struct awaitable : public 
        hce::scheduler::reschedule<
           hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<bool>::interface>>
    {
        awaitable();
        virtual ~awaitable();
        static std::string info_name();
        std::string name() const;
        bool on_ready();
        void on_notify(void* m);
        bool get_result();

    private:
        bool result_;
        bool ready_;
        hce::spinlock slk_;
    };

    // internal timer object
    struct timer_ {
        timer_(const hce::sid& s, 
               const hce::chrono::time_point& t, 
               hce::timer::awaitable* a);

        hce::sid sid;
        hce::chrono::time_point timeout;
        hce::timer::awaitable* awt;
    };

    /*
     The timer service thread doesn't start right away, because it's not a 
     thread that's guaranteed to be needed by user code. Instead, thread 
     launching is lazy. This is especially fine because the bottleneck in timer 
     code will never be a boolean check.
     */
    timer();
    virtual ~timer();
    void thread_guard_();
    void init_();

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

    hce::awt<bool> start_(hce::sid& sid, const hce::chrono::time_point& timeout);
    hce::awt<bool> start_(hce::sid& sid, const hce::chrono::duration& dur);
    bool running_(const hce::sid& sid);
    bool cancel_(const hce::sid& sid);
    void notify_();
    void run();

    mutable hce::spinlock lk_;
    bool runflag_;
    bool waiting_; // help guard against unnecessary system calls
    size_t micro_runtime_ticks_;
    size_t micro_busywait_ticks_;
    const hce::chrono::duration busy_wait_threshold_;
    std::condition_variable_any cv_;
    std::list<timer_*,hce::allocator<timer_*>> timers_;
    std::thread thd_;
    hce::config::timer::algorithm_function_ptr timeout_algorithm_;

    friend hce::lifecycle;
};

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
