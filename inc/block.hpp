//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_BLOCK__
#define __HERMES_COROUTINE_ENGINE_BLOCK__ 

// c++
#include <condition_variable>
#include <deque>

// local
#include "utility.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {
namespace detail {
namespace blocking {

template <typename T>
inline hce::co<T> wrapper(std::function<T()> f) {
    auto& tl_co = hce::detail::coroutine::tl_this_coroutine();
    auto& tl_sch = hce::detail::scheduler::tl_this_scheduler();
    auto parent_co = tl_co;
    auto parent_sch = tl_sch;
    T result;

    try {
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
using awt_interface = hce::awt<T>::interface;

template <typename T>
struct done : public 
    hce::scheduler::reschedule<
        hce::awaitable::lockable<
            awt_interface<T>,
            hce::lockfree>>
{
    template <typename... As>
    done(As&&... as) : 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                awt_interface<T>,
                hce::lockfree>>(
                    lf_,
                    hce::awaitable::await::policy::adopt,
                    hce::awaitable::resume::policy::no_lock),
        t_(std::forward<As>(as)...) { }

    inline bool on_ready() { return true; }
    inline void on_resume(void* m) { }
    inline T get_result() { return std::move(t_); }

private:
    hce::lockfree lf_;
    T t_;
};

template <>
struct done<void> : public 
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                awt_interface<void>,
                hce::lockfree>>
{
    done() :
        hce::scheduler::reschedule<
            hce::awaitable::lockable<
                awt_interface<void>,
                hce::lockfree>>(
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

struct blocking {
    /**
     @brief execute a Callable on a dedicated thread

     This mechanism allows execution of arbitrary code on a dedicated thread, 
     and and to `co_await` the result of the `hce::blocking::call()` (or if not 
     called from a coroutine, just assign the awaitable to a variable of the 
     resulting type). IE in a coroutine:
     ```
     T result = co_await hce::blocking::call(my_function_returning_T);
     ```

     Outside a coroutine:
     ```
     T result = hce::blocking::call(my_function_returning_T);
     ```

     This allows for executing blocking code (which would be unsafe to do in a 
     coroutine!) via a mechanism which is safely callable *from within* a 
     coroutine.

     The user Callable will execute on a dedicated thread, and won't have direct 
     access to the caller's local scheduler or coroutine (if any).

     If the caller is already executing in a thread managed by another 
     `hce::blocking::call()`, or if called outside of an `hce` coroutine, the 
     Callable will be executed immediately on the current thread.

     The given Callable can access values owned by the coroutine's body (or 
     values on a thread's stack if called outside of a coroutine) by reference, 
     because the caller of `call()` will be blocked while the Callable is 
     executed on the other thread.

     WARNING: It is highly recommended to immediately `co_await` the awaitable 
     returned by `hce::blocking::call()`. If the executing Callable accesses the 
     stack of the caller in an unsynchronized way, and the caller of 
     `blocking::call()` is a coroutine, then the caller should `co_await` the 
     returned awaitable before reading or writing accessed values again 
     (preferrably *immediately*). Doing this ensures the coroutine is safely 
     blocked until `hce::blocking::call()` completes.

     @param cb a function, Functor, or lambda 
     @param as arguments to cb
     @return an awaitable returning the result of the Callable 
     */
    template <typename Callable, typename... As>
    static auto
    call(Callable&& cb, As&&... as) {
        typedef hce::detail::function_return_type<Callable,As...> RETURN_TYPE;

        HCE_TRACE_FUNCTION_ENTER(
            "hce::blocking::call",
            hce::detail::utility::callable_to_string(cb));
        return blocking::instance().block_<RETURN_TYPE>(
            std::forward<Callable>(cb),
            std::forward<As>(as)...);
    }

    /// return the current count of managed blocking threads
    static inline size_t count() {
        auto c = blocking::instance().count_();
        HCE_TRACE_FUNCTION_BODY("hce::blocking::count",c);
        return c;
    }

    /// return the minimum count of managed blocking threads
    static inline size_t minimum() {
        auto m = blocking::instance().minimum_();
        HCE_TRACE_FUNCTION_BODY("hce::blocking::minimum",m);
        return m;
    }

private:
    // block workers implicitly start a scheduler on a new thread during 
    // construction and shutdown said scheduler during destruction.
    struct worker {
        worker() : sch_([&]() -> std::shared_ptr<hce::scheduler> {
            auto sch = hce::scheduler::make(lf_);
            std::thread([=]() mutable { sch->install(); }).detach();
            return sch;
        }())
        { }

        // thread_local value defaults to false
        static bool& tl_is_block();

        hce::scheduler& scheduler() { return *sch_; }

    private: 
        std::unique_ptr<hce::scheduler::lifecycle> lf_;
        std::shared_ptr<hce::scheduler> sch_;
    };

    // contractors are transient managers of block worker threads, ensuring 
    // they are checked out and back in at the proper times
    struct contractor {
        contractor() : 
            // on construction get a worker
            wkr_(blocking::instance().checkout_worker_()) 
        { }

        ~contractor() { 
            // on destruction return the worker
            if(wkr_){ blocking::instance().checkin_worker_(std::move(wkr_)); } 
        }

        // allow conversion to a coroutine's cleanup handler thunk
        inline void operator()(){}

        // return the worker's scheduler 
        inline hce::scheduler& scheduler() { return wkr_->scheduler(); }

    private:
        std::unique_ptr<worker> wkr_;
    };

    blocking();

    static blocking& instance();

    inline size_t minimum_() const { return min_worker_cnt_; }

    inline size_t count_() { 
        std::unique_lock<spinlock> lk(lk_);
        return worker_cnt_; 
    }

    /*
     Embed a Callable in a coroutine. The Callable will not have access to the 
     running coroutine or scheduler.
     */
    template <typename Callable, typename... As>
    auto 
    to_coroutine_(Callable&& cb, As&&... as) {
        return detail::blocking::wrapper(
            std::bind(
                std::forward<Callable>(cb),
                std::forward<As>(as)...));
    }

    template <typename T, typename Callable, typename... As>
    hce::awt<T>
    block_(Callable&& cb, As&&... as) {
        if(!scheduler::in() || worker::tl_is_block()) {
            /// we own the thread, call cb immediately and return the result
            return hce::awt<T>::make(
                new detail::blocking::done<T>(cb(std::forward<As>(as)...)));
        } else {
            contractor cr; // get a contractor to do the work of managing threads
            auto& sch = cr.scheduler(); // acquire the contractor's scheduler

            // acquire the calling thread's scheduler redirect pointer, if any
            auto tl_cs_re = 
                hce::detail::scheduler::tl_this_scheduler_redirect();

            // Get the proper scheduler redirect pointer. Coroutines executing 
            // on the managed thread should not be able to directly access the 
            // actual scheduler they are running on via 
            // `hce::scheduler::local()`, instead getting access to the 
            // scheduler they came from.
            scheduler* tl_cs_source = tl_cs_re 
                ? tl_cs_re 
                : &(hce::scheduler::global());

            // convert our Callable to a coroutine
            hce::co<T> co = blocking::to_coroutine_(
                [tl_cs_source](Callable cb, As&&... as) -> T {
                    // set the worker thread's scheduler redirect pointer to the 
                    // proper scheduler before executing the Callable
                    hce::detail::scheduler::tl_this_scheduler_redirect() = 
                        tl_cs_source;
                    return cb(std::forward<As>(as)...);
                }, 
                std::forward<Callable>(cb), 
                std::forward<As>(as)...);

            // install the contractor as a coroutine cleanup handler to check in 
            // the worker when the coroutine goes out of scope
            co.promise().install(std::move(cr));

            // schedule the coroutine on the contractor's worker thread
            return sch.join(std::move(co));
        }
    }

    template <typename Callable, typename... As>
    hce::awt<void>
    block_(Callable&& cb, As&&... as) {
        if(!scheduler::in() || worker::tl_is_block()) {
            cb(std::forward<As>(as)...);
            return hce::awt<void>::make(
                new detail::blocking::done<void>());
        } else {
            contractor cr; 
            auto& sch = cr.scheduler(); 

            auto tl_cs_re = 
                hce::detail::scheduler::tl_this_scheduler_redirect();

            scheduler* tl_cs_source = tl_cs_re 
                ? tl_cs_re 
                : &(hce::scheduler::global());

            hce::co<void> co = hce::to_coroutine(
                [tl_cs_source](Callable cb, As&&... as) -> void {
                    hce::detail::scheduler::tl_this_scheduler_redirect() = 
                        tl_cs_source;
                    cb(std::forward<As>(as)...);
                }, 
                std::forward<Callable>(cb), 
                std::forward<As>(as)...);

            co.promise().install(std::move(cr));

            return sch.join(std::move(co));
        }
    }

    inline std::unique_ptr<worker> checkout_worker_() {
        std::unique_ptr<worker> w;

        std::unique_lock<spinlock> lk(lk_);
        if(workers_.size()) {
            // get the first available worker
            w = std::move(workers_.front());
            workers_.pop_front();
            lk.unlock();
        } else {
            // as a fallback generate a new await worker thread
            ++worker_cnt_;
            w = std::unique_ptr<worker>(new worker);
        }

        return w;
    }

    inline void checkin_worker_(std::unique_ptr<worker>&& w) {
        std::unique_lock<spinlock> lk(lk_);
        if(workers_.size() < min_worker_cnt_) {
            workers_.push_back(std::move(w));
        } else { --worker_cnt_; }
    }

    spinlock lk_;

    // minimum block() worker thread count
    const size_t min_worker_cnt_; 

    // current block() worker thread count
    size_t worker_cnt_; 

    // worker memory
    std::deque<std::unique_ptr<worker>> workers_;
};

}

#endif
