#pragma once
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

#include <cstddef>
#include <cstdlib>

namespace hce {
namespace memory {

/**
 @brief allocate a pointer of `size` bytes
 @param size the size of the requested allocation
 @return an allocated pointer of at least `size` bytes
 */
void* allocate(std::size_t size);

/**
 @brief deallocate an `memory::allocate()`d pointer of `size` bytes
 @param p the allocated pointer 
 */
void deallocate(void* p);

/**
 @brief operations here will attempt to use cached pools of memory 

 hce::memory::pool::allocate() should be paired with
 hce::memory::pool::deallocate().
 */
namespace pool {

/**
 @brief allocate a pointer of `size` bytes
 @param size the size of the requested allocation
 @return an allocated pointer of at least `size` bytes
 */
void* allocate(std::size_t size);

/**
 @brief deallocate an `memory::allocate()`d pointer of `size` bytes
 @param p the allocated pointer 
 */
void deallocate(void* p);

}
}
