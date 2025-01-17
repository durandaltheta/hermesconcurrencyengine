//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <thread>

#include "memory.hpp"
    
hce::memory::cache* hce::memory::cache::main_cache_ = nullptr;;

// thread_local memory cache thread type
hce::config::memory::cache::info::thread::type& 
hce::config::memory::cache::info::thread::get_type() {
    thread_local hce::config::memory::cache::info::thread::type t = 
        hce::config::memory::cache::info::thread::type::system;
    return t;
}

// acquire the main thread id
std::thread::id get_main_thread_id() {
    // static const should be forced to evaluate on main thread before main()
    static const std::thread::id main_thread_id = std::this_thread::get_id();
    return main_thread_id;
}

// acquire a reference to a thread_local cache_allocator, the source of the 
// majority of memory allocations made by this framework
hce::memory::cache& get_thread_local_cache() {
    thread_local hce::memory::cache tlc;
    return tlc;
}

// acquire the appropriate cache for the caller
hce::memory::cache& hce::memory::cache::get() {
    thread_local hce::memory::cache& tlca = 
        get_main_thread_id() == std::this_thread::get_id() 
              ? *main_cache_ 
              : get_thread_local_cache();

    return tlca;
}
