#include "memory.hpp"

// acquire a reference to a thread_local cache_allocator, the source of the 
// majority of memory allocations made by this framework
hce::memory::cache& hce::memory::cache::get() {
    thread_local cache tlca;
    return tlca;
}
