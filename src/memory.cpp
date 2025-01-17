//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <thread>

#include "memory.hpp"

// acquire the main thread id
std::thread::id get_main_thread_id() {
    // static const should be forced to evaluate on main thread before main()
    static const std::thread::id main_thread_id = std::this_thread::get_id();
    return main_thread_id;
}

// Get the cache for the main thread only. Static initialization means the 
// memory for this will be available during `std::atexit()`.
hce::memory::cache& get_main_cache() {
    static hce::memory::cache tlca;
    return tlca;
}

// acquire a reference to a thread_local cache_allocator, the source of the 
// majority of memory allocations made by this framework
hce::memory::cache& get_thread_local_cache() {
    thread_local hce::memory::cache tlca;
    return tlca;
}

// acquire the appropriate cache for the caller
hce::memory::cache& hce::memory::cache::get() {
    thread_local hce::memory::cache& tlca = 
        get_main_thread_id() == std::this_thread::get_id() 
              ? get_main_cache()
              : get_thread_local_cache();

    return tlca;
}
