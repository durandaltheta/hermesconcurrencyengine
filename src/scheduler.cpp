//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <thread>
#include <memory>
#include <mutex>
#include <deque>

#include "atomic.hpp"
#include "scheduler.hpp"

hce::scheduler*& hce::detail::scheduler::tl_this_scheduler() {
    thread_local hce::scheduler* tlts = nullptr;
    return tlts;
}

hce::scheduler::lifecycle::manager& hce::scheduler::lifecycle::manager::instance() {  
    static hce::scheduler::lifecycle::manager m;
    return m;
}

extern std::unique_ptr<hce::scheduler::config> hce_global_config();

#ifndef HCEGLOBALREUSEBLOCKPROCS
#define HCEGLOBALREUSEBLOCKPROCS 1
#endif 

#ifndef HCECUSTOMGLOBALCONFIG
std::unique_ptr<hce::scheduler::config> hce_global_config() {
    auto config = hce::scheduler::config::make();
    config->block_workers_reuse_pool = HCEGLOBALREUSEBLOCKPROCS;
    return config;
}
#endif

hce::scheduler& hce::scheduler::global_() {
    static std::shared_ptr<hce::scheduler> sch(
        []() -> std::shared_ptr<hce::scheduler> {
            auto sch = hce::scheduler::make();
            std::thread([](std::shared_ptr<hce::scheduler> sch) mutable {
                sch->install(hce_global_config()); 
            }, sch).detach();
            return sch;
        }());
    return *sch;
}

bool& hce::scheduler::blocking::worker::tl_is_block() {
    thread_local bool ib = false;
    return ib;
}
