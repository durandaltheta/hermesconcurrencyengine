//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_SCHEDULER
#define HERMES_COROUTINE_ENGINE_SCHEDULER

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
#include "base.hpp"
#include "utility.hpp"
#include "atomic.hpp"
#include "memory.hpp"
#include "list.hpp"
#include "coroutine.hpp"

namespace hce {

struct scheduler;

struct scheduler_halted_exception : public std::exception {
    scheduler_halted_exception(scheduler* sch) : 
        estr([&]() -> std::string {
            std::stringstream ss;
            ss << "schedule() failed because scheduler is halted:" << sch;
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
size_t default_resource_limit();

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
        typename hce::awt<T>::interface>
{
    joiner(hce::co<T>& co) :
        hce::awaitable::lockable<
            hce::spinlock,
            typename hce::awt<T>::interface>(
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
        HCE_TRACE_FUNCTION_ENTER(hce::detail::scheduler::joiner<T>::info_name() + "::cleanup", data.install, data.promise);
        // acquire a reference to the joiner<T>
        auto& joiner = *(static_cast<hce::detail::scheduler::joiner<T>*>(data.install));

        // acquire a reference to the promise
        auto& promise = *(static_cast<hce::co<T>::promise_type*>(data.promise));

        // get a copy of the handle to see if the coroutine completed 
        auto handle = std::coroutine_handle<hce::co_promise_type<T>>::from_promise(promise);

        if(handle.done()) [[likely]] {
            //HCE_MIN_FUNCTION_BODY("joiner<T>::cleanup()","done@",handle);
            HCE_TRACE_FUNCTION_BODY(hce::detail::scheduler::joiner<T>::info_name() + "::cleanup","done@",handle);
            // resume the blocked awaitable and pass the allocated result pointer
            joiner.resume(&(promise.result));
        } else [[unlikely]] {
            HCE_ERROR_FUNCTION_BODY(hce::detail::scheduler::joiner<T>::info_name() + "::cleanup","NOT done@",handle);
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
        typename hce::awt<void>::interface>
{
    joiner(hce::co<void>& co) :
        hce::awaitable::lockable<
            hce::spinlock,
            typename hce::awt<void>::interface>(
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

}
}

/** 
 @brief object responsible for scheduling and executing coroutines
 
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
   
    template <typename T>
    struct null_coroutine_exception : public std::exception {
        null_coroutine_exception(hce::co<T>* c) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "cannot schedule empty coroutine: " << c;
                return ss.str();
            }())
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };
   
    template <typename T>
    struct done_coroutine_exception : public std::exception {
        done_coroutine_exception(hce::co<T>* c) :
            estr([&]() -> std::string {
                std::stringstream ss;
                ss << "cannot schedule done coroutine: " << c;
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
                if(h) [[likely]] {
                    if(h.done()) [[unlikely]] {
                        HCE_ERROR_METHOD_BODY("destination",h," is already done, destroying...");
                        // we got a bad, completed handle and need to free memory, 
                        // allow inline coroutine destructor to do it for us... 
                        hce::coroutine c(std::move(h));
                    } else [[likely]] {
                        d->schedule_(std::move(h));
                    }
                } else [[unlikely]] {
                    HCE_ERROR_METHOD_BODY("destination","cannot reschedule null handle ",h);
                }
            } else [[unlikely]] {
                HCE_ERROR_METHOD_BODY("destination","cannot reschedule with null destination ",d);
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
            manager() : state_(executing) { 
                HCE_HIGH_CONSTRUCTOR(); 
            }

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
            static inline manager& instance() { return *instance_; }

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
            static manager* instance_;
            hce::spinlock lk_;
            hce::scheduler::state state_;

            // it is important that the lifecycles are destroyed last, so this
            // list is declared first (destructors are called first-in-last-out)
            std::list<std::unique_ptr<scheduler::lifecycle>> lifecycle_pointers_;

            friend hce::lifecycle;
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
        config(){}

        /**
         Set the log level of the thread the scheduler is installed on.

         Maximum value: 9
         Minimum value: -9
         */
        int log_level = -1;

        /**
         The count of coroutine resources the scheduler can pool for reuse 
         without deallocating.
         */
        size_t coroutine_resource_limit = 64;
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
    static inline std::unique_ptr<lifecycle> make(config c = {})  {
        return scheduler::make_(c, false);
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
        return *(scheduler::global_);
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

    /// compare two schedulers, equality is always memory address equality
    inline bool operator==(const scheduler& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator ==(const scheduler&)");
        return this == &rhs;
    }
    
    /// contrast two schedulers, inequality is always memory address inequality
    inline bool operator!=(const scheduler& rhs) const {
        HCE_TRACE_METHOD_ENTER("operator !=(const scheduler&)");
        return this != &rhs;
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
     waiting to execute. block() worker threads and blocked (awaiting) 
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

    /***
     @brief schedule a single coroutine and return an awaitable to await the `co_return`ed value

     `void` is a valid return type for the given `hce::co<T>` (`hce::co<void>`), 
     in which case the returned awaitable should be called with `co_await` but 
     no value shall be assigned from the result of the statement.

     Scheduling order is not guaranteed to be FIFO (first in, first out). This 
     is not because of a piority mechanism, but because scheduling is done 
     based on throughput efficiency and *not* ordering. This includes using 
     lockless batch scheduling, which may incidentally cause coroutines to 
     execute out of order. Ordering can be enforced by using awaitable 
     mechanisms to block coroutines and synchronize operations.

     @param co a coroutine to schedule
     @return an awaitable which when `co_await`ed will join with and return the result of the completed coroutine
     */
    template <typename T>
    inline hce::awt<T> schedule(hce::co<T> co) {
        HCE_HIGH_METHOD_ENTER("schedule",co);
        hce::awt<T> awt;

        if(co) [[likely]] {
            if(co.done()) [[unlikely]] {
                throw done_coroutine_exception(&co);
            } else [[likely]] {
                auto j = new hce::scheduler::joiner<T>(*this, co);

                // returned awaitable resumes when the coroutine handle is destroyed 
                awt = hce::awt<T>(j);
                schedule_(co.release());
            }
        } else [[unlikely]] {
            throw null_coroutine_exception(&co);
        }

        return awt;
    }
 
private:
    scheduler() : 
        state_(executing), 
        log_level_(-1),
        coroutine_resource_limit_(0)
    { 
        HCE_HIGH_CONSTRUCTOR();
        reset_flags_(); // initialize flags
    }

    // initialize the lifecycle manager
    static std::shared_ptr<scheduler> init_lifecycle_manager_();

    static inline std::unique_ptr<lifecycle> make_(const config& c, bool is_global) {
        HCE_HIGH_FUNCTION_ENTER("hce::scheduler::make",c.get());

        // make the first shared pointer
        std::shared_ptr<scheduler> s(new scheduler);

        // finish initialization and configure the scheduler's runtime behavior
        s->configure_(s, c);

        // allocate and return the lifecycle pointer 
        lifecycle* lp = new lifecycle(std::move(s), is_global);
        return std::unique_ptr<lifecycle>(lp);
    }

    // finish initializing and configuring the scheduler
    void configure_(std::shared_ptr<scheduler>& self, const config& cfg) {
        // set the weak_ptr
        self_wptr_ = self;

        log_level_ = cfg.log_level;
        coroutine_resource_limit_ = cfg.coroutine_resource_limit;
        coroutine_queue_.reset(new hce::list<std::coroutine_handle<>>(
            hce::pool_allocator<std::coroutine_handle<>>(
                coroutine_resource_limit_)));
    }

    // handle validation is done at a higher level
    inline void schedule_(std::coroutine_handle<> h) {
        if(this == detail::scheduler::tl_this_scheduler()) [[likely]] {
            HCE_TRACE_METHOD_BODY("schedule_","pushing ",h," onto local queue ");
            // scheduling inside call to executing scheduler::run(), can do a 
            // lockfree push to local queue 
            (*detail::scheduler::tl_this_scheduler_local_queue())->push_back(h);
        } else [[unlikely]] {
            HCE_TRACE_METHOD_BODY("schedule_","pushing ",h," onto remote queue ");

            std::lock_guard<spinlock> lk(lk_);

            if(state_ == halted) [[unlikely]] {
                throw scheduler_halted_exception(this);
            }

            coroutine_queue_->push_back(h);
            coroutines_notify_();
        }
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
            coroutines_notify_();
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
            coroutines_notify_();
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
        waiting_for_coroutines_ = false;
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
    inline void coroutines_notify_() {
        // only do notify if necessary
        if(waiting_for_coroutines_) {
            HCE_TRACE_METHOD_BODY("coroutines_notify_");
            waiting_for_coroutines_ = false;
            coroutines_cv_.notify_one();
        }
    }

    /*
     Execute coroutines continuously. This processing loop is highly optimized, 
     and contains a variety of comments to explain its design.

     It is an ERROR if this is called inside another scheduler. 

     It is an ERROR if this is called more than once by a scheduler.
     */
    void run() {
        // the local queue of coroutines to evaluate, won't do any logging 
        // because it is an unallocated std:: object
        std::unique_ptr<hce::list<std::coroutine_handle<>>> local_queue;

        // manage the thread_local pointers for this scheduler with RAII
        struct scoped_locals {
            scoped_locals(
                    size_t log_level,
                    scheduler* s, 
                    std::unique_ptr<hce::list<std::coroutine_handle<>>>* q) :
                prev_log_level_(hce::logger::thread_log_level())
            { 
                hce::logger::thread_log_level(log_level);
                detail::scheduler::tl_this_scheduler() = s;
                detail::scheduler::tl_this_scheduler_local_queue() = q;
            }

            ~scoped_locals() {
                detail::scheduler::tl_this_scheduler_local_queue() = nullptr;
                detail::scheduler::tl_this_scheduler() = nullptr;
                hce::logger::thread_log_level(prev_log_level_);
            }

        private:
            size_t prev_log_level_;
        };

        scoped_locals stl(log_level_, this, &local_queue);

        HCE_HIGH_METHOD_ENTER("run");

        // allocate the queue
        local_queue.reset(
            new hce::list<std::coroutine_handle<>>(
                hce::pool_allocator<std::coroutine_handle<>>(
                    coroutine_resource_limit_)));

        // push_back any remaining coroutines back into the main queue. Lock must be
        // held before this is called.
        auto cleanup_batch = [&] {
            // reset scheduler batch evaluating count 
            batch_size_ = 0; 

            // Concatenate every uncompleted coroutine to the back of the 
            // scheduler's main coroutine queue. Concatenation is constant time 
            // with hce::list<T>.
            coroutine_queue_->concatenate(*local_queue);
        };

        // acquire the lock
        std::unique_lock<spinlock> lk(lk_);

        try {
            // if halted return immediately
            while(state_ != halted) [[likely]] {

                // block until no longer suspended
                while(state_ == suspended) { 
                    HCE_HIGH_METHOD_BODY("run","suspended");
                    // wait for resumption
                    waiting_for_resume_ = true;
                    resume_cv_.wait(lk);
                }
            
                HCE_HIGH_METHOD_BODY("run","executing");

                /*
                 Evaluation loop runs fairly continuously. 99.9% of the time 
                 it is expected a scheduler is executing code within this loop
                 */
                while(state_ == executing) [[likely]] {

                    // check for waiting coroutines
                    if(coroutine_queue_->size()) [[likely]] {
                        /*
                         Acquire the current batch of coroutines by trading the 
                         empty local queue with the scheduler's main queue, 
                         reducing lock contention by collecting the entire batch 
                         via a pointer swap.
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

                            // this object is scoped to enable RAII of handles
                            coroutine co;

                            /*
                             Evaluate the batch of coroutines once through,
                             deterministically exiting this loop so the 
                             scheduler can re-evaluate other state.
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

                        // cleanup batch results, requeueing local coroutines
                        cleanup_batch();
                    } else [[unlikely]] {
                        // wait for more tasks
                        waiting_for_coroutines_ = true;
                        coroutines_cv_.wait(lk);
                    }
                }

                // reset member state flags
                reset_flags_();
            }
        } catch(...) { // catch all other exceptions 
            // it is an error in this framework if an exception occurs when 
            // the lock is held, it should only be when executing user 
            // coroutines that this can even occur
            lk.lock();

            cleanup_batch();

            lk.unlock();

            std::rethrow_exception(std::current_exception());
        }

        HCE_HIGH_METHOD_BODY("run","halted");
    }

    // synchronization primative, marked mutable for use in const methods.
    mutable hce::spinlock lk_;

    // a weak_ptr to the scheduler's shared memory
    std::weak_ptr<scheduler> self_wptr_;

    // the current lifecycle state of the scheduler
    state state_; 

    // scheduler log level, is written only once on configure_, called before 
    // run() is called
    int log_level_;

    /*
     Maximum count of reusable coroutine resources. Each coroutine has a 
     variety or resources provided by the scheduler that it uses. This value 
     determines the maximum size of the caches used for these purposes.

     This value is used by several places, and setting a large enough value can 
     enable smoother coroutine processing in the average case.
     */
    size_t coroutine_resource_limit_;
                  
    // the count of actively executing coroutines in this scheduler's run()
    size_t batch_size_; 

    // flag for when scheduler::resume() is called
    bool waiting_for_resume_;
    
    // flag for when scheduled operation state changes
    bool waiting_for_coroutines_; 
  
    // condition for when scheduler::resume() is called
    std::condition_variable_any resume_cv_;

    // condition for when scheduled operation state changes
    std::condition_variable_any coroutines_cv_;

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

    // reference to the global scheduler. 
    static scheduler* global_;

    friend hce::lifecycle;
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
extern hce::scheduler::config scheduler_config();

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

}

#endif
