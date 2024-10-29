//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <memory>
#include <thread>
#include <mutex>
#include <unordered_set>

#include "utility.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "slab_allocator.hpp"
#include "transfer.hpp"
#include "channel.hpp"
#include "lifecycle.hpp"

/*
 This object's purpose is to enable process-wide synchronization of arbitrary 
 awaitables that ensures everything is awaited *BEFORE* schedulers are shutdown 
 and destructed.
 */
struct lifecycle_root {
    /*
     This mechanism wraps an allocated awaitable interface in an allocated key 
     so we can ensure the key pointers are contiguous, improving hash 
     collisions.
     */
    struct key_ { hce::awaitable::interface* interface; };

    typedef key_* key;

    lifecycle_root() : 
        allocator_limit(hce::scheduler::global().coroutine_resource_limit()),
        // Pre-allocate all memory in the allocator in a single contiguous block 
        // by pre-caching to the maximum size. This should help limit hash 
        // collisions on allocated keys.
        allocator_(allocator_limit, hce::pre_cache(allocator_limit))
    {
        // allocate a channel of unlimited size
        channel_.context(std::shared_ptr<hce::channel<key>::interface>(
            static_cast<hce::channel<key>::interface*>(
                new hce::unlimited<key,hce::spinlock>(allocator_limit))));

        // ensure the global scheduler and threadpool are constructed, started 
        // and that each hce::scheduler::lifecycle is indirectly stashed in 
        // with register_opaque_pointer() by the 
        // hce::scheduler::lifecycle::manager
        hce::scheduler::global();
        hce::threadpool::get();

        // ensure this happens after constructor list because this may actually 
        // spawn and register the global scheduler with the manager
        root_awaiter_launcher_thd_ = 
            std::thread(root_awaiter_launcher_func_, &root_awaiter_transfer_);
    };

    ~lifecycle_root() {
        // close the awaiter channel
        channel_.close();

        // get the root awaitable and join with the root await receiver 
        // operation as it goes out scope
        root_awaiter_transfer_.recv();

        // join the one-shot launcher thread 
        root_awaiter_launcher_thd_.join();

        // At this point, there should be no outstanding awaitables unless the 
        // user has failed to properly await operations they've launched before 
        // main() returns.
    }

    void register_opaque_pointer(hce::opaque_pointer&& p) {
        // this function can have it's own lock as it is the only api which
        // modifies lifecycle_pointers_
        static hce::spinlock slk;

        std::lock_guard<hce::spinlock> lk(slk);
        lifecycle_pointers_.push_back(std::move(p));
    }

    key register_awaitable(hce::awaitable::interface* i) {
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

    void awaitable_done(key k) { channel_.send(k); }

    void return_key(key k) {
        k->~key_();

        {
            std::lock_guard<hce::spinlock> lk(lk_);
            allocator_.deallocate(k,1);
        }
    }

    inline hce::awt<bool> recv(key& k) { return channel_.recv(k); }

    const size_t allocator_limit;

private:
    static hce::co<void> root_awaiter_op_();

    static void root_awaiter_launcher_func_(
            hce::transfer<hce::awt<void>>* transfer) 
    {
        // transfer the root awaitable back to the lifecycle_root
        transfer->send(
            std::unique_ptr<hce::awt<void>>(
                new hce::awt<void>(
                    // launch the root awaiter operation
                    hce::scheduler::global().schedule(root_awaiter_op_()))));
    }
    
    hce::spinlock lk_;

    // memory stashed until end of process, should go out of scope after 
    // everything else so it is defined first (destructors are first-in,
    // last-out)
    hce::queue<hce::opaque_pointer> lifecycle_pointers_;

    // thread to schedule the root await operation
    std::thread root_awaiter_launcher_thd_;

    // mechanism to transfer the root awaitable 
    hce::transfer<hce::awt<void>> root_awaiter_transfer_;

    // slab allocator allows for contiguous memory and fewer hash collisions
    hce::slab_allocator<key_> allocator_; 

    // transfer allocated hash keys to the root awaiter operation
    hce::channel<key> channel_;
};

// a global channel of unlimited size for awaitable interfaces
lifecycle_root g_lifecycle_root;

void hce::lifecycle::register_opaque_pointer(hce::opaque_pointer&& p) {
    g_lifecycle_root.register_opaque_pointer(std::move(p));
}

size_t hce::lifecycle::register_awaitable(hce::awaitable::interface* i) {
    return reinterpret_cast<size_t>(g_lifecycle_root.register_awaitable(i));
}

void hce::lifecycle::awaitable_done(size_t key) {
    g_lifecycle_root.awaitable_done(reinterpret_cast<lifecycle_root::key>(key));
}

/*
 The coroutine responsible for awaiting any awaitable passed to 
 hce::scheduler::lifecycle::manager::register_awaitable(). The expected usecase 
 is any awaitable that has `hce::awa<T>::detach()` called.
 */
hce::co<void> lifecycle_root::root_awaiter_op_() {
    typedef lifecycle_root::key key;

    // readability reference
    auto& glr = g_lifecycle_root;

    /*
     Fast insert and erasure of keys is critical, order not important. The 
     default bucket count is the global scheduler's coroutine resource limit.
     */
    std::unordered_set<
        key,
        std::hash<key>, // default
        std::equal_to<key>, // default
        hce::pool_allocator<key>> // will be rebound for reusable node allocations 
    registered_keys(
        glr.allocator_limit, // initial bucket count
        std::hash<key>(), // hash function
        std::equal_to<key>(), // equality function
        hce::pool_allocator<key>(glr.allocator_limit)
    );

    key k;

    // continuously receive updates for registered and done awaitable interfaces
    while(co_await glr.recv(k)) {
        auto it = registered_keys.find(k);

        if(it == registered_keys.end()) {
            // register key containing awaitable
            registered_keys.insert(k);
        } else {
            // handle done awaitable
            registered_keys.erase(it);
            hce::awaitable::interface* interface = k->interface;
            glr.return_key(k);
            co_await hce::awt<void>::make(interface);
        }
    }

    // co_await all remaining interfaces
    for(auto k : registered_keys) {
        hce::awaitable::interface* interface = k->interface;
        glr.return_key(k);
        co_await hce::awt<void>::make(interface); 
    }

    // return allowing process exit
    co_return;
}
