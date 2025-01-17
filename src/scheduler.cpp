//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <memory>
#include <unordered_set>

#include "utility.hpp"
#include "memory.hpp"
#include "lifecycle.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

hce::scheduler::lifecycle::manager* hce::scheduler::lifecycle::manager::instance_ = nullptr;
hce::scheduler* hce::scheduler::global_ = nullptr;

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

std::unique_ptr<hce::list<std::coroutine_handle<>>>*& 
hce::detail::scheduler::tl_this_scheduler_local_queue() {
    thread_local std::unique_ptr<hce::list<std::coroutine_handle<>>>* tllq = nullptr;
    return tllq;
}

void hce::scheduler::lifecycle::global_thread_function(hce::scheduler* sch) {
    // allow the memory cache to initialize as the global scheduler cache
    hce::config::memory::cache::info::thread::get_type() = 
        hce::config::memory::cache::info::thread::type::global;
    sch->run();
}

void hce::scheduler::lifecycle::scheduler_thread_function(hce::scheduler* sch) {
    // allow the memory cache to initialize as a default scheduler cache
    hce::config::memory::cache::info::thread::get_type() = 
        hce::config::memory::cache::info::thread::type::scheduler;
    sch->run();
}
