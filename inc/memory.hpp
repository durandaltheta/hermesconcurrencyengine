//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/**
 @file unified memory allocation/deallocation definitions

 This file provides an optional location to replace *only* the allocation 
 mechanism used by this framework, instead of requiring the user to link against 
 an alternative implementation to std::malloc/std::free. User code can modify 
 this source to make the framework allocate and deallocate using their private 
 definitions.
 */
#ifndef HERMES_COROUTINE_ENGINE_MEMORY
#define HERMES_COROUTINE_ENGINE_MEMORY

#include <cstdlib>

namespace hce {
namespace memory {

/**
 @brief allocate a pointer of `size` bytes
 @param size the size of the requested allocation
 @return an allocated pointer of at least `size` bytes
 */
inline void* allocate(size_t size) {
    return std::malloc(size);
}

/**
 @brief deallocate an `memory::allocate()`d pointer of `size` bytes
 @param p the allocated pointer 
 */
inline void deallocate(void* p) {
    std::free(p);
}

}
}

#endif
