//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_BLOCK__
#define __HERMES_COROUTINE_ENGINE_BLOCK__ 

// local
#include "utility.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {
namespace detail {
namespace blocking {

struct blocker {
    // block workers implicitly start a scheduler on a new thread during 
    // construction and shutdown said scheduler during destruction.
    struct worker {
        std::shared_ptr<hce::scheduler> sch;
        std::thread thd;

        // thread_local value defaults to false
        static bool& tl_is_block();

        worker() : 
            // construct the child scheduler with no worker threads
            sch(hce::scheduler::make()),
            // spawn a worker thread with a running scheduler, redirecting 
            // this_scheduler() to the parent scheduler
            thd(std::thread([](scheduler* sch){ 
                    // thread_local value is true when executing as a block
                    // worker thread
                    worker::tl_is_block() = true;
                    while(sch->run()) { }; 
                }, 
                sch.get()))
        { }

        ~worker() {
            if(sch) {
                sch->halt();
                thd.join();
            }
        }
    };

    template <typename T>
    struct awaitable : public hce::awaitable<T> {
        awaitable(awaitable&& rhs) = default;

        awaitable(hce::awaitable<T>&& a, std::unique_ptr<worker>&& wkr) :
            hce::awaitable<T>(std::move(a)),
            wkr_(std::move(wkr)) 
        { }

        virtual ~awaitable() {
            if(wkr_) { blocker::instance().checkin_worker(std::move(wkr_)); }
        }
        
        awaitable& operator=(awaitable&& rhs) = default;

    private:
        std::unique_ptr<worker> wkr_;
    };

    blocker();

    static blocker& instance();

    inline size_t minimum() const { return min_worker_cnt_; }

    inline size_t count() { 
        std::unique_lock<spinlock> lk(lk_);
        return worker_cnt_; 
    }

    template <typename T, typename Callable, typename... As>
    awaitable<T>
    block(Callable&& cb, As&&... as) {
        auto wkr = checkout_worker();
        return blocker::awaitable<T>(
                wkr->sch->await(hce::to_coroutine(
                    std::forward<Callable>(cb),
                    std::forward<As>(as)...)),
                std::move(wkr));

    }

    inline std::unique_ptr<worker> checkout_worker() {
        std::unique_lock<spinlock> lk(lk_);
        if(workers_.size()) {
            auto w = std::move(workers_.front());
            workers_.pop_front();
            // return the first available worker
            return w;
        }

        // as a fallback generate a new await worker thread
        ++worker_cnt_;
        return std::unique_ptr<worker>(new worker);
    }

    inline void checkin_worker(std::unique_ptr<worker>&& w) {
        std::unique_lock<spinlock> lk(lk_);
        if(workers_.size() < min_worker_cnt_) {
            workers_.push_back(std::move(w));
        } else { --worker_cnt_; }
    }

private:
    spinlock lk_;

    // minimum block() worker thread count
    const size_t min_worker_cnt_; 

    // current block() worker thread count
    size_t worker_cnt_; 

    // worker memory
    std::deque<std::unique_ptr<worker>> workers_;
};

}
}

struct blocking {
    /**
     @brief execute a Callable on a dedicated thread 

     This mechanism allows execution of arbitrary code on a dedicated thread, 
     and `co_await` the result (or if in a standard thread, just cast the 
     awaitable to get the result).

     This allows for executing blocking code (which would be unsafe to do in a 
     coroutine!) via a mechanism which is safely callable *from* a coroutine.

     The given callable can access values owned by the coroutine/thread by 
     reference, because the caller of wait() will be blocked while the Callable 
     is executed.

     This operation will succeed even if the scheduler is halted.

     @param cb a function, Functor, or lambda 
     @param as arguments to cb
     @return an awaitable returning the result of the Callable 
     */
    template <typename Callable, typename... As>
    static auto
    call(Callable&& cb, As&&... as) {
        typedef hce::detail::function_return_type<Callable,As...> RETURN_TYPE;

        return detail::blocking::blocker::instance().block<RETURN_TYPE>(
            std::forward<Callable>(cb),
            std::forward<As>(as)...);
    }

    /// return the current count of wait() managed threads associated with this thread
    static inline size_t count() {
        return detail::blocking::blocker::instance().count();
    }

    /// return the minimum count of wait() managed threads
    static inline size_t minimum() {
        return detail::blocking::blocker::instance().minimum();
    }
};

}

#endif
