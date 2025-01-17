//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_BLOCKING
#define HERMES_COROUTINE_ENGINE_BLOCKING

#include <memory>
#include <type_traits>
#include <thread>

#include "utility.hpp"
#include "memory.hpp"
#include "logging.hpp"
#include "atomic.hpp"
#include "circular_buffer.hpp"
#include "synchronized_list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {
namespace blocking {
namespace config {

extern size_t process_worker_resource_limit();
extern size_t global_scheduler_worker_resource_limit();
extern size_t default_scheduler_worker_resource_limit();

}

namespace detail {

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

}

/**
 @brief singleton service maintaining worker threads for executing blocking calls

 Much attention is paid to the design of this object because threads are 
 expensive:
 - their memory cost is larger than most user objects (due to stack size and `thread_local`s)
 - they require system calls for startup and shutdown 

 Instead this mechanism launches threads which listen for tasks over a private 
 synchronized queue, shutting down only when necessary.

 Several layers of optimization exist in order to limit the amount of worker 
 threads that need to get created/destroyed as well as limiting process-wide 
 lock contention:
 - block() checks if the current thread is a scheduler. If it isn't, the Callable 
 is immediately invoked.
 - a thread_local, lockless cache is used to hold reusable worker threads 
 capable of invoking Callables which can be drawn upon to invoke Callables.
 - a process-wide pool of reusable workers is maintained by this object which 
 can be drawn upon to invoke Callables.
 - if none of the previous options are available a new worker thread is 
 created/destroyed as necessary to execute the Callable
 */
struct service : public hce::printable {
    static inline std::string info_name() { return ("hce::blocking::service"); }
    inline std::string name() const { return service::info_name(); }

    /**
     @return the process-wide blocking service
     */
    static service& get();

    /**
     This value is determined by:
     hce::blocking::config::process_worker_resource_limit()

     This value represents the process-wide limit of reusable worker threads 
     maintained by this `service` object. This value only represents the count 
     of threads this mechanism will *persist*, as many worker threads as 
     necessary will be spawned and destroyed.

     Worker threads utilized by the `block()` mechanism can be reused by the 
     scheduler, potentially increasing program efficiency when blocking calls 
     need to be regularly made.

     @return the minimum count of `block()` worker threads the scheduler will persist
     */
    inline size_t worker_resource_limit() const { 
        HCE_LOW_METHOD_BODY("worker_resource_limit",workers_.size());
        return workers_.size();
    }
    
    /**
     This value is determined by either, depending on whether the thread is the 
     running the hce::scheduler::global() scheduler or another scheduler:
     hce::blocking::config::global_scheduler_process_worker_resource_limit()
     hce::blocking::config::default_scheduler_process_worker_resource_limit()

     If this is called by a non-scheduler thread, it will return 0.

     This value represents the cache size of reusable block workers for *only* 
     the calling thread. It is a distinct value from the process-wide worker 
     resource limit provided by `worker_resource_limit()`.

     @return the resource limit of reusable block workers for the calling thread
     */
    inline size_t thread_local_worker_resource_limit() const { 
        size_t sz = service::tl_worker_cache_().size();
        HCE_LOW_METHOD_BODY("worker_resource_limit",sz);
        return sz;
    }

    /**
     @return the process total count of worker threads spawned for blocking operations in the entire process
     */
    inline size_t worker_count() const {
        size_t c;

        {
            std::lock_guard<hce::spinlock> lk(lk_);
            c = worker_active_count_ + workers_.used();
        }

        HCE_LOW_METHOD_BODY("worker_count",c);
        return c; 
    }

    /**
     This value includes cached (waiting) workers.

     @return the count of worker threads spawned for blocking operations held by this thread
     */
    inline size_t thread_local_worker_count() const {
        size_t c = tl_worker_cache_().worker_count();
        HCE_LOW_METHOD_BODY("worker_count",c);
        return c; 
    }

    /**
     @brief shutdown, join, destruct and deallocate all workers in the process-wide cache 
     */
    inline void clear_workers() {
        HCE_LOW_METHOD_ENTER("clear");

        std::lock_guard<spinlock> lk(lk_);
        while(workers_.used()) {
            workers_.pop();
        }
    }

    /**
     @brief shutdown, join, destruct and deallocate all workers in the thread local cache
     */
    inline void thread_local_clear_workers() {
        HCE_LOW_METHOD_ENTER("thread_local_clear");
        tl_worker_cache_().clear();
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

     In a coroutine (with the high level `hce::blocking::block()` call):
     ```
     T result = co_await hce::blocking::block(my_function_returning_T, arg1, arg2);
     ```

     If the caller of `block()` immediately awaits the result then the given 
     Callable can access values owned by the coroutine's body (or values on a 
     thread's stack if called outside of a coroutine) by reference, because the 
     caller of `block()` will be blocked while awaiting.

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

        HCE_LOW_METHOD_ENTER("block",hce::callable_to_string(cb));

        return block_(
            std::integral_constant<bool,isv::value>(),
            std::forward<Callable>(cb),
            std::forward<As>(as)...);
    }

private:
    // block workers receive operations over a queue and execute them
    struct worker : public printable {
        worker() : thd_(worker::run_, &operations_) { 
            HCE_LOW_CONSTRUCTOR();
        }

        ~worker() { 
            HCE_LOW_DESTRUCTOR(); 
            operations_.close();
            thd_.join();
        }

        static inline std::string info_name() { 
            return "hce::blocking::service::worker"; 
        }

        inline std::string name() const { return worker::info_name(); }

        inline void schedule(std::unique_ptr<hce::thunk>&& operation) { 
            operations_.push_back(std::move(operation));
        }

    private:
        // worker thread scheduler run function
        static inline void run_(
                synchronized_list<std::unique_ptr<hce::thunk>>* operations) 
        {
            std::unique_ptr<hce::thunk> operation;

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
        hce::synchronized_list<std::unique_ptr<hce::thunk>> operations_;

        // operating system thread
        std::thread thd_;
    };

    // The thread_local worker cache object, acting as a buffer between the 
    // blocking::service's members and user code.
    struct cache : public printable {
        cache() :
            workers_([]() -> size_t {
                if(hce::scheduler::in()) {
                    if(hce::scheduler::local() == hce::scheduler::global()) {
                        return hce::blocking::config::global_scheduler_worker_resource_limit();
                    } else {
                        return hce::blocking::config::default_scheduler_worker_resource_limit();
                    }
                } else {
                    // non-scheduler threads don't utilize the cache, so size
                    // is 0
                    return 0;
                }
            }())
        { 
            HCE_MED_CONSTRUCTOR();
        }

        ~cache() {
            HCE_MED_DESTRUCTOR();

            while(workers_.used()) {
                hce::blocking::service::get().checkin_worker_(
                    std::move(workers_.front()));
                workers_.pop();
            }
        }

        static inline std::string info_name() { 
            return "hce::blocking::service::cache"; 
        }

        inline std::string name() const { return cache::info_name(); }

        // the size of the cache
        inline size_t size() const { return workers_.size(); }

        // count of workers checked out by the thread
        inline size_t worker_count() const {
            size_t c = worker_active_count_ + workers_.used();
            HCE_TRACE_METHOD_BODY("worker_count",c);
            return c;
        }

        // checkout a worker from the thread local cache, falling back to the 
        // process-wide service if necessary
        inline std::unique_ptr<worker> checkout() {
            HCE_TRACE_METHOD_ENTER("checkout");
            std::unique_ptr<worker> w;
            ++worker_active_count_;

            if(workers_.used()) [[likely]] {
                w = std::move(workers_.front());
                workers_.pop();
            } else [[unlikely]] {
                w = service::get().checkout_worker_();
            }

            return w;
        }

        // checkin a worker to the thread local cache, falling back to the 
        // process-wide service if necessary
        inline void checkin(std::unique_ptr<worker>&& w) {
            HCE_TRACE_METHOD_ENTER("checkin");
            --worker_active_count_;

            if(workers_.full()) [[unlikely]] {
                service::get().checkin_worker_(std::move(w));
            } else [[likely]] {
                workers_.push(std::move(w));
            }
        }
       
        inline void clear() {
            HCE_TRACE_METHOD_ENTER("clear");

            while(workers_.used()) {
                workers_.pop();
            }
        }
 
    private:
        size_t worker_active_count_;
        hce::circular_buffer<std::unique_ptr<worker>> workers_;
    };

    // awaitable implementation for returning an immediately available value
    template <typename T>
    struct sync : public 
            hce::scheduler::reschedule<
                hce::blocking::detail::sync_partial<T>>
    {
        template <typename... As>
        sync(As&&... as) : 
            hce::scheduler::reschedule<
                hce::blocking::detail::sync_partial<T>>(
                    std::forward<As>(as)...)
        { }

        virtual ~sync() { }
        
        static inline std::string info_name() { 
            return type::templatize<T>("hce::blocking::service::sync"); 
        }

        inline std::string name() const { return sync<T>::info_name(); }
    };

    // awaitable implementation for returning an asynchronously available value
    template <typename T>
    struct async : public 
           scheduler::reschedule<
            hce::blocking::detail::async_partial<T>>
    {
        async() : 
            scheduler::reschedule<
                hce::blocking::detail::async_partial<T>>(),
            // on construction get a worker
            wkr_(tl_worker_cache_().checkout())
        { }

        virtual ~async() {
            // return the worker to its scheduler
            if(wkr_) [[likely]] { 
                tl_worker_cache_().checkin(std::move(wkr_)); 
            }
        }
        
        static inline std::string info_name() { 
            return type::templatize<T>("hce::blocking::service::async"); 
        }

        inline std::string name() const { return async<T>::info_name(); }

        // return the contractor's worker
        inline service::worker& worker() { return *wkr_; }

    private:
        std::unique_ptr<service::worker> wkr_;
    };

    service() :
        worker_active_count_(0),
        workers_(blocking::config::process_worker_resource_limit())
    { 
        HCE_HIGH_CONSTRUCTOR();
    }

    ~service() {
        HCE_HIGH_DESTRUCTOR();
    }
   
    // a thread_local worker cache 
    static cache& tl_worker_cache_();

    // retrieve a worker thread from the service to execute blocking operations on
    inline std::unique_ptr<worker> checkout_worker_() {
        // attempt to pull from the thread_local cache first
        std::unique_ptr<worker> w;

        std::unique_lock<spinlock> lk(lk_);
        ++worker_active_count_; // update checked out thread count
        
        // check if we have any workers in reserve
        if(workers_.empty()) [[unlikely]] {
            lk.unlock();

            // as a fallback generate a new worker thread 
            w.reset(new worker());
            HCE_TRACE_METHOD_BODY("checkout_worker_","allocated ",w.get());
        } else [[likely]] {
            // get the first available worker
            w = std::move(workers_.front());
            workers_.pop();
            lk.unlock();

            HCE_TRACE_METHOD_BODY("checkout_worker_","reused ",w.get());
        }

        return w;
    }

    // return a worker to the service when blocking operation is completed
    inline void checkin_worker_(std::unique_ptr<worker>&& w) {
        std::lock_guard<spinlock> lk(lk_);
        --worker_active_count_; // update checked out thread count
        
        if(workers_.full()) {
            HCE_TRACE_METHOD_BODY("checkin_worker_","discarded ",w.get());
        } else { 
            HCE_TRACE_METHOD_BODY("checkin_worker_","cached ",w.get());
            workers_.push(std::move(w)); // reuse worker
        }
    }

    template <typename Callable, typename... As>
    inline hce::awt<hce::function_return_type<Callable,As...>>
    block_(std::false_type, Callable&& cb, As&&... as) {
        typedef hce::function_return_type<Callable,As...> T;

        if(hce::coroutine::in()) {
            // construct an asynchronous awaitable implementation
            auto ai = new service::async<T>();
            auto& wkr = ai->worker();
            HCE_MIN_METHOD_BODY("block","executing on ",wkr);
            
            hce::thunk* th = new hce::thunk;

            hce::memory::construct_thunk_ptr(
                th,
                [ai,
                 cb=std::forward<Callable>(cb),
                 ... as=std::forward<As>(as)]() mutable -> void {
                    // pass the allocated T to the async and resume it
                    ai->resume(new T(cb(std::forward<As>(as)...)));
                });

            // construct the operation and send to the worker
            wkr.schedule(std::unique_ptr<hce::thunk>(th));

            // return an awaitable to await the result of the blocking call
            return hce::awt<T>(ai);
        } else {
            HCE_MIN_METHOD_BODY("block","executing on current thread");
            
            // we own the thread already, call cb immediately and return the 
            // result
            return hce::awt<T>(new service::sync<T>(
                cb(std::forward<As>(as)...)));
        }
    }

    // void return specialization 
    template <typename Callable, typename... As>
    inline hce::awt<void>
    block_(std::true_type, Callable&& cb, As&&... as) {
        if(hce::coroutine::in()) {
            auto ai = new service::async<void>();
            auto& wkr = ai->worker(); 
            HCE_MIN_METHOD_BODY("block","executing on ",wkr);

            hce::thunk* th = new hce::thunk;

            hce::memory::construct_thunk_ptr(
                th, 
                [ai,
                 cb=std::forward<Callable>(cb),
                 ... as=std::forward<As>(as)]() mutable -> void {
                    cb(std::forward<As>(as)...);
                    ai->resume(nullptr);
                });

            wkr.schedule(std::unique_ptr<hce::thunk>(th));
            return hce::awt<void>(ai);
        } else {
            HCE_MIN_METHOD_BODY("block","executing on current thread");
            cb(std::forward<As>(as)...);
            return hce::awt<void>(new service::sync<void>);
        }
    }

    // synchronize checkin/checkout of workers
    mutable hce::spinlock lk_;

    // count of threads executing blocking operations
    size_t worker_active_count_; 

    /*
     Queue of reusable block workers threads. When a blocking operation 
     finishes, and the thread_local worker cache is full, the worker thread that 
     executed the operation will be placed in this queue if there is room. 
     */
    hce::circular_buffer<std::unique_ptr<worker>> workers_;
};

}

/**
 @brief call a Callable on a thread that is not running a coroutine 
 @param cb a Callable function, function pointer, Functor or lambda
 @param as arguments for the callable
 @return an awaitable for the result of the callable
 */
template <typename Callable, typename... Args>
inline hce::awt<hce::function_return_type<Callable,Args...>> 
block(Callable&& cb, Args&&... args) {
    HCE_MED_FUNCTION_ENTER("hce::block");
    return blocking::service::get().block(
        std::forward<Callable>(cb),
        std::forward<Args>(args)...);
}

}

#endif
