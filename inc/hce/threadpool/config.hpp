#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <cstddef>

#include "hce/scheduler/config.hpp"
#include "hce/scheduler.hpp"

namespace hce {
namespace config {
namespace threadpool {

/**
 0: attempt to match scheduler count to the count of runtime detected CPU cores
 n: launch n-1 additional schedulers (the first index is always the global scheduler)

 The actual count of schedulers in the threadpool is guaranteed to be >=1.

 @return the configured scheduler count for this threadpool
 */
size_t count();

/**
 @brief provide the default threadpool scheduler configuration

 Similar to `hce::config::global_config()` but used for any 
 `scheduler`s spawned for the threadpool. This method's implementation can be 
 overridden by a user definition of global method 
 `hce::config::threadpool_config() at library compile time if compiler 
 define `HCECUSTOMCONFIG` is provided, otherwise a default implementation is 
 used.

 @return a copy of the global configuration 
 */
hce::config::scheduler::config config();

// Define a function pointer type that matches your function pointer
using algorithm_function_ptr = hce_threadpool_algorithm_function_ptr_t;

/**
 @return an algorithm to be called by `hce::threadpool::algorithm()`
 */
algorithm_function_ptr algorithm();

} // threadpool
} // config
} // hce
