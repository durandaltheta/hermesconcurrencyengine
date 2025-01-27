//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "memory.hpp"
#include "lifecycle.hpp"

// acquire the thread_local memory cache for the caller
hce::memory::cache& hce::memory::cache::get() {
    // allocate the memory cache in the lifecycle instance to enforce 
    // destructor ordering
    thread_local hce::lifecycle::scoped_cache sc;
    return sc.cache();
}
