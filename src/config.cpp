//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/*
 This file contains the various `::config::` `extern` implementations necessary 
 for determining framework defaults. They are typically compiler define driven.
 */
#include <cstring>
#include <bit>
#include <memory>
#include <vector>
#include <stdexcept>

#include "loguru.hpp"
#include "utility.hpp"
#include "chrono.hpp"
#include "thread.hpp"
#include "memory.hpp"
#include "scheduler.hpp"
#include "timer.hpp"
#include "blocking.hpp"
#include "channel.hpp"
#include "threadpool.hpp"
#include "lifecycle.hpp"

hce::chrono::duration hce::config::timer::service::busy_wait_threshold() {
    return hce::lifecycle::get().configuration().tmr.busy_wait_threshold;
}

hce::chrono::duration hce::config::timer::service::early_wakeup_threshold() {
    return hce::lifecycle::get().configuration().tmr.early_wakeup_threshold;
}

hce::chrono::duration hce::config::timer::service::early_wakeup_long_threshold() {
    return hce::lifecycle::get().configuration().tmr.early_wakeup_long_threshold;
}

size_t hce::config::pool_allocator::default_block_limit() {
    return hce::lifecycle::get().configuration().mem.pool_allocator_default_block_limit;
}

size_t hce::config::scheduler::default_resource_limit() {
    return hce::lifecycle::get().configuration().sch.default_resource_limit;
}

hce::scheduler::config hce::config::global::scheduler_config() {
    return hce::lifecycle::get().configuration().sch.global;
}

size_t hce::blocking::config::process_worker_resource_limit() {
    return hce::lifecycle::get().configuration().blk.process_worker_resource_limit;
}

size_t hce::blocking::config::global_scheduler_worker_resource_limit() {
    return hce::lifecycle::get().configuration().blk.global_scheduler_worker_resource_limit;
}

size_t hce::blocking::config::default_scheduler_worker_resource_limit() {
    return hce::lifecycle::get().configuration().blk.default_scheduler_worker_resource_limit;
}

size_t hce::config::threadpool::count() {
    return hce::lifecycle::get().configuration().tp.count;
}

hce::scheduler::config hce::config::threadpool::scheduler_config() {
    return hce::lifecycle::get().configuration().tp.config;
}

hce::config::threadpool::algorithm_function_ptr hce::config::threadpool::algorithm() {
    return hce::lifecycle::get().configuration().tp.algorithm;
}

size_t hce::config::channel::resource_limit() {
    if(hce::scheduler::in()) {
        return hce::scheduler::local().coroutine_resource_limit();
    } else {
        return hce::config::pool_allocator::default_block_limit();
    }
}

#ifdef _WIN32
int hce::config::timer::service::thread_priority() {
    return THREAD_PRIORITY_ABOVE_NORMAL;
}
#elif defined(_POSIX_VERSION)
int hce::config::timer::service::thread_priority() {
    static int priority = []{
        // Calculate the priority range
        int min_priority = sched_get_priority_min(SCHED_OTHER);
        int max_priority = sched_get_priority_max(SCHED_OTHER);

        // Calculate an intelligent high priority (somewhere near the maximum), 
        // 80% of max priority
        return min_priority + (max_priority - min_priority) * 0.8;  
    }();

    return priority;
}
#else 
int hce::config::timer::service::thread_priority() {
    return 0;
}
#endif 

hce::timer::service::algorithm_function_ptr hce::config::timer::service::timeout_algorithm() {
    return hce::lifecycle::get().configuration().tmr.algorithm;
}
