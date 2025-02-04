//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_SCOPE
#define HERMES_COROUTINE_ENGINE_SCOPE

#include <coroutine>
#include <unordered_set>

#include "utility.hpp"
#include "logging.hpp"
#include "memory.hpp"
#include "atomic.hpp"
#include "alloc.hpp"
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
template <typename Lock=hce::spinlock, typename Allocator=hce::pool_allocator<size_t>>
struct scope : public hce::printable {
    /**
     @brief construct scope with one or more awaitables add()ed to it 
     @param one or more awaitables 
     */
    template <typename... Awts>
    scope(Awts&&... awts) :
        // unlimited channel never blocks on send
        root_ch_(hce::make_unique<channel_t>()),
        root_awt_(hce::schedule(root_awaiter_(root_ch_.get())))
    {
        // add all the constructed awaitables to the scope
        add_(std::forward<Awts>(awts)...);

        HCE_MED_CONSTRUCTOR();
    }

    scope(scope&& rhs) :
        root_ch_(std::move(rhs.root_ch_)),
        root_awt_(std::move(rhs.root_awt_))
    {
        HCE_MED_CONSTRUCTOR(rhs);
    }

    virtual ~scope() {
        HCE_MED_DESTRUCTOR();

        if(root_ch_ && !(root_ch_->closed())) [[likely]] {
            root_ch_->close(); // sanity enable root awaiter to end  
        }

        // root_awt_'s destructor will block the current thread and wait() if 
        // await() not called
    }

    inline scope& operator=(scope&& rhs) {
        HCE_MED_METHOD_ENTER("operator=",rhs);
        root_ch_ = std::move(rhs.root_ch_);
        root_awt_ = std::move(rhs.root_awt_);
        return *this;
    }

    static inline std::string info_name() { return "hce::scope"; }
    inline std::string name() const { return scope::info_name(); }

    /**
     @brief add one or more awaitables to the scope 

     This operation will fail if `await()` has already been called.

     @param awt the first awaitable
     @param awts any remaining awaitables
     @return `true` if successfully added, else `false`
     */
    template <typename Awt, typename... Awts >
    inline bool add(Awt&& awt, Awts&&... awts) {
        HCE_MED_METHOD_ENTER("add", awt, awts...);
       
        if(root_ch_ && !(root_ch_->closed())) {
            add_(std::forward<Awt>(awt), std::forward<Awts>(awts)...);
            return true;
        } else {
            return false;
        }
    }

    /// return `true` if the scope can be await()ed, else `false`
    inline bool awaitable() const {
        return root_awt_.valid();
    }

    /// return awaiter of scoped awaitables
    inline hce::awt<void> await() {
        HCE_MED_METHOD_ENTER("await");

        if(root_ch_ && !(root_ch_->closed())) [[likely]] {
            root_ch_->close();
        }

        return std::move(root_awt_);
    }

private:
    typedef typename Allocator::template rebind<hce::awaitable::interface*>::other alloc_t;
    typedef hce::channel::unlimited<hce::awaitable::interface*,Lock,alloc_t> channel_t;

    template <typename T, typename... Awts >
    inline void add_(hce::awt<T> awt, Awts&&... awts) {
        // add an awaitable 
        root_ch_->send(awt.release());

        // add more awaitables
        add_(std::forward<Awts>(awts)...);
    }

    inline void add_() { }

    // a root awaiter coroutine which awaits all the awaitables
    static inline hce::co<void> root_awaiter_(channel_t* awaiters) {
        hce::awaitable::interface* i = nullptr;

        while(co_await awaiters->recv(i)) {
            // join with the awaitable
            co_await hce::awt<void>(i);
        }

        co_return;
    }

    // Communication with root awaiter coroutine. A unique_ptr so the scope 
    // object can be moveable.
    hce::unique_ptr<channel_t> root_ch_;

    // root awaitable
    hce::awt<void> root_awt_;
};

}

#endif
