//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_SCHEDULER__
#define __HCE_COROUTINE_ENGINE_SCHEDULER__ 

// c++
#include <memory>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <list>
#include <string>
#include <sstream>
#include <optional>
#include <cstdlib>
#include <thread>

// local 
#include "utility.hpp"
#include "chrono.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"

namespace hce {

struct scheduler;

struct coroutine_destroyed_without_completing : 
        public printable,
        public std::exception 
{
    coroutine_destroyed_without_completing(void* c, void* j) :
        estr([&]() -> std::string {
            std::stringstream ss;
            ss << "std::coroutine_handle<>@" 
               << (void*)c
               << " was destroyed before it was completed, so joiner@" 
               << (void*)j
               << " cannot join with it";
            return ss.str();
        }()) 
    { 
        HCE_ERROR_CONSTRUCTOR();
    }

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "coroutine_destroyed_without_completing"; }
    inline std::string content() const { return what(); }

    inline const char* what() const noexcept { return estr.c_str(); }

private:
    const std::string estr;
};

namespace detail {
namespace scheduler {

// the true, current scheduler
hce::scheduler*& tl_this_scheduler();

template <typename T>
using awt_interface = hce::awt<T>::interface;

template <typename T>
using cleanup_handler = hce::cleanup<typename hce::co<T>::promise_type&>::handler;

/*
 An implementation of hce::awt<T>::interface capable of joining a coroutine 

 Requires implementation for awaitable::interface::destination().
 */
template <typename T>
struct joiner : 
    public hce::awaitable::lockable<
        awt_interface<T>,
        hce::spinlock>
{
    joiner(hce::co<T>& co) :
        hce::awaitable::lockable<
            awt_interface<T>,
            hce::spinlock>(
                slk_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
        ready_(false),
        address_(co.address())
    { 
        struct handler : public cleanup_handler<T> {
            handler(joiner<T>* parent) : parent_(parent) { }
            virtual ~handler(){}

            inline void operator()(typename hce::co<T>::promise_type& p){
                // get a copy of the handle to see if the coroutine completed 
                auto handle = 
                    std::coroutine_handle<
                        typename hce::co<T>::promise_type>::from_promise(p);

                if(handle.done()) {
                    HCE_TRACE_FUNCTION_BODY("joiner<T>::handler::operator()()","cleanup done@",handle);
                    // resume the blocked awaitable
                    parent_->resume(&(p.result));
                } else {
                    HCE_ERROR_FUNCTION_BODY("joiner<T>::handler::operator()()","cleanup NOT done@",handle);
                    // resume with no result
                    parent_->resume(nullptr);
                }
            }

        private:
            joiner<T>* parent_;
        };

        HCE_TRACE_CONSTRUCTOR(co);

        // install a cleanup handler to resume the returned awaitable when 
        // the coroutine goes out of scope
        co.promise().install(
            std::unique_ptr<cleanup_handler<T>>(
                dynamic_cast<cleanup_handler<T>*>(new handler(this))));
    }

    inline bool on_ready() { return ready_; }

    inline void on_resume(void* m) { 
        ready_ = true;

        if(m) { 
            t_ = std::move(*((std::unique_ptr<T>*)m)); 
        };
    }

    inline T get_result() { 
        if(!t_) { throw coroutine_destroyed_without_completing(address_, this); }
        return std::move(*t_); 
    }

private:
    spinlock slk_;
    bool ready_;
    void* address_;
    std::unique_ptr<T> t_;
};

/// variant for coroutine returning void, not reporting success
template <>
struct joiner<void> : 
    public hce::awaitable::lockable<
        awt_interface<void>,
        hce::spinlock>
{
    joiner(hce::co<void>& co) :
        hce::awaitable::lockable<
            awt_interface<void>,
            hce::spinlock>(
                slk_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
        ready_(false)
    { 
        struct handler : public cleanup_handler<void> {
            handler(joiner<void>* parent) : parent_(parent) { }
            virtual ~handler(){}
            inline void operator()(typename hce::co<void>::promise_type& p){ 
                parent_->resume(nullptr); 
            }

        private:
            joiner<void>* parent_;
        };

        HCE_TRACE_CONSTRUCTOR(co);

        // install a cleanup handler to resume the returned awaitable when 
        // the coroutine goes out of scope
        co.promise().install(
            std::unique_ptr<cleanup_handler<void>>(
                dynamic_cast<cleanup_handler<void>*>(new handler(this))));
    }

    inline bool on_ready() { return ready_; }
    inline void on_resume(void* m) { ready_ = true; }

private:
    spinlock slk_;
    bool ready_;
};

template <typename T>
void assemble_joins_(std::deque<hce::awt<void>>& dq, awt<T>&& a) {
    dq.push_back(awt<void>::make(a.release()));
}

template <>
inline void assemble_joins_<void>(std::deque<hce::awt<void>>& dq, awt<void>&& a) {
    dq.push_back(std::move(a));
}

template <typename T>
inline hce::co<T> wrapper(std::function<T()> f) {
    auto& tl_co = hce::detail::coroutine::tl_this_coroutine();
    auto& tl_sch = hce::detail::scheduler::tl_this_scheduler();
    auto parent_co = tl_co;
    auto parent_sch = tl_sch;
    T result;

    try {
        // unset coroutine and scheduler pointers during execution of Callable
        tl_co = nullptr;
        tl_sch = nullptr;
        result = f();
        tl_co = parent_co;
        tl_sch = parent_sch;
    } catch(...) {
        tl_co = parent_co;
        tl_sch = parent_sch;
        std::rethrow_exception(std::current_exception());
    }

    co_return result;
}

template <>
inline hce::co<void> wrapper(std::function<void()> f) {
    auto& tl_co = hce::detail::coroutine::tl_this_coroutine();
    auto& tl_sch = hce::detail::scheduler::tl_this_scheduler();
    auto parent_co = tl_co;
    auto parent_sch = tl_sch;

    try {
        tl_co = nullptr;
        tl_sch = nullptr;
        f();
        tl_co = parent_co;
        tl_sch = parent_sch;
    } catch(...) {
        tl_co = parent_co;
        tl_sch = parent_sch;
        std::rethrow_exception(std::current_exception());
    }

    co_return;
}

template <typename T>
struct done_partial : public 
        hce::awaitable::lockable<awt_interface<T>,hce::lockfree>
{
    template <typename... As>
    done_partial(As&&... as) : 
        hce::awaitable::lockable<
            awt_interface<T>,
            hce::lockfree>(
                lf_,
                hce::awaitable::await::policy::adopt,
                hce::awaitable::resume::policy::no_lock),
        t_(std::forward<As>(as)...) 
    { }

    inline bool on_ready() { return true; }
    inline void on_resume(void* m) { }
    inline T get_result() { return std::move(t_); }

private:
    hce::lockfree lf_;
    T t_;
};

template <>
struct done_partial<void> : public 
        hce::awaitable::lockable<awt_interface<void>,hce::lockfree>
{
    done_partial() :
        hce::awaitable::lockable<
            awt_interface<void>,
            hce::lockfree>(
                lf_,
                hce::awaitable::await::policy::adopt,
                hce::awaitable::resume::policy::no_lock)
    { }

    inline bool on_ready() { return true; }
    inline void on_resume(void* m) { }

private:
    hce::lockfree lf_;
};

}
}

/** 
 @brief object responsible for scheduling and executing coroutines and timers
 
 scheduler cannot be created directly, it must be created by calling 
 scheduler::make() 
    
 Scheduler API, unless otherwise specified, is threadsafe and coroutine-safe.
 That is, it can be called from anywhere safely, including from within a 
 coroutine running on the scheduler which is being accessed.
*/
struct scheduler : public printable {
    /// an enumeration which represents the scheduler's current state
    enum state {
        ready, /// ready to execute coroutines
        executing, /// is executing coroutines
        suspended, /// temporarily halted by a call to suspend()
        halted /// permanently halted by lifecycle destructor
    };

    // implementation of awaitable::interface::destination()
    template <typename INTERFACE>
    struct reschedule : public INTERFACE {
        template <typename... As>
        reschedule(As&&... as) : 
            INTERFACE(std::forward<As>(as)...),
            destination_([]() -> scheduler& {
                auto& tl_sch = detail::scheduler::tl_this_scheduler();
                return tl_sch ? *tl_sch : scheduler::global();
            }())
        { }

        inline void destination(std::coroutine_handle<> h) {
            HCE_LOW_METHOD_ENTER("destination",h);

            auto d = destination_.lock();

            if(d) { 
                HCE_LOW_METHOD_BODY("destination",*d,h);
                d->schedule(std::move(h)); 
            }
        }

    private:
        // a weak_ptr to the scheduler we will reschedule on
        std::weak_ptr<scheduler> destination_;
    };

    struct is_halted : 
            public std::exception,
            public printable 
    {
        is_halted(scheduler* s, const char* method_name) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "hce::scheduler[0x" 
                   << (void*)s
                   << "] is halted, but operation[hce::scheduler::"
                   << method_name
                   << "] was called";
                return ss.str();
            }()) 
        { 
            HCE_ERROR_CONSTRUCTOR();
            HCE_ERROR_METHOD_BODY("is_halted",estr.c_str());
        }

        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "is_halted"; }
        inline std::string content() const { return estr; }
        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    struct coroutine_called_install : public std::exception {
        coroutine_called_install() : 
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
     @brief object for controlling the lifecycle state of schedulers 

     When this object is assigned during a call to:
     ```
     std::shared_ptr<scheduler> scheduler::make(std::unique_ptr<scheduler::lifecycle>&)
     ```

     It will be allocated during the call to `make()` and take ownership of the 
     returned scheduler object.

     When that lifecycle object goes out of scope, the scheduler will be 
     permanently halted (no more coroutines will be executed).
     */
    struct lifecycle : public printable {
        /**
         @brief object responsible for destructing registered lifecycle instances at process termination
         */
        struct manager : public printable {
            /// access the process wide manager object
            static manager& instance();

            ~manager() { 
                HCE_HIGH_DESTRUCTOR();
                exited_ = true; 
            }

            /// register a lifecycle pointer to be destroyed at process exit
            inline void registration(std::unique_ptr<lifecycle> lptr) {
                if(lptr) {
                    HCE_HIGH_METHOD_ENTER("registration", *lptr);
                    std::lock_guard<hce::spinlock> lk(lk_);
                    if(!exited_) { lptrs_.push_back(std::move(lptr)); }
                }
            }
            
            inline const char* nspace() const { 
                return "hce::scheduler::lifecycle"; 
            }

            inline const char* name() const { return "manager"; }

            inline std::string content() const { 
                std::stringstream ss;
                auto it = lptrs_.begin();
                auto end = lptrs_.end();

                if(it != end) {
                    ss << **it;
                    ++it;

                    for(; it!=end; ++it) {
                        ss << ", " << **it;
                    }
                }

                return ss.str();
            }

            /// call lifecycle::suspend() on all registered lifecycles
            inline void suspend() {
                HCE_HIGH_METHOD_ENTER("suspend");
                std::lock_guard<hce::spinlock> lk(lk_);
                for(auto& lp : lptrs_) { lp->suspend(); }
            }

            /// call lifecycle::resume() on all registered lifecycles
            inline void resume() {
                HCE_HIGH_METHOD_ENTER("resume");
                std::lock_guard<hce::spinlock> lk(lk_);
                for(auto& lp : lptrs_) { lp->suspend(); }
            }

        private:
            manager() { 
                std::atexit(manager::atexit); 
                HCE_HIGH_DESTRUCTOR();
            }

            static inline void atexit() { manager::instance().exit(); }

            inline void exit() {
                HCE_HIGH_METHOD_BODY("exit");
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
        virtual ~lifecycle(){ 
            HCE_HIGH_DESTRUCTOR();
            parent_->halt_(); 
        }
            
        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "lifecycle"; }

        inline std::string content() const {
            std::stringstream ss;
            if(parent_) { ss << *parent_; }
            return ss.str();
        }

        /// return the associated scheduler
        inline hce::scheduler& scheduler() { 
            HCE_HIGH_METHOD_ENTER("scheduler");
            return *parent_; 
        }

        /**
         @brief temporarily suspend a scheduler executing coroutines

         A suspended scheduler will cease processing coroutines, execute any 
         installed `config::on_suspend` handlers, and then block until either 
         `scheduler::resume()` is called or the scheduler's associated `lifecycle`
         object goes out of scope. 
         */
        inline void suspend() { 
            HCE_HIGH_METHOD_ENTER("suspend");
            return parent_->suspend_(); 
        }

        /**
         @brief resume coroutine execution after a call to `suspend()`
         */
        inline void resume() { 
            HCE_HIGH_METHOD_ENTER("resume");
            return parent_->resume_(); 
        }

    private:
        lifecycle() = delete;

        lifecycle(std::shared_ptr<hce::scheduler>& parent) : parent_(parent) {
            HCE_HIGH_CONSTRUCTOR();
        }

        std::shared_ptr<hce::scheduler> parent_;
        friend struct hce::scheduler;
    };

    /**
     @brief object for configuring an installed scheduler 

     An instance of this object passed to `scheduler::install()` will configure 
     the scheduler with the given event handlers and options.
     */
    struct config : public printable {
        typedef std::function<void(hce::scheduler&)> handler;

        struct handlers : public printable {
            handlers() { HCE_HIGH_CONSTRUCTOR(); }

            handlers(const handlers& rhs) { 
                HCE_HIGH_CONSTRUCTOR(rhs); 
                copy_(rhs);
            }

            handlers(handlers&& rhs) {
                HCE_HIGH_CONSTRUCTOR(rhs); 
                move_(std::move(rhs));
            }

            virtual ~handlers() {
                HCE_HIGH_DESTRUCTOR();
                // clear handlers in reverse install order
                while(hdls_.size()) { hdls_.pop_back(); }
            }

            handlers& operator=(const handlers& rhs) { 
                HCE_HIGH_METHOD_ENTER("operator=",rhs); 
                copy_(rhs);
                return *this;
            }

            handlers& operator=(handlers&& rhs) { 
                HCE_HIGH_METHOD_ENTER("operator=",rhs); 
                move_(std::move(rhs));
                return *this;
            }

            inline const char* nspace() const { return "hce::scheduler::config"; }
            inline const char* name() const { return "handlers"; }

            inline std::string content() const {
                std::stringstream ss;

                auto it = hdls_.begin();
                auto end = hdls_.end();

                if(it != end) {
                    ss << hce::detail::utility::callable_to_string(*it);
                    ++it;
                    for(; it != end; ++it) {
                        ss << ", " << hce::detail::utility::callable_to_string(*it);
                    }
                }

                return ss.str();
            }

            /// install a handler
            inline void install(hce::thunk th) {
                hdls_.push_back([th=std::move(th)](hce::scheduler&){ th(); });
                HCE_HIGH_METHOD_ENTER("install",hce::detail::utility::callable_to_string(hdls_.back()));
            }

            /// install a handler
            inline void install(handler h) {
                hdls_.push_back(std::move(h));
                HCE_HIGH_METHOD_ENTER("install",hce::detail::utility::callable_to_string(hdls_.back()));
            }

            /// remove installed handlers
            inline void clear() { hdls_.clear(); }

            /// call all handlers with the given scheduler
            inline void call(hce::scheduler& sch) {
                HCE_HIGH_METHOD_ENTER("call", sch);

                for(auto& hdl : hdls_) { 
                    HCE_HIGH_METHOD_BODY("call",hce::detail::utility::callable_to_string(hdl));
                    hdl(sch); 
                }
            }

        private:
            inline void copy_(const handlers& rhs) {
                HCE_MED_METHOD_ENTER("copy_",rhs);
                hdls_ = rhs.hdls_; // deep copy
            }

            inline void move_(handlers&& rhs) {
                HCE_MED_METHOD_ENTER("move_",rhs);
                hdls_ = std::move(rhs.hdls_); // shallow copy
            }

            std::list<handler> hdls_;
        };

        config(const config& rhs) {
            HCE_HIGH_CONSTRUCTOR(rhs); 
            copy_(rhs);
        }

        config(config&& rhs) {
            HCE_HIGH_CONSTRUCTOR(rhs); 
            move_(std::move(rhs));
        }

        virtual ~config() { HCE_HIGH_DESTRUCTOR(); }

        config& operator=(const config& rhs) {
            HCE_HIGH_METHOD_ENTER("operator=",rhs);
            copy_(rhs);
            return *this;
        }

        config& operator=(config&& rhs) {
            HCE_HIGH_METHOD_ENTER("operator=",rhs);
            move_(std::move(rhs));
            return *this;
        }

        /**
         @brief allocate and construct a config 

         This is the only mechanism to create a `hce::scheduler::config`.

         @return an allocated and constructed config
         */
        static inline std::unique_ptr<config> make() { 
            return std::unique_ptr<config>(new config);
        }

        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "config"; }

        inline std::string content() const {
            std::stringstream ss;
            ss << "block_workers_reuse_pool:"
               << block_workers_reuse_pool
               << "on_init:" 
               << on_init 
               << ", on_suspend:" 
               << on_suspend
               << ", on_halt:" 
               << on_halt;
            return ss.str(); 
        }

        /**
         The count of worker threads used by `block()` this scheduler will 
         persist for reuse. No workers are created until they are required.
         */
        size_t block_workers_reuse_pool = 0;
        
        /**
         Handlers to be called on initialization (before executing coroutines) 
         during a call to `hce::scheduler::install()`.
         */
        handlers on_init;

        /**
         Handlers to be called when a scheduler is `hce::scheduler::suspend()`ed 
         during a call to `hce::scheduler::install()`.
         */
        handlers on_suspend;

        /**
         Handlers to be called during a call to `hce::scheduler::install()` when 
         a scheduler is halted by its associated `hce::scheduler::lifecycle` 
         unique pointer going out of scope.
         */
        handlers on_halt;

    private:
        config(){ HCE_HIGH_CONSTRUCTOR(); }

        // deep copy
        inline void copy_(const config& rhs) {
            HCE_MED_METHOD_ENTER("copy_",rhs);
            block_workers_reuse_pool = rhs.block_workers_reuse_pool;
            on_init = rhs.on_init;
            on_suspend = rhs.on_suspend;
            on_halt = rhs.on_halt;
        }

        // shallow copy
        inline void move_(config&& rhs) {
            HCE_MED_METHOD_ENTER("move_",rhs);
            block_workers_reuse_pool = std::move(rhs.block_workers_reuse_pool);
            on_init = std::move(rhs.on_init);
            on_suspend = std::move(rhs.on_suspend);
            on_halt = std::move(rhs.on_halt);
        }
    };

    virtual ~scheduler() {
        HCE_HIGH_CONSTRUCTOR();
        halt_();
        // ensure all scheduled coroutines and timers are properly destroyed
        clear_queues_();
    }

    /**
     @brief construct an allocated scheduler 

     `scheduler::make()` calls are the only way to make a new scheduler. 

     This variant will allocate and construct the argument lifecycle pointer 
     with the newly allocated and constructed scheduler.

     It is generally a bad idea to allow the lifecycle pointer to go out of 
     scope (causing the scheduler to halt) before all coroutines executed 
     through the scheduler are completed cleanly. 

     As such, it is often a good idea to register schedulers with the 
     `lifecycle::manager` instance (call `scheduler::make()` without an argument 
     lifecycle pointer to do this automatically) so they will be automatically 
     halted on program exit instead of manually. Of course, doing so will cause 
     `hce::scheduler::install()` to block until program exit, so design 
     accordingly.

     @param lc reference to a lifecycle unique pointer
     @return an allocated and initialized scheduler 
     */
    static inline std::shared_ptr<scheduler> make(std::unique_ptr<lifecycle>& lc) {
        HCE_HIGH_FUNCTION_ENTER("hce::scheduler::make","std::unique_ptr<hce::scheduler::lifecycle>&");
        scheduler* sp = new scheduler();
        std::shared_ptr<scheduler> s(sp);
        s->self_wptr_ = s;
        lc = std::unique_ptr<lifecycle>(new lifecycle(s));
        return s;
    }

    /**
     @brief construct an allocated scheduler

     This variant automatically registers a `lifecycle` with the 
     `lifecycle::manager` instance to be halted when the process exits.

     @return an allocated and initialized scheduler 
     */
    static inline std::shared_ptr<scheduler> make() {
        HCE_HIGH_FUNCTION_ENTER("hce::scheduler::make");
        std::unique_ptr<lifecycle> lc;
        auto s = scheduler::make(lc);
        lifecycle::manager::instance().registration(std::move(lc));
        return s;
    };

    /**
     @return `true` if calling thread is running a scheduler, else `false`
     */
    static inline bool in() { 
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::in");
        return detail::scheduler::tl_this_scheduler(); 
    }

    /**
     @brief retrieve the calling thread's scheduler
     @return a scheduler reference
     */
    static inline scheduler& local() {
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::local");
        return *(detail::scheduler::tl_this_scheduler());
    }

    /**
     @brief access the running, process wide scheduler instance

     The instance will not which will not constructed until this operation is 
     first called.

     @return the global scheduler
     */
    static inline scheduler& global() {
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::global");
        return global_();
    }

    /**
     @brief retrieve some scheduler

     Prefer scheduler::local(), falling back to scheduler::global().

     @return a scheduler reference
     */
    static inline scheduler& get() {
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::get");
        return scheduler::in() ? scheduler::local() : scheduler::global();
    }

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "scheduler"; }

    /// return a copy of this scheduler's shared pointer by conversion
    inline operator std::shared_ptr<scheduler>() { 
        HCE_TRACE_METHOD_ENTER("hce::scheduler::operator std::shared_ptr<scheduler>()");
        return self_wptr_.lock(); 
    }

    /// return a copy of this scheduler's weak pointer by conversion
    inline operator std::weak_ptr<scheduler>() { 
        HCE_TRACE_METHOD_ENTER("hce::scheduler::operator std::weak_ptr<scheduler>()");
        return self_wptr_; 
    }

    /**
     @brief install the scheduler on the calling thread to continuously execute scheduled coroutines and process timers until halted

     WARNING: It is an ERROR to call this method from inside a coroutine.

     Execution of coroutines can be paused by calling `lifecycle::suspend()` and 
     will block until `lifecycle::resume()` (or until the scheduler is halted by 
     the lifecycle object going out of scope). If the scheduler was created 
     without an explicit lifecycle option the same can be achieved by calling 
     `lifecycle::manager::suspend()`/`lifecycle::manager::resume()`, though this 
     will affect *all* similar schedulers.

     @param c optional configuration for the executing scheduler
     */
    inline void install(std::unique_ptr<config> c = {}) {
        // error out immediately if called improperly
        if(coroutine::in()) { throw coroutine_called_install(); }

        if(c) { 
            auto& conf = *c;
            HCE_HIGH_METHOD_ENTER("install",conf);
            blocking_.set_worker_reuse_count(conf.block_workers_reuse_pool);
            conf.on_init.call(*this);

            while(run_()) {
                // Call suspend handlers every time run_() returns early due 
                // to `hce::scheduler::lifecycle::suspend()` being called.
                conf.on_suspend.call(*this);
            } 

            conf.on_halt.call(*this);
        } else { 
            HCE_HIGH_METHOD_ENTER("install");
            while(run_()) { } 
        }
    }

    /**
     @brief schedule coroutines

     Arguments to this function can be:
     - an `hce::coroutine` or `hce::co<T>`
     - an iterable container of `hce::coroutine`s or `hce::co<T>`s

     Multi-argument schedule()s hold the scheduler's lock throughout (they are 
     simultaneously scheduled).

     A resuming coroutine that attempts to reschedule on a halted scheduler 
     will fail. This means, to ensure program logic operates correctly, all 
     communication operations between coroutines (via `channel`s or some other 
     mechanism) should be completed before the user allows the scheduler to 
     halt. This typically means all `send()`/`recv()` operations are completed 
     and `channel::close()` is called before a scheduler is halted.

     If a coroutine is *required* to actually finish, as opposed to simply 
     complete a series of required communications, it should be scheduled and 
     awaited with `join()`/`scope()` instead.

     @param a the first argument
     @param as any remaining arguments
     */
    template <typename A, typename... As>
    void schedule(A&& a, As&&... as) {
        HCE_HIGH_METHOD_ENTER("schedule",a,as...);

        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) { 
            schedule_(std::forward<A>(a), std::forward<As>(as)...);
        } 
    }

    /**
     @brief schedule a coroutine and return an awaitable to await the `co_return`ed value

     It is an error if the `co_return`ed value from the coroutine cannot be 
     converted to expected type `T`.

     Attempting to launch a joinable operation (calling either `join()` or 
     `scope()`) on a scheduler when it is halted will throw an exception. User 
     code is responsible for ensuring their coroutines which utilize join 
     mechanisch are cleanly finished before the scheduler halts (or user code 
     must catch and handle exceptions).

     The above can be done by assembling a tree of coroutines synchronized by 
     `join()`/`scope()` and only halting the scheduler (allowing the associated 
     `scheduler::lifecycle` to be destroyed) when the root coroutine of the tree 
     is joined. This is good practice when designing any program which needs to 
     exit cleanly.

     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will return an the result of the completed coroutine
     */
    template <typename T>
    hce::awt<T> join(hce::co<T> co) {
        HCE_HIGH_METHOD_ENTER("join",co);

        std::unique_lock<spinlock> lk(lk_);
        if(state_ == halted) { throw is_halted(this,"join()"); }

        return join_(co);
    }

    /**
     @brief join with all argument co<T>s

     Like `join()`, except it can join with one or more of argument coroutines. 

     Unlike `join()` this method does not return the `co_return`ed values of its 
     argument coroutines. 

     @param as all coroutines to join with
     @return an awaitable which will not resume until all argument coroutines have been joined
     */
    template <typename... As>
    hce::awt<void> scope(As&&... as) {
        // all arguments to scope should be coroutines, and therefore printable
        HCE_HIGH_METHOD_ENTER("scope",as...);

        std::unique_lock<spinlock> lk(lk_);
        if(state_ == halted) { throw is_halted(this,"scope()"); }

        std::unique_ptr<std::deque<hce::awt<void>>> dq(
                new std::deque<hce::awt<void>>);
        assemble_scope_(*dq, std::forward<As>(as)...);

        return hce::awt<void>::make(new scoper(std::move(dq)));
    }

    /**
     @brief start a timer on this scheduler 

     The returned awaitable will result in `true` if the timer timeout was 
     reached, else `false` will be returned if it was cancelled early.

     @param id a reference to an hce::id which will be set to the launched timer's id
     @param as the remaining arguments which will be passed to `hce::chrono::duration()`
     @return an awaitable to join with the timer timing out (returning true) or being cancelled (returning false)
     */
    template <typename... As>
    inline hce::awt<bool> start(hce::id& id, As&&... as) {
        hce::chrono::time_point timeout =
            hce::chrono::duration(std::forward<As>(as)...) + hce::chrono::now();

        HCE_MED_METHOD_ENTER("start",id,timeout);
        
        std::unique_ptr<timer> t(new timer(*this, timeout));

        // acquire the id of the constructed timer
        id = t->id();

        {
            std::lock_guard<spinlock> lk(lk_);

            if(state_ == halted) { 
                // cancel the timer immediately
                t->resume((void*)0); 
            } else { insert_timer_(t); }
        }

        return hce::awt<bool>::make(t.release());
    }

    /**
     @brief start a timer on this scheduler 

     Abstracts away the timer's id to block the awaiter for a time (no way to 
     directly cancel this running timer).

     The returned awaitable will result in `true` if the timer timeout was 
     reached, else `false` will be returned if it was cancelled early.

     @param as the arguments will be passed to `hce::chrono::duration()`
     @return an awaitable to join with the timer timing out or being cancelled
     */
    template <typename... As>
    inline hce::awt<bool> sleep(As&&... as) {
        hce::id i;
        return start(i, std::forward<As>(as)...);
    }

    /**
     @brief determine if a timer with the given id is running
     @return true if the timer is running, else false
     */
    inline bool running(hce::id& id) const {
        HCE_MED_METHOD_ENTER("running",id);
        bool result = false;

        {
            std::lock_guard<spinlock> lk(lk_);
            for(auto& t : timers_) {
                if(t->id() == id) {
                    result = true;
                    break;
                }
            }
        }

        HCE_MED_METHOD_BODY("running",result);
        return result;
    }

    /**
     @brief attempt to cancel a scheduled `hce::scheduler::timer`

     The `hce::id` should be acquired from a call to the 
     `hce::scheduler::start()` method.

     @param id the hce::id associated with the timer to be cancelled
     @return true if cancelled timer successfully, false if timer already timed out or was never scheduled
     */
    inline bool cancel(const hce::id& id) {
        HCE_MED_METHOD_ENTER("cancel",id);

        if(id) {
            timer* t = nullptr;

            {
                std::lock_guard<spinlock> lk(lk_);
                if(state_ == halted) { return false; }

                auto it = timers_.begin();
                auto end = timers_.end();

                while(it!=end) {
                    if((*it)->id() == id) {
                        t = *it;
                        timers_.erase(it);
                        break;
                    }

                    ++it;
                }
            }

            if(t) {
                HCE_MED_METHOD_BODY("cancel","cancelled timer with id:",t->id());
                t->resume((void*)0);
                return true;
            } else {
                return false;
            }
        } else { return false; }
    }

    /**
     @brief execute a Callable on a dedicated thread

     This mechanism allows execution of arbitrary code on a dedicated thread, 
     and and to `co_await` the result of the `block()` (or if not 
     called from a coroutine, just assign the awaitable to a variable of the 
     resulting type). IE in a coroutine:
     ```
     T result = co_await hce::block(my_function_returning_T);
     ```

     Outside a coroutine:
     ```
     T result = hce::block(my_function_returning_T);
     ```

     This allows for executing blocking code (which would be unsafe to do in a 
     coroutine!) via a mechanism which is safely callable *from within* a 
     coroutine.

     The user Callable will execute on a dedicated thread, and won't have direct 
     access to the caller's local scheduler or coroutine (if any). 

     If the caller is already executing in a thread managed by another call to 
     `block()`, or if called outside of an `hce` coroutine, the Callable will be 
     executed immediately on the *current* thread.

     The given Callable can access values owned by the coroutine's body (or 
     values on a thread's stack if called outside of a coroutine) by reference, 
     because the caller of `call()` will be blocked while awaiting the Callable 
     executing on the other thread.

     WARNING: It is highly recommended to immediately `co_await` the awaitable 
     returned by `block()`. If the executing Callable accesses the stack of the 
     caller in an unsynchronized way, and the caller of `block()` is a 
     coroutine, then the caller should `co_await` the returned awaitable before 
     reading or writing others values again. Doing this ensures the coroutine 
     is safely blocked until `block()` completes.

     @param cb a function, Functor, or lambda 
     @param as arguments to cb
     @return an awaitable returning the result of the Callable 
     */
    template <typename Callable, typename... As>
    awt<hce::detail::function_return_type<Callable,As...>> 
    block(Callable&& cb, As&&... as) {
        typedef hce::detail::function_return_type<Callable,As...> RETURN_TYPE;

        HCE_MED_METHOD_ENTER("block",hce::detail::utility::callable_to_string(cb));

        return blocking_.block<RETURN_TYPE>(
            std::forward<Callable>(cb),
            std::forward<As>(as)...);
    }

    /**
     @brief schedule a coroutine to set the scheduler thread's log level 

     The level cannot be set higher than the level specified by compiler define 
     `HCELOGLEVEL`, but can be set lower.
     */
    inline void set_log_level(int level) {
        HCE_TRACE_METHOD_ENTER("log_level",level);
        schedule(scheduler::co_set_log_level(level));
    }

    /// return the state of the scheduler
    inline state status() const {
        HCE_MIN_METHOD_ENTER("status");
        std::unique_lock<spinlock> lk(lk_);
        return state_;
    }

    /**
     @return the count of scheduled coroutines
     */
    inline size_t scheduled() const {
        size_t c;

        {
            std::lock_guard<spinlock> lk(lk_);
            c = evaluating_ + coroutine_queue_->size();
        }
        
        HCE_TRACE_METHOD_BODY("scheduled",c);
        return c;
    }

    /// return the current count of worker threads spawned for `block()`ing tasks
    inline size_t block_workers() const { 
        auto c = blocking_.count(); 
        HCE_TRACE_METHOD_BODY("block_workers",c);
        return c;
    }

    /**
     Worker threads utilized by the `block()` mechanism can be reused by the 
     scheduler, potentially increasing program efficiency when blocking calls 
     need to be regularly made.

     The default count of reused worker threads is 0 threads. However, any 
     number of 0 or higher can be specified by `install()`ing the scheduler with 
     an `hce::scheduler::config` with its `block_workers_reuse_pool` member set
     to the desired count of worker threads to reuse. Consider configuring 
     schedulers with a higher value `block_workers_reuse_pool` if blocking calls 
     are made frequently.

     The default global scheduler's reuse blocker worker count is specified by 
     the `std::unique_ptr<hce::scheduler::config>` returned by global function 
     `hce_scheduler_global_config()`. `hce_scheduler_global_config()` is an 
     `extern` function, and its library default implementation will not be 
     compiled if compiler flag `HCECUSTOMGLOBALCONFIG` is provided during 
     compilation of the library, allows the user to write and link against 
     their own implementation.

     Additionally, the global scheduler's default 
     `hce::scheduler::config::block_workers_reuse_pool` member's value is 
     specified at library compile time by the compiler define 
     `HCEGLOBALREUSEBLOCKPROCS` (which defaults to 1 thread). This value is 
     used in the default implementation of `hce_global_config()`.

     @return the minimum count of `block()` worker threads the scheduler will persist
     */
    inline size_t block_workers_reuse_pool() const { 
        auto c = blocking_.reuse(); 
        HCE_TRACE_METHOD_BODY("block_workers_reuse_pool",c);
        return c;
    }
    
    /**
     @brief schedule a coroutine to acquire and return the scheduler thread's log level 
     @return an awaitable to return the log level
     */
    inline awt<int> log_level() {
        HCE_TRACE_METHOD_ENTER("log_level");
        return join(scheduler::co_get_log_level());
    }
 
private:
    typedef std::unique_ptr<std::deque<std::coroutine_handle<>>> coroutine_queue;

    struct scoper :
        public reschedule<
            hce::awaitable::lockable<
                awt<void>::interface, 
                hce::spinlock>>
    {
        template <typename... As>
        scoper(std::unique_ptr<std::deque<awt<void>>>&& awts) :
            reschedule<
                awaitable::lockable<
                    awt<void>::interface, 
                    hce::spinlock>>(
                        lk_,
                        hce::awaitable::await::policy::defer,
                        hce::awaitable::resume::policy::lock),
            awts_(std::move(awts))
        { }

        virtual ~scoper() { }

        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "scoper"; }

        inline bool on_ready() { 
            scheduler::get().schedule(scoper::op(this, awts_.get()));
            return false;
        }

        inline void on_resume(void* m) { }

        static inline co<void> op(
                scoper* sa, 
                std::deque<hce::awt<void>>* awts) {

            for(auto& awt : *awts) {
                co_await hce::awt<void>(std::move(awt));
            }

            // once all awaitables in the scope are joined, return
            sa->resume(nullptr);
            co_return;
        }

    private:
        hce::spinlock lk_;
        std::unique_ptr<std::deque<awt<void>>> awts_;
    };

    struct timer : 
        public hce::scheduler::reschedule<
            hce::awaitable::lockable<
                hce::awt<bool>::interface,
                hce::spinlock>>
    {
        timer(hce::scheduler& parent,
              const hce::chrono::time_point& tp) : 
            hce::scheduler::reschedule<
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    hce::spinlock>>(
                        slk_,
                        hce::awaitable::await::defer,
                        hce::awaitable::resume::lock),
            tp_(tp),
            id_(std::make_shared<bool>()),
            ready_(false),
            result_(false),
            parent_(parent)
        { }

        virtual ~timer(){
            if(!ready_) {
                // need to cancel because the awaitable was not be awaited 
                // properly...
                auto parent = parent_.lock();
                if(parent) { parent->cancel(id()); }
            }
        }

        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "timer"; }

        inline std::string content() const { 
            std::stringstream ss;
            ss << id_ << ", " << tp_;
            return ss.str(); 
        }

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

        inline const hce::chrono::time_point& timeout() const { return tp_; }
        inline const hce::id& id() const { return id_; }

    private:
        hce::chrono::time_point tp_;
        hce::id id_;
        bool ready_; 
        bool result_;
        hce::spinlock slk_;
        std::weak_ptr<scheduler> parent_;
    };

    struct blocking : public hce::printable {
        // block workers implicitly start a scheduler on a new thread during 
        // construction and shutdown said scheduler during destruction.
        struct worker : public hce::printable {
            worker() : sch_([&]() -> std::shared_ptr<hce::scheduler> {
                    auto sch = hce::scheduler::make(lf_);
                    std::thread([sch]() mutable { sch->install(); }).detach();
                    return sch;
                }())
            { 
                HCE_MED_CONSTRUCTOR();
            }

            ~worker() { HCE_MED_DESTRUCTOR(); }

            // thread_local value defaults to false
            static bool& tl_is_block();

            inline const char* nspace() const { return "hce::scheduler::blocking"; }
            inline const char* name() const { return "worker"; }
            inline std::string content() const { return sch_->to_string(); }

            // return the scheduler installed on the worker's thread
            hce::scheduler& scheduler() { return *sch_; }

        private: 
            std::unique_ptr<hce::scheduler::lifecycle> lf_;
            std::shared_ptr<hce::scheduler> sch_;
        };

        template <typename T>
        struct done : public 
                hce::scheduler::reschedule<
                    hce::detail::scheduler::done_partial<T>>
        {
            template <typename... As>
            done(As&&... as) : 
                hce::scheduler::reschedule<
                    hce::detail::scheduler::done_partial<T>>(
                        std::forward<As>(as)...)
            { }
        };

        // contractors are transient managers of block worker threads, ensuring 
        // they are checked out and back in at the proper times
        template <typename T>
        struct contractor : public detail::scheduler::cleanup_handler<T> {
            contractor(hce::scheduler& parent) : 
                // on construction get a worker
                wkr_(parent.blocking_.checkout_worker_()),
                wparent_(parent)
            { }

            virtual ~contractor() { 
                // on destruction return the worker
                if(wkr_){ 
                    auto sch = wparent_.lock();

                    if(sch) {
                        sch->blocking_.checkin_worker_(std::move(wkr_)); 
                    }
                } 
            }

            inline void operator()(typename hce::co<T>::promise_type& p){ }

            // return the worker's scheduler 
            inline hce::scheduler& scheduler() { return wkr_->scheduler(); }

        private:
            std::unique_ptr<worker> wkr_;
            std::weak_ptr<hce::scheduler> wparent_;
        };

        blocking(hce::scheduler& parent) : 
            reuse_cnt_(0),
            count_(0),
            parent_(parent)
        { }

        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "blocking"; }

        inline void set_worker_reuse_count(size_t reuse_count) {
            reuse_cnt_ = reuse_count;
        }

        template <typename T, typename Callable, typename... As>
        hce::awt<T>
        block(Callable&& cb, As&&... as) {
            if(!scheduler::in() || worker::tl_is_block()) {
                HCE_TRACE_METHOD_BODY("block","executing current thread");
                
                /// we own the thread, call cb immediately and return the result
                return hce::awt<T>::make(
                    new done<T>(cb(std::forward<As>(as)...)));
            } else {
                HCE_TRACE_METHOD_BODY("block","executing on worker thread");
                // get a contractor to do the work of managing threads
                std::unique_ptr<contractor<T>> cr(
                    new contractor<T>(parent_)); 
                auto& sch = cr->scheduler(); // acquire the contractor's scheduler

                // convert our Callable to a coroutine
                hce::co<T> co = detail::scheduler::wrapper<T>(
                    [cb=std::forward<Callable>(cb),
                     ... as=std::forward<As>(as)]() mutable -> T {
                        return cb(std::forward<As>(as)...);
                    });

                // install the contractor as a coroutine cleanup handler to 
                // check in the worker when the coroutine goes out of scope
                co.promise().install(
                    std::unique_ptr<detail::scheduler::cleanup_handler<T>>(
                        dynamic_cast<detail::scheduler::cleanup_handler<T>*>(
                            cr.release())));

                // schedule the coroutine on the contractor's worker thread
                return sch.join(std::move(co));
            }
        }

        template <typename Callable, typename... As>
        hce::awt<void>
        block(Callable&& cb, As&&... as) {
            if(!scheduler::in() || worker::tl_is_block()) {
                HCE_TRACE_METHOD_BODY("block","executing current thread");
                cb(std::forward<As>(as)...);
                return hce::awt<void>::make(new done<void>());
            } else {
                HCE_TRACE_METHOD_BODY("block","executing on worker thread");
                std::unique_ptr<contractor<void>> cr(
                    new contractor<void>(parent_)); 
                auto& sch = cr->scheduler(); 

                hce::co<void> co = detail::scheduler::wrapper<void>(
                    [cb=std::forward<Callable>(cb),
                     ... as=std::forward<As>(as)]() mutable -> void {
                        cb(std::forward<As>(as)...);
                    });

                co.promise().install(
                    std::unique_ptr<detail::scheduler::cleanup_handler<void>>(
                        dynamic_cast<detail::scheduler::cleanup_handler<void>*>(
                            cr.release())));

                return sch.join(std::move(co));
            }
        }

        // return the reused count of managed blocking threads
        inline size_t reuse() const { 
            HCE_TRACE_METHOD_BODY("reuse",reuse_cnt_);
            return reuse_cnt_; 
        }


        // return the current count of managed blocking threads
        inline size_t count() const { 
            size_t c;

            {
                std::unique_lock<hce::spinlock> lk(lk_);
                c = count_ + workers_.size();
            }

            HCE_TRACE_METHOD_BODY("count",c);
            return c; 
        }

    private:
        inline std::unique_ptr<worker> checkout_worker_() {
            std::unique_ptr<worker> w;

            HCE_TRACE_METHOD_BODY("checkin_worker_","++count");
            std::unique_lock<spinlock> lk(lk_);
            ++count_;

            if(workers_.size()) {
                // get the first available worker
                w = std::move(workers_.front());
                workers_.pop_front();
                lk.unlock();
            } else {
                // as a fallback generate a new await worker thread
                w = std::unique_ptr<worker>(new worker);
            }

            return w;
        }

        inline void checkin_worker_(std::unique_ptr<worker> w) {
            HCE_TRACE_METHOD_BODY("checkin_worker_","--count");
            std::unique_lock<spinlock> lk(lk_);
            --count_;

            if(workers_.size() < reuse_cnt_) {
                workers_.push_back(std::move(w));
            } 
        }

        mutable spinlock lk_;

        // reused block() worker thread count
        size_t reuse_cnt_; 
        size_t count_;
        hce::scheduler& parent_;

        // worker memory
        std::deque<std::unique_ptr<worker>> workers_;
    };

    scheduler() : 
            // state_ persists between suspends
            state_(ready), 
            // persists as necessary
            coroutine_queue_(new std::deque<std::coroutine_handle<>>),
            blocking_(*this)
    { 
        HCE_HIGH_CONSTRUCTOR();
        reset_flags_(); // initialize flags
    }
    
    static scheduler& global_();

    static inline co<void> co_set_log_level(int level) {
        hce::printable::thread_log_level(level);
        co_return;
    }

    static inline co<int> co_get_log_level() {
        co_return hce::printable::thread_log_level();
    }

    inline void assemble_scope_(std::deque<hce::awt<void>>& dq) { }

    template <typename A, typename... As>
    void assemble_scope_(
            std::deque<hce::awt<void>>& dq, 
            A&& a, 
            As&&... as) 
    {
        detail::scheduler::assemble_joins_(dq, join_(a));
        assemble_scope_(dq, std::forward<As>(as)...);
    }

    inline void insert_timer_(std::unique_ptr<timer>& t) {
        timers_.push_back(t.get());
        timers_.sort([](timer* lhs, timer* rhs) {
            // resort timers from soonest to latest
            return lhs->timeout() < rhs->timeout();
        });

        tasks_available_notify_();
    }

    /*
     Coroutine to run the scheduler and execute coroutines and timers continuously.

     Returns true on suspend, false on halt.
     */
    inline bool run_() {
        HCE_MED_METHOD_ENTER("run_");

        // only call function tl_this_scheduler() once to acquire reference to 
        // thread shared scheduler pointer 
        auto& tl_sch = detail::scheduler::tl_this_scheduler();

        // acquire the parent, if any, of the current coroutine scheduler
        auto parent_sch = tl_sch;
        
        // assign thread_local scheduler pointer to this scheduler
        tl_sch = this; 

        // batch of coroutines to evaluate
        coroutine_queue local_queue(new std::deque<std::coroutine_handle<>>);

        // ready timer batch
        std::deque<timer*> ready_timers;

        // Push any remaining coroutines back into the main queue. Lock must be 
        // held before this is called.
        auto requeue_coroutines = [&] {
            evaluating_ = 0; // reset scheduler evaluating count 
            auto& src = *local_queue;
            auto& dst = *coroutine_queue_;

            while(src.size()) {
                dst.push_back(src.front());
                src.pop_front();
            }
        };

        // acquire the lock
        std::unique_lock lk(lk_);

        // block until no longer suspended
        while(state_ == suspended) { 
            HCE_MED_METHOD_BODY("run_","suspended before run loop");
            resume_block_(lk); 
        }
        
        HCE_MED_METHOD_BODY("run_","entering run loop");

        // If halt_() has been called, return immediately only one caller of 
        // run() is possible. A double run() will cause a halt_().
        if(state_ == ready) {
            // the current caller of run() claims this scheduler, any calls to 
            // run() while it is already running will halt the scheduler 
            state_ = executing; 

            // flag to control evaluation loop
            bool evaluate = true;

            try {
                // evaluation loop
                while(evaluate) {
                    if(coroutine_queue_->size()) {
                        // acquire the current batch of coroutines by trading 
                        // with the scheduler's queue, reducing lock contention 
                        // by collecting the entire current batch
                        std::swap(local_queue, coroutine_queue_);
                        evaluating_ = local_queue->size();
                    }

                    // check for any ready timers 
                    if(timers_.size()) {
                        auto now = hce::chrono::now();
                        auto it = timers_.begin();
                        auto end = timers_.end();

                        while(it!=end) {
                            // check if a timer is ready to timeout
                            if((*it)->timeout() <= now) {
                                ready_timers.push_back(*it);
                                it = timers_.erase(it);
                            } else {
                                // no remaining ready timers, exit loop early
                                break;
                            }
                        }
                    }
                        
                    // unlock scheduler when running executing coroutines and timers
                    lk.unlock();

                    auto& src = *local_queue;
                    size_t count = src.size();

                    // scope coroutine to ensure lifecycle management occurs
                    {
                        coroutine co;

                        // evaluate the batch of coroutines once through
                        while(count) { 
                            // decrement from our initial batch count
                            --count;

                            // get a new task
                            co.reset(src.front());
                            src.pop_front();

                            // evaluate coroutine
                            co.resume();

                            // check if the coroutine yielded
                            if(co && !co.done()) {
                                // locally re-enqueue coroutine 
                                src.push_back(co.release()); 
                            } 
                        }
                    }

                    // reschedule any ready timers
                    while(ready_timers.size()) {
                        auto t = ready_timers.front();
                        ready_timers.pop_front();
                        t->resume((void*)1);
                    }

                    // reacquire lock
                    lk.lock(); 

                    // cleanup batch results
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
                                    timers_.front()->timeout());
                            }
                        } else {
                            // break out of the evaluation loop
                            evaluate=false;
                        }
                    }

                    update_executing_state_();
                }
            } catch(...) { // catch all other exceptions 
                // reset state in case of uncaught exception
                tl_sch = parent_sch; 

                // it is an error in this framework if an exception occurs when 
                // the lock is held, it should only be when executing user 
                // coroutines that this can occur
                lk.lock();

                requeue_coroutines();

                lk.unlock();

                std::rethrow_exception(std::current_exception());
            }
        }

        // restore parent thread_local pointer
        tl_sch = parent_sch; 

        // move any coroutines in the stack local queue to the object's queue
        requeue_coroutines();

        if(state_ == suspended) {
            HCE_MED_METHOD_BODY("run_","exitted run loop, suspended");
            // reset scheduler state so run() can be called again
            reset_flags_();
            return true;
        } else {
            HCE_MED_METHOD_BODY("run_","exitted run loop, halted");
            // clear queues so memory can be released 
            clear_queues_();
            halt_notify_();
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
     scheduler. 

     This operation also deletes any scheduled coroutines. Any coroutines
     scheduled on a halted scheduler are thrown out.
     
     As a general rule, `scheduler`s are intended to run indefinitely and 
     `halt_()` should only be called on process shutdown. Failure to do so can 
     cause strange program errors where code which is expected to run does not.

     If called by a different thread than the one the scheduler is running on,
     will block until scheduler is halted.
     */
    inline void halt_() { 
        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) {
            bool was_running = state_ == executing; 

            // set the scheduler to the permanent halted state
            state_ = halted;

            // resume scheduler if necessary
            resume_notify_();

            if(detail::scheduler::tl_this_scheduler() != this) {
                // wakeup scheduler if necessary
                tasks_available_notify_();
            
                if(was_running) {
                    // block until halted
                    HCE_MED_METHOD_BODY("halt_","waiting");
                    waiting_for_halt_ = true;
                    halt_cv_.wait(lk);
                }
            }
        }
    }

    template <typename T>
    awt<T> join_(hce::co<T>& co) {
        /// returned awaitable resumes when the coroutine handle is destroyed
        auto awt = hce::awt<T>::make(
            new hce::scheduler::reschedule<
                hce::detail::scheduler::joiner<T>>(co));
        schedule_(std::move(co));
        return awt;
    }

    // Reset scheduler state flags, etc. Does not reset scheduled coroutine
    // queues. 
    //
    // This method can ONLY be safely called by the constructor or run()
    inline void reset_flags_() {
        evaluating_ = 0;
        waiting_for_resume_ = false;
        waiting_for_tasks_ = false;
        waiting_for_halt_ = false;
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
            t->resume((void*)0);
            // no delete, awaitable will do that
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

    inline void halt_notify_() {
        if(waiting_for_halt_) {
            HCE_MED_METHOD_BODY("halt_notify_");
            waiting_for_halt_ = false;
            halt_cv_.notify_one();
        }
    }

    inline void update_executing_state_() {
        // reacquire running state in edgecase where suspend() and resume()
        // happened quickly
        if(state_ == ready) { state_ = executing; }
    }

    // notify caller of run() that tasks are available
    inline void tasks_available_notify_() {
        HCE_TRACE_METHOD_ENTER("tasks_available_notify_");
        // only do notify if necessary
        if(waiting_for_tasks_) {
            HCE_TRACE_METHOD_BODY("tasks_available_notify_","notified");
            waiting_for_tasks_ = false;
            tasks_available_cv_.notify_one();
        }
    }
    
    inline void schedule_coroutine_(std::coroutine_handle<> h) {
        HCE_TRACE_METHOD_ENTER("schedule_coroutine_",h);

        if(!(h.done())) { 
            HCE_TRACE_METHOD_BODY("schedule_coroutine_","pushing onto queue");
            coroutine_queue_->push_back(h); 
        } 
    }
    
    // schedule an individual coroutine's handle
    inline void schedule_coroutine_(coroutine c) {
        HCE_TRACE_METHOD_ENTER("schedule_coroutine_",c);

        if(c) { 
            HCE_TRACE_METHOD_BODY("schedule_coroutine_","extracting handle");
            schedule_coroutine_(c.release()); 
        }
    }
   
    // schedule an individual templated coroutine's handle
    template <typename T>
    inline void schedule_coroutine_(co<T> c) {
        HCE_TRACE_METHOD_ENTER("schedule_coroutine_",c);

        if(c) { 
            schedule_coroutine_(c.release()); 
        }
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

    // synchronization primative
    mutable hce::spinlock lk_;

    // the current state of the scheduler
    state state_; 

    // run configuration
    std::unique_ptr<config> config_;
                  
    // the count of executing coroutines on this scheduler 
    size_t evaluating_; 
   
    // condition data for when scheduler::resume() is called
    bool waiting_for_resume_;
    std::condition_variable_any resume_cv_;
   
    // condition data for when scheduler::halt_() is called
    bool waiting_for_halt_;
    std::condition_variable_any halt_cv_;

    // condition data for when new tasks are available
    bool waiting_for_tasks_; // true if waiting on tasks_available_cv_
    std::condition_variable_any tasks_available_cv_;

    // a weak_ptr to the scheduler's shared memory
    std::weak_ptr<scheduler> self_wptr_;

    // Queue holding scheduled coroutine handles. Simpler to just use underlying 
    // data than wrapper conversions between hce::coroutine/hce::co<T>. This 
    // object is a unique_ptr because it is routinely swapped between this 
    // object and the stack memory of the caller of scheduler::run().
    coroutine_queue coroutine_queue_;

    // list holding scheduled timers. This will be regularly resorted from 
    // soonest to latest timeout. Timer pointers are allocated and must be 
    // deleted by this object when they go out of scope.
    std::list<timer*> timers_;

    // the service object memory managing blocking calls and their threads
    blocking blocking_;

    friend struct lifecycle;
    friend struct blocking;
};


// return the allocated blocking service object
/**
 @brief call schedule() on a scheduler
 @param as arguments for scheduler::schedule()
 */
template <typename... As>
void schedule(As&&... as) {
    HCE_HIGH_FUNCTION_ENTER("schedule");
    scheduler::get().schedule(std::forward<As>(as)...);
}

/**
 @brief call join() on a scheduler
 @param as arguments for scheduler::join()
 @return result of join()
 */
template <typename... As>
auto join(As&&... as) {
    HCE_HIGH_FUNCTION_ENTER("join",as...);
    return scheduler::get().join(std::forward<As>(as)...);
}

/**
 @brief call scope() on a scheduler
 @param as arguments for scheduler::scope()
 @return result of scope()
 */
template <typename... As>
auto scope(As&&... as) {
    HCE_HIGH_FUNCTION_ENTER("scope");
    return scheduler::get().scope(std::forward<As>(as)...);
}

/**
 @brief call sleep() on a scheduler
 @param as arguments for scheduler::sleep()
 @return result of sleep()
 */
template <typename... As>
auto sleep(As&&... as) {
    HCE_MED_FUNCTION_ENTER("sleep");
    return scheduler::get().sleep(std::forward<As>(as)...);
}

/**
 @brief call block() on a scheduler
 @param as arguments for scheduler::block()
 @return result of block()
 */
template <typename... As>
auto block(As&&... as) {
    HCE_MED_FUNCTION_ENTER("block");
    return scheduler::get().block(std::forward<As>(as)...);
}

}

#endif
