//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SCHEDULER__
#define __HERMES_COROUTINE_ENGINE_SCHEDULER__ 

// c++
#include <memory>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <list>
#include <chrono>
#include <string>
#include <sstream>
#include <optional>
#include <cstdlib>

// local 
#include "utility.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"

namespace hce {

struct scheduler;

namespace detail {
namespace scheduler {

// the true, current scheduler
hce::scheduler*& tl_this_scheduler();

/*
 Generally the same as tl_this_scheduler(), but can be different. Useful for 
 redirecting operations away from the actual scheduler to some other scheduler.
 */ 
hce::scheduler*& tl_this_scheduler_redirect();

template <typename T>
using awt_interface = hce::awt<T>::interface;

/*
 An implementation of hce::awt<T>::interface capable of joining a coroutine 

 Requires implementation for awaitable::interface::destination().
 */
template <typename T>
struct joiner : 
    public hce::awaitable::lockable<
        //hce::awt<T>::interface,
        awt_interface<T>,
        hce::spinlock>
{
    /// resume immediately and return a constructed T
    template <typename... As>
    joiner(As&&... as) : 
        hce::awaitable::lockable<
            //awt<T>::interface,
            awt_interface<T>,
            hce::spinlock>(slk_,false),
        ready_(true),
        t_(std::forward<As>(as)...)
    { }

    joiner(hce::co<T>& co) :
        hce::awaitable::lockable<
            //awt<T>::interface,
            awt_interface<T>,
            hce::spinlock>(slk_,false),
        ready_(false)
    { 
        // install a cleanup handler to resume the returned awaitable when 
        // the coroutine goes out of scope
        co.promise().install([self=this](hce::co<T>::promise_type& p){
            // get a copy of the handle to see if the coroutine completed 
            auto handle = 
                std::coroutine_handle<typename hce::co<T>::promise_type>::from_promise(p);

            if(handle.done()) {
                // resume the blocked awaitable
                self->resume(&(p->result));
            } else {
                // resume with no result
                self->resume(nullptr);
            }
        });
    }

    inline bool on_ready() { return ready_; }

    inline void on_resume(void* m) { 
        ready_ = true;
        if(m) { *t_ = std::move(*((T*)m)); };
    }

    inline T get_result() { return std::move(t_); }

private:
    spinlock slk_;
    bool ready_;
    T t_;
};

/// variant for coroutine returning void, not reporting success
template <>
struct joiner<void> : 
    public hce::awaitable::lockable<
        //awt<void>::interface,
        awt_interface<void>,
        hce::spinlock>
{
    joiner() : 
        hce::awaitable::lockable<
            //awt<void>::interface,
            awt_interface<void>,
            hce::spinlock>(slk_,false),
        ready_(true)
    { }

    joiner(hce::co<void>& co) :
        hce::awaitable::lockable<
            //awt<void>::interface,
            awt_interface<void>,
            hce::spinlock>(slk_,false),
        ready_(false)
    { 
        // install a cleanup handler to resume the returned awaitable when 
        // the coroutine goes out of scope
        co.promise().install([self=this]() mutable { self->resume(nullptr); });
    }

    inline bool on_ready() { return ready_; }
    inline void on_resume(void* m) { ready_ = true; }

private:
    spinlock slk_;
    bool ready_;
};

}
}

/**
 @brief timer which can be scheduled on the scheduler with scheduler::start() 

 Timers contain suspended `hce::coroutine`s which will be scheduled by the 
 scheduler. If the timer reaches its timeout, the timeout operation will be 
 called. If the timer is `hce::scheduler::cancel()`led or object is destructed
 without calling timeout(), the cancel operation will be called instead. 
 */
struct timer {
    // Arbitrary word sized allocated memory. The uniquen address of this 
    // memory is used to associate a timer object with an id
    typedef std::shared_ptr<bool> id;

    // units of time for setting timeout point
    enum unit {
        hour,
        minute,
        second,
        millisecond,
        microsecond,
        nanosecond
    };

    timer() = delete; // must specify a timeout
    timer(const timer&) = delete;
    timer(timer&&) = default;
    timer& operator=(const timer&) = delete;
    timer& operator=(timer&&) = default;

    /**
     @brief construct a timer 

     Timeout and cancel opertions are any Callable (function pointer, Functor, 
     or lambda) which satisfies `std::function<void()>`.

     @param tp the time_point when timeout should be scheduled
     @param as timeout operation followed by optional cancel operation
     */
    template <typename... As>
    timer(const std::chrono::steady_clock::time_point& tp, As&&... as) :
        time_point_(tp)
    { 
        finalize_(std::forward<As>(as)...);
    }

    /**
     @brief construct a timer 

     Timeout and cancel opertions are any Callable (function pointer, Functor, 
     or lambda) which satisfies `std::function<void()>`.

     @param dur the duration till timeout should be scheduled
     @param as timeout operation followed by optional cancel operation
     */
    template <typename... As>
    timer(const std::chrono::steady_clock::duration& dur, As&&... as) :
        time_point_(timer::now() + dur)
    { 
        finalize_(std::forward<As>(as)...);
    }

    /**
     @brief construct a timer 

     Timeout and cancel opertions are any Callable (function pointer, Functor, 
     or lambda) which satisfies `std::function<void()>`.

     @param u the time unit to construct the timeout duration
     @param count the count of units to construct the timeout duration
     @param as timeout operation followed by optional cancel operation
     */
    template <typename... As>
    timer(unit u, size_t count, As&&... as) :
        time_point_(timer::now() + timer::to_duration(u, count))
    { 
        finalize_(std::forward<As>(as)...);
    }

    /// call cancel handler
    ~timer(){ if(!timeout_) { cancel_(); } }

    /// construct an allocated timer
    template <typename... As>
    static std::unique_ptr<timer> make(As&&... as) {
        return std::unique_ptr<timer>(new timer(std::forward<As>(as)...));
    }

    /// acquire the current time
    static inline std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    /// return a duration equivalent to the count of units
    static inline 
    std::chrono::steady_clock::duration 
    to_duration(unit u, size_t count) {
        switch(u) {
            case unit::hour:
                return std::chrono::hours(count);
                break;
            case unit::minute:
                return std::chrono::minutes(count);
                break;
            case unit::second:
                return std::chrono::seconds(count);
                break;
            case unit::millisecond:
                return std::chrono::milliseconds(count);
                break;
            case unit::microsecond:
                return std::chrono::microseconds(count);
                break;
            default:
                return std::chrono::milliseconds(0);
                break;
        }
    }

    /// timer timeout comparison
    inline bool operator<(const timer& rhs) { 
        return time_point() < rhs.time_point(); 
    }

    /// return the timeout time_point for the timer
    const std::chrono::steady_clock::time_point& time_point() const {
        return time_point_;
    }

    /// return true if a given id matches the timer's id, else false
    inline bool compare_id(const id& i) const { return id_ == i; }

    /// set the id of this timer
    inline void set_id(const id& i) { id_ = i; }
    
    /// set the id of this timer
    inline void set_id(id&& i) { id_ = std::move(i); }

    /// the operation for if the timer is times out
    inline void timeout() { 
        if(timeout_) {
            timeout_(); 
            timeout_ = std::function<void()>(); // reset timeout memory
        }
    }

private:
    template <typename TIMEOUT, typename CANCEL>
    inline void finalize_(TIMEOUT&& t, CANCEL&& c) {
        timeout_ = std::forward<TIMEOUT>(t);
        cancel_ = std::forward<CANCEL>(c);
    }

    template <typename TIMEOUT>
    inline void finalize_(TIMEOUT&& t) {
        timeout_ = std::forward<TIMEOUT>(t);
        cancel_ = []{}; // empty handler
    }

    const std::chrono::steady_clock::time_point time_point_;
    id id_;
    std::function<void()> timeout_;
    std::function<void()> cancel_;
    friend struct scheduler;
};

/** 
 @brief object responsible for scheduling and executing coroutines
 
 scheduler cannot be created directly, it must be created by calling 
 scheduler::make() 
    
 Scheduler API, unless otherwise specified, is threadsafe and coroutine-safe.
 That is, it can be called from anywhere safely, including from within a 
 coroutine running on the scheduler which is being accessed.

 Attempting scheduleing operations on a scheduler when it is halted will throw 
 an exception. This means that the user is responsible for ensuring their 
 coroutines are cleanly finished before the scheduler halts or the process 
 returns.
*/
struct scheduler {
    /// an enumeration which represents the scheduler's current state
    enum state {
        ready, /// ready to execute coroutines
        running, /// run() has been called and is executing coroutines
        suspended, /// temporarily halted by a call to suspend()
        halted /// permanently halted by lifecycle destructor
    };

    // implementation of awaitable::interface::destination()
    template <typename INTERFACE>
    struct reschedule : public INTERFACE {
        template <typename... As>
        reschedule(As&&... as) : 
            INTERFACE(std::forward<As>(as)...),
            destination_(scheduler::get()) 
        { }

        inline void destination(std::coroutine_handle<> h) {
            auto d = destination_.lock();
            if(d) { d->schedule(std::move(h)); }
        }

    private:
        // a weak_ptr to the scheduler we will reschedule on
        std::weak_ptr<scheduler> destination_;
    };

    struct scheduler_is_halted : public std::exception {
        scheduler_is_halted(scheduler* s, const char* method_name) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "hce::scheduler[0x" 
                   << (void*)s
                   << "] is halted, but operation[hce::scheduler::"
                   << method_name
                   << "] was called";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    struct coroutine_called_run : public std::exception {
        coroutine_called_run() : 
            estr([]() -> std::string {
                std::stringstream ss;
                ss << "coroutine[0x" 
                   << (void*)&(coroutine::local())
                   << "] called scheduler::run()";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    /**
     @brief object for controlling the state of schedulers 

     When this object is assigned via a call to:
     ```
     std::shared_ptr<scheduler> scheduler::make(std::unique_ptr<scheduler::lifecycle>&)
     ```

     It will be allocated and take ownership of the returned scheduler object.
     When that lifecycle object goes out of scope, the scheduler will be 
     permanently halted (no more coroutines will be executed).
     */
    struct lifecycle {
        /**
         @brief object responsible for destructing registered lifecycle instances at process termination
         */
        struct manager {
            /// access the process wide manager object
            static manager& instance();

            /// register a lifecycle pointer to be destroyed at process exit
            inline void registration(std::unique_ptr<lifecycle> lptr) {
                if(lptr) {
                    LOG_F(1, "hce::scheduler::lifecycle::manager::registration:[0x%p]",lptr.get());
                    std::lock_guard<hce::spinlock> lk(lk_);
                    if(!exited_) { lptrs_.push_back(std::move(lptr)); }
                }
            }

            /// call lifecycle::suspend() on all registered lifecycles
            inline void suspend() {
                std::lock_guard<hce::spinlock> lk(lk_);
                for(auto& lp : lptrs_) { lp->suspend(); }
            }

            /// call lifecycle::resume() on all registered lifecycles
            inline void resume() {
                std::lock_guard<hce::spinlock> lk(lk_);
                for(auto& lp : lptrs_) { lp->suspend(); }
            }

        private:
            manager(){ std::atexit(manager::atexit); }
            ~manager() {
                LOG_F(1, "hce::scheduler::lifecycle::manager::~manager()");
                exited_ = true;
            }

            static inline void atexit() { manager::instance().exit(); }

            inline void exit() {
                LOG_F(1, "hce::scheduler::lifecycle::manager::exit()");
                std::lock_guard<hce::spinlock> lk(lk_);
                if(!exited_) {
                    exited_ = true;
                    // exit called early, clear data
                    lptrs_.clear();
                }
            }

            hce::spinlock lk_;
            bool exited_ = false;
            std::deque<std::unique_ptr<lifecycle>> lptrs_;
        };

        /**
         When the scheduler's associated lifecycle goes out of scope, the 
         scheduler is permanently halted.
         */
        ~lifecycle(){ parent_->halt_(); }

        /// return the associated scheduler
        inline hce::scheduler& scheduler() { return *parent_; }

        /**
         @brief temporarily suspend a scheduler executing coroutines

         A suspended scheduler will cease processing coroutines, execute any 
         installed `config::on_suspend` handlers, and then block until either 
         `scheduler::resume()` is called or the scheduler's associated `lifecycle`
         object goes out of scope. 
         */
        inline void suspend() { return parent_->suspend_(); }

        /**
         @brief resume coroutine execution after a call to `suspend()`
         */
        inline void resume() { return parent_->resume_(); }

    private:
        lifecycle() = delete;
        lifecycle(std::shared_ptr<hce::scheduler>& parent) : parent_(parent) {}

        std::shared_ptr<hce::scheduler> parent_;
        friend struct hce::scheduler;
    };

    /**
     @brief object for configuring an installed scheduler 

     An instance of this object passed to `scheduler::install()` will configure 
     the scheduler with the given event handlers and options.
     */
    struct config {
        typedef std::function<void(hce::scheduler&)> handler;

        struct handlers {
            ~handlers() {
                // clear handlers in reverse install order
                while(hdls_.size()) { hdls_.pop_back(); }
            }

            /// install a handler
            inline void install(hce::thunk th) {
                hdls_.push_back([th=std::move(th)](hce::scheduler&){ th(); });
            }

            /// install a handler
            inline void install(handler h) {
                hdls_.push_back(std::move(h));
            }

            /// call all handlers with the given scheduler
            inline void call(hce::scheduler& sch) {
                for(auto& hdl : hdls_) {
                    hdl(sch);
                }
            }

        private:
            std::list<handler> hdls_;
        };

        /// return an allocated and constructed config
        static inline std::unique_ptr<config> make() { 
            return std::unique_ptr<config>{ new config }; 
        }
        
        /**
         Handlers to be called on initialization during a call 
         to `hce::scheduler::install()`.
         */
        handlers on_init;

        /**
         Handlers to be called when a scheduler is `hc::scheduler::suspend()`ed 
         during a call to `hce::scheduler::install()`.
         */
        handlers on_suspend;

        /**
         Handlers to be called when a scheduler is `hc::scheduler::halt_()`ed 
         during a call to `hce::scheduler::install()`.
         */
        handlers on_halt;

    private:
        config(){}
    };

    ~scheduler() {
        LOG_F(1, "hce::scheduler::~scheduler()");
        // ensure all tasks are manually deleted
        clear_queues_();
    }

    /// return a copy of this scheduler's shared pointer by conversion
    inline operator std::shared_ptr<scheduler>() { return self_wptr_.lock(); }

    /// return a copy of this scheduler's weak pointer by conversion
    inline operator std::weak_ptr<scheduler>() { return self_wptr_; }

    /**
     @return `true` if calling thread is running a scheduler, else `false`
     */
    static inline bool in() { 
        return detail::scheduler::tl_this_scheduler_redirect(); 
    }

    /**
     @brief retrieve the calling thread's scheduler
     @return a scheduler reference
     */
    static inline scheduler& local() {
        return *(detail::scheduler::tl_this_scheduler_redirect());
    }

    /**
     @brief access the running, process wide scheduler instance

     The instance will not which will not constructed until this operation is 
     first called.

     @return the global scheduler
     */
    static scheduler& global();

    /**
     @brief retrieve some scheduler

     Prefer scheduler::local(), falling back to scheduler::global().

     @return a scheduler reference
     */
    static inline scheduler& get() {
        return scheduler::in() ? scheduler::local() : scheduler::global();
    }

    /**
     @brief construct an allocated scheduler 

     `scheduler::make()` calls are the only way to make a new scheduler. 

     This variant will allocate and construct the argument lifecycle pointer 
     with the newly allocated and constructed scheduler.

     It is generally a bad idea to allow the lifecycle pointer to go out of 
     scope (causing the scheduler to halt) before all operations executed 
     through the scheduler are completed cleanly. As such, it is often a good 
     idea to register schedulers with the `lifecycle::manager` instance (IE, 
     call `scheduler::make()` without an argument lifecycle pointer to do this 
     automatically) so they will be automatically halted on program exit.

     @param lc reference to a lifecycle unique pointer
     @return an allocated and initialized scheduler 
     */
    static inline std::shared_ptr<scheduler> make(std::unique_ptr<lifecycle>& lc) {
        scheduler* sp = new scheduler();
        std::shared_ptr<scheduler> s(sp);
        s->self_wptr_ = s;
        LOG_F(1, "hce::scheduler::make::new scheduler[0x%p]",s.get());
        lc = std::unique_ptr<lifecycle>(new lifecycle(s));
        LOG_F(1, "hce::scheduler::make::new lifecycle:[0x%p]",lc.get());
        return s;
    }

    /**
     @brief construct an allocated scheduler

     This variant automatically registers a `lifecycle` with the 
     `lifecycle::manager` instance to be halted when the process exits.

     @return an allocated and initialized scheduler 
     */
    static inline std::shared_ptr<scheduler> make() {
        std::unique_ptr<lifecycle> lc;
        auto s = make(lc);
        lifecycle::manager::instance().registration(std::move(lc));
        return s;
    };

    /// return the state of the scheduler
    inline state status() {
        std::unique_lock<spinlock> lk(lk_);
        return state_;
    }

    /**
     @brief install and run a scheduler on the current thread, continuously executing coroutines

     Once called, this will not return until `halt_()` is called or process 
     shutdown (in the case the the scheduler was constructed with 
     `hce::scheduler::make(true)`, in which case `halt_()` will be called by 
     the framework).

     WARNING: It is a generally an ERROR to call this method from inside a 
     coroutine.

     Execution of coroutines by the caller of `install()` can be paused by 
     calling `lifecycle::suspend()` and will block until `lifecycle::resume()` 
     (or until the scheduler is halted by the lifecycle object going out of 
     scope).

     @param config optional configuration for the installed scheduler
     */
    inline void install(std::unique_ptr<config> c = {}) {
        if(c) {
            c->on_init.call(*this);

            while(run_()) { 
                c->on_suspend.call(*this);
            } 

            c->on_halt.call(*this);
        } else { while(run_()) { } }
    }

    /**
     @brief Schedule allocated coroutines

     Arguments to this function can be:
     - an `hce::coroutine` or `hce::co<T>`
     - an iterable container of `hce::coroutine`s or `hce::co<T>`s

     Multi-argument schedule()s hold the scheduler's lock throughout (they are 
     simultaneously scheduled).

     @param a the first argument
     @param as any remaining arguments
     @return false if the scheduler is halted, else true
     */
    template <typename A, typename... As>
    void schedule(A&& a, As&&... as) {
        std::unique_lock<spinlock> lk(lk_);

        if(state_ == halted) { 
            scheduler_is_halted e(this,"schedule()");
            LOG_F(ERROR, e.what());
            throw e; 
        }

        schedule_(std::forward<A>(a), std::forward<As>(as)...);
    }

    /**
     @brief return an awaitable which joins a coroutine and returns a constructed type `T`

     This mechanism does not schedule a coroutine, it is exposed to allow 
     joining in cases where a coroutine is not able to be scheduled.

     The returned awaitable does not need to be `co_await`ed immediately, the 
     coroutine can be scheduled for execution first.

     If argument is a reference to a `co<T>`, the awaitable will join 
     with the coroutine completing. Otherwise the arguments are passed to the 
     result `T`'s constructor and the awaitable resumes immediately.

     @param as either a hce::co<T> or optional arguments to T
     @return an awaitable capable of joining the coroutine 
     */
    template <typename T, typename... As>
    static hce::awt<T> joinable(As&&... as) {
        return hce::awt<T>(
            new hce::scheduler::reschedule<
                hce::detail::scheduler::joiner<T>>(std::forward<As>(as)...));
    }

    /**
     @brief schedule a coroutine and return an awaitable to await the `co_return`ed value

     It is an error if the `co_return`ed value from the coroutine cannot be 
     converted to expected type `T`.

     @brief await until the coroutine completes
     @param t reference to type T where the result of the coroutine should be assigned
     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will return an allocated `std::optional<T>` if the coroutine completed, else an unallocated one (operator bool() == false)
     */
    template <typename T>
    awt<T> join(hce::co<T> co) {
        std::unique_lock<spinlock> lk(lk_);

        if(state_ == halted) { 
            scheduler_is_halted e(this,"join()");
            LOG_F(ERROR, e.what());
            throw e; 
        }

        auto awt = scheduler::joinable<T>(co);
        schedule_(std::move(co));
        lk.unlock();

        /// returned awaitable resumes when the coroutine handle is destroyed
        return awt;
    }

    /**
     @brief schedule a coroutine and return an awaitable to await the coroutine completing

     This is a useful operation whenever a coroutine needs to block on another 
     coroutine (without using other mechanisms like channels).

     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will return true if the coroutine completed, else false
     */
    inline awt<void> join(hce::co<void> co) {
        std::unique_lock<spinlock> lk(lk_);

        if(state_ == halted) { 
            scheduler_is_halted e(this,"join()");
            LOG_F(ERROR, e.what());
            throw e; 
        }

        auto awt = scheduler::joinable<void>(co);
        schedule_(std::move(co));
        lk.unlock();

        /// returned awaitable resumes when the coroutine handle is destroyed
        return awt;
    }

    /**
     @brief schedule a timer 

     Timers are required to be allocated unique pointers for internal processing 
     speed.

     @param t a timer 
     @return if successful an allocated timer::id, else an unallocated one
     */
    inline timer::id start(std::unique_ptr<timer> t) {
        timer::id id;

        std::unique_lock<spinlock> lk(lk_);

        // ensure state is good and timer is allocated
        if(state_ == halted) { 
            scheduler_is_halted e(this,"start()");
            LOG_F(ERROR, e.what());
            throw e; 
        }

        id = std::make_shared<bool>(false);
        t->set_id(id);

        // need to notify the caller of run() if our new timeout is sooner 
        // than the previous soonest
        auto do_notify = t->time_point() < timers_.front()->time_point();

        // insert the timer into the queue
        timers_.push_back(t.release());

        // re-sort based on timer::operator<
        timers_.sort([](timer*& lt, timer*& rh) { return *lt < *rh; });

        if(do_notify) { tasks_available_notify_(); }

        return id;
    }

    /**
     @brief cancel a scheduled timer if it is not yet executing
     */
    inline void cancel(timer::id id) {
        if(id) {
            std::unique_lock<spinlock> lk(lk_);

            timer* t = nullptr;
            auto it = timers_.begin();
            auto end = timers_.end();

            while(it!=end) {
                if((*it)->compare_id(id)) {
                    t = *it;
                    timers_.erase(it);
                    break;
                }

                ++it;
            }

            lk.unlock();

            if(t) {
                delete t; // destructor calls cancel operation
            }
        } 
    }

    /// return the count of scheduled coroutines
    inline size_t measure() {
        std::lock_guard<spinlock> lk(lk_);
        return evaluating_ + coroutine_queue_->size();
    }

    /**
     @brief schedule a coroutine to set the redirect pointer 

     Redirect operations need to be well understood by callers of this code. If
     usage of this feature is not required by user code it *should not be used*.
     Redirection is only safe to use when executing on privately managed 
     schedulers.

     Redirecting a scheduler causes certain API to point to the *specified* 
     scheduler:
     ```
     scheduler::in() // returns true if redirect pointer is not null
     scheduler::local() // dereferences the redirect pointer
     ```

     Modifying the behavior of the above functions in this causes features which 
     build upon them to access the redirected scheduler instead of the running 
     scheduler. 

     As an example, awaitables returned by function calls in this library resume 
     suspended coroutines by inheritting `scheduler::reschedule` object. This 
     object calls `scheduler::local()` to store a destination to reschedule a 
     coroutine on resumption. If `scheduler::redirect()` is used, this will 
     cause resumption to schedule the coroutine on the *other* scheduler 
     instead.

     Redirection will only be valid by coroutines executing on this scheduler 
     that were scheduled *after* this redirect operation's scheduled coroutine 
     executes. 

     This means that the user must synchronize their schedule calls so that 
     redirect is called before the dependent coroutines are scheduled. 
     Additionally, they are responsible for ensuring that the scheduler code is 
     being redirected to remains in existence, and not halted, as long as 
     necessary. 
     */
    inline void redirect(scheduler* redir) {
        return schedule(scheduler::redirect_co(redir));
    }

    /**
     @brief schedule a coroutine to set the redirect pointer 

     This operation can be called to reset the redirect pointer back to the 
     original (and safe) value.
     */
    inline void redirect() { return redirect(this); }
 
private:
    scheduler() : 
            state_(ready), // state_ persists between suspends
            coroutine_queue_(new std::deque<std::coroutine_handle<>>) { // persists as necessary
        reset_flags_(); // initialize flags
    }

    /*
     Run the scheduler.

     This function is heavily optimized as it is a processing bottleneck.
     */
    inline bool run_() {
        // error out immediately if called improperly
        if(coroutine::in()) { throw coroutine_called_run(); }

        // stack variables 

        // the currently running coroutine, ensures handle is managed properly
        coroutine co;

        // list of ready timers
        std::list<timer*> timers;

        // only call function tl_this_scheduler() once to acquire reference to 
        // thread shared scheduler pointer 
        scheduler*& tl_cs = detail::scheduler::tl_this_scheduler();
        scheduler*& tl_cs_re = detail::scheduler::tl_this_scheduler_redirect();

        // acquire the parent, if any, of the current coroutine scheduler
        scheduler* parent_cs = tl_cs;
        scheduler* parent_cs_re = tl_cs_re;
        
        // temporarily reassign thread_local this_scheduler state to this scheduler
        tl_cs = this; 
        tl_cs_re = this; 

        // construct a new coroutine queue
        std::unique_ptr<std::deque<std::coroutine_handle<>>> coroutine_queue(
                new std::deque<std::coroutine_handle<>>);

        // Push any remaining coroutines back into the main queue. Lock must be 
        // held before this is called.
        auto requeue_coroutines = [&] {
            for(auto c : *coroutine_queue) { coroutine_queue_->push_back(c); }
            coroutine_queue->clear();
        };

        // acquire the lock
        std::unique_lock<spinlock> lk(lk_);

        // block until no longer suspended
        while(state_ == suspended) { resume_block_(lk); }

        // If halt_() has been called, return immediately only one caller of 
        // run() is possible. A double run() will cause a halt_().
        if(state_ == ready) {
            // the current caller of run() claims this scheduler, any calls to 
            // run() while it is already running will halt the scheduler 
            state_ = running; 

            // flag to control evaluation loop
            bool evaluate = true;
            size_t count = 0;

            try {
                // evaluation loop
                while(evaluate) {
                    if(coroutine_queue_->size()) {
                        // acquire the current batch of coroutines by trading 
                        // with the scheduler's queue, reducing lock contention 
                        // by collecting the entire current batch
                        std::swap(coroutine_queue, coroutine_queue_);
                        evaluating_ = coroutine_queue->size();
                        count = evaluating_;
                    }

                    // check for any ready timers 
                    if(timers_.size()) {
                        auto now = timer::now();
                        auto it = timers_.begin();
                        auto end = timers_.end();

                        while(it!=end) {
                            // check if a timer is ready to timeout
                            if((*it)->time_point() <= now) {
                                timers.push_back(*it);
                                it = timers_.erase(it);
                            } else {
                                // no remaining ready timers, exit loop
                                break;
                            }
                        }
                    }
                        
                    // unlock scheduler state when running a task
                    lk.unlock();

                    // evaluate a batch of coroutines
                    while(count) { 
                        // decrement from our initial batch count
                        --count;

                        // get a new task
                        co.reset(coroutine_queue->front());
                        coroutine_queue->pop_front();

                        // evaluate coroutine
                        co.resume();

                        // check if the coroutine yielded
                        if(co && !co.done()) {
                            // re-enqueue coroutine 
                            coroutine_queue->push_back(co.release()); 
                        } 
                    }

                    // call timeout for all timers
                    while(timers.size()) {
                        auto t = timers.front();
                        timers.pop_front();
                        t->timeout();
                        delete t;
                    }

                    // reacquire lock
                    lk.lock(); 

                    // cleanup batch results
                    evaluating_ = 0;
                    requeue_coroutines();

                    // verify run state and block if necessary   
                    if(coroutine_queue_->empty()) {
                        if(can_continue_()) {
                            if(timers_.empty()) {
                                // wait for more tasks
                                waiting_for_tasks_ = true;
                                tasks_available_cv_.wait(lk);
                            } else {
                                // wait, at a maximum, till the next scheduled 
                                // timer timeout
                                waiting_for_tasks_ = true;
                                tasks_available_cv_.wait_until(
                                    lk, 
                                    timers_.front()->time_point());
                            }
                        } else {
                            // break out of the evaluation loop
                            evaluate=false;
                        }
                    }

                    update_running_state_();
                }
            } catch(...) { // catch all other exceptions 
                // reset state in case of uncaught exception
                tl_cs = parent_cs; 
                tl_cs_re = parent_cs_re;

                // it is an error in this framework if an exception occurs when 
                // the lock is held, it should only be in user code that this 
                // can occur
                lk.lock();

                requeue_coroutines();

                lk.unlock();

                std::rethrow_exception(std::current_exception());
            }
        }

        // restore parent thread_local this_scheduler state
        tl_cs = parent_cs; 
        tl_cs_re = parent_cs_re;

        // move any coroutines in the local queue to the object queue
        requeue_coroutines();

        if(state_ == suspended) {
            // reset scheduler state so run() can be called again
            reset_flags_();
            return true;
        } else {
            // clear task queue so coroutine controlled memory can be released 
            clear_queues_();
            return false; 
        }
    }

    inline void suspend_() {
        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) { 
            state_ = suspended;
            // wakeup scheduler if necessary from waiting for tasks to force 
            // run() to exit
            tasks_available_notify_();
        }
    }

    inline void resume_() {
        std::unique_lock<spinlock> lk(lk_);
       
        if(state_ == suspended) { 
            state_ = ready; 
            resume_notify_();
        }
    }

    /*
     Halt the scheduler.

     This operation ends all current and future coroutine execution on the 
     scheduler. `halt_()` can be called multipled times without error.

     This operation also deletes any scheduled coroutines. Any coroutines
     scheduled on a halted scheduler are thrown out.
     
     As a general rule, `scheduler`s are intended to run indefinitely and 
     `halt_()` should only be called on process shutdown. Failure to do so can 
     cause strange program errors where code which is expected to run does not.
     */
    inline void halt_() { 
        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) {
            // set the scheduler to the permanent halted state
            state_ = halted;

            // wakeup from suspend if necessary
            resume_notify_();

            // called from outside this scheduler
            if(detail::scheduler::tl_this_scheduler() != this) {
                // wakeup scheduler if necessary
                tasks_available_notify_();
            }
        }
    }

    // Reset scheduler state flags, etc. Does not reset scheduled coroutine
    // queues. 
    //
    // This method can ONLY be safely called by the constructor or run()
    inline void reset_flags_() {
        evaluating_ = 0;
        waiting_for_resume_ = false;
        waiting_for_tasks_ = false;
    }

    // abstract frequently used inlined state to this function for correctness
    inline bool can_continue_() { return state_ < suspended; }

    // safely clear all our queues
    void clear_queues_() {
        while(coroutine_queue_->size()) {
            coroutine_queue_->front().destroy();
            coroutine_queue_->pop_front();
        }

        while(timers_.size()) {
            auto t = timers_.front();
            timers_.pop_front();
            delete t; // destructor calls cancel operation
        }
    }

    // attempt to wait for resumption
    template <typename LOCK>
    void resume_block_(LOCK& lk) {
        waiting_for_resume_ = true;
        resume_cv_.wait(lk);
    }
    
    // notify caller of run() that resume() has been called 
    inline void resume_notify_() {
        // only do notify if necessary
        if(waiting_for_resume_) {
            waiting_for_resume_ = false;
            resume_cv_.notify_one();
        }
    }

    inline void update_running_state_() {
        // reacquire running state in edgecase where suspend() and resume()
        // happened quickly
        if(state_ == ready) { state_ = running; }
    }

    // notify caller of run() that tasks are available
    inline void tasks_available_notify_() {
        // only do notify if necessary
        if(waiting_for_tasks_) { 
            waiting_for_tasks_ = false; 
            tasks_available_cv_.notify_one(); 
        }
    }
    
    inline void schedule_coroutine_(std::coroutine_handle<> h) {
        if(!(h.done())) { coroutine_queue_->push_back(h); }
    }
    
    // schedule an individual coroutine's handle
    inline void schedule_coroutine_(coroutine c) {
        if(c) { schedule_coroutine_(c.release()); }
    }
   
    // schedule an individual templated coroutine's handle
    template <typename T>
    inline void schedule_coroutine_(co<T> c) {
        if(c) { schedule_coroutine_(c.release()); }
    }

    // when all coroutines are scheduled, notify
    inline void schedule_() { tasks_available_notify_(); }

    template <typename A, typename... As>
    void schedule_(A&& a, As&&... as) {
        // detect if A is a container or a Stackless
        schedule_fallback_(
                detail::is_container<typename std::decay<A>::type>(),
                std::forward<A>(a));
        schedule_(std::forward<As>(as)...);
    }

    template <typename Container>
    void schedule_fallback_(std::true_type, Container&& coroutines) {
        for(auto& c : coroutines) {
            schedule_coroutine_(std::move(c));
        }
    }

    template <typename Coroutine>
    void schedule_fallback_(std::false_type, Coroutine&& c) {
        schedule_coroutine_(std::move(c));
    }

    static inline co<void> redirect_co(scheduler* redir) {
        detail::scheduler::tl_this_scheduler_redirect() = redir;
        co_return;
    }

    // synchronization primative
    hce::spinlock lk_;

    // the current state of the scheduler
    state state_; 
                  
    // the count of executing coroutines on this scheduler 
    size_t evaluating_; 
   
    // condition data for when scheduler::resume() is called
    bool waiting_for_resume_;
    std::condition_variable_any resume_cv_;

    // condition data for when new tasks are available
    bool waiting_for_tasks_; // true if waiting on tasks_available_cv_
    std::condition_variable_any tasks_available_cv_;

    // a weak_ptr to the scheduler's shared memory
    std::weak_ptr<scheduler> self_wptr_;

    // Queue holding scheduled coroutine handles. Simpler to just use underlying 
    // data than wrapper conversions between hce::coroutine/hce::co<T>. This 
    // object is a unique_ptr because it is routinely swapped between this 
    // object and the stack memory of the caller of scheduler::run_().
    std::unique_ptr<std::deque<std::coroutine_handle<>>> coroutine_queue_;

    // list holding scheduled timers. This will be regularly resorted from 
    // soonest to latest timeout. Timer pointers are allocated and must be 
    // deleted by this object when they go out of scope.
    std::list<timer*> timers_;

    friend struct lifecycle;
};

/**
 @brief call schedule() on a scheduler
 @param as arguments for scheduler::schedule()
 */
template <typename... As>
void schedule(As&&... as) {
    scheduler::get().schedule(std::forward<As>(as)...);
}

/**
 @brief call join() on a scheduler
 @param as arguments for scheduler::join()
 @return result of join()
 */
template <typename... As>
auto join(As&&... as) {
    return scheduler::get().join(std::forward<As>(as)...);
}

}

#endif
