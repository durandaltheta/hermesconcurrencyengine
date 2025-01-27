//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
/*
 This file contains the various `hce::config::` implementations necessary for 
 setting runtime framework values. 

 They are typically accessors to the `hce::lifecycle::config` objects returned 
 from a runtime call to `hce::lifecycle::config::SPECIFICDOMAIN::get()`. Said 
 objects are declared after other objects in the framework, because the 
 `hce::lifecycle` manages the memory for all services and singletons in this 
 framework. Therefore these functions are declared early for each feature to be 
 able to indirectly access what they need to configure themselves.
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

thread_local hce::config::memory::cache::info* this_thread_memory_cache_info_ = 
    hce::lifecycle::config::memory::get().system;

int hce::config::logging::default_log_level() { 
    return hce::lifecycle::config::logging::get().loglevel;
}

hce::config::memory::cache::info& hce::config::memory::cache::info::get() {
    return *(this_thread_memory_cache_info_);
}

void hce::config::memory::cache::info::set(hce::config::memory::cache::info& i) {
    this_thread_memory_cache_info_ = &i;
}

hce::config::memory::cache::info::indexer_function hce::config::memory::cache::info::indexer() {
    return hce::lifecycle::config::memory::get().indexer;
}

size_t hce::config::pool_allocator::default_block_limit() {
    return hce::lifecycle::config::allocator::get().pool_allocator_default_block_limit;
}

hce::config::scheduler::config hce::config::scheduler::global::config() {
    return hce::lifecycle::config::scheduler::get().global_config;
}

size_t hce::config::threadpool::count() {
    return hce::lifecycle::config::threadpool::get().count;
}

hce::config::scheduler::config hce::config::threadpool::config() {
    return hce::lifecycle::config::threadpool::get().worker_config;
}

hce::config::threadpool::algorithm_function_ptr hce::config::threadpool::algorithm() {
    return hce::lifecycle::config::threadpool::get().algorithm;
}

size_t hce::config::blocking::reusable_block_worker_cache_size() {
    return hce::lifecycle::config::blocking::get().reusable_block_worker_cache_size;
}

int hce::config::timer::thread_priority() {
    return hce::lifecycle::config::timer::get().priority;
}

hce::chrono::duration hce::config::timer::busy_wait_threshold() {
    return hce::lifecycle::config::timer::get().busy_wait_threshold;
}

hce::chrono::duration hce::config::timer::early_wakeup_threshold() {
    return hce::lifecycle::config::timer::get().early_wakeup_threshold;
}

hce::chrono::duration hce::config::timer::early_wakeup_long_threshold() {
    return hce::lifecycle::config::timer::get().early_wakeup_long_threshold;
}

hce::config::timer::algorithm_function_ptr hce::config::timer::timeout_algorithm() {
    return hce::lifecycle::config::timer::get().algorithm;
}
