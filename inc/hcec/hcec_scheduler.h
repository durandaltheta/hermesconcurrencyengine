#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis  

/// used to tag hce::scheduler instances
struct hce_scheduler_t {};

/// used to tag hce::scheduler::lifecycle instance
struct hce_scheduler_lifecycle_t {};

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

/**
 @brief initialize the scheduler config 
 @param cfg the configuration
 */
void hce_scheduler_config_initialize(hce_scheduler_config_t* cfg);

/**
 @brief construct an hce::scheduler and return its lifecycle object 

 @param cfg the scheduler configuration 
 @param 
 */
hce_scheduler_lifecycle_t* hce_scheduler_lifecycle_make(hce_scheduler_config_t* cfg);

/**
 @brief shutdown the scheduler and destroy the lifecycle 
 */
void hce_scheduler_lifecycle_destroy(hce_scheduler_lifecycle_t* lifecycle);

/**
 @brief acquire the scheduler from the lifecycle 
 @param lifecycle the lifecycle 
 @return the scheduler
 */
hce_scheduler_t* hce_scheduler_lifecycle_get_scheduler(hce_scheduler_lifecycle_t* lifecycle);
