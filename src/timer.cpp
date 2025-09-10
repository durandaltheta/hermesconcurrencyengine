//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "timer.hpp"
#include "lifecycle.hpp"

std::string hce::timer::info_name() { return "hce::timer"; }
std::string hce::timer::name() const { return hce::timer::info_name(); }

hce::timer::ticks hce::timer::get_ticks() const { 
    std::lock_guard<hce::spinlock> lk(lk_);
    return { runflag_, micro_runtime_ticks_, micro_busywait_ticks_ };
}

void hce::timer::reset_ticks() {
    std::lock_guard<hce::spinlock> lk(lk_);
    micro_runtime_ticks_ = 0;
    micro_busywait_ticks_ = 0;
}

void hce::timer::init() { hce::service<timer>::get().init_(); }

bool hce::timer::running(const hce::sid& sid) {
    HCE_MED_FUNCTION_ENTER("hce::running",sid);
    bool result = hce::service<timer>::get().running_(sid);
    HCE_MED_FUNCTION_BODY("hce::running",result);
    return result;
}

bool hce::timer::cancel(const hce::sid& sid) {
    HCE_MED_FUNCTION_ENTER("hce::cancel",sid);
    bool result = hce::service<timer>::get().cancel_(sid);
    HCE_MED_FUNCTION_BODY("hce::cancel",result);
    return result;
}

hce::timer::awaitable::awaitable() : 
    hce::scheduler::reschedule<
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt<bool>::interface>>(
                slk_,
                hce::awaitable::await::policy::defer_lock,
                hce::awaitable::resumed::policy::unlocked,
                hce::awaitable::resume::policy::lock),
    result_(false)
{ 
    HCE_MED_CONSTRUCTOR();
}

hce::timer::awaitable::~awaitable(){
    HCE_MED_DESTRUCTOR();
    this->clean();

    if(!ready()) {
        std::stringstream ss;
        ss << *this << "was not awaited nor resumed";
        HCE_FATAL_METHOD_BODY("~awaitable",ss.str());
        std::terminate();
    }
}

std::string hce::timer::awaitable::info_name() { 
    return "hce::timer::awaitable"; 
}

std::string hce::timer::awaitable::name() const { 
    return hce::timer::awaitable::info_name(); 
}

void hce::timer::awaitable::on_resume(void* m) { 
    HCE_MED_METHOD_ENTER("on_resume",m);
    this->ready(true);
    result_ = (bool)m; 
}

bool hce::timer::awaitable::get_result() { 
    HCE_MED_METHOD_BODY("get_result",result_);
    return result_; 
}

hce::timer::timer_::timer_(const hce::sid& s, 
      const hce::chrono::time_point& t, 
      hce::timer::awaitable* a) :
    sid(s),
    timeout(t),
    awt(a)
{ }

/*
 The timer service thread doesn't start right away, because it's not a 
 thread that's guaranteed to be needed by user code. Instead, thread 
 launching is lazy. This is especially fine because the bottleneck in timer 
 code will never be a boolean check.
 */
hce::timer::timer() :
    runflag_(false),
    waiting_(false),
    micro_runtime_ticks_(0),
    micro_busywait_ticks_(0),
    busy_wait_threshold_(hce::config::timer::busy_wait_threshold()),
    timeout_algorithm_(hce::config::timer::timeout_algorithm())
{
    HCE_HIGH_CONSTRUCTOR();
}

hce::timer::~timer() {
    HCE_HIGH_DESTRUCTOR();

    {
        std::unique_lock<hce::spinlock> lk(lk_);

        if(runflag_) {
            runflag_ = false; 
            notify_();
            lk.unlock();
            thd_.join();
        }
    }

    // properly cancel and cleanup timers
    while(timers_.size()) {
        // let unique_ptr call destructor
        std::unique_ptr<timer_> t(timers_.front());
        timers_.pop_front();
        t->awt->resume((void*)0); // cancel awaitable

        HCE_HIGH_METHOD_BODY("~timer","cancelled timer with ", t->sid);
    }
}

void hce::timer::thread_guard_() {
    if(!runflag_) [[unlikely]] {
        // launch the timer service thread if it was never started
        runflag_ = true;

        thd_ = std::thread([](timer* ts) { 
            HCE_HIGH_FUNCTION_ENTER("hce::timer::thread");
            ts->run(); 
            HCE_HIGH_FUNCTION_BODY("hce::timer::thread","exit");
        }, this);

        hce::thread::set_priority(
            thd_, 
            hce::config::timer::thread_priority());
    }
}

void hce::timer::init_() {
    std::lock_guard<hce::spinlock> lk(lk_);
    thread_guard_();
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
hce::chrono::time_point hce::timer::default_timeout_algorithm(
        const hce::chrono::time_point& now, 
        const hce::chrono::time_point& requested_timeout)
{
    static const auto btw = hce::config::timer::busy_wait_threshold();
    static const auto ewt = hce::config::timer::early_wakeup_threshold();
    static const auto ewlt = hce::config::timer::early_wakeup_long_threshold();
    auto timeout = requested_timeout;
    auto requested_timeout_dur = requested_timeout - now;

    if(requested_timeout_dur > ewlt) {
        // try to wakeup long sleeps extra early to account for slower behaviors 
        // like power saving, hopefully increasing precision
        timeout = requested_timeout - ewlt;
    } else {
        if(requested_timeout_dur < ewt) {
            // since we are not currently busy waiting try to wakeup during 
            // the busy-wait window
            timeout = requested_timeout - btw;
        } else {
            // as we approach timeout, continously wakeup in 
            // small increments to keep precision high (CPU less likely to 
            // power save) and increase the chance of waking up within the busy 
            // wait threshold
            timeout = now + ewt;
        }
    }

    return timeout;
}

hce::awt<bool> hce::timer::start_(
        hce::sid& sid, 
        const hce::chrono::time_point& timeout)
{
    HCE_TRACE_METHOD_ENTER("start_",sid,timeout);

    // allocate and construct the timer service awaitable
    auto awt = new hce::timer::awaitable;

    // allocate and construct timer using default `new` (don't need to steal 
    // from calling thread's memory cache
    auto t = new timer_(sid, timeout, awt);

    {
        std::lock_guard<hce::spinlock> lk(lk_);
        thread_guard_();

        timers_.push_back(t);
        timers_.sort([](timer_* lhs, timer_* rhs) {
            return lhs->timeout < rhs->timeout;
        });

        notify_();
    }

    // return the awaitable
    return hce::awt<bool>(awt);
}

hce::awt<bool> hce::timer::start_(hce::sid& sid, const hce::chrono::duration& dur) {
    HCE_LOW_METHOD_ENTER("start", sid, dur);
    return start_(sid, hce::chrono::now() + dur);
}

bool hce::timer::running_(const hce::sid& sid) {
    HCE_LOW_METHOD_ENTER("running",sid);
    bool result = false;

    {
        std::lock_guard<hce::spinlock> lk(lk_);

        if(runflag_) [[likely]] {
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

bool hce::timer::cancel_(const hce::sid& sid) {
    HCE_LOW_METHOD_ENTER("cancel",sid);
    bool result = false;

    if(sid) {
        // let unique_ptr call destructor
        std::unique_ptr<timer_> t;

        std::unique_lock<hce::spinlock> lk(lk_);

        if(runflag_) [[likely]] {
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

void hce::timer::notify_() {
    if(waiting_) {
        waiting_ = false;
        cv_.notify_one();
    } 
}

void hce::timer::run() {
    HCE_HIGH_METHOD_ENTER("run");
    hce::chrono::time_point now = hce::chrono::now();
    hce::chrono::time_point prev = now;
    hce::chrono::time_point timeout;
    hce::list<hce::timer::awaitable*> timed_out;

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
    while(runflag_) [[likely]] {
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
                    std::unique_ptr<timer_> t(*it);
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
