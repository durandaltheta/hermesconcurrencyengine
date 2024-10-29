//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SCOPE__
#define __HERMES_COROUTINE_ENGINE_SCOPE__ 

#include <coroutine>

#include "logging.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

/**
 @brief synchronization object capable of blocking until zero or more awaitables are joined

 This object is designed to collect any number of `hce::awt<T>`s and join with 
 them all:
 ```
 co_await hce::scope{ hce::schedule(co1), hce::schedule(co2) }.join();
 ```

 This object can store awaitables of *different* templated types `T`. The return 
 types of all coroutines are ignored when joined.
 */
struct scope : public printable {
    /**
     @brief construct scope with zero or more awaitables add()ed to it 
     @param one or more awaitables 
     */
    template <typename... As >
    scope(As&&... as) { 
        add(std::forward<As>(as)...);
    }

    scope(const scope&) = delete;
    scope(scope&&) = default;

    ~scope() {
        if(dq_) [[unlikely]] {
            // Block the current thread and join. Throws standard exception if 
            // called without `co_await` in a coroutine.
            join(); 
        }
    }

    static inline std::string info_name() { return "hce::scope"; }
    inline std::string name() const { return scope::info_name(); }

    scope& operator=(const scope&) = delete;
    scope& operator=(scope&&) = default;

    /**
     @return true if the scope is joinable(), else false
     */
    inline operator bool() { return joinable(); }

    /**
     @brief add zero awaitables to the scope 
     @return a reference to this scope
     */
    inline scope& add() { return *this; }

    /**
     @brief add one or more awaitables to the scope 
     @param one or more awaitables 
     @return a reference to this scope
     */
    template <typename A, typename... As >
    inline scope& add(A&& a, As&&... as) {
        if(!dq_) [[likely]] { dq_.reset(new std::deque<hce::awt<void>>); }
        assemble_scope_(*dq, std::forward<A>(a), std::forward<As>(as)...);
        return *this;
    }
   
    /**
     @return true if the scope can be join()ed, else false
     */
    inline bool joinable() { return (bool)dq_; }

    /**
     @return an awaitable to join with all add()ed awaitables
     */
    inline hce::awt<void> join() {
        return hce::awt<void>::make(new scoper(std::move(dq_)));
    }

private:
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

        static inline std::string info_name() { return "scoper"; }
        inline std::string name() const { return scoper::info_name(); }

        inline bool on_ready() { 
            scheduler::get().schedule(scoper::op(this, awts_.get()));
            return false;
        }

        inline void on_resume(void* m) { }

        static inline co<void> op(
            scoper* sa, 
            std::deque<hce::awt<void>>* awts) 
        {
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

    template <typename Container>
    inline void assemble_join_(std::true_type, std::deque<hce::awt<void>>& dq, Container& c) {
        for(auto& elem : c) {
            dq.push_back(hce::awt<void>::make(join_(elem).release()));
        }
    }

    template <typename Coroutine>
    inline void assemble_join_(std::false_type, std::deque<hce::awt<void>>& dq, Coroutine& c) {
        dq.push_back(hce::awt<void>::make(join_(c).release()));
    }

    static inline void assemble_scope_(std::deque<hce::awt<void>>& dq) { }

    template <typename A, typename... As>
    inline void assemble_scope_(
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

    std::unique_ptr<std::deque<hce::awt<void>>> dq_;
};

}

#endif
