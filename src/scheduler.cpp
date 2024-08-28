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

extern std::unique_ptr<hce::scheduler::config> hce_scheduler_global_config();

#ifndef HCEGLOBALREUSEBLOCKPROCS
#define HCEGLOBALREUSEBLOCKPROCS 1
#endif 

#ifndef HCECUSTOMGLOBALCONFIG
std::unique_ptr<hce::scheduler::config> hce_scheduler_global_config() {
    auto config = hce::scheduler::config::make();
    config->block_workers_reuse_pool = HCEGLOBALREUSEBLOCKPROCS;
    return config;
}
#endif

hce::scheduler& hce::scheduler::global_() {
    static std::shared_ptr<hce::scheduler> sch(
        []() -> std::shared_ptr<hce::scheduler> {
            auto i = hce::scheduler::make(hce_scheduler_global_config());
            std::shared_ptr<hce::scheduler> sch = i->scheduler();
            HCE_WARNING_FUNCTION_BODY("hce::scheduler::global_",*sch);
            std::thread([](std::shared_ptr<hce::scheduler::install> i) { }, std::move(i)).detach();
            return sch;
        }());
    return *sch;
}

hce::scheduler::block_manager::worker*& 
hce::scheduler::block_manager::worker::tl_worker() {
    thread_local hce::scheduler::block_manager::worker* wkr = nullptr;
    return wkr;
}
