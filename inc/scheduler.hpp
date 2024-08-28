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

/*
 Used by 'block()' calls to unset the user accessible scheduler and coroutine 
 state for the calling thread during the lifetime of the user blocking 
 operation.
 */
template <typename T>
inline hce::co<T> block_wrapper(std::function<T()> f) {
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
inline hce::co<void> block_wrapper(std::function<void()> f) {
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
struct done_partial : 
    public hce::awaitable::lockable<
        awt_interface<T>,
        hce::lockfree>
{
    template <typename... As>
    done_partial(As&&... as) : 
        hce::awaitable::lockable<
            awt_interface<T>,
            hce::lockfree>(
                lkf_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock),
        t_(std::forward<As>(as)...) 
    { }

    inline bool on_ready() { return true; }
    inline void on_resume(void* m) { }
    inline T get_result() { return std::move(t_); }

private:
    hce::lockfree lkf_;
    T t_;
};

template <>
struct done_partial<void> : public 
        hce::awaitable::lockable<awt_interface<void>,hce::spinlock>
{
    done_partial() :
        hce::awaitable::lockable<
            awt_interface<void>,
            hce::spinlock>(
                slk_,
                hce::awaitable::await::policy::defer,
                hce::awaitable::resume::policy::lock)
    { }

    inline bool on_ready() { return true; }
    inline void on_resume(void* m) { }

private:
    hce::spinlock slk_;
};

}
}

/** 
 @brief object responsible for scheduling and executing coroutines and timers
 
 `scheduler`s cannot be created directly, it must be created by calling 
 `scheduler::make()` and retrieving it from the returned `install` pointer by 
 calling its `install::scheduler()` method.
    
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
                // call version which safely locks scheduler
                d->reschedule_coroutine_handle(std::move(h)); 
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

    struct cannot_install_in_a_coroutine : public std::exception {
        cannot_install_in_a_coroutine() : 
            estr([]() -> std::string {
                std::stringstream ss;
                ss << coroutine::local()
                   << " attempted to install and run an hce::scheduler";
                return ss.str();
            }()) 
        { }

        inline const char* what() const noexcept { return estr.c_str(); }

    private:
        const std::string estr;
    };

    /**
     @brief object for controlling the lifecycle state of schedulers 

     This object is allocated and assigned during a call to:
     ```
     std::shared_ptr<scheduler::install> scheduler::make(std::unique_ptr<scheduler::lifecycle>&)
     ```

     When the allocated `lifecycle` object goes out of scope, the scheduler will 
     be permanently halted (no more coroutines will be executed). However, the 
     destructor for the `lifecycle` object will not return until all scheduled 
     coroutines and timers complete (or, in the case of timers, cancelled). This 
     requires user code to correctly shutdown (or be triggered to shutdown) 
     prior to the `lifecycle` object going out of scope.

     Additionally, allocated `lifecycle` objects can temporarily suspend and 
     resume execution of coroutines on their associated `scheduler` using the 
     `lifecycle::suspend()` and `lifecycle::resume()` methods.
     */
    struct lifecycle : public printable {
        /**
         @brief object responsible for destructing registered lifecycle instances at process termination
         */
        struct manager : public printable {
            /// access the process wide manager object
            static manager& instance();

            ~manager() { HCE_HIGH_DESTRUCTOR(); }
            
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

            /// register a lifecycle pointer to be destroyed at process exit
            inline void registration(std::unique_ptr<lifecycle> lptr) {
                if(lptr) {
                    HCE_HIGH_METHOD_ENTER("registration", *lptr);
                    std::lock_guard<hce::spinlock> lk(lk_);

                    if(state_ != halted) { 
                        // Synchronize the new lifecycle with the global state
                        if(state_ == executing) { lptr->resume(); }
                        else if(state_ == suspended) { lptr->suspend(); }
                        lptrs_.push_back(std::move(lptr)); 
                    }
                }
            }

            /// call lifecycle::suspend() on all registered lifecycles
            inline void suspend() {
                HCE_HIGH_METHOD_ENTER("suspend");
                std::lock_guard<hce::spinlock> lk(lk_);

                if(state_ != halted) {
                    state_ = suspended;
                    for(auto& lp : lptrs_) { lp->suspend(); }
                }
            }

            /// call lifecycle::resume() on all registered lifecycles
            inline void resume() {
                HCE_HIGH_METHOD_ENTER("resume");
                std::lock_guard<hce::spinlock> lk(lk_);

                if(state_ == suspended) {
                    state_ = executing;
                    for(auto& lp : lptrs_) { lp->resume(); }
                }
            }

        private:
            manager() : state_(executing) { 
                std::atexit(manager::atexit); 
                HCE_HIGH_DESTRUCTOR();
            }

            static inline void atexit() { manager::instance().exit(); }

            inline void exit() {
                HCE_HIGH_METHOD_BODY("exit");
                std::lock_guard<hce::spinlock> lk(lk_);

                if(state_ != halted) {
                    state_ = halted;

                    for(auto& lptr : lptrs_) {
                        HCE_WARNING_METHOD_BODY("exit",lptr->scheduler());
                    }

                    lptrs_.clear();
                }
            }

            hce::spinlock lk_;
            hce::scheduler::state state_;
            std::deque<std::unique_ptr<lifecycle>> lptrs_;
        };

        /**
         When the scheduler's associated lifecycle goes out of scope, the 
         scheduler is permanently halted.
         */
        virtual ~lifecycle(){ 
            HCE_HIGH_DESTRUCTOR();
            sch_->halt_(); 
        }
            
        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "lifecycle"; }

        inline std::string content() const {
            std::stringstream ss;
            if(sch_) { ss << *sch_; }
            return ss.str();
        }

        /**
         @brief return a reference to the `lifecycle`'s associated scheduler
         */
        inline hce::scheduler& scheduler() { 
            HCE_HIGH_METHOD_ENTER("scheduler");
            return *sch_; 
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
            return sch_->suspend_(); 
        }

        /**
         @brief resume coroutine execution after a call to `suspend()`
         */
        inline void resume() { 
            HCE_HIGH_METHOD_ENTER("resume");
            return sch_->resume_(); 
        }

    private:
        lifecycle() = delete;

        lifecycle(std::shared_ptr<hce::scheduler>& sch) : sch_(sch) {
            HCE_HIGH_CONSTRUCTOR(sch_.get());
        }

        std::shared_ptr<hce::scheduler> sch_;
        friend struct hce::scheduler;
    };

    /**
     @brief object for configuring runtime behavior of a scheduler 

     An instance of this object passed to `scheduler::make()` will configure 
     the scheduler during its runtime with the given event handlers and options.
     */
    struct config : public printable {
        typedef std::function<void(hce::scheduler&)> handler;

        /**
         @brief a collection of handlers 

         Handler functions are neither copiable nor movable once installed.
         */
        struct handlers : public printable {
            handlers() { HCE_HIGH_CONSTRUCTOR(); }

            handlers(const handlers& rhs) = delete;
            handlers(handlers&& rhs) = delete;

            virtual ~handlers() {
                HCE_HIGH_DESTRUCTOR();
                // clear handlers in reverse install order
                while(hdls_.size()) { hdls_.pop_back(); }
            }

            handlers& operator=(const handlers& rhs) = delete;
            handlers& operator=(handlers&& rhs) = delete;

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
            inline void install(handler h) {
                hdls_.push_back(std::move(h));
                HCE_HIGH_METHOD_ENTER("install",hce::detail::utility::callable_to_string(hdls_.back()));
            }

            /// install a handler that accepts no arguments
            inline void install(hce::thunk th) {
                hdls_.push_back([th=std::move(th)](hce::scheduler&){ th(); });
                HCE_HIGH_METHOD_ENTER("install",hce::detail::utility::callable_to_string(hdls_.back()));
            }

            /// return count of installed handlers
            inline size_t count() { return hdls_.size(); }

        private:
            // Call all handlers with the given scheduler. Only invoked by 
            // `scheduler::install`'s call to `scheduler::run()`
            inline void call(hce::scheduler& sch) {
                HCE_HIGH_METHOD_ENTER("call", sch);

                for(auto& hdl : hdls_) { 
                    HCE_HIGH_METHOD_BODY("call",hce::detail::utility::callable_to_string(hdl));
                    hdl(sch); 
                }
            }

            std::list<handler> hdls_;

            friend struct hce::scheduler;
        };

        config(const config& rhs) = delete;
        config(config&& rhs) = delete;

        virtual ~config() { HCE_HIGH_DESTRUCTOR(); }

        config& operator=(const config& rhs) = delete;
        config& operator=(config&& rhs) = delete;

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
            ss << "log_level:"
               << log_level
               << ", block_workers_reuse_pool: "
               << block_workers_reuse_pool
               << ", on_init: " 
               << on_init 
               << ", on_suspend: " 
               << on_suspend
               << ", on_halt:" 
               << on_halt
               << ", on_exception: " 
               << on_exception;

            return ss.str(); 
        }

        /**
         Set the log level of the thread the scheduler is installed on.

         Maximum value: 9
         Minimum value: -9
         */
        int log_level;

        /**
         The count of worker threads used by `block()` the scheduler will 
         persist for reuse. No workers are created until they are required.
         */
        size_t block_workers_reuse_pool;
        
        /**
         Handlers to be called on initialization (before executing coroutines) 
         when a scheduler runs by the associated `hce::scheduler::install` 
         going out of scope.
         */
        handlers on_init;

        /**
         Handlers to be called when a scheduler is suspended while the scheduler 
         is running by a call to `hce::scheduler::lifecycle::suspend()`.
         */
        handlers on_suspend;

        /**
         Handlers to be called when a scheduler is halted by its associated 
         `hce::scheduler::lifecycle` unique pointer going out of scope.
         */
        handlers on_halt;

        /**
         Handlers to be called when an exception is caught. The caught exception 
         can be retrieved with `hce::scheduler::current_exception()`. This 
         mechanism allows exception handling to be installed into the `config`, 
         and permanently associated with a constructed scheduler, rather than 
         having to properly and manually setup handlers in the installation 
         thread.
         */
        handlers on_exception;

    private:
        config() :
            log_level(hce::printable::default_log_level()),
            block_workers_reuse_pool(0)
        { 
            HCE_HIGH_CONSTRUCTOR(); 
        }
    };

    /**
     @brief the object responsible for installing and running a scheduler on a thread

     An allocated `std::unique_ptr<scheduler::install>` will be returned from 
     `scheduler::make()` function calls. 

     Wherever the `install` object's destructor is called, at that point the 
     `scheduler` will run and continuously execute coroutines until it is halted
     by an associated `lifecycle` object's destructor. `install`'s destructor 
     will not return until the scheduler is halted. This guarantees the 
     scheduler will only be run from one location, and in the proper way.
     */
    struct install : public printable {
        install() = delete;
        install(const install&) = delete;
        install(install&&) = delete;

        install& operator=(const install&) = delete;
        install& operator=(install&&) = delete;

        /**
         @brief when the install object goes out of scope, it will block and run the scheduler on the calling thread to continuously execute scheduled coroutines and process timers until halted

         WARNING: It is an ERROR to call this method from inside a running 
         coroutine.

         Execution of coroutines can be paused by calling `lifecycle::suspend()` 
         ceasing operations until `lifecycle::resume()` (or until the scheduler 
         is halted by the lifecycle object going out of scope). If the scheduler 
         was created without an explicit `lifecycle` the same can be achieved by 
         calling `lifecycle::manager::suspend()`/`lifecycle::manager::resume()`, 
         on the process-wide `lifecycle::manager` instance (accessible by 
         calling `lifecycle::manager::instance()`). However, this will affect 
         *all* schedulers registered on the `lifecycle::manager`.

         @param c optional configuration for the executing scheduler
         */
        ~install() { 
            HCE_HIGH_DESTRUCTOR(); 
            if(sch_) { sch_->run(); }
        }

        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "install"; }

        inline std::string content() const {
            std::stringstream ss;
            ss << sch_.get();
            return ss.str();
        }

        /**
         Retrieving the scheduler as a shared_ptr can be done by conversion:
         ```
         std::unique_ptr<hce::scheduler::install> i = hce::scheduler::make();
         std::shared_ptr<hce::scheduler> sch = i.scheduler();
         ```

         @brief return a reference to the `install`'s associated scheduler
        */ 
        inline hce::scheduler& scheduler() { return *sch_; }

    private:
        install(std::shared_ptr<hce::scheduler> sch) : sch_(std::move(sch)) {
            HCE_HIGH_CONSTRUCTOR(); 
        }

        std::shared_ptr<hce::scheduler> sch_;
        friend struct hce::scheduler;
    };

    virtual ~scheduler() {
        HCE_HIGH_CONSTRUCTOR();
        halt_();
    }

    /**
     @brief allocate and construct a scheduler 

     `scheduler::make()` calls are the only way to make a new scheduler. The 
     returned allocated `std::unique_ptr<scheduler::install>` contains the new 
     scheduler (as does the `std::unique_ptr<scheduler::lifecycle>` argument). 
     Said scheduler can be retrieved as a reference by calling method 
     `install::scheduler()`. 

     Retrieving the scheduler as a shared_ptr can be done by conversion:
     ```
     std::unique_ptr<hce::scheduler::install> i = hce::scheduler::make();
     std::shared_ptr<hce::scheduler> sch = i.scheduler();

     // ... later 
     
     // install's destructor runs the scheduler until the associated lifecycle 
     // object is destructed
     i.reset() 
     ```

     This variant will allocate and construct the argument lifecycle pointer 
     with the newly allocated and constructed scheduler.

     Unless the user wants to manually manage the `scheduler`'s lifecycle, it is 
     often a good idea to register schedulers with the 
     `lifecycle::manager` instance (call `scheduler::make()` without an argument 
     `lifecycle` pointer to do this automatically) so they will be automatically 
     halted on program exit instead of manually. Of course, doing so will cause 
     `hce::scheduler::install::~install()` to block its thread until program 
     exit, so design accordingly.

     WARNING: Using the `make(std::unique_ptr<lifecycle>&)` variant, as opposed 
     to the simpler `make()` variant, suggests user code is going to potentially 
     halt a `scheduler` before the process exits. This adds a design burden onto 
     user code, because unsynchronized early halts potentially create race 
     conditions with multithreaded user operations on the `scheduler`. Thus, the 
     simpler `make()` variant is recommended unless the user is willing to 
     ensure their multithreaded operations are complete before letting the 
     `lifecycle` destruct.

     @param lc reference to a lifecycle unique pointer
     @param c optional config unique pointer to configure the runtime behavior of the scheduler
     @return an allocated install unique pointer containing the scheduler
     */
    static inline std::unique_ptr<install> make(
            std::unique_ptr<lifecycle>& lc, 
            std::unique_ptr<config> c = {}) {
        HCE_HIGH_FUNCTION_ENTER("hce::scheduler::make","std::unique_ptr<hce::scheduler::lifecycle>&","std::unique_ptr<hce::scheduler::config>");

        // allocate a new scheduler
        scheduler* sp = new scheduler();

        // make the first shared pointer
        std::shared_ptr<scheduler> s(sp);

        // finish initialization and configure the scheduler's runtime behavior
        s->configure_(s, std::move(c));

        // allocate and assign the lifecycle pointer
        lc = std::unique_ptr<lifecycle>(new lifecycle(s));

        // allocate and return the install pointer
        return std::unique_ptr<install>(new install(std::move(s)));
    }

    /**
     @brief allocate and construct a scheduler 

     This variant automatically registers an allocated `lifecycle` with the 
     `lifecycle::manager` instance to be halted when the process exits.

     @param c optional config unique pointer to configure the runtime behavior of the scheduler
     @return an allocated install unique pointer containing the scheduler
     */
    static inline std::unique_ptr<install> make(
            std::unique_ptr<config> c = {}) {
        HCE_HIGH_FUNCTION_ENTER("hce::scheduler::make");
        // manage the lifecycle automatically
        std::unique_ptr<lifecycle> lc;

        // construct a scheduler and its associated objects
        auto i = scheduler::make(lc, std::move(c));

        // register the lifecycle with the process wide manager
        lifecycle::manager::instance().registration(std::move(lc));
        return i;
    };

    /**
     @return `true` if calling thread is executing an installed scheduler, else `false`
     */
    static inline bool in() {
        HCE_TRACE_FUNCTION_ENTER("hce::scheduler::in");
        return detail::scheduler::tl_this_scheduler(); 
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

     Prefer scheduler::local(), falling back to scheduler::global().

     Useful when some scheduler is required, but a specific scheduler is not. 
     Additionally, operating on the current thread's scheduler, where possible,
     mitigates inter-thread communication delays.

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
     @return the scheduler thread's log level
     */
    inline int log_level() {
        auto l = config_->log_level;
        HCE_TRACE_METHOD_BODY("log_level",l);
        return l;
    }

    /// return the state of the scheduler
    inline state status() const {
        HCE_MIN_METHOD_ENTER("status");
        std::unique_lock<spinlock> lk(lk_);
        return state_;
    }

    /**
     This value must reach 0 for a scheduler to complete the halt process when 
     a `scheduler`'s associated `lifecycle` object goes out of scope. Until this 
     occurs, the `lifecycle`'s destructor will block. Each scheduled  coroutine 
     (including those scheduled with `join()` and `scope()`) and each started 
     timer (including `sleep()` timers) contribute to this count.

     @return the count of all incomplete operations on this scheduler 
     */
    inline size_t operations() const {
        size_t c;

        {
            std::lock_guard<spinlock> lk(lk_);
            c = operations_.count();
        }
        
        HCE_TRACE_METHOD_BODY("operations",c);
        return c;
    }

    /**
     @brief access a heuristic for the scheduler's active workload
     @return the count of coroutines executing and waiting to execute
     */
    inline size_t workload() const {
        size_t c;

        {
            std::lock_guard<spinlock> lk(lk_);
            c = batch_size_ + coroutine_queue_->size();
        }
        
        HCE_TRACE_METHOD_BODY("workload",c);
        return c;
    }

    /**
     Useful heuristic for determining ideal `block_workers_reuse_pool()` size.

     @return the current count of worker threads spawned for `block()`ing tasks 
     */
    inline size_t block_workers() const {
        auto c = block_manager_->count(); 
        HCE_TRACE_METHOD_BODY("block_workers",c);
        return c;
    }

    /**
     Worker threads utilized by the `block()` mechanism can be reused by the 
     scheduler, potentially increasing program efficiency when blocking calls 
     need to be regularly made.

     The default count of reused worker threads is 0 threads (except for the 
     global scheduler which has a default of 1 thread). However, any number of 0 
     or higher can be specified by `install()`ing the scheduler with an 
     `hce::scheduler::config` with its `block_workers_reuse_pool` member set to 
     the desired count of worker threads to reuse. Consider configuring 
     schedulers with a higher value `block_workers_reuse_pool` if blocking calls 
     are made frequently.

     The default global scheduler's reuse blocker worker count is specified by 
     the `std::unique_ptr<hce::scheduler::config>` returned by global function 
     `hce_scheduler_global_config()`. `hce_scheduler_global_config()` is an 
     `extern` function, and its library default implementation will not be 
     compiled if compiler flag `HCECUSTOMGLOBALCONFIG` is provided during 
     compilation of the library, allowing the user to write and link against 
     their own implementation. The default implementation's `config` returned by 
     `hce_scheduler_global_config()` has its `block_workers_reuse_pool` member 
     specified at library compile time by the compiler define 
     `HCEGLOBALREUSEBLOCKPROCS` (which defaults to 1 thread). 

     @return the minimum count of `block()` worker threads the scheduler will persist
     */
    inline size_t block_workers_reuse_pool() const { 
        auto c = block_manager_->reuse(); 
        HCE_TRACE_METHOD_BODY("block_workers_reuse_pool",c);
        return c;
    }

    /**
     @brief provides access to a caught exception for use in `hce::scheduler::config` on_exception handlers
     @return the current exception pointer
     */
    inline std::exception_ptr current_exception() {
        HCE_TRACE_METHOD_ENTER("current_exception");
        return current_exception_;
    }

    /**
     @brief schedule coroutines

     Arguments to this function can be:
     - an `hce::coroutine` or `hce::co<T>`
     - an iterable container of `hce::coroutine`s or `hce::co<T>`s
     - any combination of the above

     Multi-argument `schedule()`s hold the scheduler's lock throughout (they are 
     simultaneously scheduled). 

     Calls to `schedule()` will fail if the scheduler is halted and no 
     other scheduled operations remain. These exceptions allow work to be 
     finished on a scheduler BUT they require that user code properly cease 
     operation in order for schedulers to halt properly. If this is called by 
     a coroutine executing on this scheduler, it will always succeed (similar 
     for all other schedule or timer operations). Method calls to the `global()` 
     `scheduler` (alongside any other `scheduler`s registered on the 
     `scheduler::lifecycle::manager` instance) should also always succeed.

     The above behavior means that user code is responsible for ensuring all 
     necessary operations are completed before manually destructing a 
     `scheduler`'s `lifecycle` instance. This can be done by `join()`ing or 
     `scope()`ing all operations. If a `scheduler` is managed by the 
     `scheduler::lifecycle::manager`, no such care is required.

     @param a the first argument
     @param as any remaining arguments 
     @return `true` on successful schedule, else `false`
     */
    template <typename A, typename... As>
    bool schedule(A&& a, As&&... as) {
        HCE_HIGH_METHOD_ENTER("schedule",a,as...);

        std::unique_lock<spinlock> lk(lk_);

        if(can_schedule_()) {
            schedule_(std::forward<A>(a), std::forward<As>(as)...);
            return true;
        } else {
            return false;
        }
    }

    /***
     @brief schedule a single coroutine and return an awaitable to await the `co_return`ed value

     It is an error if the `co_return`ed value from the coroutine cannot be 
     converted to expected type `T`.

     Calls to `join()` can fail in an identical way to `schedule()`. Attempting 
     to launch a joinable operation (calling either `join()` or `scope()`) that 
     fails will throw an exception. 

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
        if(!can_schedule_()) { throw is_halted(this,"join()"); }

        return join_(co);
    }

    /***
     @brief join with all argument coroutines, ignoring return values

     Like `join()`, except it can join with one or more argument coroutines and 
     fail in an identical way.

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
        if(!can_schedule_()) { throw is_halted(this,"scope()"); }

        return scope_(std::forward<As>(as)...);
    }

    /**
     @brief start a timer on this scheduler 

     The returned awaitable will result in `true` if the timer timeout was 
     reached, else `false` will be returned if it was cancelled early due to 
     scheduler being totally halted.

     Timers count as a scheduled operation similar to coroutines for the 
     purposes of lifecycle management; running timers will prevent a scheduler 
     from halting. If a timer needs to be cancelled early, call `cancel()`.

     @param id a reference to an hce::id which will be set to the launched timer's id
     @param as the remaining arguments which will be passed to `hce::chrono::duration()`
     @return an awaitable to join with the timer timing out (returning true) or being cancelled (returning false)
     */
    template <typename... As>
    inline hce::awt<bool> start(hce::id& id, As&&... as) {
        hce::chrono::time_point timeout =
            hce::chrono::duration(std::forward<As>(as)...) + hce::chrono::now();
        
        std::unique_ptr<timer> t(new timer(*this, timeout));

        // acquire the id of the constructed timer
        id = t->id();

        HCE_MED_METHOD_ENTER("start",id,timeout);

        {
            std::lock_guard<spinlock> lk(lk_);

            if(!can_schedule_()) { 
                // cancel the timer immediately
                t->resume((void*)0); 
            } else { insert_timer_(t.get()); }
        }

        return hce::awt<bool>::make(t.release());
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
        
        std::unique_ptr<timer> t(new timer(*this, timeout));

        {
            std::lock_guard<spinlock> lk(lk_);

            if(!can_schedule_()) { 
                // cancel the timer immediately
                t->resume((void*)0); 
            } else { insert_timer_(t.get()); }
        }

        return hce::awt<bool>::make(t.release());
    }

    /**
     @brief determine if a timer with the given id is running
     @return true if the timer is running, else false
     */
    inline bool running(const hce::id& id) const {
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
        bool result = false;

        if(id) {
            std::lock_guard<spinlock> lk(lk_);
            auto it = timers_.begin();
            auto end = timers_.end();

            while(it!=end) {
                // search through the timers for a matching id
                if((*it)->id() == id) {
                    result = true;
                    operations_.completed(1);
                    (*it)->resume((void*)0);
                    HCE_MED_METHOD_BODY("cancel","cancelled timer with id:",(*it)->id());
                    timers_.erase(it);
                    break;
                }

                ++it;
            }
        } 

        return result;
    }

    /**
     @brief execute a Callable on a dedicated thread and block the calling coroutine or thread

     A Callable is any function, Functor, lambda or std::function. It is 
     anything that is invokable with the parenthesis `()` operator.

     `block()`'s potential failure behavior is identical `join()`. `block()` 
     calls act as a scheduled operation and therefore block `lifecycle` object's
     destructor from halting a `scheduler` until they return.

     This mechanism allows execution of arbitrary code on a dedicated thread, 
     and to `co_await` the result of the `block()` (or if not called from a 
     coroutine, just assign the awaitable to a variable of the resulting type). 

     IE in a coroutine (with the high level `hce::block()` call):
     ```
     T result = co_await hce::block(my_function_returning_T);
     ```

     Outside a coroutine:
     ```
     T result = hce::block(my_function_returning_T);
     ```

     This allows for executing arbitrary blocking code (which would be unsafe to 
     do in a coroutine!) via a mechanism which *is* safely callable from within
     a coroutine.

     The user Callable will execute on a dedicated thread, and won't have direct 
     access to the caller's local scheduler or coroutine. IE, 
     `hce::scheduler::in()` and `hce::coroutine::in()` will return `false`. 
     Access to the source scheduler will have to be explicitly given by user 
     code by passing in the local scheduler's shared_ptr to the blocking code.

     If the caller is already executing in a thread managed by another call to 
     `block()`, or if called outside of an `hce` coroutine, the Callable will be 
     executed immediately on the *current* thread.

     The given Callable can access values owned by the coroutine's body (or 
     values on a thread's stack if called outside of a coroutine) by reference, 
     because the caller of `call()` will be blocked while awaiting the `block()`
     call.

     WARNING: It is highly recommended to immediately `co_await` the awaitable 
     returned by `block()`. IE, if the executing Callable accesses the stack of 
     the caller in an unsynchronized way, and the caller of `block()` is a 
     coroutine, then the caller should `co_await` the returned awaitable before 
     reading or writing values. 

     @param cb a function, Functor, lambda or std::function
     @param as arguments to cb
     @return an awaitable returning the result of the Callable 
     */
    template <typename Callable, typename... As>
    awt<hce::detail::function_return_type<Callable,As...>> 
    block(Callable&& cb, As&&... as) {
        typedef hce::detail::function_return_type<Callable,As...> RETURN_TYPE;
        using isv = typename std::is_void<RETURN_TYPE>;

        HCE_MED_METHOD_ENTER("block",hce::detail::utility::callable_to_string(cb));

        return block_manager_->block(
            std::integral_constant<bool,isv::value>(),
            std::forward<Callable>(cb),
            std::forward<As>(as)...);
    }
 
private:
    // a generic queue type for scheduling coroutines
    typedef std::deque<std::coroutine_handle<>> queue;

    // struct to `co_await` all `scope()`ed awaitables
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

    // internal timer implementation
    struct timer : public hce::awaitable::lockable<
            hce::awt<bool>::interface,
            hce::spinlock>
    {
        timer(hce::scheduler& parent, const hce::chrono::time_point& tp) : 
                hce::awaitable::lockable<
                    hce::awt<bool>::interface,
                    hce::spinlock>(
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

        // manually implement `reschedule` API
        inline void destination(std::coroutine_handle<> h) {
            HCE_LOW_METHOD_ENTER("destination",h);

            auto dest = parent_.lock();

            if(dest) { 
                HCE_LOW_METHOD_BODY("destination",*dest,h);
                // directly call internal version because this is always held 
                // while scheduler has the lock
                dest->reschedule_coroutine_handle_(std::move(h)); 
            }
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

    struct block_manager : public hce::printable {
        // block workers implicitly start a scheduler on a new thread during 
        // construction and shutdown said scheduler during destruction.
        struct worker : public hce::printable {
            worker() { 
                auto cfg = hce::scheduler::config::make();

                // need a special exception handler because this is a 
                // worker managed by the framework
                cfg->on_exception.install(worker::exception_handler);

                auto i = hce::scheduler::make(lf_, std::move(cfg));
                sch_ = i->scheduler();

                // launch a thread to execute the worker's scheduler
                std::thread([](std::unique_ptr<hce::scheduler::install> i, 
                               worker* parent){ 
                    // set the thread_local worker pointer
                    worker::tl_worker() = parent;
                    // destruct install object, causing scheduler to run
                }, 
                std::move(i), 
                this).detach();

                HCE_MED_CONSTRUCTOR();
            }

            ~worker() { HCE_MED_DESTRUCTOR(); }

            // returns true if called on a thread owned by a worker object, else false
            static bool tl_is_block() { return (bool)tl_worker(); }

            inline const char* nspace() const { 
                return "hce::scheduler::block_manager"; 
            }

            inline const char* name() const { return "worker"; }
            inline std::string content() const { return sch_->to_string(); }

            // return the scheduler installed on the worker's thread
            hce::scheduler& scheduler() { return *sch_; }

        private:
            static worker*& tl_worker();

            // exception handler for scheduler's config
            static void exception_handler(hce::scheduler& sch) {
                try {
                    std::rethrow_exception(sch.current_exception());
                } catch(const std::exception& e) {
                    HCE_ERROR_FUNCTION_BODY(
                        "hce::scheduler::block_manager::worker::exception_handler",
                        tl_worker(), // printing the worker will print the scheduler address
                        " caught exception: ",
                        e.what());
                } catch(...) {
                    HCE_ERROR_FUNCTION_BODY(
                        "hce::scheduler::block_manager::worker::exception_handler",
                        tl_worker(),
                        " caught unknown exception)");
                }
            }

            std::unique_ptr<hce::scheduler::lifecycle> lf_;
            std::shared_ptr<hce::scheduler> sch_;
        };

        // struct for returning an immediately available block() value
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

        // Contractors are transient managers of block worker threads, ensuring 
        // they are checked out and back in at the proper times. The templated 
        // type is only relevant for implementing the a cleanup_handler.
        template <typename T>
        struct contractor : public detail::scheduler::cleanup_handler<T> {
            contractor(std::shared_ptr<block_manager> parent) : 
                // on construction get a worker
                wkr_(parent->checkout_worker_()),
                parent_(std::move(parent))
            { }

            virtual ~contractor() { 
                // on destruction return the worker to its block_manager
                if(wkr_){ 
                    auto parent = parent_.lock();

                    if(parent) {
                        parent->checkin_worker_(std::move(wkr_)); 
                    }
                }
            }

            inline void operator()(typename hce::co<T>::promise_type& p) { 
                // do nothing, everything will be done in the destructor
            }

            // return the contractor's worker
            inline block_manager::worker& worker() { return *wkr_; }

        private:
            std::unique_ptr<block_manager::worker> wkr_;
            std::weak_ptr<block_manager> parent_;
        };

        static inline std::shared_ptr<block_manager> make(hce::scheduler& parent) {
            std::shared_ptr<block_manager> sptr(new block_manager(parent));
            sptr->init_(sptr);
            return sptr;
        }

        inline const char* nspace() const { return "hce::scheduler"; }
        inline const char* name() const { return "block_manager"; }

        inline void set_worker_reuse_count(size_t reuse_count) {
            reuse_cnt_ = reuse_count;
        }

        template <typename Callable, typename... As>
        hce::awt<hce::detail::function_return_type<Callable,As...>>
        block(std::false_type, Callable&& cb, As&&... as) {
            typedef hce::detail::function_return_type<Callable,As...> T;
            if(!scheduler::in() || worker::tl_is_block()) {
                HCE_MED_METHOD_BODY("block","executing on current thread");
                
                // we own the thread already, call cb immediately and return the 
                // result
                return hce::awt<T>::make(
                    new done<T>(cb(std::forward<As>(as)...)));
            } else {
                // get a contractor to do the work of managing the worker thread
                std::unique_ptr<contractor<T>> cr(
                        new contractor<T>(self_.lock())); 

                // acquire the contractor's scheduler
                auto& wkr = cr->worker(); 

                HCE_MED_METHOD_BODY("block","executing on ",wkr);

                // acquire the workers's scheduler
                auto& sch = wkr.scheduler(); 

                // Convert our Callable to a coroutine that has no access to
                // its executing scheduler or coroutine from within. This
                // shouldn't be a problem because there will be no competition 
                // for the thread it runs on, allowing user code to do whatever 
                // it wants.
                hce::co<T> co = detail::scheduler::block_wrapper<T>(
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

        // void return specialization 
        template <typename Callable, typename... As>
        hce::awt<void>
        block(std::true_type, Callable&& cb, As&&... as) {
            if(!scheduler::in() || worker::tl_is_block()) {
                HCE_LOW_METHOD_BODY("block","executing on current thread");
                cb(std::forward<As>(as)...);
                return hce::awt<void>::make(new done<void>());
            } else {
                std::unique_ptr<contractor<void>> cr(
                    new contractor<void>(self_.lock())); 
                auto& wkr = cr->worker(); 
                HCE_MED_METHOD_BODY("block","executing on ",wkr);
                auto& sch = wkr.scheduler(); 

                hce::co<void> co = detail::scheduler::block_wrapper<void>(
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

        // return the reuse count of managed block_manager threads
        inline size_t reuse() const { 
            HCE_TRACE_METHOD_BODY("reuse",reuse_cnt_);
            return reuse_cnt_; 
        }


        // return the current count of managed block_manager threads
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
        block_manager(hce::scheduler& parent) : 
            reuse_cnt_(0),
            count_(0),
            parent_(parent)
        { }

        inline void init_(std::shared_ptr<block_manager>& self) {
            self_ = self;
        }

        inline std::unique_ptr<worker> checkout_worker_() {
            HCE_TRACE_METHOD_ENTER("checkout_worker_");
            std::unique_ptr<worker> w;

            std::unique_lock<spinlock> lk(lk_);
            ++count_;

            // check if we have any workers in reserve
            if(workers_.size()) {
                // get the first available worker
                w = std::move(workers_.front());
                workers_.pop_front();
                lk.unlock();
            } else {
                lk.unlock();

                // as a fallback generate a new await worker thread
                w = std::unique_ptr<worker>(new worker);
            }

            return w;
        }

        inline void checkin_worker_(std::unique_ptr<worker> w) {
            HCE_TRACE_METHOD_ENTER("checkin_worker_");

            std::unique_lock<spinlock> lk(lk_);
            --count_;

            if(workers_.size() < reuse_cnt_) {
                HCE_WARNING_METHOD_BODY("checkin_worker_","reused ",w.get());
                workers_.push_back(std::move(w)); // reuse worker
            } else { 
                HCE_WARNING_METHOD_BODY("checkin_worker_","discarded ",w.get());
            }
        }

        mutable spinlock lk_;

        size_t reuse_cnt_; // maximum reused block() worker thread count
        size_t count_; // total count of managed threads 
        hce::scheduler& parent_; // scheduler which owns this block_manager
        std::deque<std::unique_ptr<worker>> workers_; // worker memory
        std::weak_ptr<block_manager> self_; // weak_ptr to self
    };

    /*
     A convenience struct for tracking started operations. Theoretically, this 
     should be the same size as a raw `size_t` variable, and take no extra 
     time to operate compared to a raw variable and basic functions (the `this` 
     pointer will be the address of the`count_` member, and all function calls 
     will be passed that pointer and use a 0 offset).
     */
    struct operations_tracker : public printable {
        operations_tracker() : count_(0) { }

        inline const char* nspace() const { return "hce::scheduler";  }
        inline const char* name() const { return "operations_tracker"; }

        // increase operations count
        inline void started(size_t count) {
            HCE_TRACE_METHOD_ENTER("started",count);
            count_ += count; 
            HCE_TRACE_METHOD_BODY("started","total operations:",count_);

        }

        // decrease operations count
        inline void completed(size_t count) { 
            HCE_TRACE_METHOD_ENTER("completed",count);
            count_ -= count; 
            HCE_TRACE_METHOD_BODY("completed","total operations:",count_);
        }

        // return the count of incomplete operations
        inline size_t count() const { 
            HCE_TRACE_METHOD_BODY("count","count_:",count_);
            return count_; 
        }

    private:
        size_t count_;
    };

    scheduler() : 
            state_(ready), 
            coroutine_queue_(new scheduler::queue),
            block_manager_(block_manager::make(*this))
    { 
        HCE_HIGH_CONSTRUCTOR();
        reset_flags_(); // initialize flags
    }
    
    static scheduler& global_();

    // the innermost coroutine schedule operation
    inline void schedule_coroutine_handle_(std::coroutine_handle<> h) {
        HCE_TRACE_METHOD_ENTER("schedule_coroutine_",h);

        // verify our handle is represents a coroutine
        if(h) {
            if(h.done()) {
                HCE_WARNING_METHOD_BODY("schedule_coroutine_",h," is already done, destroying...");
                // we got a bad, completed handle and need to free memory, 
                // allow inline coroutine destructor to do it for us... 
                hce::coroutine c(std::move(h));
            } else {
                HCE_TRACE_METHOD_BODY("schedule_coroutine_","pushing ",h," onto queue ");
                operations_.started(1);
                coroutine_queue_->push_back(h);
            }
        }
    }

    // schedule a base coroutine's handle
    inline void schedule_coroutine_(coroutine c) {
        HCE_TRACE_METHOD_ENTER("schedule_coroutine_",c);
        schedule_coroutine_handle_(c.release());
    }

    // schedule a templated coroutine's handle
    template <typename T>
    inline void schedule_coroutine_(co<T> c) {
        HCE_TRACE_METHOD_ENTER("schedule_coroutine_",c);
        schedule_coroutine_handle_(c.release());
    }

    // when all coroutines are scheduled, notify run_() if necessary
    inline void schedule_() { tasks_available_notify_(); }

    template <typename A, typename... As>
    void schedule_(A&& a, As&&... as) {
        // detect if A is a container or a Stackless
        schedule_disambiguate_(
                detail::is_container<typename std::decay<A>::type>(),
                std::forward<A>(a));
        schedule_(std::forward<As>(as)...);
    }

    template <typename Container>
    void schedule_disambiguate_(std::true_type, Container&& coroutines) {
        for(auto& c : coroutines) {
            schedule_coroutine_(std::move(c));
        }
    }

    template <typename Coroutine>
    void schedule_disambiguate_(std::false_type, Coroutine&& c) {
        schedule_coroutine_(std::move(c));
    }

    template <typename T>
    awt<T> join_(hce::co<T>& co) {
        // returned awaitable resumes when the coroutine handle is destroyed
        auto awt = hce::awt<T>::make(
            new hce::scheduler::reschedule<
                hce::detail::scheduler::joiner<T>>(co));
        schedule_(std::move(co));
        return awt;
    }

    template <typename... As>
    hce::awt<void> scope_(As&&... as) {
        std::unique_ptr<std::deque<hce::awt<void>>> dq(
                new std::deque<hce::awt<void>>);
        scheduler::assemble_scope_(*dq, std::forward<As>(as)...);

        return hce::awt<void>::make(new scoper(std::move(dq)));
    }

    template <typename Container>
    void assemble_join_(std::true_type, std::deque<hce::awt<void>>& dq, Container& c) {
        for(auto& elem : c) {
            dq.push_back(hce::awt<void>::make(join_(elem).release()));
        }
    }

    template <typename Coroutine>
    void assemble_join_(std::false_type, std::deque<hce::awt<void>>& dq, Coroutine& c) {
        dq.push_back(hce::awt<void>::make(join_(c).release()));
    }

    static inline void assemble_scope_(std::deque<hce::awt<void>>& dq) { }

    template <typename A, typename... As>
    void assemble_scope_(
            std::deque<hce::awt<void>>& dq, 
            A&& a, 
            As&&... as) 
    {
        assemble_join_(
            detail::is_container<typename std::decay<decltype(a)>::type>(),
            dq, 
            a);
        assemble_scope_(dq, std::forward<As>(as)...);
    }

    inline void insert_timer_(timer* t) {
        operations_.started(1);
        timers_.push_back(t);

        // resort timers from soonest to latest
        timers_.sort([](timer* lhs, timer* rhs) {
            return lhs->timeout() < rhs->timeout();
        });

        tasks_available_notify_();
    }
    

    inline void reschedule_coroutine_handle(std::coroutine_handle<>&& h) {
        HCE_MIN_METHOD_ENTER("reschedule_coroutine_handle",h);

        std::unique_lock<spinlock> lk(lk_);
        reschedule_coroutine_handle_(std::move(h));
        tasks_available_notify_();
    }

    /* 
     Reschedules are always allowed so the operations_ count can reach 0, 
     allowing the scheduler to permanently halt. Because this can only be called 
     by an operation previously launched on a scheduler, which should keep the
     scheduler from halting, and reschedule operations are completely handled 
     by this framework, it is therefore an error in this framework if this is 
     called on a completely halted scheduler.
     */
    inline void reschedule_coroutine_handle_(std::coroutine_handle<>&& h) {
        HCE_TRACE_METHOD_ENTER("reschedule_coroutine_handle_",h);

        // verify our handle is represents a coroutine
        if(h) {
            if(h.done()) {
                HCE_WARNING_METHOD_BODY("reschedule_coroutine_handle_",h," is already done, destroying...");
                // we got a bad, completed handle and need to free memory, 
                // allow inline coroutine destructor to do it for us... 
                hce::coroutine c(std::move(h));
            } else {
                HCE_TRACE_METHOD_BODY("reschedule_coroutine_handle_","pushing ",h," onto queue ");
                // A reschedule does not count as a new operation.
                coroutine_queue_->push_back(h);
                tasks_available_notify_();
            }
        } else {
            HCE_ERROR_METHOD_BODY("reschedule_coroutine_handle_",h," is not a valid coroutine...");
        }
    }

    inline void configure_(std::shared_ptr<scheduler>& self, 
                           std::unique_ptr<config> cfg = {}) {
        // set the weak_ptr
        self_wptr_ = self;

        // ensure our config is allocated
        if(cfg) { config_ = std::move(cfg); }
        else { config_ = config::make(); }

        // set our block reuse threadpool
        block_manager_->set_worker_reuse_count(config_->block_workers_reuse_pool);
    }

    /**
     Run the scheduler based on its configuration.
     */
    inline void run() {
        // check the thread local scheduler pointer to see if we're already in 
        // a scheduler and error out immediately if called improperly
        if(coroutine::in()) { throw cannot_install_in_a_coroutine(); }
    
        struct scoped_log_level {
            scoped_log_level() : 
                parent_log_level_(hce::printable::thread_log_level()) 
            { }

            ~scoped_log_level() {
                hce::printable::thread_log_level(parent_log_level_);
            }

        private:
            size_t parent_log_level_;
        };

        // stash the current log level and restore when going out of scope
        scoped_log_level sll;

        // ensure the proper log level is set at the point 
        hce::printable::thread_log_level(config_->log_level);
        
        HCE_HIGH_METHOD_ENTER("run");

        // assume scheduler has never been run before, run_ will handle the real 
        // state in a synchronized way
        bool cont = true;

        auto handle_exception = [&]{
            if(config_->on_exception.count()) {
                // Call exception handlers, allow them to rethrow if 
                // necessary. `scheduler::current_exception()` contains the 
                // thrown exception pointer.
                HCE_ERROR_METHOD_BODY("run","hce::scheduler::config::on_exception.call()");
                config_->on_exception.call(*this);
            } else {
                HCE_ERROR_METHOD_BODY("run","hce::scheduler::config::on_exception has no handlers, rethrowing");
                std::rethrow_exception(std::current_exception());
            }
        };
        
        HCE_HIGH_METHOD_BODY("run","hce::scheduler::config::on_init.call()");

        // call initialization handlers
        config_->on_init.call(*this);

        do {
            try {
                cont = run_();
            } catch(const std::exception& e) {
                HCE_ERROR_METHOD_BODY("run","caught exception: ", e.what());
                handle_exception();
            } catch(...) {
                HCE_ERROR_METHOD_BODY("run","caught unknown exception");
                handle_exception();
            }

            // Call suspend handlers when run_() returns early due to 
            // `hce::scheduler::lifecycle::suspend()` being called.
            if(cont) { 
                HCE_HIGH_METHOD_BODY("run","hce::scheduler::config::on_suspend.call()");
                config_->on_suspend.call(*this); 
            }
        } while(cont);

        // call halt handlers
        HCE_HIGH_METHOD_BODY("run","hce::scheduler::config::on_halt.call()");
        config_->on_halt.call(*this);
    }

    /*
     Coroutine to run the scheduler and execute coroutines and timers continuously.

     Returns true on suspend, false on halt.
     */
    inline bool run_() {
        HCE_MED_METHOD_ENTER("run_");

        // only call function tl_this_scheduler() once to acquire reference to 
        // thread local shared scheduler pointer 
        auto& tl_sch = detail::scheduler::tl_this_scheduler();
        
        // assign thread_local scheduler pointer to this scheduler
        tl_sch = this; 

        // batch of coroutines to evaluate
        std::unique_ptr<scheduler::queue> local_queue(new scheduler::queue);

        // the current time
        hce::chrono::time_point now; 

        // count of operations completed in the current batch
        size_t batch_done_count = 0;

        // Push any remaining coroutines back into the main queue. Lock must be 
        // held before this is called.
        auto cleanup_batch = [&] {
            // reset scheduler batch evaluating count 
            batch_size_ = 0; 

            // update completed operations 
            operations_.completed(batch_done_count);

            // reset batch done operation count
            batch_done_count = 0;

            // push every uncompleted coroutine to the back of the scheduler's 
            // main queue
            while(local_queue->size()) {
                coroutine_queue_->push_back(local_queue->front());
                local_queue->pop_front();
            }
        };

        // acquire the lock
        std::unique_lock<spinlock> lk(lk_);

        // block until no longer suspended
        while(state_ == suspended) { 
            HCE_MED_METHOD_BODY("run_","suspended before run loop");
            resume_block_(lk); 
        }
        
        HCE_MED_METHOD_BODY("run_","entering run loop");

        // If halt_() has been called, return immediately
        if(state_ == ready) {
            state_ = executing; 

            // flag to control evaluation loop
            bool evaluate = true;

            try {
                // Evaluation loop runs fairly continuously. 99.9% of the time 
                // it is expected a scheduler is executing code within this 
                // do-while.
                do {
                    // check for any ready timers 
                    if(timers_.size()) {
                        // update the current timepoint
                        now = hce::chrono::now();
                        auto it = timers_.begin();
                        auto end = timers_.end();

                        while(it!=end) {
                            // check if a timer is ready to timeout
                            if((*it)->timeout() <= now) {
                                // every ready timer is a done operation
                                ++batch_done_count;
                                /*
                                 Handle ready timers before coroutines in case 
                                 exceptions occur in user code. Resuming a timer 
                                 will push a coroutine handle onto the 
                                 `coroutine_queue_` member.
                                  */
                                (*it)->resume((void*)1);
                                it = timers_.erase(it);
                            } else {
                                // no remaining ready timers, exit loop early
                                break;
                            }
                        }
                    }

                    // check for waiting coroutines
                    if(coroutine_queue_->size()) {
                        // acquire the current batch of coroutines by trading 
                        // with the scheduler's queue, reducing lock contention 
                        // by collecting the entire batch via a pointer swap
                        std::swap(local_queue, coroutine_queue_);

                        // update API accessible batch count
                        batch_size_ = local_queue->size();

                        // unlock scheduler when running executing coroutines
                        lk.unlock();

                        {
                            size_t count = local_queue->size();

                            // scope local coroutine to ensure std::coroutine_handle 
                            // lifecycle management with RAII semantics
                            coroutine co;

                            // evaluate the batch of coroutines once through
                            while(count) { 
                                // decrement from our initial batch count
                                --count;

                                // get a new task, swapping with the front of the 
                                // task queue
                                co.reset(local_queue->front());
                                
                                // if a handle swap occurred, this will cause the 
                                // std::coroutine_handle to be destroyed
                                local_queue->pop_front();

                                // evaluate coroutine
                                co.resume();

                                // check if the coroutine has a handle to manage
                                if(co) {
                                    if(!co.done()) {
                                        // locally re-enqueue coroutine 
                                        local_queue->push_back(co.release()); 
                                    } else {
                                        // update done operation count
                                        ++batch_done_count;
                                    }
                                } // else coroutine was suspended
                            }
                        } // make sure last coroutine is cleaned up before lock

                        // reacquire lock
                        lk.lock(); 
                    }

                    // cleanup batch results, requeueing coroutines
                    cleanup_batch();

                    // keep executing if coroutines are available
                    if(coroutine_queue_->empty()) {
                        // check if any running timers exist
                        if(timers_.empty()) {
                            // verify run state and block if necessary
                            if(can_evaluate_()) {
                                // wait for more tasks
                                waiting_for_tasks_ = true;
                                tasks_available_cv_.wait(lk);
                            } else {
                                // break out of the evaluation loop because 
                                // execution is halted and no operations remain
                                evaluate=false;
                            }
                        } else {
                            // check the time again
                            now = hce::chrono::now();
                            auto timeout = timers_.front()->timeout();

                            // wait, at a maximum, till the next scheduled
                            // timer timeout. If a timer is ready to timeout
                            // continue to the next iteration.
                            if(timeout > now) {
                                waiting_for_tasks_ = true;
                                tasks_available_cv_.wait_until(lk, timeout);
                            }
                        }
                    }
                } while(evaluate); 
            } catch(...) { // catch all other exceptions 
                // reset thread local state in case of uncaught exception
                tl_sch = nullptr; 

                // it is an error in this framework if an exception occurs when 
                // the lock is held, it should only be when executing user 
                // coroutines that this can occur
                lk.lock();

                current_exception_ = std::current_exception();
                cleanup_batch();

                lk.unlock();

                std::rethrow_exception(current_exception_);
            }
        }

        // restore parent thread_local pointer
        tl_sch = nullptr;

        if(state_ == suspended) {
            HCE_MED_METHOD_BODY("run_","exitted run loop, suspended");
            // reset scheduler state so run() can be called again
            reset_flags_();
            return true;
        } else {
            HCE_MED_METHOD_BODY("run_","exitted run loop, halted");
            halt_notify_();
            return false;
        }
    }

    /*
     Suspend the scheduler. 

     Pauses operations on the scheduler and causes calls to run_() to return 
     early.
     */
    inline void suspend_() {
        std::unique_lock<spinlock> lk(lk_);

        if(state_ != halted) { 
            state_ = suspended;

            // wakeup scheduler if necessary from waiting for tasks to force 
            // run() to exit
            tasks_available_notify_();
        }
    }

    /*
     Resumes a suspended scheduler. 
     */
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
     scheduler. However, this will not return until all previously scheduled 
     coroutines are completed.

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

    /* 
     Reset scheduler state flags, etc. Does not reset scheduled coroutine 
     queues. 
    
     This method can ONLY be safely called by the constructor or run_() because 
     access to these values are always unsynchronized.
     */
    inline void reset_flags_() {
        batch_size_ = 0;
        waiting_for_resume_ = false;
        waiting_for_tasks_ = false;
        waiting_for_halt_ = false;
    }

    /*
     Abstract inlined state checking to this function for correctness and 
     algorithm clarity.

     Determines if processing coroutines can continue in run_(). Processing can 
     continue as long as the scheduler is not suspended or halted, with the 
     exception that processing can continue while halted as long as the count of 
     operations_ coroutines is not 0 (IE, some work needs to finish).
     */
    inline bool can_evaluate_() { 
        HCE_TRACE_FUNCTION_BODY("can_evaluate_","(state_ == executing):", (state_ == executing) ? "true" : "false",", (state_ == halted && operations_.count()):", (state_ == halted && operations_.count()) ? "true" : "false");
        return (state_ == executing) || ((state_ == halted) && operations_.count()); 
    }

    /*
     Abstract inlined state checking to this function for correctness and 
     algorithm clarity.

     Can schedule if the scheduler isn't halted or if there are any unfinished 
     operations started on the scheduler.
     */
    inline bool can_schedule_() {
        HCE_TRACE_FUNCTION_BODY("can_schedule_","(state_ != halted):", (state_ != halted) ? "true" : "false",", operations_.count():", operations_.count() ? "true" : "false");
        return (state_ != halted) || operations_.count();
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
            HCE_TRACE_METHOD_BODY("resume_notify_");
            waiting_for_resume_ = false;
            resume_cv_.notify_one();
        }
    }

    inline void halt_notify_() {
        if(waiting_for_halt_) {
            HCE_TRACE_METHOD_BODY("halt_notify_");
            waiting_for_halt_ = false;
            halt_cv_.notify_one();
        }
    }

    // notify caller of run() that tasks are available
    inline void tasks_available_notify_() {
        // only do notify if necessary
        if(waiting_for_tasks_) {
            HCE_TRACE_METHOD_BODY("tasks_available_notify_");
            waiting_for_tasks_ = false;
            tasks_available_cv_.notify_one();
        }
    }

    // synchronization primative, marked mutable for use in const methods.
    mutable hce::spinlock lk_;

    // run configuration, marked mutable for use in const methods.
    mutable std::unique_ptr<config> config_;

    // a weak_ptr to the scheduler's shared memory
    std::weak_ptr<scheduler> self_wptr_;

    // the current state of the scheduler
    state state_; 

    // the most recently raised exception
    std::exception_ptr current_exception_;

    // the count of ALL scheduled, incomplete operations (coroutines and timers)
    operations_tracker operations_;
                  
    // the count of actively executing coroutines on this scheduler 
    size_t batch_size_; 
   
    // condition data for when scheduler::resume() is called
    bool waiting_for_resume_;
    std::condition_variable_any resume_cv_;
   
    // condition data for when scheduler::halt_() is called
    bool waiting_for_halt_;
    std::condition_variable_any halt_cv_;

    // condition data for when new tasks are available
    bool waiting_for_tasks_; 
    std::condition_variable_any tasks_available_cv_;

    // Queue holding scheduled coroutine handles. Simpler to just use underlying 
    // data than wrapper conversions between hce::coroutine/hce::co<T>. This 
    // object is a unique_ptr because it is routinely swapped between this 
    // object and the stack memory of the caller of scheduler::run_().
    std::unique_ptr<scheduler::queue> coroutine_queue_;

    // list holding scheduled timers. This will be regularly resorted from 
    // soonest to latest timeout. Timer pointer's memory is managed by an 
    // awaitable object, and should not be deleted by the scheduler directly.
    std::list<timer*> timers_;

    // the service object managing blocking calls and their threads
    std::shared_ptr<block_manager> block_manager_;
};


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
