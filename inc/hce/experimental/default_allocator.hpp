//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_ALLOCATOR__
#define __HERMES_COROUTINE_ENGINE_ALLOCATOR__

#include <memory>

#include "memory.hpp"

namespace hce {

/**
 @brief an allocator that makes use of thread_local caching hce::allocate<T>/hce::deallocate<T> 

 Design Aims:
 - Provide an allocator as close to std::allocator's design as possible 
 - Utilize thread_local allocation without overriding global new/delete 
 - constant time allocation/deallocation when re-using allocated blocks
 - no exception handling (for speed)
 - usable as an std:: container allocator
 - all memory uses the same underlying allocation/deallocation method

 Design Limitations:
 - no default pre-caching 
 - relies on predefined block cache size limits within hce::allocate()/
 hce::deallocate() mechanisms (no resizing or size optimizations)
 - underlying mechanism's caches can only grow, never shrink
 */
template <typename T>
struct allocator {
    using value_type = T;

    /// necessary for rebinding this allocator to different types
    template <typename U>
    struct rebind {
        using other = allocator<U>;
    };

    allocator() { }

    template <typename U>
    allocator(const allocator<U>& rhs) { }

    template <typename U>
    allocator(allocator<U>&& rhs) { }

    ~allocator() { }

    template <typename U>
    allocator<T>& operator=(const allocator<U>& rhs) { 
        return *this;
    }

    template <typename U>
    allocator<T>& operator=(allocator<U>&& rhs) {
        return *this;
    }
   
    /// allocate a block of memory the size of T * n
    template <typename U>
    inline U* allocate(size_t n) { 
        return hce::allocate<U>(n); 
    }

    /**
     @brief deallocate a block of memory 

     It is an error for the user to pass a pointer to this function that was 
     not acquired from allocate() by the same allocator
     */
    template <typename U>
    inline void deallocate(U* t, size_t n) {
        hce::deallocate<U>(t,n); 
    }
};

}

template <typename T, typename U>
bool operator==(const hce::allocator<T>&, const hce::allocator<U>&) {
    return true;
}

template <typename T, typename U>
bool operator==(const hce::allocator<T>&, const std::allocator<U>&) {
    return true;
}

template <typename T, typename U>
bool operator==(const std::allocator<T>&, const hce::allocator<U>&) {
    return true;
}

template <typename T, typename U>
bool operator!=(const hce::allocator<T>& lhs, const hce::allocator<U>& rhs) {
    return !(lhs == rhs);
}

#endif
