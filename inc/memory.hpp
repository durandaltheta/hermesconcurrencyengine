//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_MEMORY__
#define __HERMES_COROUTINE_ENGINE_MEMORY__

#include <cstdlib>

#include "logging.hpp"

namespace hce {

/**
 @brief alignment aware memory allocation for template type T
 @param count of element Ts in allocated block 
 @return allocated pointer
 */
template <typename T>
inline T* allocate(size_t n=1) {
    HCE_TRACE_FUNCTION_ENTER("hce::allocate", n);
    T* m = static_cast<T*>(std::aligned_alloc(alignof(T), sizeof(T) * n));
    HCE_TRACE_FUNCTION_BODY("hce::allocate","memory:",m);
    return m;
}

/**
 @brief deallocate `hce::allocate<T>()`ed memory 

 NOTE: functions for any allocations from std::aligned_alloc().

 @param p pointer to memory
 */
inline void deallocate(void* p) { 
    HCE_TRACE_FUNCTION_ENTER("hce::deallocate",p);
    std::free(p); 
}

}

#endif
