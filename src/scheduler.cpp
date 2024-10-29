//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <string>
#include <thread>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "utility.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "slab_allocator.hpp"
#include "channel.hpp"

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler::lifecycle::manager& hce::scheduler::lifecycle::manager::instance() {  
    static hce::scheduler::lifecycle::manager m;
    return m;
}

struct lifecycle_manager_await {
    /*
     This mechanism wraps an allocated interface in an allocated key so we can 
     ensure the key pointers are contiguous, improving hash collisions.
     */
    struct key_ { hce::awaitable::interface* interface; };

    typedef key_* key;

    lifecycle_manager_await() : 
        size(hce::scheduler::global().coroutine_pool_limit()),
        // allocate all memory in the allocator in a single contiguous block by 
        // pre-caching to the maximum size
        allocator_(size, hce::pre_cache(size)) 
    {
        // allocate a channel of unlimited size
        channel_.context(std::shared_ptr<hce::channel<key>::interface>(
            static_cast<hce::channel<key>::interface*>(
                new hce::unlimited<key,hce::spinlock>(size))));
    };

    inline key register_await(hce::awaitable::interface* i) {
        key k;

        {
            std::lock_guard<hce::spinlock> lk(lk_);
            k = allocator_.allocate(1);
        }

        new(k) key_(i);

        // non-coroutine send will not block on unlimited channel
        channel_.send(k);
        return k;
    }

    inline void done_await(key k) { channel_.send(k); }

    inline void return_key(key k) {
        k->~key_();

        {
            std::lock_guard<hce::spinlock> lk(lk_);
            allocator_.deallocate(k,1);
        }
    }

    inline hce::awt<bool> recv(key& k) { return channel_.recv(k); }

    inline void shutdown() {
        // close the awaiter channel
        channel_.close();
    }

    const size_t size;

private:
    hce::spinlock lk_;
    hce::slab_allocator<key_> allocator_; 
    hce::channel<key> channel_;
};

// a global channel of unlimited size for awaitable interfaces
lifecycle_manager_await g_lifecycle_manager_await;


size_t hce::scheduler::lifecycle::manager::register_await(hce::awaitable::interface* i) {
    return reinterpret_cast<size_t>(g_lifecycle_manager_await.register_await(i));
}

void hce::scheduler::lifecycle::manager::done_await(size_t key) {
    g_lifecycle_manager_await.done_await(
        reinterpret_cast<lifecycle_manager_await::key>(key));
}

/*
 The coroutine responsible for awaiting any awaitable passed to 
 hce::scheduler::lifecycle::manager::register_await(). The expected usecase is 
 any awaitable that has `hce::awaitable::detach()` called.
 */
hce::co<void> hce::scheduler::lifecycle::manager::root_await_op_() {
    typedef lifecycle_manager_await::key key;

    // readability reference
    auto& glma = g_lifecycle_manager_await;

    /*
     Fast insert and erasure of keys is critical, order not important.

     The default bucket count is the global scheduler's coroutine pool limit.
     */
    std::unordered_set<
        key,
        std::hash<key>, // default
        std::equal_to<key>, // default
        hce::slab_allocator<key>> // reusable allocations
    registered_keys(glma.size, hce::slab_allocator<key>(glma.size));

    key k;

    // continuously receive updates for registered and done awaitable interfaces
    while(co_await glma.recv(k)) {
        auto it = registered_keys.find(k);

        if(it == registered_keys.end()) {
            // register key containing awaitable
            registered_keys.insert(k);
        } else {
            // handle done awaitable
            registered_keys.erase(it);
            hce::awaitable::interface* interface = k->interface;
            glma.return_key(k);
            co_await hce::awt<void>::make(interface);
        }
    }

    // co_await all remaining interfaces
    for(auto k : registered_keys) {
        hce::awaitable::interface* interface = k->interface;
        glma.return_key(k);
        co_await hce::awt<void>::make(interface); 
    }

    // return allowing process exit
    co_return;
}

void hce::scheduler::lifecycle::manager::shutdown_root_await_op_() {
    g_lifecycle_manager_await.shutdown();

    // join with the root await receiver operation 
    root_await_.wait();
}

bool& hce::scheduler::blocking::worker::tl_is_worker() {
    thread_local bool is_wkr = false;
    return is_wkr;
}

hce::scheduler& hce::scheduler::global_() {
    static std::shared_ptr<hce::scheduler> sch(
        []() -> std::shared_ptr<hce::scheduler> {
            std::unique_ptr<hce::scheduler::lifecycle> lf = 
                hce::scheduler::make(hce::config::global::scheduler_config());
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();

            hce::scheduler::lifecycle::manager::instance().registration(
                std::move(lf));

            return sch;
        }());
    return *sch;
}
