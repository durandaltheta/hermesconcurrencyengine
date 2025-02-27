//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <memory>
#include "memory.hpp"
#include "thread.hpp"
#include "thread_key_map.hpp"
#include "lifecycle.hpp"

// acquire the thread_local memory cache for the caller
hce::memory::cache& hce::memory::cache::get() {
    struct init {
        static void cleanup(hce::cleanup::data& data) {
            // the thread locals are being cleaned up so the memory cache also 
            // needs to be cleaned up
            hce::service<hce::lifecycle>::get().erase_cache_();
        }

        init() :
            ref([&]() -> hce::memory::cache& {
                // check to see if initialization needs to happen
                if(ptr.ref() == nullptr) {
                    // allocate a new memory cache
                    std::unique_ptr<hce::memory::cache> cache(
                        new hce::memory::cache(
                            hce::config::memory::cache::info::get()));

                    // install a cleanup handler for when the system thread goes 
                    // out of scope
                    ptr.install(init::cleanup, nullptr);

                    // assign the memory for future init `thread_local` checks,
                    // which can occur in separately compiled `hce::module`s
                    ptr.ref() = cache.get(); 

                    // insert the cache into the `hce::lifecycle`
                    hce::service<hce::lifecycle>::get().insert_cache_(std::move(cache));
                }

                // set the reference to eliminate double indirection
                return *(ptr.ref());
            }())
        { }

        // the thread local pointer-pointer
        hce::thread::local::ptr<hce::thread::key::memory_cache> ptr;

        // a direct reference to the cache
        hce::memory::cache& ref;
    };

    thread_local init i;
    return i.ref;
}
