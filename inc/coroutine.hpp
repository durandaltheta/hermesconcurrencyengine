//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_COROUTINE__
#define __HERMES_COROUTINE_ENGINE_COROUTINE__

// c++
#include <memory>
#include <mutex>
#include <condition_variable_any>
#include <coroutine>
#include <exception>
#include <any>
#include <deque>
#include <list>
#include <chrono>
#include <functional>
#include <string>
#include <stringstream>

// local 
#include "utility.hpp"
#include "atomic.hpp"

namespace hce {

// forward declarations
struct scheduler;
struct coroutine;

namespace detail {

// always points to the running coroutine
coroutine*& tl_this_coroutine();

// the true, current scheduler
scheduler*& tl_this_scheduler();

/*
 Generally the same as tl_this_scheduler(), but can be different based on the
 argument passed to scheduler::execute(). Useful for redirecting operations away 
 from the actual scheduler to some other scheduler.
 */ 
scheduler*& tl_this_scheduler_redirect();

}

/// Returns true if executing in a coroutine, else false
inline bool in_coroutine() {
    return detail::tl_this_coroutine() ? true : false; 
}

/// return the currently running coroutine 
inline coroutine& this_coroutine() {
    return *(detail::tl_this_coroutine()); 
}

/// return `true` if calling code is running in a scheduler, else `false`
inline bool in_scheduler() {
    return detail::tl_this_scheduler_redirect();
}

/// return the currently running scheduler 
inline scheduler& this_scheduler() {
    return *(detail::tl_this_scheduler_redirect());
}

//-----------------------------------------------------------------------------
// coroutine
//-----------------------------------------------------------------------------

/** 
 @brief stackless coroutine object

 User stackless coroutine implementations must return this object. They are 
 resumable in `scheduler` instances.
 */
struct coroutine {
    /**
     The promise object for stackless coroutines, used primarily by the compiler 
     when it constructs stackless coroutines.
     */
    struct promise_type {
        struct cleanup {
            typedef std::function<void(promise_type*)> handler;

            cleanup(promise_type* pt) : promise_(pt) {}
            ~cleanup() { for(auto& h : handlers) { h(promise_); } }

            inline void install(handler&& h) { 
                handlers_.push_back(std::move(h)); 
            }

        private:    
            promise_type* promise_;
            std::deque<handler> handlers_; 
        };

        ~promise_type() { 
            // ensure cleanup handlers execute before promise_type is destroyed
            cleanup_.reset(); 
        }

        inline coroutine get_return_object() {
            return { handle_type::from_promise(*this); };
        }

        inline std::suspend_always initial_suspend() { return {}; }
        inline std::suspend_always final_suspend() noexcept { return {}; }
        inline void return_void() {}

        // store the arbitrary result of `co_return`
        template <typename T>
        inline void return_value(T&& t) {
            result = std::forward<T>(t);
        }

        inline void unhandled_exception() { eptr = std::current_exception(); }
       
        // install a cleanup handler to be executed when the coroutine promise 
        // is destroyed (when the handle is destroyed)
        inline void install(cleanup::handler h) {
            if(!cleanup_) { 
                cleanup_ = std::unique_ptr<cleanup>(new cleanup(this));
            }

            cleanup_->install(std::move(h));
        }

        std::exception_ptr eptr = nullptr; // exception pointer
        std::any result; // potential `co_return`ed result  

    private:
        std::unique_ptr<cleanup> cleanup_;
    };
    
    coroutine() = delete;
    coroutine(const coroutine&) = delete;
    coroutine(coroutine&& rhs) = default;

    // construct the coroutine from a type erased handle
    coroutine(std::coroutine_handle<> h) : handle_(std::move(h)) { }

    virtual ~coroutine() { if(handle_) { handle_.destroy(); } }

    coroutine& operator=(const coroutine&) = delete;
    coroutine& operator=(coroutine&& rhs) = default;

    /// return true if the handle is valid, else false
    inline operator bool() { return (bool)handle; }

    /// return true if the coroutine is done, else false
    inline bool done() { return handle_.done(); }

    /**
     This reference can be moved if necessary, allowing control of the handle 
     to transfer to a new owning coroutine.

     @return a reference to the coroutine's handle
     */
    inline std::coroutine_handle<>& handle() { return handle_; }

    /// return the promise associated with this coroutine's handle
    inline promise_type& promise() {
        return handle_type::from_address(handle_.address()).promise();
    }

private:
    /*
     Execute until thunk completes or yield() is called 

     Only a scheduler may call this operation.

     Execute until thunk completes or yield() is called. If coroutine handle is 
     complete resume() returns immediately.
    */
    inline void resume() {
        if(!done()) {
            auto& tl_co = detail::tl_this_coroutine();

            // store parent coroutine pointer
            auto parent_co = tl_co;

            // set current coroutine ptr 
            tl_co = this; 
            
            // continue coroutine execution
            try {
                handle_.resume();

                // acquire the exception pointer
                auto eptr = promise().eptr;

                // rethrow any exceptions from the coroutine
                if(eptr) { std::rethrow_exception(eptr); }
            } catch(...) {
                // restore parent coroutine ptr
                tl_co = parent_co;
                std::rethrow_exception(std::current_exception());
            }

            // restore parent coroutine ptr
            tl_co = parent_co;
        };
    }

    std::coroutine_handle<> handle_;
    friend struct scheduler;
};

//-----------------------------------------------------------------------------
// awaitables
//-----------------------------------------------------------------------------

/// thread_local block/unblock functionality
struct this_thread {
    // get the this_thread object associated with the calling thread
    static this_thread* get();

    // can only block the calling thread
    static inline void block(std::unique_lock<LOCK>& lk) {
        this_thread::get()->block_(lk);
    }

    // unblock an arbitrary this_thread
    inline void unblock(std::unique_lock<LOCK>& lk) {
        ready = true;
        lk.unlock();
        cv.notify_one();
    }

private:
    this_thread() : ready{false} { }

    inline void block_(std::unique_lock<LOCK>& lk) {
        while(!(ready)) { cv.wait(lk); }
        ready = false;
    }

    bool ready;
    std::condition_variable_any cv;
};

// awaitables inherit shared functionality defined
template <typename LOCK>
struct base_awaitable {
    struct implementation {
        virtual ~implementation() { 
            std::unique_lock<LOCK>& lk = this->get_lock();
            if(!resumed_) { resume_(lk, nullptr); } 
        }

        /**
         @brief unblock and resume a suspended operation 

         This should be called by a different coroutine or thread.

         Calling this method will allow the unblock the thread or suspended 
         coroutine (`co_await` will return to its caller). 

         @param m arbitary memory passed to implementation::resume()
         */
        inline void resume(void* m) {
            std::unique_lock<LOCK>& lk = this->get_lock();
            resume_(lk, m);
        }

    protected:
        /**
         Returns a locked LOCK used by the operation. This lock will be 
         unlocked by the awaitable during suspend. This operation is called 
         when the awaitable is constructed from the implementation.

         It is held during calls to ready() and resume(), and unlocked when 
         await_suspend() is called.
         */
        virtual std::unique_lock<LOCK>& get_lock() = 0;

        /**
         @brief returns whether the operation can resume immediately or not 

         This operation is where any code which needs to execute before suspend 
         should be called.
         */
        virtual bool ready_impl() = 0;

        /**
         User inner method called when resume()ing the suspended operation.  

         It is passed whatever arbitary memory is passed to resume(). The 
         implementation may use this memory to complete the operation, in 
         whatever manner it sees fit.

         Implementations must be aware of the fact that `resume_impl()` will 
         be called with a `nullptr` by the implementation's destructor if 
         `resume_impl()` was never called.

         @param m arbitrary memory
         */
        virtual void resume_impl(void* m) = 0;

    private:
        /// called by awaitable's await_suspend()
        inline void suspend(std::coroutine_handle<> h) {
            std::unique_lock<LOCK>& lk = this->get_lock();

            if(h) {
                handle_ = h; 
                // always acquire the *actual* scheduler we are running on
                destination_ = *(detail::tl_this_scheduler());
                this_coroutine()->handle() = std::coroutine_handle<>();
                lk.unlock();
            } else {
                atp_ = this_thread::get(); 
                this_thread::block(lk);
            }
        }

        inline void resume_(std::unique_lock<LOCK>& lk, void* m) {
            resumed_ = true;

            // acquire the lock 
            lk.lock();
            this->resume_impl(m);

            if(atp_) { 
                // unblock the suspended thread
                atp_->unblock(lk); 
            } else { 
                // unblock the suspended coroutine
                lk.unlock();
                auto d = destination_.lock();
                if(d) { d->schedule(coroutine(handle_)); }
            }
        }

        bool resumed_ = false;
        this_thread* atp_ = nullptr;
        std::coroutine_handle<> handle_;
        std::weak_ptr<scheduler> destination_;

        friend struct base_awaitable<LOCK>;
    };

    struct coroutine_did_not_co_await : public std::exception {
        coroutine_did_not_co_await() : 
            estr([]() -> std::string {
                std::stringstream ss;
                ss << "coroutine[0x" 
                   << (void*)this_coroutine() 
                   << "] did not call co_await on an awaitable";
                return ss.string();
            }()) 
        { }

        const char* what() const { return estr.c_string(); }

    private:
        const std::string estr;
    };

    // awaitable is a transient object, it must be used inline
    base_awaitable() = delete;
    base_awaitable(const base_awaitable<LOCK>&) = delete;
    base_awaitable(base_awaitable<LOCK>&&) = default;
    base_awaitable<LOCK>& operator=(const base_awaitable<LOCK>&) = delete;
    base_awaitable<LOCK>& operator=(base_awaitable<LOCK>&&) = default;

    /**
     @brief construct a base_awaitable with some base_awaitable::implementation
     */
    template <typename IMPLEMENTATION>
    base_awaitable(std::unique_ptr<IMPLEMENTATION> i) : 
        impl_(static_cast<base_awaitable<LOCK>::implementation*>(i.release()))
    { }
    
    virtual inline ~base_awaitable() {
        if(impl_) {
            finalize_(); 
            delete impl_;
        }
    }

    /**
     Immediately called by `co_await` keyword. If it returns `true`, 
     `await_ready()` is immediately called.
     */
    inline bool await_ready() {
        awaited_ = true;
        return impl_->ready_impl();
    }

    /**
     When using the `co_await` keyword, if `await_ready() == false`, this 
     function is called by the compiler with a new coroutine handler capable 
     of resuming where we suspended.

     Store the new coroutine handle so we can resume later, and call the 
     `suspend` operation.

     If the argument handle does not represent a coroutine (`handle == false`), 
     then the operation is on a system thread and will block the calling thread.
     */
    inline void await_suspend(std::coroutine_handle<> h) { impl_->suspend(h); }

    /// cast the implementation pointer to a descendant type
    template <IMPLEMENTATION>
    IMPLEMENTATION& cast() {
        return *(dynamic_cast<IMPLEMENTATION*>(impl_));
    }

private:
    inline void finalize_() {
        if(!awaited_) {
            if(in_coroutine()) { 
                // coroutine failed to `co_await` the awaitable
                throw coroutine_did_not_co_await(); 
            } else if(!await_ready()) { 
                // if we're here, this awaitable is operating without the 
                // `co_await` keyword, and needs to operate as a regular system 
                // thread blocking call, not a coroutine suspend.
                await_suspend(std::coroutine_handle<>()); 
            }
        } 
    }

    implementation* impl_;
    bool awaited_ = false;
};

/**
 @brief implementation for stackless awaitables used by this library 

 If used by non-coroutines then converting this object to its templated type `T`
 or the object being destroyed will block the thread until the operation 
 completes.

 Likewise, when a coroutine `co_await`s on this object it will suspend until the 
 operation completes. If a coroutine fails to `co_await` the object an exception 
 will be thrown.

 The result of the awaitable operation is of type `T`, which will be returned 
 to a coroutine from the `co_await` expression or can be converted directly to 
 type `T` by a non-coroutine.

 coroutine:
 ```
 T my_t = co_await operation_returning_awaitable();
 ```

 non-coroutine:
 ```
 T my_t = operation_returning_awaitable();
 ```
 */
template <typename T, typename LOCK=spinlock>
struct awaitable : public base_awaitable<LOCK> {
    // the necessary operations required by an instance of awaitable<T>
    struct implementation : protected base_awaitable<LOCK>::implementation {
        // ensure destructor is virtual to properly deconstruct
        virtual inline ~implementation() { }

    protected:
        /**
         @return the final result of the `awaitable<T>`
         */
        virtual T result_impl() = 0;

        friend struct awaitable<T,LOCK>;
    };

    typedef T value_type;

    awaitable(std::unique_ptr<implementation> i) : base_awaitable(std::move(i)) { }

    virtual ~awaitable(){ }

    // construct with an an allocated implementation
    template <typename IMPLEMENTATION, typename... As>
    static awaitable<bool> make(As&&... as) 
    {
        return {
            std::unique_ptr<implementation>(
                static_cast<implementation*>(
                    new IMPLEMENTATION(std::forward<As>(as)...)))
        };
    }

    /**
     Return the final result of `awaitable<T>`
     */
    inline T await_resume(){ return cast<implementation>()->result_impl(); }

    /**
     Inline conversion for use in standard threads where `co_await` isn't called.
     */
    inline operator T() {
        finalize_(); 
        return await_resume();
    }
};

/**
 @brief full template specialization for awaitable with no return value 

 Coroutines must `co_await` this as usual to block (non-coroutines will block 
 when the object goes out of scope). However, no value will be returned by the 
 awaitable on resumption.
 */
template <typename LOCK=spinlock>
struct awaitable<void> : public base_awaitable<LOCK> {
    struct implementation : protected base_awaitable<LOCK>::implementation {
        virtual inline ~implementation() { }
    };

    typedef void value_type;

    awaitable(std::unique_ptr<implementation> i) : 
        base_awaitable<LOCK>(std::move(i)) { }

    // construct with an an allocated implementation
    template <typename IMPLEMENTATION, typename... As>
    static awaitable<void> make(As&&... as) {
        return {
            std::unique_ptr<implementation>(
                static_cast<implementation*>(
                    new IMPLEMENTATION(std::forward<As>(as)...)))
        };
    }

    virtual inline ~awaitable() { }
    inline void await_resume(){ }
};

struct base_yield {
    ~base_yield() {
        if(in_coroutine() && !awaited_){ throw coroutine_did_not_co_await(); }
    }

    inline bool await_ready() {
        awaited_ = true;
        return false; 
    }

    inline void await_suspend(std::coroutine_handle<> h) {
        this_coroutine()->handle(h);
    }

private:
    bool awaited_ = false;
};

/**
 @brief `co_await` to suspend execution and allow other coroutines to run.

 `coroutine::done()` will `== false` after suspending in this 
 manner, so the coroutine can be requeued immediately.

 `yield<T>` returns a value `T` which is returned on resumption.

 If used by non-coroutines no suspend will occur, and the value is returned 
 immediately.
 */
template <typename T>
struct yield : public base_yield {
    template <typename... As>
    yield(As&&... as) : t(std::forward<As>(as)...) { }

    inline T await_resume() { return std::move(t); }

    inline operator T() {
        if(in_coroutine()) { throw coroutine_did_not_co_await(); }
        return await_resume(); 
    }

private:
    bool awaited_ = false;
    T t;
};

/// full void specialization
template <>
struct yield<void> : public base_yield {
    inline void await_resume() { }
};

//-----------------------------------------------------------------------------
// scheduler
//-----------------------------------------------------------------------------

/// timer which can be scheduled on the scheduler with scheduler::start()
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
        inline operator bool() { return (bool)ptr; }

        /// comparison based on allocated pointer uniqueness
        inline bool operator==(const id& rhs) { return ptr == rhs.valid; }

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

    timer(const std::chrono::steady_clock::time_point& tp)
        time_point(tp)
    { }

    timer(const std::chrono::steady_clock::duration& dur)
        time_point(timer::now() + dur)
    { }

    timer(unit u, size_t count, std::unique_ptr<implementation>&& impl) :
        time_point(timer::now() + timer::to_duration(u, count))
    { }

    ~timer(){}

    /// construct a timer
    template <typename IMPLEMENTATION, typename... As>
    static std::unique_ptr<timer> make(As&&... as) {
        return {
            static_cast<timer*>(new IMPLEMENTATION>(std::forward<As>(as)...))
        };
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
            case time_unit::hour:
                return std::chrono::hours(count);
                break;
            case time_unit::minute:
                return std::chrono::minutes(count);
                break;
            case time_unit::second:
                return std::chrono::seconds(count);
                break;
            case time_unit::millisecond:
                return std::chrono::milliseconds(count);
                break;
            case time_unit::microsecond:
                return std::chrono::microseconds(count);
                break;
            default:
                return std::chrono::milliseconds(0);
                break;
        }
    }

    /// timer timeout comparison
    bool operator<(const timer& rhs) { return time_point < rhs.time_point; }

    /// call the implementation's cancel() operation 
    inline coroutine cancel() = 0;

    /// call the implementation's timeout() operation 
    inline coroutine timeout() = 0;

    /// the timeout time_point for the timer
    const std::chrono::steady_clock::time_point time_point;

private:
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
    struct coroutine_called_run : public std::exception {
        coroutine_called_run() : 
            estr([]() -> std::string {
                std::stringstream ss;
                ss << "coroutine[0x" 
                   << (void*)this_coroutine() 
                   << "] called scheduler::run()";
                return ss.string();
            }()) 
        { }

        const char* what() const { return estr.c_string(); }

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
        clear_coroutine_queue_();
        delete coroutine_queue_;
    }

    /**
     @brief construct a scheduler 
     @param io_count the minimum count of io worker threads
     @return an allocated and initialized scheduler 
     */
    static inline std::shared_ptr<scheduler> make(size_t io_count=0) {
        scheduler* sp = new scheduler(io_count);
        std::shared_ptr<scheduler> s(sp);
        s->self_wptr_ = s;
        return s;
    };

    /**
     @brief a running, process wide scheduler instance

     The instance will not which will not constructed until this operation is 
     first called.
     */
    static scheduler& global();

    /**
     Retrieve some scheduler, preferring this_scheduler(), falling back to the 
     process-wide scheduler instance.
     */
    static inline scheduler& get() {
        return in_scheduler() ? this_scheduler() : scheduler::global();
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

     @param redirect a scheduler to redirect this_scheduler() calls to
     @return `true` if `run()` was suspended by `suspend()`, else `false`
     */
    inline bool run(scheduler* redirect = nullptr) {
        // error out immediately if called improperly
        if(in_coroutine()) { throw coroutine_called_run(); }

        // stack variables 

        // the currently running coroutine
        coroutine cur_co;

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

        // construct a new coroutine queue
        std::unique_ptr<std::deque<coroutine*>> coroutine_queue(
                new std::deque<coroutine*>);

        // ready timer queue
        std::deque<timer*> ready_timers;

        auto reque_coroutines = [&] {
            for(auto c : *coroutine_queue) { coroutine_queue_->push_back(c); }
            coroutine_queue->clear();
        };

        auto notify_timeouts_complete = [&] {
            ready_timers_mem_.clear(); 

            // notify timeout waiters
            if(executing_remove_sync_required_) {
                executing_remove_sync_required_ = false;
                remove_sync_cv_.notify_all();
            }
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

                    scheduled_ = coroutine_queue.size();
                    size_t count = scheduled_;

                    // check for any ready timers 
                    if(timers_.size()) {
                        auto now = timer::now();
                        auto it = timers_.begin();
                        auto end = timers_.end();

                        while(it!=end) {
                            // check if a timer is ready to timeout
                            if((*it)->time_point <= now) {
                                timer* t = *it;
                                ready_timers.push_back(t);
                                ready_timers_mem_.push_back(t->id_.ptr.get());
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
                        --count;

                        // Get a new task. Don't swap, it's slower than copying
                        auto co = coroutine_queue->front();
                        coroutine_queue->pop_front();

                        // execute coroutine
                        co->resume();

                        if(co->done()) {
                            // cleanup after coroutine
                            delete co;
                        } else {
                            // re-enqueue coroutine 
                            coroutine_queue->push_back(co); 
                        } 
                    }

                    // handle timeouts
                    while(ready_timers.size()) {
                        auto ready_timer = ready_timers.front();
                        ready_timers.pop_front();
                        auto co = ready_timer->timeout();
                        delete ready_timer;

                        if(co) {
                            coroutine_queue.push_back(
                                new coroutine(std::move(co.handle())));
                        }
                    }

                    // reacquire lock
                    lk.lock(); 

                    // cleanup batch results
                    scheduled_ = 0;
                    reque_coroutines();
                    notify_timeouts_complete();

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
                                tasks_available_cv_.block_until(
                                    lk, 
                                    timers_.front()->time_point);
                            }
                        }
                    }

                    update_running_state_();
                }
            } catch(...) { // catch all other exceptions 
                // reset state in case of uncaught exception
                tl_cs = parent_cs; 
                tl_cs_re = parent_cs_re;

                // it is an error if an exception occurs when the lock is held,
                // it should only be in user code that this can occur
                lk.lock();

                reque_coroutines();
                notify_timeouts_complete();

                lk.unlock();

                std::rethrow_exception(std::current_exception());
            }
        }

        // restore parent thread_local this_scheduler state
        tl_cs = parent_cs; 
        tl_cs_re = parent_cs_re;

        // move any coroutines in the local queue to the object queue
        reque_coroutines();

        if(state_ == suspended) {
            // reset scheduler state so run() can be called again
            reset_flags_();
            return true;
        } else {
            // clear task queue so coroutine controlled memory can be released 
            clear_coroutine_queue_();

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
        if(this_scheduler() != self_wptr_.lock()) {
            // wakeup scheduler if necessary
            tasks_available_notify_();

            // handle case where halt() is called from another std::thread
            if(!in_coroutine()) {
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
     @brief schedule a coroutine and await until it completes 

     This is a useful operation whenever a coroutine needs to block on another 
     coroutine (without using other mechanisms like channels).

     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will return true if the coroutine completed, else false
     */
    inline awaitable<bool> await(coroutine co) {
        struct awt : protected awaitable<bool>::implementation {
            awt(std::unique_lock<spinlock> lk) : 
                ready_(false),
                lk_(std::move(lk))
            { }

            awt(bool ready) : ready_(ready) { }

        protected:
            inline std::unique_lock<spinlock>& get_lock() { return lk_; }
            inline bool ready_impl() { return ready_; }
            inline void resume_impl(void* m) { completed_ = (bool)m; }
            inline bool result_impl() { return completed_; }

        private:
            bool ready_;
            std::unique_lock<spinlock> lk_;
            bool completed_ = false;
        };

        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) {
            auto awt = new awt(std::move(lk));

            co.promise().install([awt](coroutine::promise_type* pt) mutable {
                awt->resume(1);
            });

            schedule_((coroutine)(co));

            /// returned awaitable resumes when the coroutine handle is destroyed
            return { 
                std::unique_ptr<awaitable<bool>::implementation>(awt) 
            };
        } else {
            // awaitable returns immediately
            return awaitable<bool>::make<awt>(true);
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
    awaitable<bool> await(T& t, coroutine co) {
        struct awt : protected awaitable<bool>::implementation {
            awt(std::unique_lock<spinlock> lk, T& t) : 
                ready_(false),
                lk_(std::move(lk)),
                t_(&t)
            { }

            awt(bool ready) : ready_(ready) {}

        protected:
            inline std::unique_lock<spinlock>& get_lock() { return lk_; }
            inline bool ready_impl() { return ready_; }

            inline void resume_impl(void* m) { 
                if(m) { 
                    *t_ = std::move(*((T*)m)); 
                    completed_ = true;
                }
            }

            inline bool return_impl() { return completed_; }

        private:
            bool ready_;
            std::unique_lock<spinlock> lk_;
            T* t_;
            completed_ = false;
        };

        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) {
            auto awt = new awt;

            co.promise().install([awt](coroutine::promise_type* pt) mutable {
                awt->resume(&(any_cast<T&>(pt->result)));
            });

            schedule_((coroutine)(co));

            /// returned awaitable resumes when the coroutine handle is destroyed
            return { 
                std::unique_ptr<awaitable<std::optional<T>>::implementation>(awt) 
            };
        } else {
            // awaitable returns immediately
            return awaitable<std::optional<T>>::make<awt>(true);
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

        if(state_ != halted && t) {
            id.ptr = std::make_shared<bool>(false);
            t.id_ = id;
            timers_.push_back(t.release());

            // sort based on timer::operator<
            timers_.sort([](const timer*& lt, const timer*& rh) {
                return *lt < *rh;
            });

            tasks_available_notify_();
        }

        return id;
    }

    /// cancel a scheduled timer 
    inline void cancel(timer::id id) {
        if(id) {
            auto timer_is_timing_out = [&]{ 
                auto it = ready_timers_mem_.begin();
                auto end = ready_timers_mem_.end();
                auto id_mem = id.ptr.get();

                for(auto mem : ready_timers_mem_) {
                    if(id_mem == mem) { return true; }
                }

                return false;
            };

            std::unique_lock<spinlock> lk(lk_);

            if(timer_is_timing_out()) {
                // block until timeout handler finishes
                do {
                    executing_remove_sync_required_ = true;
                    remove_sync_cv_.wait(lk);
                } while(timer_is_timing_out());
            } else {
                auto it = timers_.begin();
                auto end = timers_end();

                while(it!=end) {
                    if(it->id_ == id) {
                        auto t = std::move(*it);
                        timers_.erase(it);

                        lk.unlock();
                        t.cancel();
                        break;
                    }

                    ++it;
                }
            }
        } 
    }

    /// return the count of scheduled coroutines
    inline size_t measure() {
        std::lock_guard<spinlock> lk(lk_);
        return scheduled_ + coroutine_queue_->size();
    }

    /// return a copy of this scheduler's shared pointer by conversion
    inline operator std::shared_ptr<scheduler>() { return self_wptr_.lock(); }

    /// return a copy of this scheduler's weak pointer by conversion
    inline operator std::weak_ptr<scheduler>() { return self_wptr_; }
 
private:
    scheduler() : 
            state_(ready), // state_ persists between suspends
            coroutine_queue_(new std::deque<coroutine*>) { // persists as necessary
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
        executing_remove_sync_required_ = false
        executing_timer_memory_ = nullptr;
    }

    // abstract frequently used inlined state to this function for correctness
    inline bool can_continue_() { return state_ < suspended; }

    void clear_coroutine_queue_() {
        while(coroutine_queue_->size()) {
            delete coroutine_queue_->front();
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
    inline void schedule_coroutine_(std::coroutine_handle<>&& h) {
        if(c) {
            coroutine_queue_->push_back(
                new coroutine(std::move(h)));
        }
    }

    // when all coroutines are scheduled, notify
    inline void schedule_() { tasks_available_notify_(); }

    template <typename A, typename... As>
    void schedule_(A&& a, As&&... as) {
        // detect if A is a container or a Stackless
        schedule_fallback_(
                detail::is_container<typename std::decay<A>::type>(),
                std::forward<A>(a), 
                std::forward<As>(as)...);
    }

    template <typename Container, typename... As>
    void schedule_fallback_(std::true_type, Container&& coroutines, As&&... as) {
        for(auto& c : coroutines) {
            schedule_coroutine_(std::move(c.handle()));
        }

        schedule_(std::forward<As>(as)...);
    }

    template <typename Coroutine, typename... As>
    void schedule_fallback_(std::false_type, Coroutine&& c, As&&... as) {
        schedule_coroutine_(std::move(c.handle()));
        schedule_(std::forward<As>(as)...);
    }

    spinlock lk_;

    // the current state of the scheduler
    state state_; 
                  
    // the count of scheduled coroutines on this scheduler 
    size_t scheduled_; 
   
    // condition data for when 
    bool waiting_for_resume_;
    std::condition_variable_any resume_cv_;

    // condition data for when new tasks are available
    bool waiting_for_tasks_; // true if waiting on tasks_available_cv_
    std::condition_variable_any tasks_available_cv_;

    // condition data for when halt completes
    bool halt_complete_; 
    std::condition_variable_any halt_complete_cv_;

    std::weak_ptr<scheduler> self_wptr_;

    // condition data for when cancelling a timer that's currently timing out
    bool executing_remove_sync_required_;
    std::condition_variable_any remove_sync_cv_;

    // a set of timer's allocated ptr addresses which are actively timing out
    std::deque<bool*> ready_timers_mem_;

    // queue holding scheduled coroutines. Raw coroutine pointers are used 
    // internally because we don't want even the cost of rvalue swapping 
    // unique pointers during internal operations (potentially 3 instructions), 
    // when we can deep copy a word size value intead. This requires careful 
    // calls to `delete` when a coroutine goes out of scope in this object. 
    std::unique_ptr<std::deque<coroutine*>> coroutine_queue_;

    // list holding scheduled timers. This will be regularly resorted from 
    // soonest to latest timeout.
    std::list<timer*> timers_;
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
