#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis  

/**
 @file HCE framework C API glue code
 
 This file serves as a convenient frontend for initializing the framework 
 lifecycle and for running HCE modules from C. 
 */

#include <stddef.h>

struct hce_scheduler_config_t {
    /**
     Set the log level of the thread the scheduler is installed on.

     Maximum value: 9
     Minimum value: -9
     */
    int loglevel;

    /**
     The count of coroutine handles the scheduler can pool for reuse 
     without deallocating. A sane value (roughly equal or above the median count 
     of executing coroutines during busy periods) allows avoiding unnecessary 
     re-allocation of handles during normal operation. 

     This only potentially affects the scheduler's throughput processing 
     efficiency, it has no effect on the underlying coroutine execution.

     Handles are very small allocations (pointer sized).
     */
    size_t reusable_coroutine_handle_limit;
};

typedef hce_scheduler_config_t* (*hce_threadpool_algorithm_function_ptr_t)();

struct hce_lifecycle_config_logging_t {
    /**
     @brief runtime default log level

     Defaults set by compiler define(s):
     HCELOGLEVEL
     */
    int loglevel; 
};

struct hce_lifecycle_allocator_config_t {
    /**
     @brief pool_allocator's default block limit 

     This value sets the default limit for pool allocator's reusable 
     cached memory count.

     Defaults set by compiler define(s):
     HCEPOOLALLOCATORDEFAULTBLOCKLIMIT
     */
    size_t pool_allocator_default_block_limit; 
};

struct hce_lifecycle_scheduler_config_t {

    /**
     @brief global scheduler config

     This is defaulted to a valid implementation.

     Defaults set by compiler define(s):
     HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT
     */
    hce_scheduler_config_t global_config;
};

struct hce_lifecycle_threadpool_config_t {
    /**
     @brief count of worker schedulers in the threadpool 

     0: attempt to match scheduler count to the count of runtime detected CPU cores
     n: launch n-1 additional schedulers (the first index is always the global scheduler)

     The actual count of schedulers in the threadpool is guaranteed to be >=1.

     Defaults set by compiler define(s):
     HCETHREADPOOLSCHEDULERCOUNT
     */
    size_t count; 


    /**
     @brief threadpool scheduler config

     This is defaulted to a valid implementation.

     Defaults set by compiler define(s):
     HCETHREADPOOLCOROUTINERESOURCELIMIT
     */
    hce_scheduler_config_t worker_config;

    /**
     @brief threadpool worker selector algorithm

     This is defaulted to a valid implementation if none is provided.
     */
    hce_threadpool_algorithm_function_ptr_t algorithm;
};

struct hce_lifecycle_blocking_config_t {
    /**
     @brief reusable block workers count shared by the process

     Defaults set by compiler define(s):
     HCEPROCESSREUSABLEBLOCKWORKERPROCESSLIMIT
     */
    size_t reusable_block_worker_cache_size;
};

struct hce_lifecycle_timer_config_t {
    int priority;

    /**
     @brief threshold used by default timeout algorithm for when to busy-wait 

     This value is in microseconds.

     If the difference between the current time and the nearest timer 
     timeout is under this threshold, the timer thread will busy-wait 
     until timeout for increased precision.

     Defaults set by compiler define(s):
     HCETIMERBUSYWAITMICROSECONDTHRESHOLD
     */
    size_t busy_wait_threshold;

    /**
     @brief threshold used by default timeout algorithm for the early-wakeup duration

     This value is in microseconds.

     How this value is used is determined by the timeout algorithm.

     Defaults set by compiler define(s):
     HCETIMEREARLYWAKEUPMICROSECONDTHRESHOLD
     */
    size_t early_wakeup_threshold;

    /**
     @brief threshold used by default timeout algorithm for when to begin early-wakeups for long running timers

     This value is in microseconds.

     How this value is used is determined by the timeout algorithm.

     Defaults set by compiler define(s):
     HCETIMEREARLYWAKEUPMICROSECONDLONGTHRESHOLD
     */
    size_t early_wakeup_long_threshold;
};

struct hce_lifecycle_network_config_t {
    /**
     @brief count of worker threads 

     If 0, count is set to hardware concurrency
     */
    size_t thread_count;
};

struct hce_lifecycle_config_t {
    hce_lifecycle_config_logging_t log;
    hce_lifecycle_allocator_config_t alloc;
    hce_lifecycle_scheduler_config_t sch;
    hce_lifecycle_threadpool_config_t tp;
    hce_lifecycle_blocking_config_t blk;
    hce_lifecycle_timer_config_t tmr;
    hce_lifecycle_network_config_t net;
};

/// return the precompiled runtime default loglevel 
int hce_default_log_level();

/// construct the configuration
void hce_lifecycle_config_initialize(hce_lifecycle_config_t*);

/**
 @brief start the framework lifecycle with the configuration
 @param cfg the configuration to initialize with
 @return a pointer to the initialized lifecycle
 */
void hce_lifecycle_initialize(hce_lifecycle_config_t* cfg);

/**
 @brief shutdown and destroy the framework lifecycle 
 @param lifecycle the lifecycle pointer returned form hce_lifecycle_initialize()
 */
void hce_lifecycle_destroy(void* lifecycle);

/**
 @brief import and start an HCE module .so library  
 @param context the pointer to some implementation defined context 
 @param the threadpool scheduler index to schedule on, 0 is the default scheduler
 @return the awaitable context to pass to hce_module_join, nullptr on error
 */
void* hce_module_import(const char* filepath, void* context, size_t index);

/**
 @brief join with and destroy an imported module
 @param await the value returned from hce_module_import 
 @return the return code of the module
 */
int hce_module_join(void* await);
