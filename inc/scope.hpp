//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_SCOPE__
#define __HERMES_COROUTINE_ENGINE_SCOPE__ 

#include <coroutine>
#include <unordered_set>

#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "atomic.hpp"
#include "pool_allocator.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

namespace hce {

/**
 @brief awaitable synchronization object capable of awaiting zero or more awaitables 

 This object can store and await an arbitrary count of awaitables of *different* 
 templated types `T`. The return types of all coroutines are ignored. Call 
 `await()` to return the root awaitable which blocks until all scoped awaitables 
 complete:
 ```
 co_await hce::scope(hce::schedule(co1), hce::schedule(co2)).await();
 ```

 Additional awaitables can be `hce::scope::add()`ed after construction. `add()`
 accepts one or more awaitables.
 ```
 hce::scope s;
 s.add(hce::schedule(co1), hce::schedule(co2));
 s.add(hce::schedule(co3));
 co_await s.await();
 ```

 No additional coroutines can be `add()`ed after `await()` is called.
 */
template <typename Lock=hce::spinlock, typename Allocator=hce::pool_allocator<T>>
struct scope {
    /**
     @brief construct scope with one or more awaitables add()ed to it 
     @param one or more awaitables 
     */
    template <typename... As>
    scope(AAs&&... as) :
        // unlimited channel never blocks on send
        root_ch_(hce::channel<hce::awaitable::interface*>::make<Lock,Allocator>(-1)),
        root_awt_(hce::schedule(root_awaiter_(root_ch_)))
    {
        // add all the constructed awaitables to the scope
        add_(std::forward<As>(as)...);

        HCE_MED_CONSTRUCTOR();
    }

    ~scope() {
        HCE_MED_DESTRUCTOR();
        root_ch_.close(); // sanity enable root awaiter to end 
        // root_awt_ will block the current thread and wait() if await() not called
    }

    static inline std::string info_name() { return "hce::scope"; }
    inline std::string name() const { return scope::info_name(); }

    /**
     @brief add one or more awaitables to the scope 

     This operation will fail if `await()` has already been called.

     @param a the first awaitable
     @param as any remaining awaitables
     @return `true` if successfully added, else `false`
     */
    template <typename A, typename... As >
    inline bool add(A&& a, As&&... as) {
        HCE_MED_METHOD_ENTER("add", a, as...);
        
        if(root_ch_.closed()) {
            return false;
        } else {
            add_(std::forward<A>(a), std::forward<As>(as)...);
            return true;
        }
    }

    /// return `true` if the scope can be await()ed, else `false`
    inline operator bool() {
        return root_awt_.valid();
    }

    /// return awaiter of scoped awaitables
    inline hce::awt<void> await() {
        HCE_MED_METHOD_ENTER("await");
        root_ch_.close();
        return std::move(root_awt_);
    }

private:
    template <typename A, typename... As >
    inline scope& add_(A&& a, As&&... as) {
        // add an awaitable 
        root_ch_.send(a.release());

        // add more awaitables
        add_(std::forward<As>(as)...);
    }

    inline scope& add_() { return *this; }

    // a root awaiter coroutine which awaits all the launched awaiter coroutines
    static inline hce::co<void> root_awaiter_(
            hce::channel<hce::awaitable::interface*>> awaiters)
    {
        for(auto interface : awaiters) {
            co_await hce::awt<void>::make(interface);
        }

        co_return;
    }

    // communication with root awaiter coroutine
    hce::channel<hce::awaitable::interface*> root_ch_;

    // root awaitable
    hce::awt<void> root_awt_;
};

}

#endif
