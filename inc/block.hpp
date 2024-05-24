//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_BLOCK__
#define __HERMES_COROUTINE_ENGINE_BLOCK__ 

#include "utility.hpp"
#include "coroutine.hpp"

namespace hce {

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

     Tgit@github.com:Elektrobit/mercuryconcurrencyengine.githis operation will succeed even if the scheduler is halted.

     @param cb a function, Functor, or lambda 
     @param as arguments to cb
     @return an awaitable returning the result of the Callable 
     */
    template <typename Callable, typename... As>
    awaitable<detail::function_return_type<Callable,As...>> 
    call(Callable&& cb, As&&... as) {
        std::unique_lock<spinlock> lk(lk_);

        return tl_this_blocker().block<detail::function_return_type<Callable,As...>>(
            std::move(lk),
            std::forward<Callable>(cb),
            std::forward<As>(args)...);
    }

    /// return the current count of wait() managed threads associated with this thread
    static inline size_t count() {
        return tl_this_blocker().count();
    }

    /// return the minimum count of wait() managed threads
    static inline size_t minimum() {
        return tl_this_blocker().minimum();
    }

private:
    // block workers implicitly start a scheduler on a new thread during 
    // construction and shutdown said scheduler during destruction.
    struct worker {
        std::shared_ptr<scheduler> sch;
        std::thread thd;

        // thread_local value defaults to false
        static bool& tl_is_wait();

        worker(scheduler* redirect) : 
            // construct the child scheduler with no worker threads
            sch(scheduler::make(0)),
            // spawn a worker thread with a running scheduler, redirecting 
            // this_scheduler() to the parent scheduler
            thd(std::thread([](scheduler* sch, scheduler* redirect){ 
                    // thread_local value is true when executing as a block
                    // worker thread
                    worker::tl_is_wait() = true;
                    while(sch->run(redirect)) { }; 
                }, 
                sch.get(),
                redirect))
        { }

        ~worker() {
            if(sch) {
                sch->halt();
                thd.join();
            }
        }
    };

    struct blocker {
        blocker();

        inline size_t minimum() const { return min_worker_cnt_; }
        inline size_t count() { 
            std::unique_lock<spinlock> lk(lk_);
            return worker_cnt_; 
        }

        template <typename T, typename Callable, typename... As>
        awaitable<T>
        block(Callable&& cb, 
              As&&... args) {
            struct awt : protected awaitable<T>::implementation {
                template <typename Callable, typename... As>
                awt(std::unique_lock<spinlock>& lk,
                    blocker* blkr,
                    Callable&& cb,
                    As&&... as) :
                    lk_(std::move(lk)),
                    blkr_(blkr),
                    f_(std::bind(std::forward<Callable>(cb),
                                 std::forward<As>(as)...))
                { }

            protected:
                inline std::unique_lock<spinlock>& get_lock() { return lk_; }
                
                inline bool ready_impl() { 
                    if(in_scheduler() && !detail::worker::tl_is_wait()) {
                        wkr_ = blkr_->checkout_worker_();
                        wkr_->sch->schedule(awt::op(this, std::move(f_)));
                        return false; 
                    } else {
                        // execute Callable directly if not in a coroutine running 
                        // in a scheduler OR if we are already executing in a parent 
                        // block() call. No need to worry about blocking other 
                        // running coroutines... we already have a dedicated system 
                        // thread. Additionally, any further coroutine schedule()/
                        // await() operations will be redirected to the parent 
                        // scheduler, not the child scheduler.
                        t_ = f_();
                        return true;
                    }
                }

                inline void resume_impl(void* m) { 
                    if(m) { t_ = std::move(*((T*)m)); }
                    if(wkr_) { blkr_->checkin_worker_(wkr_); }
                }

                inline T result_impl() { return std::move(t_); }

            private:
                template <typename Callable, typename... As>
                static inline coroutine op(awt* a, std::function<T()> f) {
                    T t = f();
                    a->resume(&t);
                    co_return;
                }

                std::unique_lock<spinlock> lk_;
                blocker* blkr_;
                std::function<T()> f_;
                T t_;
                std::unique_ptr<worker> wkr_;
            };
                
            std::unique_lock<spinlock> lk(lk_);

            return awaitable<T>::make<awt>(
                    std::move(lk),
                    this,
                    std::forward<Callable>(cb), 
                    std::forward<As>(as)...);
        }

        template <typename Callable, typename... As>
        awaitable<void>
        block<void>(Callable&& cb, 
                     As&&... args) {
            struct awt : protected awaitable<void>::implementation {
                template <typename Callable, typename... As>
                awt(std::unique_lock<spinlock>& lk,
                    blocker* blkr,
                    Callable&& cb,
                    As&&... as) :
                    lk_(std::move(lk)),
                    blkr_(blkr),
                    f_(std::bind(std::forward<Callable>(cb),
                                 std::forward<As>(as)...))
                { }

            protected:
                inline std::unique_lock<spinlock>& get_lock() { return lk_; }

                inline bool ready_impl() { 
                    if(in_scheduler() && !detail::worker::tl_is_wait()) {
                        wkr_ = blkr_->checkout_worker_();
                        wkr_->sch->schedule(awt::op(this, std::move(f_)));
                        return false; 
                    } else {
                        f_();
                        return true;
                    }
                }

                inline void resume_impl(void* m) { 
                    if(wkr_) { 
                        blkr_->checkin_worker_(wkr_); 
                        wkr_ = nullptr;
                    }
                }

            private:
                template <typename Callable, typename... As>
                static inline coroutine op(awt* a, std::function<void()> f) {
                    f();
                    a->resume(nullptr);
                    co_return;
                }

                std::unique_lock<spinlock> lk_;
                blocker* blkr_;
                std::function<void()> f_;
                std::unique_ptr<worker> wkr_;
            };

            std::unique_lock<spinlock> lk(lk_);

            return awaitable<void>::make<awt>(
                    std::move(lk),
                    this,
                    std::forward<Callable>(cb), 
                    std::forward<As>(as)...);
        }

    private:
        inline void checkin_worker_(std::unique_ptr<worker>&& w) {
            if(workers_.size() < min_worker_cnt_) {
                workers_.push_back(std::move(w));
            } else { --worker_cnt_; }
        }

        inline std::unique_ptr<worker> checkout_worker_() {
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

        spinlock lk_;

        // minimum block() worker thread count
        const size_t min_worker_cnt_; 

        // current block() worker thread count
        size_t worker_cnt_; 

        // worker memory
        std::deque<std::unique_ptr<worker>> workers_;
    };

    static blocker& instance();
};

}

#endif
