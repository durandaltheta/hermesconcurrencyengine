//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SCHEDULER__
#define __HERMES_COROUTINE_ENGINE_SCHEDULER__ 

// c++
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <list>
#include <string>
#include <sstream>
#include <thread>

// local 
#include "logging.hpp"
#include "utility.hpp"
#include "atomic.hpp"
#include "memory.hpp"
#include "circular_buffer.hpp"
#include "list.hpp"
#include "synchronized_list.hpp"
#include "id.hpp"
#include "chrono.hpp"
#include "coroutine.hpp"

namespace hce {

struct scheduler;

struct scheduler_halted_exception : public std::exception {
    scheduler_halted_exception(scheduler* sch, const char* op) : 
        estr([&]() -> std::string {
            std::stringstream ss;
            ss << "op failed:" << op << ", scheduler is halted:" << sch;
            return ss.str();
        }())
    { }

    inline const char* what() const noexcept { return estr.c_str(); }

private:
    const std::string estr;
};

struct awaitable_destroyed_without_joining_result : public std::exception {
    awaitable_destroyed_without_joining_result(void* c, void* j) :
        estr([&]() -> std::string {
            std::stringstream ss;
            ss << "std::coroutine_handle<>@" 
               << (void*)c
               << " was destroyed before it was completed, so " 
               << (hce::printable*)j
               << " cannot join with it";
            return ss.str();
        }()) 
    { }

    inline const char* what() const noexcept { return estr.c_str(); }

private:
    const std::string estr;
};

namespace config {
namespace scheduler {
   
// specify the value for `hce::scheduler::config::coroutine_resource_limit`
extern size_t default_resource_limit();

}
}

namespace detail {
namespace scheduler {

// the current scheduler
hce::scheduler*& tl_this_scheduler();

// the queue to the thread_local current scheduler, used for lockless reschedule
std::unique_ptr<hce::list<std::coroutine_handle<>>>*& 
tl_this_scheduler_local_queue();

/*
 An implementation of hce::awt<T>::interface capable of joining a coroutine 

 Requires implementation for awaitable::interface::destination().
 */
template <typename T>
struct joiner : 
    public hce::awaitable::lockable<
        hce::spinlock,
        hce::awt_interface<T>>
{
    joiner(hce::co<T>& co) :
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt_interface<T>>(
                lk_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
        ready_(false),
        address_(co.address())
    { 
        HCE_TRACE_CONSTRUCTOR(co);

        // install a cleanup handler to resume the returned awaitable when 
        // the coroutine goes out of scope 
        hce::get_promise(co).install(&joiner<T>::cleanup, this);
    }

    virtual ~joiner(){}

    static inline std::string info_name() { 
        return type::templatize<T>("hce::detail::scheduler::joiner"); 
    }

    inline std::string name() const { return joiner<T>::info_name(); }
    inline void* address() { return address_; }
    inline bool on_ready() { return ready_; }

    inline void on_resume(void* m) { 
        HCE_TRACE_METHOD_ENTER("on_resume",m);
        ready_ = true;

        if(m) [[likely]] { 
            // move the unique pointer from the promise to this object
            std::unique_ptr<T>* result = static_cast<std::unique_ptr<T>*>(m);
            t_ = std::move(*result); 
        } 
    }

    inline T get_result() { 
        if(!t_) [[unlikely]] { 
            throw awaitable_destroyed_without_joining_result(address_, this); 
        }

        return std::move(*t_); 
    }

private:
    /*
     The purpose of joiner<T>::cleanup is to act as a connector between a 
     co<T>'s promise_type, which contains the result of `co_return`, and the 
     awaitable (an allocated instance of joiner<T>) returned by 
     `scheduler::schedule()`, which needs the result of `co_return`. This 
     handler is triggered just before the promise_type is destroyed and passes 
     around the necessary data to the awaitable (joiner<T>) and resumes it.
     */
    static inline void cleanup(hce::coroutine::promise_type::cleanup_data& data) { 
        auto fname = "joiner<T>::cleanup";
        HCE_TRACE_FUNCTION_ENTER(fname, data.install, data.promise);
        // acquire a reference to the joiner<T>
        auto& joiner = *(static_cast<hce::detail::scheduler::joiner<T>*>(data.install));

        // acquire a reference to the promise
        auto& promise = *(static_cast<hce::co<T>::promise_type*>(data.promise));

        // get a copy of the handle to see if the coroutine completed 
        auto handle = std::coroutine_handle<hce::co_promise_type<T>>::from_promise(promise);

        if(handle.done()) [[likely]] {
            //HCE_MIN_FUNCTION_BODY("joiner<T>::cleanup()","done@",handle);
            HCE_TRACE_FUNCTION_BODY(fname,"done@",handle);
            // resume the blocked awaitable and pass the allocated result pointer
            joiner.resume(&(promise.result));
        } else [[unlikely]] {
            HCE_ERROR_FUNCTION_BODY(fname,"NOT done@",handle);
            // resume with no result, presumably triggering error
            joiner.resume(nullptr);
        } 
    };

    spinlock lk_;
    bool ready_;
    void* address_;
    std::unique_ptr<T> t_;
};

// joiner variant for coroutine returning void 
template <>
struct joiner<void> : 
    public hce::awaitable::lockable<
        hce::spinlock,
        hce::awt_interface<void>>
{
    joiner(hce::co<void>& co) :
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt_interface<void>>(
                lk_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
        ready_(false),
        address_(co.address())
    { 
        HCE_TRACE_CONSTRUCTOR(co);

        hce::get_promise(co).install(&joiner<void>::cleanup, this);
    }

    virtual ~joiner(){}
    
    static inline std::string info_name() { 
        return type::templatize<void>("hce::detail::scheduler::joiner"); 
    }

    inline std::string name() const { return joiner<void>::info_name(); }
    inline void* address() { return address_; }
    inline bool on_ready() { return ready_; }
    inline void on_resume(void* m) { ready_ = true; }

private:
    static inline void cleanup(hce::coroutine::promise_type::cleanup_data& data) { 
        HCE_TRACE_FUNCTION_ENTER("joiner<void>::cleanup()", data.install, data.promise);
        static_cast<hce::detail::scheduler::joiner<void>*>(data.install)->resume(nullptr);
    }

    spinlock lk_;
    bool ready_;
    void* address_;
};

// sync operations are lockfree because they are allocated on the same thread 
// as the caller and are immediately complete (no interthread communication 
// required)
template <typename T>
struct sync_partial : 
    public hce::awaitable::lockable<
        hce::lockfree,
        hce::awt_interface<T>>
{
    template <typename... As>
    sync_partial(As&&... as) : 
        hce::awaitable::lockable<
            hce::lockfree,
            hce::awt_interface<T>>(
                lf_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
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
struct sync_partial<void> : public 
        hce::awaitable::lockable<
            hce::lockfree,
            hce::awt_interface<void>>
{
    sync_partial() :
        hce::awaitable::lockable<
            hce::lockfree,
            hce::awt_interface<void>>(
                lf_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock)
    { }

    inline bool on_ready() { return true; }
    inline void on_resume(void* m) { }

private:
    hce::lockfree lf_;
};

// async operations require a lock because they are used to communicate across
// thread boundaries
template <typename T>
struct async_partial : 
    public hce::awaitable::lockable<
        hce::spinlock,
        hce::awt_interface<T>>
{
    async_partial() : 
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt_interface<T>>(
                lk_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
        ready_(false)
    { }

    inline bool on_ready() { return ready_; }

    // this will never be called *except* in cases where m!=nullptr
    inline void on_resume(void* m) { 
        t_ = std::unique_ptr<T>((T*)m);
        ready_ = true;
    }

    inline T get_result() { return std::move(*t_); }

private:
    hce::spinlock lk_;
    bool ready_;
    std::unique_ptr<T> t_;
};

template <>
struct async_partial<void> : 
    public hce::awaitable::lockable<
        hce::spinlock,
        hce::awt_interface<void>>
{
    async_partial() : 
        hce::awaitable::lockable<
            hce::spinlock,
            hce::awt_interface<void>>(
                lk_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
        ready_(false)
    { }

    inline bool on_ready() { return ready_; }
    inline void on_resume(void* m) { ready_ = true; }

private:
    hce::spinlock lk_;
    bool ready_;
};

// interface for detacher object which will await argument awaitables for user code
struct detacher {
    virtual hce::awt<void> await(hce::awaitable::interface* ai) = 0;
    virtual void shutdown() = 0;

    // construct the detacher 
    static std::unique_ptr<detacher> make(hce::scheduler*);
};

}
}

/** 
 @brief object responsible for scheduling and executing coroutines and timers
 
 A scheduler` cannot be created directly, it must be created by calling 
 `scheduler::make()`.

 `scheduler` API, unless otherwise specified, is threadsafe and coroutine-safe.
 That is, it can be called from anywhere safely, including from within a 
 coroutine running on the scheduler which is being accessed.

 `scheduler`s run independently on their own system threads. However, their 
 lifecycle is controlled and synchronized through a globally accessible 
 `lifecycle::manager` object. This object allows the user to suspend and resume
 execution of coroutines as necessary, and is responsible for ensuring that
 schedulers properly halted when the process exits, taking into account 
 non-deterministic runtime behavior of scheduled coroutines.
*/
struct scheduler : public printable {
    /// an enumeration which represents the scheduler's current state
    enum state {
        executing, /// scheduler is executing coroutines
        suspended, /// execution paused by a call to `lifecycle::suspend()`
        halted /// scheduler is completely halted
    };

    struct scheduler_cannot_run_in_a_coroutine_exception : public std::exception {
        scheduler_cannot_run_in_a_coroutine_exception() : 
            estr([]() -> std::string {
                std::stringstream ss;
                ss << coroutine::local()
                   << " attempted to run an hce::scheduler";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    /**
     @brief implementation of hce::awaitable::interface::destination()

     Allows rescheduling a coroutine on a parent scheduler when an awaitable 
     implementation is resumed. 

     This is only a partial implementation of hce::awaitable::interface, and is 
     intended to be used as part of the inheritted interface of a complete 
     implementation.
     */
    template <typename INTERFACE>
    struct reschedule : public INTERFACE {
        /// framework selects the scheduler to reschedule on
        template <typename... As>
        reschedule(As&&... as) : INTERFACE(std::forward<As>(as)...) { }

        /// reschedule on a specific scheduler
        template <typename... As>
        reschedule(hce::scheduler& sch, As&&... as) :
            INTERFACE(std::forward<As>(as)...),
            destination_(sch)
        { }

        virtual ~reschedule(){}

        /// pass a resumed coroutine to its destination
        inline void destination(std::coroutine_handle<> h) {
            HCE_LOW_METHOD_ENTER("destination",h);

            auto d = destination_.lock();

            if(d) { 
                HCE_LOW_METHOD_BODY("destination",*d,h);
                d->reschedule_(hce::coroutine(std::move(h)));
            }
        }
       
        /// acquire the destination
        inline void on_suspend() {
            auto& tl_sch = detail::scheduler::tl_this_scheduler();

            /* 
             Now that we're suspending we know to try and acquire a 
             destination. If we're not in a scheduler then we're going to block 
             on the thread_local condition variable and destination() won't be 
             called.
             */
            if(tl_sch) {
                destination_ = *tl_sch;
            }
        }

    private:
        // a weak_ptr to the scheduler we will reschedule on
        std::weak_ptr<scheduler> destination_;
    };

    /**
     @brief a complete awaitable interface capable of joining with a coroutine  

     An allocated instance of this object can be passed to 
     `hce::awt<T>()` and awaited. 

     One potential usecase for this mechanism is to await a coroutine that is 
     being executed *outside* an `hce::scheduler`. It is used internally in all 
     `hce::scheduler::schedule()` operations.
     */
    template <typename T>
    struct joiner : 
        public hce::scheduler::reschedule<hce::detail::scheduler::joiner<T>> 
    { 
        /**
         Join the coroutine and resume any suspended coroutine on a selected
         scheduler
         */
        joiner(hce::co<T>& co) :
            hce::scheduler::reschedule<hce::detail::scheduler::joiner<T>>(
                hce::scheduler::get(), 
                co)
        { 
            HCE_MIN_CONSTRUCTOR(co);
        }

        /**
         Join the coroutine and resume any suspended coroutine on the specified 
         scheduler
         */
        joiner(hce::scheduler& sch, hce::co<T>& co) :
            hce::scheduler::reschedule<hce::detail::scheduler::joiner<T>>(
                sch, 
                co)
        { 
            HCE_MIN_CONSTRUCTOR(sch, co);
        }

        virtual ~joiner() { HCE_MIN_DESTRUCTOR(); }

        static inline std::string info_name() { 
            return type::templatize<T>("hce::scheduler::joiner"); 
        }

        inline std::string name() const { return joiner<T>::info_name(); }

        inline std::string content() {
            std::stringstream ss;
            ss << std::coroutine_handle<>::from_address(this->address());
            return ss.str();
        }
    };

    /**
     @brief object controlling the lifecycle of a scheduler
     */
    struct lifecycle : public printable {
        /**
         @brief process-wide object responsible for managing global scheduler lifecycles
         */
        struct manager : public printable {
            virtual ~manager() { 
                HCE_HIGH_DESTRUCTOR(); 
            }

            static inline std::string info_name() { 
                return "hce::scheduler::lifecycle::manager"; 
            }

            inline std::string name() const { return manager::info_name(); }

            /** 
             @brief access the process wide manager object
             @return a reference to the manager 
             */
            static manager& instance();

            inline std::string content() const { 
                std::stringstream ss;
                auto it = lifecycle_pointers_.begin();
                auto end = lifecycle_pointers_.end();

                if(it != end) {
                    ss << **it;
                    ++it;

                    for(; it!=end; ++it) {
                        ss << ", " << **it;
                    }
                }

                return ss.str();
            }

            /**
             @brief register a lifecycle pointer to be destroyed at process exit
             */
            inline void registration(std::unique_ptr<scheduler::lifecycle> lptr) {
                if(lptr) {
                    HCE_HIGH_METHOD_ENTER("registration", *lptr);

                    std::lock_guard<hce::spinlock> lk(lk_);

                    // Synchronize the new lifecycle with the global state
                    if(state_ == executing) { lptr->resume(); }
                    else if(state_ == suspended) { lptr->suspend(); }

                    lifecycle_pointers_.push_back(std::move(lptr)); 
                }
            }

            /*
             @brief temporarily suspend all running schedulers executing coroutines
             */
            inline void suspend() {
                HCE_HIGH_METHOD_ENTER("suspend");
                std::lock_guard<hce::spinlock> lk(lk_);

                state_ = suspended;
                for(auto& lp : lifecycle_pointers_) { lp->suspend(); }
            }

            /**
             @brief resume coroutine execution on all schedulers after a call to `suspend()`
             */
            inline void resume() {
                HCE_HIGH_METHOD_ENTER("resume");
                std::lock_guard<hce::spinlock> lk(lk_);

                if(state_ == suspended) {
                    state_ = executing;
                    for(auto& lp : lifecycle_pointers_) { lp->resume(); }
                }
            }

        private:
            manager() : state_(executing) { 
                HCE_HIGH_CONSTRUCTOR(); 
            }

            hce::spinlock lk_;
            hce::scheduler::state state_;

            // it is important that the lifecycles are destroyed last, so this
            // list is declared first (destructors are called first-in-last-out)
            std::list<std::unique_ptr<scheduler::lifecycle>> lifecycle_pointers_;
        };

        /**
         @brief shutdown the scheduler
         */
        virtual ~lifecycle(){ 
            HCE_HIGH_DESTRUCTOR(); 
            sch_->halt_(); // halt the scheduler
            thd_.join(); // join the scheduler's thread
        }

        static inline std::string info_name() { 
            return "hce::scheduler::lifecycle"; 
        }

        inline std::string name() const { return lifecycle::info_name(); }

        inline std::string content() const {
            std::stringstream ss;
            ss << sch_.get() << ", std::thread::id@" << thd_.get_id();
            return ss.str();
        }

        /**
         @return a reference to the `lifecycle`'s associated scheduler
         */
        inline hce::scheduler& scheduler() { 
            HCE_HIGH_METHOD_ENTER("scheduler");
            return *sch_; 
        }

        /*
         @brief temporarily suspend the scheduler from executing coroutines

         A suspended scheduler will cease processing coroutines until either 
         resume() is called or the `lifecycle` goes out of scope.
         */
        inline void suspend() { 
            HCE_HIGH_METHOD_ENTER("suspend");
            return sch_->suspend_(); 
        }

        /**
         @brief resume coroutine execution on the scheduler after a call to `suspend()`
         */
        inline void resume() { 
            HCE_HIGH_METHOD_ENTER("resume");
            return sch_->resume_(); 
        }

    private:
        lifecycle() = delete;
        lifecycle(lifecycle&&) = delete;
        lifecycle(const lifecycle&) = delete;
        lifecycle& operator=(lifecycle&&) = delete;
        lifecycle& operator=(const lifecycle&) = delete;

        lifecycle(std::shared_ptr<hce::scheduler> sch, bool is_global) :
            sch_(std::move(sch)),
            thd_(is_global 
                 ? &global_thread_function
                 : &scheduler_thread_function,
                 sch_.get())
        {
            HCE_HIGH_CONSTRUCTOR();
        }

        static void global_thread_function(hce::scheduler*);
        static void scheduler_thread_function(hce::scheduler*);

        std::shared_ptr<hce::scheduler> sch_;
        std::thread thd_;
        friend struct hce::scheduler;
    };

    /**
     @brief object for configuring runtime behavior of a `scheduler`

     An instance of this object passed to `scheduler::make()` will configure 
     the scheduler during its runtime with the given event handlers and options.

     Unless the user manually creates new schedulers with `scheduler::make()`, 
     then most instances of `config` will be generated by 
     `hce::config::global::scheduler_config()` (and 
     `hce::config::threadpool::scheduler_config()`) when the framework is 
     constructing managed `scheduler`s. Said functions are `extern`and can be 
     overridden if `cmake` is configured with the `HCECUSTOMCONFIG` definition, 
     requiring the user to link their own implementation.
     */
    struct config {
        config(const config& rhs) = delete;
        config(config&& rhs) = delete;

        ~config() { }

        config& operator=(const config& rhs) = delete;
        config& operator=(config&& rhs) = delete;

        /**
         @brief allocate and construct a config 

         This is the only mechanism to allocate an `hce::scheduler::config`.

         @return an allocated and constructed config
        */
        static inline std::unique_ptr<config> make() { 
            return std::unique_ptr<config>(new config);
        }

        /**
         Set the log level of the thread the scheduler is installed on.

         Maximum value: 9
         Minimum value: -9
         */
        int log_level;

        /**
         The count of coroutine resources the scheduler can pool for reuse 
         without deallocating.
         */
        size_t coroutine_resource_limit;

        /**
         The count of worker threads used by `block()` the scheduler will 
         persist for reuse. No workers are created until they are required.
         */
        size_t block_worker_resource_limit;

    private:
        config() :
            log_level(hce::printable::default_log_level()),
            coroutine_resource_limit(hce::config::scheduler::default_resource_limit()),
            block_worker_resource_limit(0)
        { }
    };

    virtual ~scheduler() {
        HCE_HIGH_DESTRUCTOR();
    }

    static inline std::string info_name() { return "hce::scheduler"; }
    inline std::string name() const { return scheduler::info_name(); }

    /**
     @brief allocate, construct and run a scheduler on a new system thread

     Allocates and registers a new `scheduler` and its `lifecycle` object.

     `scheduler::make()` calls are the only way to make a new scheduler. The 
     returned allocated `std::unique_ptr<scheduler::lifecycle>` contains the new 
     scheduler. Said scheduler can be retrieved as a reference by calling method 
     `lifecycle::scheduler()`. 

     Unless there is a very pressing reason, a scheduler accessed with 
     `hce::scheduler::get()` or the threadpool with `hce::threadpool::get()` 
     (called by high level mechanisms such as `hce::schedule()`) should be 
     sufficient for all user needs.

     @param c optional config unique pointer to configure the runtime behavior of the scheduler
     @return an allocated lifecycle unique pointer containing the scheduler shared pointer
     */
    static inline std::unique_ptr<lifecycle> make(
            std::unique_ptr<config> c = {}) {
        return make_(std::move(c),false);
    }

    /**
     This can only return `true` when called by a code evaluating in a coroutine 
     scheduled on the the scheduler.

     @return `true` if calling thread is executing an installed scheduler, else `false`
     */
    static inline bool in() {
        bool b = detail::scheduler::tl_this_scheduler();
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::in",b);
        return b; 
    }

    /**
     @brief retrieve the calling thread's running scheduler 

     WARNING: it is an ERROR to call this function when if 
     `scheduler::in() == false`

     @return a scheduler reference
     */
    static inline scheduler& local() {
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::local");
        return *(detail::scheduler::tl_this_scheduler());
    }

    /**
     @brief access the process wide running scheduler instance

     The instance will not be constructed until this operation is first called.

     @return the global scheduler
     */
    static inline scheduler& global() {
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::global");
        return global_();
    }

    /**
     @brief retrieve some running scheduler

     Prefer `scheduler::local()`, falling back to `scheduler::global()`.

     Useful when some scheduler is required, but a specific scheduler is not. 
     Additionally, operating on the current thread's scheduler (with 
     `scheduler::local()`), where possible, mitigates inter-thread communication 
     delays. Hence, this is a useful function for building generic higher level 
     operations which require scheduling.

     @return a scheduler reference
     */
    static inline scheduler& get() {
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::get");
        return scheduler::in() ? scheduler::local() : scheduler::global();
    }

    /// return a copy of this scheduler's shared pointer by conversion
    inline operator std::shared_ptr<scheduler>() {
        HCE_TRACE_METHOD_ENTER("operator std::shared_ptr<scheduler>()");
        return self_wptr_.lock(); 
    }

    /// return a copy of this scheduler's weak pointer by conversion
    inline operator std::weak_ptr<scheduler>() {
        HCE_TRACE_METHOD_ENTER("operator std::weak_ptr<scheduler>()");
        return self_wptr_; 
    }
    
    /**
     @return the scheduler thread's log level
     */
    inline int log_level() const {
        auto l = log_level_;
        HCE_TRACE_METHOD_BODY("log_level",l);
        return l;
    }

    /// return the state of the scheduler
    inline state status() const {
        state s;

        {
            std::lock_guard<spinlock> lk(lk_);
            s = state_;
        }

        HCE_MIN_METHOD_BODY("status",s);
        return s;
    }

    /**
     @brief access a heuristic for the scheduler's active workload 

     This value only accounts for actively scheduled coroutines executing or 
     waiting to execute. Timers, block() worker threads, and blocked (awaiting) 
     coroutines are not considered.

     @return the count of coroutines executing and waiting to execute
     */
    inline size_t scheduled_count() const {
        size_t c;

        {
            std::lock_guard<spinlock> lk(lk_);
            c = batch_size_ + coroutine_queue_->size();
        }
        
        HCE_TRACE_METHOD_BODY("workload",c);
        return c;
    }

    /**
     Useful heuristic for determining ideal `block_worker_resource_limit()` size.

     @return the current count of worker threads spawned for `block()`ing operations 
     */
    inline size_t block_worker_count() const {
        size_t c;

        {
            std::lock_guard<hce::spinlock> lk(lk_);
            c = block_worker_active_count_ + block_worker_pool_->used();
        }

        HCE_MIN_METHOD_BODY("block_worker_count",c);
        return c; 
    }

    /**
     Each started timer (including `sleep()` timers) contribute to this count.

     @return the count of all incomplete timers on this scheduler 
     */
    inline size_t timer_count() const {
        size_t c;

        {
            std::lock_guard<spinlock> lk(lk_);
            c = timers_.size();
        }
        
        HCE_MIN_METHOD_BODY("operations",c);
        return c;
    }

    /**
     This value is determined by the `scheduler::config::coroutine_resource_limit` 
     member in the `scheduler::config` passed to `scheduler::make()`.

     The scheduler will cache for reuse allocated resources for a count of 
     coroutines up to this limit. A higher value potentially increases the 
     amount of memory the scheduler will use in exchange for lowering the amount 
     of allocation/deallocation required during coroutine execution.

     A `coroutine_resource_limit()` at or above the median count of coroutines 
     operating in user code is often a sensible value.
     */
    inline size_t coroutine_resource_limit() const {
        HCE_MIN_METHOD_BODY("coroutine_resource_limit",coroutine_resource_limit_);
        return coroutine_resource_limit_;
    }

    /**
     This value is determined by the `
     scheduler::config::block_worker_resource_limit` member in the 
     `scheduler::config` passed to `scheduler::make()`.

     Worker threads utilized by the `block()` mechanism can be reused by the 
     scheduler, potentially increasing program efficiency when blocking calls 
     need to be regularly made.

     @return the minimum count of `block()` worker threads the scheduler will persist
     */
    inline size_t block_worker_resource_limit() const { 
        HCE_MIN_METHOD_BODY("block_worker_resource_limit",block_worker_pool_->size());
        return block_worker_pool_->size();
    }

    /***
     @brief schedule a single coroutine and return an awaitable to await the `co_return`ed value

     `void` is a valid return type for the given `co<T>` (`co<void>`), in which 
     case the returned awaitable should be called with `co_await` but no value 
     shall be assigned from the result of the statement.

     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will return an the result of the completed coroutine
     */
    template <typename T>
    inline hce::awt<T> schedule(hce::co<T> co) {
        HCE_HIGH_METHOD_ENTER("schedule",co);

        std::lock_guard<spinlock> lk(lk_);
        scheduler_halted_guard_("schedule");

        auto j = new hce::scheduler::joiner<T>(*this, co);

        // returned awaitable resumes when the coroutine handle is destroyed 
        auto awt = hce::awt<T>(j);
        schedule_coroutine_handle_(co.release());
        return awt;
    }

    /**
     @brief execute a Callable on a dedicated thread (if necessary) and block the co_awaiting coroutine or calling thread until the Callable returns

     A Callable is any function, Functor, lambda or std::function. Simply, it is 
     anything that is invokable with the parenthesis `()` operator.

     This allows for executing arbitrary blocking code (which would be unsafe to 
     do in a coroutine!) via a mechanism which *is* safely callable from within
     a coroutine. A coroutine can `co_await` the result of the `block()` (or if 
     not called from a coroutine, just assign the awaitable to a variable of the 
     resulting type). 

     In a coroutine (with the high level `hce::block()` call):
     ```
     T result = co_await hce::block(my_function_returning_T, arg1, arg2);
     ```

     If the caller of `block()` immediately awaits the result then the given 
     Callable can access values owned by the coroutine's body (or 
     values on a thread's stack if called outside of a coroutine) by reference, 
     because the caller of `block()` will be blocked while awaiting.

     The user Callable will execute on a dedicated thread, and won't have direct 
     access to the caller's local scheduler or coroutine. IE, 
     `hce::scheduler::in()` and `hce::coroutine::in()` will return `false`. 
     Access to the source scheduler will have to be explicitly given by user 
     code by passing in the local scheduler's shared_ptr to the blocking code.

     If the caller is already executing in a thread managed by another call to 
     `block()`, or if called outside of an `hce` coroutine, the Callable will be 
     executed immediately on the *current* thread.

     @param cb a function, Functor, lambda or std::function
     @param as arguments to cb
     @return an awaitable returning the result of the Callable 
     */
    template <typename Callable, typename... As>
    inline awt<hce::function_return_type<Callable,As...>> 
    block(Callable&& cb, As&&... as) {
        typedef hce::function_return_type<Callable,As...> RETURN_TYPE;
        using isv = typename std::is_void<RETURN_TYPE>;

        HCE_MED_METHOD_ENTER("block",hce::callable_to_string(cb));

        return block_(
            std::integral_constant<bool,isv::value>(),
            std::forward<Callable>(cb),
            std::forward<As>(as)...);
    }

    /**
     @brief start a timer on this scheduler 

     Identical to `start()` except it abstracts away the timer's id. There is no 
     way to cancel a running sleep early, thus it should not be used in 
     scenarios where the sleep timeout exceeds any process lifecycle time 
     constraints.

     While it is not cancellable, this abstraction also allows the user to 
     ignore which scheduler the timer is running on, allowing for the existence 
     of global function `hce::sleep()`.

     @param as the arguments will be passed to `hce::chrono::duration()`
     @return an awaitable to join with the timer timing out or being cancelled
     */
    template <typename... As>
    inline hce::awt<bool> sleep(As&&... as) {
        hce::chrono::time_point timeout =
            hce::chrono::duration(std::forward<As>(as)...) + hce::chrono::now();

        HCE_MED_METHOD_ENTER("sleep",timeout);
       
        auto tp = new timer(*this, timeout);
        std::unique_ptr<timer> t(tp);

        {
            std::lock_guard<spinlock> lk(lk_);
            scheduler_halted_guard_("sleep");
            insert_timer_(t.get());
        }

        return hce::awt<bool>(t.release());
    }

    /**
     @brief start a timer on this scheduler 

     The returned awaitable will result in `true` if the timer timeout was 
     reached, else `false` will be returned if it was cancelled early due to 
     scheduler being totally .

     Timers count as a scheduled operation similar to coroutines for the 
     purposes of lifecycle management; running timers will prevent a scheduler 
     from . If a timer needs to be cancelled early, call `cancel()`.

     @param id a reference to an hce::sid which will be set to the launched timer's id
     @param as the remaining arguments which will be passed to `hce::chrono::duration()`
     @return an awaitable to join with the timer timing out (returning true) or being cancelled (returning false)
     */
    template <typename... As>
    inline hce::awt<bool> start(hce::sid& id, As&&... as) {
        hce::chrono::time_point timeout =
            hce::chrono::duration(std::forward<As>(as)...) + hce::chrono::now();
        
        auto tp = new timer(*this, timeout);
        std::unique_ptr<timer> t(tp);

        // acquire the id of the constructed timer
        id = t->id();

        HCE_MED_METHOD_ENTER("start",id,timeout);

        {
            std::lock_guard<spinlock> lk(lk_);
            scheduler_halted_guard_("start");
            insert_timer_(t.get());
        }

        return hce::awt<bool>(t.release());
    }

    /**
     @brief determine if a timer with the given id is running
     @return true if the timer is running, else false
     */
    inline bool running(const hce::sid& id) const {
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

     The `hce::sid` should be acquired from a call to the 
     `hce::scheduler::start()` method.

     @param id the hce::sid associated with the timer to be cancelled
     @return true if cancelled timer successfully, false if timer already timed out or was never scheduled
     */
    inline bool cancel(const hce::sid& id) {
        HCE_MED_METHOD_ENTER("cancel",id);
        bool result = false;

        if(id) {
            std::unique_lock<spinlock> lk(lk_);
            scheduler_halted_guard_("cancel");
            auto it = timers_.begin();
            auto end = timers_.end();

            while(it != end) [[likely]] {
                // search through the timers for a matching id
                if((*it)->id() == id) [[unlikely]] {
                    schedule_coroutine_handle_(timer::resume_op(*it,(void*)0).release());
                    timers_.erase(it);
                    operations_notify_();
                    lk.unlock();

                    result = true;
                    HCE_MED_METHOD_BODY("cancel","cancelled timer with id:",found->id());
                    break;
                }

                ++it;
            }
        } 

        return result;
    }
 
private:
    // namespaced objects for handling blocking calls
    struct blocking : public printable {
        // block workers receive operations over a queue and execute them
        struct worker : public printable {
            worker() : thd_(worker::run_, &operations_) { 
                HCE_MED_CONSTRUCTOR();
            }

            ~worker() { 
                HCE_MED_DESTRUCTOR(); 
                operations_.close();
                thd_.join();
            }

            // returns true if called on a thread owned by a worker object, else false
            static bool tl_is_block() { return tl_is_worker(); }

            static inline std::string info_name() { 
                return "hce::scheduler::blocking::worker"; 
            }

            inline std::string name() const { return worker::info_name(); }

            inline void schedule(std::unique_ptr<hce::thunk> operation) { 
                operations_.push_back(std::move(operation));
            }

        private:
            static bool& tl_is_worker();

            // worker thread scheduler run function
            static inline void run_(
                    synchronized_queue<std::unique_ptr<hce::thunk>>* operations) 
            {
                std::unique_ptr<hce::thunk> operation;

                worker::tl_is_worker() = true;

                while(operations->pop(operation)) [[likely]] {
                    // execute operations sequentially until recv() returns false
                    (*operation)();
                }
            }

            // Blocking operation queue. No reason to use thread_local cache
            // for hce::thunk, because it would be essentially doing a one-way 
            // memory steal from the scheduler thread to the block thread.
            //
            // However, it's fine that the object itself use the 
            // `pool_allocator` as its allocator, because list node memory will 
            // be reused inside the object to matter which thread.
            synchronized_queue<std::unique_ptr<hce::thunk>> operations_;

            // operating system thread
            std::thread thd_;
        };

        // awaitable implementation for returning an immediately available 
        // block() value
        template <typename T>
        struct sync : public 
                hce::scheduler::reschedule<
                    hce::detail::scheduler::sync_partial<T>>
        {
            template <typename... As>
            sync(As&&... as) : 
                hce::scheduler::reschedule<
                    hce::detail::scheduler::sync_partial<T>>(
                        std::forward<As>(as)...)
            { }

            virtual ~sync() { }
            
            static inline std::string info_name() { 
                return type::templatize<T>("hce::detail::scheduler::sync"); 
            }

            inline std::string name() const { return sync<T>::info_name(); }
        };

        // awaitable implementation for returning an asynchronously available 
        // block() value
        template <typename T>
        struct async : 
            public scheduler::reschedule<detail::scheduler::async_partial<T>>
        {
            async(scheduler& parent) : 
                scheduler::reschedule<detail::scheduler::async_partial<T>>(),
                // on construction get a worker
                wkr_(parent.checkout_block_worker_()),
                parent_(parent)
            { }

            virtual ~async() {
                // return the worker to its scheduler
                if(wkr_) [[likely]] { 
                    // after this returns, parent_ becomes potentially dangling
                    parent_.checkin_block_worker_(std::move(wkr_)); 
                }
            }
            
            static inline std::string info_name() { 
                return type::templatize<T>("hce::detail::scheduler::async"); 
            }

            inline std::string name() const { return async<T>::info_name(); }

            // return the contractor's worker
            inline blocking::worker& worker() { return *wkr_; }

        private:
            hce::unique_ptr<blocking::worker> wkr_;
            scheduler& parent_;
        };
    };

    // internal timer implementation
    struct timer : public 
        scheduler::reschedule<
           hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<bool>::interface>>
    {
        timer(hce::scheduler& parent, const hce::chrono::time_point& tp) : 
                scheduler::reschedule<
                    hce::awaitable::lockable<
                        hce::spinlock,
                        hce::awt<bool>::interface>>(
                            slk_,
                            hce::awaitable::await::policy::defer,
                            hce::awaitable::resume::policy::lock),
            tp_(tp),
            ready_(false),
            result_(false),
            parent_(parent)
        { 
            id_.make();
            HCE_MED_CONSTRUCTOR(parent, tp);
        }

        inline virtual ~timer(){
            HCE_MED_DESTRUCTOR();

            if(!ready_) {
                std::stringstream ss;
                ss << *this << "was not awaited nor resumed";
                HCE_FATAL_METHOD_BODY("~timer",ss.str());
                std::terminate();
            }
        }

        static inline std::string info_name() { 
            return "hce::scheduler::timer"; 
        }

        inline std::string name() const { return timer::info_name(); }

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
        inline const hce::sid& id() const { return id_; }

        static inline hce::co<void> resume_op(timer* t, void* val) {
            t->resume(val);
            co_return;
        }

    private:
        hce::chrono::time_point tp_;
        hce::sid id_;
        bool ready_; 
        bool result_;
        hce::spinlock slk_;
        std::weak_ptr<scheduler> parent_;
    };

    scheduler() : 
        state_(executing), 
        log_level_(-1),
        coroutine_resource_limit_(0),
        block_worker_active_count_(0)
    { 
        HCE_HIGH_CONSTRUCTOR();
        reset_flags_(); // initialize flags
    }

    static inline std::unique_ptr<lifecycle> make_(
            std::unique_ptr<config> c, 
            bool is_global) 
    {
        // ensure our config is allocated and assigned
        if(!c) { c = config::make(); }

        HCE_HIGH_FUNCTION_ENTER("hce::scheduler::make",c.get());

        // make the first shared pointer
        std::shared_ptr<scheduler> s(new scheduler);

        // finish initialization and configure the scheduler's runtime behavior
        s->configure_(s, std::move(c));

        // allocate and return the lifecycle pointer 
        lifecycle* lp = hce::allocate<lifecycle>(1);

        new(lp) lifecycle(std::move(s), is_global);
        return std::unique_ptr<lifecycle>(lp);
    }

    // initialize the lifecycle manager
    static std::shared_ptr<scheduler> init_lifecycle_manager_();
   
    // retrieve reference to the global scheduler. 
    static scheduler& global_();

    // the innermost coroutine schedule operation
    inline void schedule_coroutine_handle_(std::coroutine_handle<> h) {
        HCE_TRACE_METHOD_ENTER("schedule_coroutine_handle_",h);

        // verify our handle is represents a coroutine
        if(h) [[likely]] {
            if(h.done()) [[unlikely]] {
                HCE_WARNING_METHOD_BODY("schedule_coroutine_handle_",h," is already done, destroying...");
                // we got a bad, completed handle and need to free memory, 
                // allow inline coroutine destructor to do it for us... 
                hce::coroutine c(std::move(h));
            } else [[likely]] {
                HCE_TRACE_METHOD_BODY("schedule_coroutine_handle_","push_backing ",h," onto queue ");
                coroutine_queue_->push_back(h);
                operations_notify_();
            }
        } else [[unlikely]] {
            HCE_WARNING_METHOD_BODY("schedule_coroutine_handle_",h," is not a valid coroutine, ignoring");
        }
    }

    /*
     Reschedule a coroutine, only callable by hce::scheduler::reschedule.

     Arguments to this function can be an `hce::coroutine` or `hce::co<T>`
     */
    inline void reschedule_(hce::coroutine&& co) {
        HCE_HIGH_METHOD_ENTER("schedule",co);

        if(co) [[likely]] {
            if(this == detail::scheduler::tl_this_scheduler()) [[likely]] {
                // rescheduling inside scheduler::run(), lockfree push to 
                // local queue 
                (*detail::scheduler::tl_this_scheduler_local_queue())->push_back(
                    co.release());
            } else [[unlikely]] {
                // rescheduling from another thread
                std::lock_guard<spinlock> lk(lk_);
                scheduler_halted_guard_("schedule");
                schedule_coroutine_handle_(co.release());
            }
        }
    }

    inline void insert_timer_(timer* t) {
        timers_.push_back(t);

        // resort timers from soonest to latest
        timers_.sort([](timer* lhs, timer* rhs) {
            return lhs->timeout() < rhs->timeout();
        });

        operations_notify_();
    }

    // finish initializing and configuring the scheduler
    void configure_(std::shared_ptr<scheduler>& self, 
                    std::unique_ptr<config> cfg) {
        // set the weak_ptr
        self_wptr_ = self;

        log_level_ = cfg->log_level;
        coroutine_resource_limit_ = cfg->coroutine_resource_limit;
        coroutine_queue_.reset(new hce::list<std::coroutine_handle<>>(
            hce::pool_allocator<std::coroutine_handle<>>(
                coroutine_resource_limit_)));
        block_worker_pool_.reset(
            new hce::circular_buffer<hce::unique_ptr<blocking::worker>>(
                cfg->block_worker_resource_limit));
    }

    /*
     Execute coroutines and timers continuously. This processing loop is highly 
     optimized, and contains a variety of comments to explain its design.

     Returns true on `suspended`, false on `halted` state.
     */
    void run() {
        // check the thread local scheduler pointer to see if we're already in 
        // a scheduler and error out immediately if called improperly
        if(hce::coroutine::in()) { 
            throw scheduler_cannot_run_in_a_coroutine_exception(); 
        }

        // acquire the current log level
        size_t parent_log_level = hce::printable::thread_log_level();

        // set the necessary log level for the worker thread
        hce::printable::thread_log_level(log_level_);

        HCE_HIGH_METHOD_ENTER("run");

        // the local queue of coroutines to evaluate
        std::unique_ptr<hce::list<std::coroutine_handle<>>> local_queue(
            new hce::list<std::coroutine_handle<>>(
                hce::pool_allocator<std::coroutine_handle<>>(
                    coroutine_resource_limit_)));

        // timers extracted from the main timers list
        std::list<timer*,hce::allocator<timer*>> local_timers;

        // the current time
        hce::chrono::time_point now; 

        // manage the thread_local pointers for this scheduler with RAII
        struct scoped_thread_locals {
            scoped_thread_locals(
                    scheduler* s, 
                    std::unique_ptr<hce::list<std::coroutine_handle<>>>* q) 
            { 
                detail::scheduler::tl_this_scheduler() = s;
                detail::scheduler::tl_this_scheduler_local_queue() = q;
            }

            ~scoped_thread_locals() {
                detail::scheduler::tl_this_scheduler() = nullptr;
                detail::scheduler::tl_this_scheduler_local_queue() = nullptr;
            }
        };

        scoped_thread_locals stl(this, &local_queue);

        // push_back any remaining coroutines back into the main queue. Lock must be
        // held before this is called.
        auto cleanup_batch = [&] {
            // reset scheduler batch evaluating count 
            batch_size_ = 0; 

            // concatenate every uncompleted coroutine to the back of the 
            // scheduler's main coroutine queue 
            coroutine_queue_->concatenate(*local_queue);
        };

        // acquire the lock
        std::unique_lock<spinlock> lk(lk_);

        // If halted return immediately
        while(state_ != halted) [[likely]] {

            // block until no longer suspended
            while(state_ == suspended) { 
                HCE_HIGH_METHOD_BODY("run","suspended");
                // wait for resumption
                waiting_for_resume_ = true;
                resume_cv_.wait(lk);
            }
        
            HCE_HIGH_METHOD_BODY("run","executing");

            // flag to control evaluation loop
            bool evaluate = true;

            try {
                /*
                 Evaluation loop runs fairly continuously. 99.9% of the time 
                 it is expected a scheduler is executing code within this loop
                 */
                while(evaluate) [[likely]] {
                    // check for any ready timers 
                    if(timers_.size()) [[unlikely]] {
                        // update the current timepoint
                        now = hce::chrono::now();
                        auto it = timers_.begin();
                        auto end = timers_.end();

                        while(it!=end) [[likely]] {
                            // check if a timer is ready to timeout
                            if((*it)->timeout() <= now) {
                                coroutine_queue_->push_back(timer::resume_op(*it,(void*)1).release());
                                it = timers_.erase(it);
                            } else {
                                // no remaining ready timers, exit loop early
                                break;
                            }
                        }
                    }

                    // check for waiting coroutines
                    if(coroutine_queue_->size()) [[likely]] {
                        /*
                         Acquire the current batch of coroutines by trading with 
                         the scheduler's queue, reducing lock contention by 
                         collecting the entire batch via a pointer swap.
                         */
                        std::swap(local_queue, coroutine_queue_);

                        // update API accessible batch count
                        batch_size_ = local_queue->size();

                        /* 
                         Unlock scheduler when running executing coroutines to 
                         allow public API to acquire the lock.
                         */
                        lk.unlock();

                        // scope any local variables
                        {
                            size_t count = local_queue->size();

                            /*
                             This object is scoped to ensure 
                             std::coroutine_handle lifecycle management with 
                             RAII semantics
                             */
                            coroutine co;

                            /*
                             Evaluate the batch of coroutines once through,
                             deterministically exiting this loop so the 
                             scheduler can re-evaluate timers and other state.
                             */
                            while(count) [[likely]] { 
                                // decrement from our initial batch count
                                --count;

                                // Get a new task from the front of the task 
                                // queue, cleaning up the old coroutine handle.
                                co.reset(local_queue->front());
                                local_queue->pop();

                                // execute the coroutine
                                co.resume();

                                // check if the coroutine still has a handle
                                if(co) [[unlikely]] {
                                    if(!co.done()) [[likely]] {
                                        // locally re-enqueue coroutine 
                                        local_queue->push_back(co.release()); 
                                    }
                                } // else coroutine was suspended during await
                            }
                        } // make sure last coroutine is cleaned up before lock

                        // reacquire lock
                        lk.lock(); 
                    }

                    // cleanup batch results, requeueing coroutines
                    cleanup_batch();

                    // keep executing if coroutines are available
                    if(coroutine_queue_->empty()) [[unlikely]] {
                        // check if any running timers exist
                        if(timers_.empty()) [[likely]] {
                            // verify run state and block if necessary
                            if(can_evaluate_()) [[likely]] {
                                // wait for more tasks if we can evaluate
                                waiting_for_operations_ = true;
                                operations_cv_.wait(lk);
                            } else [[unlikely]] {
                                // break out of the evaluation loop because 
                                // execution is  and no operations remain
                                evaluate=false;
                            }
                        } else [[unlikely]] {
                            // check the time again
                            now = hce::chrono::now();
                            auto timeout = timers_.front()->timeout();

                            // wait, at a maximum, till the next scheduled
                            // timer timeout. If a timer is ready to timeout
                            // continue to the next iteration.
                            if(timeout > now) [[likely]] {
                                waiting_for_operations_ = true;
                                operations_cv_.wait_until(lk, timeout);
                            }
                        }
                    }
                }
            } catch(...) { // catch all other exceptions 
                // reset log level
                hce::printable::thread_log_level(parent_log_level);

                // it is an error in this framework if an exception occurs when 
                // the lock is held, it should only be when executing user 
                // coroutines that this can even occur
                lk.lock();

                cleanup_batch();

                lk.unlock();

                std::rethrow_exception(std::current_exception());
            }

            // reset member state flags
            reset_flags_();
        }

        HCE_HIGH_METHOD_BODY("run","halted");

        // reset log level
        hce::printable::thread_log_level(parent_log_level);
    }

    /*
     Suspend the scheduler. 

     Pauses operations on the scheduler and causes calls to run() to return 
     early.
     */
    inline void suspend_() {
        std::lock_guard<hce::spinlock> lk(lk_);

        if(state_ != halted) { 
            state_ = suspended;

            // wakeup scheduler if necessary from waiting for tasks to force 
            // run() to exit
            operations_notify_();
        }
    }

    /*
     Resumes a suspended scheduler. 
     */
    inline void resume_() {
        std::lock_guard<spinlock> lk(lk_);
       
        if(state_ == suspended) { 
            state_ = executing; 
            resume_notify_();
        }
    }

    /*
     Initialize the halt process in the scheduler. After this is called the 
     scheduler will begin communicating with the `lifecycle::manager` to 
     negotiate a clean halted of the process.

     This operation ends all current and future coroutine execution on the 
     scheduler. However, this will not return until all previously scheduled 
     coroutines are completed.
     */
    inline void halt_() {
        std::lock_guard<hce::spinlock> lk(lk_);

        if(state_ != halted) {
            // set the scheduler to the  state
            state_ = halted;

            // resume scheduler if necessary
            resume_notify_();

            // wakeup scheduler if necessary
            operations_notify_();
        }
    }

    /* 
     Reset scheduler state flags, etc. Does not reset scheduled coroutine 
     queues. 
    
     This method can ONLY be safely called by the constructor or run() because 
     access to these values is always unsynchronized.
     */
    inline void reset_flags_() {
        batch_size_ = 0;
        waiting_for_resume_ = false;
        waiting_for_operations_ = false;
    }

    /*
     Abstract inlined state checking to this function for correctness and 
     algorithm clarity.

     Determines if processing coroutines can continue in run(). 
     */
    inline bool can_evaluate_() { 
        HCE_TRACE_FUNCTION_BODY(
            "can_evaluate_", 
            "(state_ == executing):", 
            (state_ == executing) ? "true" : "false");
        return (state_ == executing); 
    }

    inline void scheduler_halted_guard_(const char* op) {
        if(state_ == halted) [[unlikely]] {
            HCE_ERROR_METHOD_BODY("scheduler_halted_guard_","scheduler halted");
            throw scheduler_halted_exception(this, op);
        }
    }
 
    // notify caller of run() that resume() has been called 
    inline void resume_notify_() {
        // only do notify if necessary
        if(waiting_for_resume_) {
            HCE_TRACE_METHOD_BODY("resume_notify_");
            waiting_for_resume_ = false;
            resume_cv_.notify_one();
        }
    }
   
    /* 
     Notify caller of run() that operations are potentially available and 
     state needs to be rechecked.
     */
    inline void operations_notify_() {
        // only do notify if necessary
        if(waiting_for_operations_) {
            HCE_TRACE_METHOD_BODY("operations_notify_");
            waiting_for_operations_ = false;
            operations_cv_.notify_one();
        }
    }

    // retrieve a worker thread to execute blocking operations on
    inline hce::unique_ptr<blocking::worker> checkout_block_worker_() {
        HCE_TRACE_METHOD_ENTER("checkout_block_worker_");
        hce::unique_ptr<blocking::worker> w;

        std::unique_lock<spinlock> lk(lk_);
        ++block_worker_active_count_; // update checked out thread count

        // check if we have any workers in reserve
        if(block_worker_pool_->empty()) {
            lk.unlock();

            // as a fallback generate a new worker thread 
            blocking::worker* wp = hce::allocate<blocking::worker>(1);
            new(wp) blocking::worker();
            w.reset(wp);
        } else {
            // get the first available worker
            w = std::move(block_worker_pool_->front());
            block_worker_pool_->pop();
            lk.unlock();
        }

        return w;
    }

    // return a worker when blocking operation is completed
    inline void checkin_block_worker_(hce::unique_ptr<blocking::worker> w) {
        HCE_TRACE_METHOD_ENTER("checkin_block_worker_");

        std::lock_guard<spinlock> lk(lk_);
        --block_worker_active_count_; // update checked out thread count

        if(block_worker_pool_->full()) {
            HCE_TRACE_METHOD_BODY("checkin_block_worker_","discarded ",w.get());
        } else { 
            HCE_TRACE_METHOD_BODY("checkin_block_worker_","reused ",w.get());
            block_worker_pool_->push(std::move(w)); // reuse worker
        }

        operations_notify_();
    }

    template <typename Callable, typename... As>
    inline hce::awt<hce::function_return_type<Callable,As...>>
    block_(std::false_type, Callable&& cb, As&&... as) {
        typedef hce::function_return_type<Callable,As...> T;

        if(!scheduler::in() || blocking::worker::tl_is_block()) {
            HCE_MED_METHOD_BODY("block","executing on current thread");
            
            // we own the thread already, call cb immediately and return the 
            // result
            return hce::awt<T>(new blocking::sync<T>(
                cb(std::forward<As>(as)...)));
        } else {
            // construct an asynchronous awaitable implementation
            auto ai = new blocking::async<T>(*this);
            auto& wkr = ai->worker();
            HCE_MED_METHOD_BODY("block","executing on ",wkr);

            // construct the operation and send to the worker
            wkr.schedule(
                std::unique_ptr<hce::thunk>(
                    new hce::thunk(
                        [ai,
                         cb=std::forward<Callable>(cb),
                         ... as=std::forward<As>(as)]() mutable -> void {
                            // pass the allocated T to the async and resume it
                            ai->resume(new T(cb(std::forward<As>(as)...)));
                        })));

            // return an awaitable to await the result of the blocking call
            return hce::awt<T>(ai);
        }
    }

    // void return specialization 
    template <typename Callable, typename... As>
    inline hce::awt<void>
    block_(std::true_type, Callable&& cb, As&&... as) {
        if(!scheduler::in() || blocking::worker::tl_is_block()) {
            HCE_LOW_METHOD_BODY("block","executing on current thread");
            cb(std::forward<As>(as)...);
            return hce::awt<void>(new blocking::sync<void>);
        } else {
            auto ai = new blocking::async<void>(*this);
            auto& wkr = ai->worker(); 
            HCE_MED_METHOD_BODY("block","executing on ",wkr);

            wkr.schedule(
                std::unique_ptr<hce::thunk>(
                    new hce::thunk(
                        [ai,
                         cb=std::forward<Callable>(cb),
                         ... as=std::forward<As>(as)]() mutable -> void {
                            cb(std::forward<As>(as)...);
                            ai->resume(nullptr);
                        })));

            return hce::awt<void>(ai);
        }
    }

    // synchronization primative, marked mutable for use in const methods.
    mutable hce::spinlock lk_;

    // a weak_ptr to the scheduler's shared memory
    std::weak_ptr<scheduler> self_wptr_;

    // the current lifecycle state of the scheduler
    state state_; 

    // scheduler log level
    int log_level_;

    /*
     Maximum count of reusable coroutine resources. Each coroutine has a 
     variety or resources provided by the scheduler that it uses. This value 
     determines the maximum size of the caches used for these purposes.

     This value is used by several places, and setting a large enough value can 
     enable smoother coroutine processing in the average case.
     */
    size_t coroutine_resource_limit_;

    // count of threads executing blocking operations
    size_t block_worker_active_count_; 
                  
    // the count of actively executing coroutines in this scheduler's run()
    size_t batch_size_; 

    // flag for when scheduler::resume() is called
    bool waiting_for_resume_;
    
    // flag for when scheduled operation state changes
    bool waiting_for_operations_; 
  
    // condition for when scheduler::resume() is called
    std::condition_variable_any resume_cv_;

    // condition for when scheduled operation state changes
    std::condition_variable_any operations_cv_;

    // Queue holding scheduled coroutine handles. Simpler to just use underlying 
    // std::coroutine_handle than to utilize conversions between 
    // hce::coroutine and hce::co<T>. This object is a unique_ptr because it is 
    // routinely swapped between this object and the stack memory of the caller 
    // of scheduler::run().
    //
    // thread_local memory caching isn't used for allocating the queue itself,
    // there's no reason to pull memory from the cache for something that is 
    // generally allocated for an entire process' lifecycle.
    std::unique_ptr<hce::list<std::coroutine_handle<>>> coroutine_queue_;

    // Queue of reusable block workers threads. When a blocking operation 
    // finishes, the worker thread that executed the operation will be placed in 
    // this queue if there is room. This queue will grow no larger than the 
    // config::block_worker_resource_limit.  
    //
    // thread_local memory caching isn't used for this, there's no reason to 
    // pull memory from the cache for something that is generally allocated for 
    // an entire process' lifecycle. 
    std::unique_ptr<hce::circular_buffer<hce::unique_ptr<blocking::worker>>>
    block_worker_pool_;

    // List holding scheduled timers. This will be regularly re-sorted from 
    // soonest to latest timeout. Timer pointer's allocated memory is managed by 
    // an awaitable object, and should not be deleted by the scheduler directly.
    // Instead, when timers are resumed, they can simply be erased from this 
    // list without deallocating.
    //
    // Timers will be arbitrarily resorted and/or erased from anywhere in the 
    // list. It is for this reason that `std::list` is used instead of 
    // `hce::list`, so that iterators, erasure and `sort()`ing capabilities 
    // don't need to be implemented and maintained.
    std::list<timer*,hce::allocator<timer*>> timers_;
};

namespace config {
namespace global {

/**
 @brief provide the default global scheduler configuration

 This method's implementation can be overridden by a user definition of 
 global method `hce::scheduler::global::scheduler_config() at library compile 
 time if compiler define `HCECUSTOMCONFIG` is provided, otherwise a default 
 implementation is used.

 This `config` is used when constructing the process wide, default `scheduler` 
 (IE, the `scheduler` returned from `scheduler::global()`).

 @return a copy of the global configuration 
 */
extern std::unique_ptr<hce::scheduler::config> scheduler_config();

}
}

/**
 @brief call schedule() on a scheduler
 @param as arguments for scheduler::schedule()
 @return result of scheduler::schedule()
 */
template <typename... As>
inline auto schedule(As&&... as) {
    HCE_HIGH_FUNCTION_ENTER("hce::schedule");
    return scheduler::get().schedule(std::forward<As>(as)...);
}

/**
 @brief call block() on a scheduler
 @param as arguments for scheduler::block()
 @return result of block()
 */
template <typename... As>
inline auto block(As&&... as) {
    HCE_MED_FUNCTION_ENTER("hce::block");
    return scheduler::get().block(std::forward<As>(as)...);
}

/**
 @brief call sleep() on a scheduler
 @param as arguments for scheduler::sleep()
 @return result of scheduler::sleep()
 */
template <typename... As>
inline auto sleep(As&&... as) {
    HCE_MED_FUNCTION_ENTER("hce::sleep");
    return scheduler::get().sleep(std::forward<As>(as)...);
}

/**
 @brief call start() on the global scheduler 
 @param as arguments for scheduler::start()
 @return result of scheduler::start()
 */
template <typename... As>
inline auto start(As&&... as) {
    HCE_MED_FUNCTION_ENTER("hce::start");
    return scheduler::global().start(std::forward<As>(as)...);
}

/**
 @brief call running() on the global scheduler 
 @param as arguments for scheduler::running()
 @return result of scheduler::running()
 */
template <typename... As>
inline auto running(As&&... as) {
    HCE_MED_FUNCTION_ENTER("hce::running");
    return scheduler::global().running(std::forward<As>(as)...);
}

/**
 @brief call cancel() on the global scheduler 
 @param as arguments for scheduler::cancel()
 @return result of scheduler::cancel()
 */
template <typename... As>
inline auto cancel(As&&... as) {
    HCE_MED_FUNCTION_ENTER("hce::cancel");
    return scheduler::global().cancel(std::forward<As>(as)...);
}

}

#endif
