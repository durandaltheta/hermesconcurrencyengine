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

// local 
#include "utility.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"

namespace hce {

struct scheduler;

namespace detail {

// the true, current scheduler
scheduler*& tl_this_scheduler();

/*
 Generally the same as tl_this_scheduler(), but can be different based on the
 argument passed to scheduler::execute(). Useful for redirecting operations away 
 from the actual scheduler to some other scheduler.
 */ 
scheduler*& tl_this_scheduler_redirect();

}

/**
 @brief timer which can be scheduled on the scheduler with scheduler::start() 

 Timers contain suspended `hce::coroutine`s which will be scheduled by the 
 scheduler. If the timer reaches its timeout, the timeout coroutine will be 
 extracted and scheduled. If the timer is `hce::scheduler::cancel()`led the 
 cancel coroutine will be extracted and scheduled. If the `hce::scheduler` is 
 halted the timer will be simply deleted.
 */
struct timer {
    enum unit {
        hour,
        minute,
        second,
        millisecond,
        microsecond,
        nanosecond
    };

    struct id {
        /// return true if the id is allocated, else false
        inline operator bool() const { return (bool)ptr; }

        /// comparison based on allocated pointer uniqueness
        inline bool operator==(const id& rhs) const { return ptr == rhs.ptr; }

    private:
        // arbitrary word sized memory
        std::shared_ptr<bool> ptr;
        friend struct scheduler;
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
     @param as timeout and/or cancel operations(s)
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
     @param as timeout and/or cancel operation(s)
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
     @param as timeout coroutine and/or cancel operation(s)
     */
    template <typename... As>
    timer(unit u, size_t count, As&&... as) :
        time_point_(timer::now() + timer::to_duration(u, count))
    { 
        finalize_(std::forward<As>(as)...);
    }

    virtual ~timer(){}

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

    /// set the id of this timer
    inline void set_id(id i) { id_ = std::move(i); }

    /// return true if a given id matches the timer's id, else false
    inline bool compare_id(const id& i) const { return id_ == i; }

    /// the timeout time_point for the timer
    const std::chrono::steady_clock::time_point& time_point() const {
        return time_point_;
    }

    /// the operation for if the timer is times out
    inline void timeout() { timeout_(); }

    /// the operation for if the timer is cancelled
    inline void cancel() { cancel_(); }

private:
    inline void finalize_(std::function<void()> t, std::function<void()> c) {
        timeout_ = std::move(t);
        cancel_ = std::move(c);
    }

    inline void finalize_(std::function<void()> t) {
        timeout_ = std::move(t);
        cancel_ = []{};
    }

    const std::chrono::steady_clock::time_point time_point_;
    std::function<void()> timeout_;
    std::function<void()> cancel_;
    id id_;
    friend struct scheduler;
};

/** 
 @brief object responsible for scheduling and executing coroutines
 
 scheduler cannot be created directly, it must be created by calling 
 scheduler::make() 
    
 Scheduler API, unless otherwise specified, is threadsafe and coroutine-safe.
 That is, it can be called from anywhere safely, including from within a 
 coroutine running on the scheduler which is being accessed.
*/
struct scheduler {
    // implementation of coroutine::destination
    struct reschedule {
        inline void operator()(std::coroutine_handle<>&& h) {
            auto d = destination_.lock();
            if(d) { d->schedule(std::move(h)); }
        }

        std::weak_ptr<scheduler> destination_;
    };

    struct coroutine_called_run : public std::exception {
        coroutine_called_run() : 
            estr([]() -> std::string {
                std::stringstream ss;
                ss << "coroutine[0x" 
                   << (void*)&(base_coroutine::local())
                   << "] called scheduler::run()";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    /// an enumeration which represents the scheduler's current state
    enum state {
        ready, /// ready to execute coroutines
        running, /// run() has been called and is executing coroutines
        suspended, /// temporarily halted by a call to suspend()
        halted /// permanently halted by a call to halt() 
    };

    ~scheduler() {
        // ensure all tasks are manually deleted
        clear_queues_();
    }

    /**
     @brief construct a scheduler 
     @param io_count the minimum count of io worker threads
     @return an allocated and initialized scheduler 
     */
    static inline std::shared_ptr<scheduler> make() {
        scheduler* sp = new scheduler;
        std::shared_ptr<scheduler> s(sp);
        s->self_wptr_ = s;
        return s;
    };

    /// return `true` if calling code is running in a scheduler, else `false`
    static inline bool in() { return detail::tl_this_scheduler_redirect(); }

    /// return the currently running scheduler 
    static inline scheduler& local() {
        return *(detail::tl_this_scheduler_redirect());
    }

    /**
     @brief a running, process wide scheduler instance

     The instance will not which will not constructed until this operation is 
     first called.
     */
    static scheduler& global();

    /**
     Retrieve some scheduler, preferring scheduler::local(), falling back to 
     scheduler::global().
     */
    static inline scheduler& get() {
        return scheduler::in() ? scheduler::local() : scheduler::global();
    }

    /// return the state of the scheduler
    inline state get_state() {
        std::unique_lock<spinlock> lk(lk_);
        return state_;
    }
    
    /**
     @brief run the scheduler, continuously executing coroutines

     WARNING: It is an ERROR to call this method from inside a coroutine.

     This procedure can only be called by one caller at a time, and will block 
     the caller until `suspend()` or `halt()` is called. 

     Execution of coroutines by the caller of `run()` can be paused by calling 
     `suspend()`, causing `run()` to return `true`. Further calls to `run()` 
     will block until `resume()` is called.
     
     If `halt()` was previously called or `run()` is already evaluating 
     somewhere else will immediately return `false`.

     This function is heavily optimized as it is a processing bottleneck.

     @param redirect a scheduler to redirect scheduler::local() calls to
     @return `true` if `run()` was suspended by `suspend()`, else `false`
     */
    inline bool run(scheduler* redirect = nullptr) {
        // error out immediately if called improperly
        if(base_coroutine::in()) { throw coroutine_called_run(); }

        // stack variables 

        // the currently running coroutine
        base_coroutine co;

        // list of ready timers
        std::list<timer*> timers;

        // only call function tl_this_scheduler() once to acquire reference to 
        // thread shared scheduler pointer 
        scheduler*& tl_cs = detail::tl_this_scheduler();
        scheduler*& tl_cs_re = detail::tl_this_scheduler_redirect();

        // acquire the parent, if any, of the current coroutine scheduler
        scheduler* parent_cs = tl_cs;
        scheduler* parent_cs_re = tl_cs_re;
        
        // temporarily reassign thread_local this_scheduler state to this scheduler
        tl_cs = this; 
        tl_cs_re = redirect ? redirect : this; 
        redirect_ = tl_cs_re;

        // construct a new coroutine queue
        std::unique_ptr<std::deque<std::coroutine_handle<>>> coroutine_queue(
                new std::deque<std::coroutine_handle<>>);

        // push any remaining coroutines back into the main queue
        auto requeue_coroutines = [&] {
            for(auto c : *coroutine_queue) { coroutine_queue_->push_back(c); }
            coroutine_queue->clear();
        };

        // acquire the lock
        std::unique_lock<spinlock> lk(lk_);

        // block until no longer suspended
        while(state_ == suspended) { resume_block_(lk); }

        // If halt() has been called, return immediately only one caller of 
        // run() is possible. A double run() will cause a halt().
        if(state_ == ready) {
            // the current caller of run() claims this scheduler, any calls to 
            // run() while it is already running will halt the scheduler 
            state_ = running; 
        
            try {
                // evaluation loop
                while(can_continue_()) {
                    // acquire the current batch of coroutines by trading with 
                    // the scheduler's queue
                    std::swap(coroutine_queue, coroutine_queue_);

                    scheduled_ = coroutine_queue->size();
                    size_t count = scheduled_;

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

                    // execute a batch of coroutines
                    while(count) { 
                        // decrement from our initial batch count
                        --count;

                        // get a new task, don't need to swap
                        co.reset(coroutine_queue->front());
                        coroutine_queue->pop_front();

                        // execute coroutine
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
                    scheduled_ = 0;
                    requeue_coroutines();

                    // verify run state and block if necessary   
                    if(can_continue_()) {
                        if(coroutine_queue_->empty()) {
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

            halt_complete_ = true;

            // notify any listeners that the scheduler is halted
            halt_complete_cv_.notify_all();
            return false; 
        }
    }

    /** 
     @brief temporarily suspend any current or future calls to `run()` 

     A suspended `run()` call will return early with the value `true`. 
     Further `run()` calls can be attempted but will block without evaluating 
     any coroutines until `halt()` or `resume()` is called. 

     A simple usage of this feature is calling `run()` in a loop:
     ```
     while(my_scheduler->run()) { 
         // do other things after suspend()
     }

     // do other things after halt()
     ```

     Doing so allows coroutine execution on this scheduler to be "put to sleep" 
     until `resume()` is called elsewhere. IE, this blocks the entire scheduler,
     and all coroutines scheduled on it, until `resume()` is called.

     @return `false` if scheduler is halted, else `true`
     */
    inline bool suspend() {
        std::unique_lock<spinlock> lk(lk_);

        if(state_ == halted) { return false; }
        else {
            state_ = suspended;
            // wakeup scheduler if necessary from waiting for tasks to force 
            // run() to exit
            tasks_available_notify_();
            return true;
        }
    }

    /**
     @brief resume any current or future call to `run()` after an `suspend()`
     */
    inline void resume() {
        std::unique_lock<spinlock> lk(lk_);
        
        if(state_ == suspended) { 
            state_ = ready; 
            resume_notify_();
        }
    }

    /**
     @brief halt and join scheduler execution 

     This operation ends all current and future coroutine execution on the 
     scheduler.

     This operation also deletes any scheduled coroutines. Any coroutines
     scheduled on a halted scheduler are thrown out.
     
     As a general rule, `scheduler`s are intended to run indefinitely and 
     halt() should only be called on process shutdown. Failure to do so can 
     cause strange program errors where code which is expected to run does not.

     This operation is synchronized such that the scheduler will be completely 
     halted by the time `halt()` returns.
     */
    inline yield<void> halt() { 
        std::unique_lock<spinlock> lk(lk_);
        
        // set the scheduler to the permanent halted state
        state_ = halted;

        // wakeup from suspend if necessary
        resume_notify_();

        // called from outside this scheduler
        if(detail::tl_this_scheduler() != this) {
            // wakeup scheduler if necessary
            tasks_available_notify_();

            // handle case where halt() is called from another std::thread
            if(!base_coroutine::in()) {
                while(!halt_complete_){ halt_complete_cv_.wait(lk); }
            }
        }
                
        return {};
    }

    /**
     @brief Schedule allocated coroutines

     Arguments to this function can be:
     - a `coroutine` 
     - an iterable container of `coroutine`s

     Multi-argument schedule()s hold the scheduler's lock throughout (they are 
     simultaneously scheduled).

     @param a the first argument
     @param as any remaining arguments
     */
    template <typename A, typename... As>
    void schedule(A&& a, As&&... as) {
        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) {
            schedule_(std::forward<A>(a), std::forward<As>(as)...);
        }
    }
    
    /**
     @brief schedule a coroutine and await the `co_return`ed value_

     It is an error if the `co_return`ed value from the coroutine cannot be 
     converted to expected type `T`.

     @brief await until the coroutine completes
     @param t reference to type T where the result of the coroutine should be assigned
     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will return an allocated `std::optional<T>` if the coroutine completed, else an unallocated one (operator bool() == false)
     */
    template <typename T>
    awaitable<std::optional<T>> await(coroutine<T> co) {
        struct awt : public awaitable<std::optional<T>>::implementation {
            awt(bool ready) : ready_(ready), lk_(slk_) {}

        protected:
            inline std::unique_lock<spinlock>& get_lock() { return lk_; }
            inline bool ready_impl() { return ready_; }

            inline void resume_impl(void* m) { 
                if(m) { 
                    *t_ = std::optional<T>{ std::move(*((T*)m)) }; 
                    completed_ = true;
                }
            }

            inline std::optional<T> return_impl() { return std::move(t_); }
        
            inline base_coroutine::destination acquire_destination() {
                return reschedule{ scheduler::local() };
            }

        private:
            bool ready_;
            spinlock slk_;
            std::unique_lock<spinlock> lk_;
            std::optional<T> t_;
            bool completed_ = false;
        };

        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) {
            auto a = new awt(false);

            co.promise().install([a](coroutine<T>::promise_type* p) mutable {
                // pass the coroutine result to the awaitable
                a->resume(&(p->result));
            });

            schedule_(std::move(co));
            lk.unlock();

            /// returned awaitable resumes when the coroutine handle is destroyed
            return awaitable<std::optional<T>>(a);
        } else {
            // awaitable returns immediately
            return awaitable<std::optional<T>>(new awt(true));
        }
    }

    /**
     @brief schedule a coroutine and await until it completes 

     This is a useful operation whenever a coroutine needs to block on another 
     coroutine (without using other mechanisms like channels).

     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will return true if the coroutine completed, else false
     */
    inline awaitable<bool> await(coroutine<void> co) {
        struct awt : public awaitable<bool>::implementation {
            awt(bool ready) : ready_(ready), lk_(slk_) { }

        protected:
            inline std::unique_lock<spinlock>& get_lock() { return lk_; }
            inline bool ready_impl() { return ready_; }
            inline void resume_impl(void* m) { completed_ = (bool)m; }
            inline bool result_impl() { return completed_; }
        
            inline base_coroutine::destination acquire_destination() {
                return reschedule{ scheduler::local() };
            }

        private:
            bool ready_;
            spinlock slk_;
            std::unique_lock<spinlock> lk_;
            bool completed_ = false;
        };

        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) {
            auto a = new awt(false);

            co.promise().install([a](coroutine<void>::promise_type* pt) mutable {
                // resume the blocked awaitable
                a->resume((void*)1);
            });

            schedule_(std::move(co));
            lk.unlock();

            /// returned awaitable resumes when the coroutine handle is destroyed
            return awaitable<bool>(a);
        } else {
            // awaitable returns immediately
            return awaitable<bool>(new awt(true));
        }
    }

    /**
     @brief schedule a timer
     @param t a timer 
     @return if successful an allocated timer::id, else an unallocated one
     */
    inline timer::id start(std::unique_ptr<timer> t) {
        timer::id id;

        std::unique_lock<spinlock> lk(lk_);

        // ensure state is good and timer is allocated
        if(state_ != halted && t) {
            id.ptr = std::make_shared<bool>(false);
            t->set_id(id);
            timers_.push_back(t.release());

            // need to notify the caller of run() if our new timeout is sooner 
            // than the previous soonest
            auto do_notify = t->time_point() < timers_.front()->time_point();

            // re-sort based on timer::operator<
            timers_.sort([](timer*& lt, timer*& rh) { return *lt < *rh; });

            if(do_notify) { tasks_available_notify_(); }
        }

        return id;
    }

    /// cancel a scheduled timer 
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
                t->cancel();
                delete t;
            }
        } 
    }

    /// return the count of scheduled coroutines
    inline size_t measure() {
        std::lock_guard<spinlock> lk(lk_);
        return scheduled_ + coroutine_queue_->size();
    }

    /// set the redirect pointer and block till complete
    inline awaitable<bool> redirect(scheduler* redir) {
        struct awt : public awaitable<bool>::implementation { 
            awt(std::unique_lock<spinlock>&& lk) : lk_(std::move(lk)) { }

        protected:
            inline std::unique_lock<spinlock>& get_lock() { return lk_; }
            inline bool ready_impl() { return true; }
            inline void resume_impl(void* m) { } 
            inline bool result_impl() { return true; }
        
            inline base_coroutine::destination acquire_destination() {
                return reschedule{ scheduler::local() };
            }

        private:
            std::unique_lock<spinlock> lk_;
        };

        std::unique_lock<spinlock> lk(lk_);

        if(redirect_ == redir) {
            return awaitable<bool>(new awt(std::move(lk)));
        } else {
            return await(scheduler::redirect_co(redir));
        }
    }

    /// return a copy of this scheduler's shared pointer by conversion
    inline operator std::shared_ptr<scheduler>() { return self_wptr_.lock(); }

    /// return a copy of this scheduler's weak pointer by conversion
    inline operator std::weak_ptr<scheduler>() { return self_wptr_; }
 
private:
    scheduler() : 
            state_(ready), // state_ persists between suspends
            coroutine_queue_(new std::deque<std::coroutine_handle<>>) { // persists as necessary
        reset_flags_(); // initialize flags
    }

    // Reset scheduler state flags, etc. Does not reset scheduled coroutine
    // queues. 
    //
    // This method can ONLY be safely called by the constructor or run()
    inline void reset_flags_() {
        scheduled_ = 0;
        halt_complete_ = false;
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
            t->cancel();
            delete t;
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
    
    // schedule an individual coroutine handle
    inline void schedule_coroutine_(base_coroutine c) {
        if(c && !(c.done())) { coroutine_queue_->push_back(c.release()); }
    }
   
    template <typename T>
    inline void schedule_coroutine_(coroutine<T> c) {
        schedule_coroutine_(base_coroutine(c.release()));
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

    static inline coroutine<void> redirect_co(scheduler* redir) {
        detail::tl_this_scheduler_redirect() = redir;
        co_return;
    }

    // synchronization primative
    hce::spinlock lk_;

    // the current state of the scheduler
    state state_; 
                  
    // the count of scheduled coroutines on this scheduler 
    size_t scheduled_; 
   
    // condition data for when scheduler::resume() is called
    bool waiting_for_resume_;
    std::condition_variable_any resume_cv_;

    // condition data for when new tasks are available
    bool waiting_for_tasks_; // true if waiting on tasks_available_cv_
    std::condition_variable_any tasks_available_cv_;

    // condition data for when halt completes
    bool halt_complete_; 
    std::condition_variable_any halt_complete_cv_;

    // a weak_ptr to the scheduler's shared memory
    std::weak_ptr<scheduler> self_wptr_;

    // queue holding scheduled coroutines. Raw coroutine pointers are used 
    // internally because we don't want even the cost of rvalue swapping 
    // unique pointers during internal operations (potentially 3 instructions), 
    // when we can deep copy a word size value intead. This requires careful 
    // calls to `delete` when a coroutine goes out of scope in this object. 
    std::unique_ptr<std::deque<std::coroutine_handle<>>> coroutine_queue_;

    // list holding scheduled timers. This will be regularly resorted from 
    // soonest to latest timeout.
    std::list<timer*> timers_;

    scheduler* redirect_ = this;
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
 @brief call await() on a scheduler
 @param as arguments for scheduler::await()
 @return result of await()
 */
template <typename... As>
auto await(As&&... as) {
    return scheduler::get().await(std::forward<As>(as)...);
}

}

#endif
